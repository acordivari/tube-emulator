#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ToneStack.h"

// Passive tone stacks have ~10-13 dB of midband insertion loss; this brings the
// level back into a usable range. Trim further with the Level knob by ear.
static constexpr float kToneStackMakeupDb = 12.0f;

//==============================================================================
namespace ParamID
{
    constexpr auto drive  = "drive";
    constexpr auto bias   = "bias";
    constexpr auto bass   = "bass";
    constexpr auto mid    = "mid";
    constexpr auto treble = "treble";
    constexpr auto sag    = "sag";
    constexpr auto level  = "level";
    constexpr auto test     = "test";       // internal test-tone on/off
    constexpr auto testType = "testtype";   // Sine / Saw / Noise
}

//==============================================================================
TubeEmulatorAudioProcessor::TubeEmulatorAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
TubeEmulatorAudioProcessor::createParameterLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> params;

    // Drive: how hard we push the signal into the nonlinearity.
    params.push_back (std::make_unique<AudioParameterFloat>(
        ParameterID { ParamID::drive, 1 }, "Drive",
        NormalisableRange<float> (1.0f, 50.0f, 0.0f, 0.4f), 6.0f));

    // Bias: triode asymmetry. 0 = symmetric, higher = more even harmonics.
    params.push_back (std::make_unique<AudioParameterFloat>(
        ParameterID { ParamID::bias, 1 }, "Bias",
        NormalisableRange<float> (0.0f, 0.7f, 0.0f), 0.2f));

    // Tone stack knobs, 0..10 like the amp's panel.
    auto knob = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<AudioParameterFloat>(
            ParameterID { id, 1 }, name,
            NormalisableRange<float> (0.0f, 10.0f, 0.01f), def);
    };
    params.push_back (knob (ParamID::bass,   "Bass",   5.0f));
    params.push_back (knob (ParamID::mid,    "Mid",    4.0f));   // < 5 = the scoop
    params.push_back (knob (ParamID::treble, "Treble", 6.0f));

    // Power-amp sag: 0 = stiff (solid-state feel), 10 = loose/spongy (tube rectifier).
    params.push_back (knob (ParamID::sag, "Sag", 4.0f));

    // Output level in dB.
    params.push_back (std::make_unique<AudioParameterFloat>(
        ParameterID { ParamID::level, 1 }, "Level",
        NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f));

    // Internal test-tone generator (replaces the input so you can audition
    // the chain without a guitar plugged in).
    params.push_back (std::make_unique<AudioParameterBool>(
        ParameterID { ParamID::test, 1 }, "Test Tone", false));
    params.push_back (std::make_unique<AudioParameterChoice>(
        ParameterID { ParamID::testType, 1 }, "Test Type",
        StringArray { "Sine", "Saw", "Noise" }, 1));   // default Saw

    return { params.begin(), params.end() };
}

//==============================================================================
void TubeEmulatorAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
    spec.numChannels      = (juce::uint32) getTotalNumOutputChannels();

    // 8x oversampling (2^3) around the clipper so clipping harmonics that land
    // above Nyquist don't alias back as inharmonic fizz.
    oversampling = std::make_unique<juce::dsp::Oversampling<float>>(
        spec.numChannels, 3,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);
    oversampling->initProcessing ((size_t) samplesPerBlock);
    oversampling->reset();

    dcBlocker.prepare (spec);

    // The tone stack is a 3rd-order filter. Seed its coefficients (7 taps) BEFORE
    // prepare so the filter allocates 3rd-order state, then update them per block.
    *toneStack.state = *makeToneStackCoeffs (sampleRate);
    toneStack.prepare (spec);

    cabFilter.prepare (spec);

    convolution.prepare (spec);

    // DC blocker (removes the offset the asymmetric clipper introduces) and a
    // fixed speaker-rolloff low-pass used when no cabinet IR is loaded.
    *dcBlocker.state = *Coefs::makeHighPass (sampleRate, 20.0f);
    *cabFilter.state = *Coefs::makeLowPass  (sampleRate, 5000.0f, 0.7071f);

    updateToneStack (sampleRate);

    // Sag time constants: one-pole coef for a given time constant in ms.
    auto coef = [sampleRate] (double ms)
    {
        return (float) (1.0 - std::exp (-1.0 / ((ms * 0.001) * sampleRate)));
    };
    envAttackCoef  = coef (1.0);    // fast: catch transients (current surge)
    envReleaseCoef = coef (20.0);   // smooth the rectified waveform
    sagDroopCoef   = coef (25.0);   // supply sags reasonably quickly
    sagRecoverCoef = coef (320.0);  // ...but recharges slowly -> bloom
    supply       = 1.0f;
    sagEnvelope  = 0.0f;
}

bool TubeEmulatorAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

//==============================================================================
juce::dsp::IIR::Coefficients<float>::Ptr
TubeEmulatorAudioProcessor::makeToneStackCoeffs (double sampleRate) const
{
    // Knobs read 0..10 on the panel; the circuit equations want pot fractions 0..1.
    // (Real Fender treble/bass pots are audio taper, mid is linear — modelled
    // linearly here; swap in a taper curve later if you want exact knob feel.)
    const double t = apvts.getRawParameterValue (ParamID::treble)->load() / 10.0; // treble
    const double l = apvts.getRawParameterValue (ParamID::bass)->load()   / 10.0; // bass
    const double m = apvts.getRawParameterValue (ParamID::mid)->load()    / 10.0; // mid

    const auto c = tonestack::computeDigital (sampleRate, t, l, m);

    // JUCE stores IIR coefficients as [b0..bN, a1..aN]; a0 is normalised to 1
    // (computeDigital already did that), so it is omitted here.
    auto coeffs = new juce::dsp::IIR::Coefficients<float>();
    coeffs->coefficients = juce::Array<float> ({
        (float) c.b[0], (float) c.b[1], (float) c.b[2], (float) c.b[3],
        (float) c.a[1], (float) c.a[2], (float) c.a[3]
    });
    return coeffs;
}

void TubeEmulatorAudioProcessor::updateToneStack (double sampleRate)
{
    // Real 3rd-order Fender ('59 Bassman) tone stack — see ToneStack.h.
    *toneStack.state = *makeToneStackCoeffs (sampleRate);
}

//==============================================================================
void TubeEmulatorAudioProcessor::renderTestTone (juce::AudioBuffer<float>& buffer)
{
    constexpr float  amplitude = 0.25f;   // moderate so Drive does the pushing
    constexpr double freqHz    = 110.0;   // ~guitar low A

    const int  type = (int) apvts.getRawParameterValue (ParamID::testType)->load();
    const int  n    = buffer.getNumSamples();
    const double inc = freqHz / currentSampleRate;

    auto* out = buffer.getWritePointer (0);
    for (int i = 0; i < n; ++i)
    {
        float s = 0.0f;
        switch (type)
        {
            case 0: s = std::sin ((float) (juce::MathConstants<double>::twoPi * testPhase)); break; // Sine
            case 1: s = 2.0f * (float) testPhase - 1.0f;                                     break; // Saw
            default: s = testRandom.nextFloat() * 2.0f - 1.0f;                               break; // Noise
        }

        out[i] = s * amplitude;
        testPhase += inc;
        if (testPhase >= 1.0)
            testPhase -= 1.0;
    }

    // Mirror the generated signal to any remaining channels.
    for (int ch = 1; ch < buffer.getNumChannels(); ++ch)
        buffer.copyFrom (ch, 0, buffer, 0, 0, n);
}

//==============================================================================
void TubeEmulatorAudioProcessor::applySag (juce::AudioBuffer<float>& buffer, float depth)
{
    if (depth <= 0.0f)            // Sag at 0 = stiff supply, nothing to do.
    {
        supply = 1.0f;
        return;
    }

    constexpr float minSupply = 0.3f;   // never let the rail fully collapse
    const int n   = buffer.getNumSamples();
    const int nch = buffer.getNumChannels();
    auto* L = buffer.getWritePointer (0);
    auto* R = nch > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < n; ++i)
    {
        // Current draw proxy: envelope of the signal feeding the power stage.
        float rect = std::abs (L[i]);
        if (R != nullptr) rect = juce::jmax (rect, std::abs (R[i]));
        sagEnvelope += (rect - sagEnvelope) * (rect > sagEnvelope ? envAttackCoef
                                                                  : envReleaseCoef);

        // More draw -> lower target rail voltage. Droop quickly, recover slowly.
        const float target = juce::jmax (minSupply, 1.0f - depth * sagEnvelope);
        supply += (target - supply) * (target < supply ? sagDroopCoef : sagRecoverCoef);

        L[i] *= supply;
        if (R != nullptr) R[i] *= supply;
    }
}

//==============================================================================
void TubeEmulatorAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                               juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    // If the test-tone generator is on, overwrite the input so the generated
    // signal runs through the whole chain just like a guitar would.
    if (apvts.getRawParameterValue (ParamID::test)->load() > 0.5f)
        renderTestTone (buffer);

    // Refresh tone-stack coefficients from the current knob positions.
    // (Cheap enough per block for a starting point; smooth later if you hear zipper.)
    updateToneStack (currentSampleRate);

    const float drive = apvts.getRawParameterValue (ParamID::drive)->load();
    const float bias  = apvts.getRawParameterValue (ParamID::bias)->load();
    const float gain  = juce::Decibels::decibelsToGain (
                            apvts.getRawParameterValue (ParamID::level)->load());
    const float sagDepth = apvts.getRawParameterValue (ParamID::sag)->load() / 10.0f * 0.7f;

    juce::dsp::AudioBlock<float> block (buffer);

    // 1) Oversample, then apply the tube curve sample-by-sample, then downsample.
    auto upBlock = oversampling->processSamplesUp (block);
    for (size_t ch = 0; ch < upBlock.getNumChannels(); ++ch)
    {
        auto* data = upBlock.getChannelPointer (ch);
        for (size_t i = 0; i < upBlock.getNumSamples(); ++i)
            data[i] = tubeShape (data[i], drive, bias);
    }
    oversampling->processSamplesDown (block);

    juce::dsp::ProcessContextReplacing<float> ctx (block);

    // 2) Remove the DC offset the asymmetric clipper added.
    dcBlocker.process (ctx);

    // 3) Tone stack (real 3rd-order Fender network).
    toneStack.process (ctx);

    // 4) Power-amp sag: dynamic supply droop at the power stage (before makeup,
    //    where the signal sits at a sane level for the envelope follower).
    applySag (buffer, sagDepth);

    // 5) Makeup for the tone stack's insertion loss.
    block.multiplyBy (juce::Decibels::decibelsToGain (kToneStackMakeupDb));

    // 6) Cabinet: real IR if the user loaded one, otherwise a speaker-rolloff LPF.
    if (irLoaded.load())
        convolution.process (ctx);
    else
        cabFilter.process (ctx);

    // 7) Output level.
    block.multiplyBy (gain);
}

//==============================================================================
void TubeEmulatorAudioProcessor::loadImpulseResponse (const juce::File& irFile)
{
    if (! irFile.existsAsFile())
        return;

    convolution.loadImpulseResponse (
        irFile,
        juce::dsp::Convolution::Stereo::yes,
        juce::dsp::Convolution::Trim::yes,
        0,
        juce::dsp::Convolution::Normalise::yes);

    irLoaded.store (true);
}

//==============================================================================
juce::AudioProcessorEditor* TubeEmulatorAudioProcessor::createEditor()
{
    return new TubeEmulatorAudioProcessorEditor (*this);
}

void TubeEmulatorAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void TubeEmulatorAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TubeEmulatorAudioProcessor();
}

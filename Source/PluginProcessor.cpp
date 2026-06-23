#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
namespace ParamID
{
    constexpr auto drive  = "drive";
    constexpr auto bias   = "bias";
    constexpr auto bass   = "bass";
    constexpr auto mid    = "mid";
    constexpr auto treble = "treble";
    constexpr auto level  = "level";
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

    // Output level in dB.
    params.push_back (std::make_unique<AudioParameterFloat>(
        ParameterID { ParamID::level, 1 }, "Level",
        NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f));

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
    bassFilter.prepare (spec);
    midFilter.prepare (spec);
    trebleFilter.prepare (spec);
    cabFilter.prepare (spec);

    convolution.prepare (spec);

    // DC blocker (removes the offset the asymmetric clipper introduces) and a
    // fixed speaker-rolloff low-pass used when no cabinet IR is loaded.
    *dcBlocker.state = *Coefs::makeHighPass (sampleRate, 20.0f);
    *cabFilter.state = *Coefs::makeLowPass  (sampleRate, 5000.0f, 0.7071f);

    updateToneStack (sampleRate);
}

bool TubeEmulatorAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

//==============================================================================
void TubeEmulatorAudioProcessor::updateToneStack (double sampleRate)
{
    // NOTE: this is a voiced approximation of the Fender tone stack, not the real
    // passive RC network. For circuit-accurate behaviour, port the transfer
    // function from the Duncan Amps Tone Stack Calculator (Fender topology) into
    // these coefficients. The knob->dB mapping here is the easy starting point.
    const float bassKnob   = apvts.getRawParameterValue (ParamID::bass)->load();
    const float midKnob    = apvts.getRawParameterValue (ParamID::mid)->load();
    const float trebleKnob = apvts.getRawParameterValue (ParamID::treble)->load();

    const float bassDb   = (bassKnob   - 5.0f) * 2.4f;   // +/-12 dB
    const float midDb    = (midKnob    - 5.0f) * 2.4f;
    const float trebleDb = (trebleKnob - 5.0f) * 2.4f;

    *bassFilter.state   = *Coefs::makeLowShelf  (sampleRate, 120.0f,  0.7071f,
                                                 juce::Decibels::decibelsToGain (bassDb));
    *midFilter.state    = *Coefs::makePeakFilter (sampleRate, 500.0f, 0.7f,
                                                 juce::Decibels::decibelsToGain (midDb));
    *trebleFilter.state = *Coefs::makeHighShelf (sampleRate, 3000.0f, 0.7071f,
                                                 juce::Decibels::decibelsToGain (trebleDb));
}

//==============================================================================
void TubeEmulatorAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                               juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    // Refresh tone-stack coefficients from the current knob positions.
    // (Cheap enough per block for a starting point; smooth later if you hear zipper.)
    updateToneStack (currentSampleRate);

    const float drive = apvts.getRawParameterValue (ParamID::drive)->load();
    const float bias  = apvts.getRawParameterValue (ParamID::bias)->load();
    const float gain  = juce::Decibels::decibelsToGain (
                            apvts.getRawParameterValue (ParamID::level)->load());

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

    // 3) Tone stack.
    bassFilter.process (ctx);
    midFilter.process (ctx);
    trebleFilter.process (ctx);

    // 4) Cabinet: real IR if the user loaded one, otherwise a speaker-rolloff LPF.
    if (irLoaded.load())
        convolution.process (ctx);
    else
        cabFilter.process (ctx);

    // 5) Output level.
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

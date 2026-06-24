#pragma once

#include <JuceHeader.h>

//==============================================================================
// Tube Emulator — a starting-point '67-Fender-flavoured amp sim.
//
// Signal chain (see processBlock):
//   input drive  ->  oversampled asymmetric tube clipper  ->  DC blocker
//   ->  Fender-voiced tone stack (bass / mid / treble)
//   ->  cabinet (convolution IR if loaded, else a speaker-rolloff low-pass)
//   ->  output level
//==============================================================================
class TubeEmulatorAudioProcessor  : public juce::AudioProcessor
{
public:
    TubeEmulatorAudioProcessor();
    ~TubeEmulatorAudioProcessor() override = default;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                    { return true; }

    const juce::String getName() const override        { return JucePlugin_Name; }
    bool acceptsMidi() const override                  { return false; }
    bool producesMidi() const override                 { return false; }
    bool isMidiEffect() const override                 { return false; }
    double getTailLengthSeconds() const override       { return 0.0; }

    int getNumPrograms() override                      { return 1; }
    int getCurrentProgram() override                   { return 0; }
    void setCurrentProgram (int) override              {}
    const juce::String getProgramName (int) override   { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    // Public so the editor can offer a "Load Cabinet IR..." button.
    void loadImpulseResponse (const juce::File& irFile);
    bool hasImpulseResponse() const noexcept { return irLoaded; }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // --- the asymmetric soft-clip "tube" curve --------------------------------
    // Asymmetry (bias) biases the triode operating point off-centre so the top
    // and bottom of the wave clip differently -> even-order harmonics -> warmth.
    static inline float tubeShape (float x, float drive, float bias) noexcept
    {
        const float driven  = drive * x + bias;
        const float shaped  = std::tanh (driven) - std::tanh (bias); // remove DC
        return shaped / std::tanh (drive + bias);                    // rough makeup
    }

    void updateToneStack (double sampleRate);
    juce::dsp::IIR::Coefficients<float>::Ptr makeToneStackCoeffs (double sampleRate) const;

    //==========================================================================
    using Filter = juce::dsp::IIR::Filter<float>;
    using Coefs  = juce::dsp::IIR::Coefficients<float>;
    using Duplicated = juce::dsp::ProcessorDuplicator<Filter, Coefs>;

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;

    juce::dsp::ProcessorDuplicator<Filter, Coefs> dcBlocker;   // 20 Hz high-pass
    juce::dsp::ProcessorDuplicator<Filter, Coefs> toneStack;   // 3rd-order Fender stack
    juce::dsp::ProcessorDuplicator<Filter, Coefs> cabFilter;   // speaker rolloff LPF

    juce::dsp::Convolution convolution;
    std::atomic<bool>      irLoaded { false };

    // Internal test-tone generator.
    void   renderTestTone (juce::AudioBuffer<float>& buffer);
    double testPhase { 0.0 };
    juce::Random testRandom;

    double currentSampleRate { 44100.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TubeEmulatorAudioProcessor)
};

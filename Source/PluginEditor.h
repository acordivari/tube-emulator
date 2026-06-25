#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class TubeEmulatorAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    explicit TubeEmulatorAudioProcessorEditor (TubeEmulatorAudioProcessor&);
    ~TubeEmulatorAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    // One labelled rotary knob bundled with its APVTS attachment.
    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void setUpKnob (Knob& knob, const juce::String& paramID, const juce::String& text);

    TubeEmulatorAudioProcessor& processor;

    Knob drive, bias, bass, mid, treble, sag, reverb, tremRate, tremDepth, level;

    juce::TextButton loadIRButton { "Load Cabinet IR..." };
    juce::Label      irStatus;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::TextButton loadReverbIRButton { "Load Reverb IR..." };
    juce::Label      reverbIrStatus;
    std::unique_ptr<juce::FileChooser> reverbChooser;

    // Internal test-tone generator controls.
    juce::ToggleButton testButton { "Test Tone" };
    juce::ComboBox     testTypeBox;
    juce::Label        testLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   testAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> testTypeAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TubeEmulatorAudioProcessorEditor)
};

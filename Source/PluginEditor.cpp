#include "PluginEditor.h"

//==============================================================================
TubeEmulatorAudioProcessorEditor::TubeEmulatorAudioProcessorEditor (
    TubeEmulatorAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setUpKnob (drive,  "drive",  "Drive");
    setUpKnob (bias,   "bias",   "Bias");
    setUpKnob (bass,   "bass",   "Bass");
    setUpKnob (mid,    "mid",    "Mid");
    setUpKnob (treble, "treble", "Treble");
    setUpKnob (sag,    "sag",    "Sag");
    setUpKnob (reverb,    "reverb",    "Reverb");
    setUpKnob (tremRate,  "tremrate",  "Rate");
    setUpKnob (tremDepth, "tremdepth", "Depth");
    setUpKnob (level,  "level",  "Level");

    addAndMakeVisible (loadIRButton);
    loadIRButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser>(
            "Choose a cabinet impulse response", juce::File{}, "*.wav;*.aiff");

        chooser->launchAsync (juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                const auto file = fc.getResult();
                if (file.existsAsFile())
                {
                    processor.loadImpulseResponse (file);
                    irStatus.setText ("Cab: " + file.getFileName(),
                                      juce::dontSendNotification);
                }
            });
    };

    irStatus.setText (processor.hasImpulseResponse() ? "Cab: IR loaded"
                                                     : "Cab: speaker rolloff (no IR)",
                      juce::dontSendNotification);
    irStatus.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (irStatus);

    // --- test-tone generator controls ---
    testLabel.setText ("Generator:", juce::dontSendNotification);
    testLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (testLabel);

    addAndMakeVisible (testButton);
    testAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.apvts, "test", testButton);

    // Item IDs must match the AudioParameterChoice indices (1-based).
    testTypeBox.addItem ("Sine",  1);
    testTypeBox.addItem ("Saw",   2);
    testTypeBox.addItem ("Noise", 3);
    addAndMakeVisible (testTypeBox);
    testTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.apvts, "testtype", testTypeBox);

    setSize (820, 320);
}

//==============================================================================
void TubeEmulatorAudioProcessorEditor::setUpKnob (Knob& knob,
                                                  const juce::String& paramID,
                                                  const juce::String& text)
{
    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 18);
    addAndMakeVisible (knob.slider);

    knob.label.setText (text, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (knob.label);

    knob.attachment = std::make_unique<SliderAttachment>(
        processor.apvts, paramID, knob.slider);
}

//==============================================================================
void TubeEmulatorAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1410));   // dark tweed-ish background
    g.setColour (juce::Colours::antiquewhite);
    g.setFont (juce::Font (20.0f, juce::Font::bold));
    g.drawText ("TUBE EMULATOR  ·  '67 voiced", getLocalBounds().removeFromTop (36),
                juce::Justification::centred);
}

void TubeEmulatorAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (16);
    area.removeFromTop (28);   // title

    // Bottom-most row: test-tone generator.
    auto testRow = area.removeFromBottom (28);
    testLabel.setBounds (testRow.removeFromLeft (70));
    testButton.setBounds (testRow.removeFromLeft (100));
    testRow.removeFromLeft (8);
    testTypeBox.setBounds (testRow.removeFromLeft (110));

    area.removeFromBottom (8);

    // Next row up: cabinet IR.
    auto bottom = area.removeFromBottom (32);
    loadIRButton.setBounds (bottom.removeFromLeft (160));
    bottom.removeFromLeft (8);
    irStatus.setBounds (bottom);

    area.removeFromBottom (8);

    // Lay the six knobs out in a single row.
    Knob* knobs[] = { &drive, &bias, &bass, &mid, &treble, &sag,
                      &reverb, &tremRate, &tremDepth, &level };
    const int n = (int) std::size (knobs);
    const int w = area.getWidth() / n;

    for (int i = 0; i < n; ++i)
    {
        auto cell = area.removeFromLeft (w);
        knobs[i]->label.setBounds (cell.removeFromTop (18));
        knobs[i]->slider.setBounds (cell);
    }
}

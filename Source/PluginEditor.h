#pragma once
#include "PluginProcessor.h"

// Draws the interactive overlays on top of the Disco Sauce artwork.
class DiscoLAF : public juce::LookAndFeel_V4
{
public:
    DiscoLAF();

    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos,
                          float startAngle, float endAngle, juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h, float sliderPos,
                          float minPos, float maxPos, juce::Slider::SliderStyle, juce::Slider&) override;
    int getSliderThumbRadius(juce::Slider&) override { return 12; }
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&,
                              bool highlighted, bool down) override;
};

class DiscoSauceEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DiscoSauceEditor(DiscoSauceProcessor&);
    ~DiscoSauceEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    void timerCallback() override;
    void setupKnob(juce::Slider&, bool artStyle);

    DiscoSauceProcessor& proc;
    DiscoLAF laf;
    juce::Image art;

    juce::Slider sComp, sRev, sDelay, sEq, sTune, sMix, sRetune, sOut;
    juce::ComboBox cbKey, cbScale;
    juce::TextButton pb[4];

    // attachments are declared last so they are destroyed first
    std::unique_ptr<SA> aComp, aRev, aDelay, aEq, aTune, aMix, aRetune, aOut;
    std::unique_ptr<CA> aKey, aScale;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DiscoSauceEditor)
};

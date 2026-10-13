#include "PluginEditor.h"
#include <BinaryData.h>

// ------------------------------------------------------------------------------------------------
// Layout constants, in pixels of the original artwork (1096 x 976). Tweak here if a control
// needs nudging onto its picture.
// ------------------------------------------------------------------------------------------------
namespace layout
{
    constexpr float artW = 1096.0f, artH = 976.0f, stripH = 120.0f;
    constexpr float totalH = artH + stripH;

    struct Knob { float cx, cy, r; };
    constexpr Knob comp  { 221.0f, 483.0f, 88.0f };
    constexpr Knob rev   { 530.0f, 486.0f, 88.0f };
    constexpr Knob delay { 230.0f, 716.0f, 89.0f };
    constexpr Knob eq    { 448.0f, 727.0f, 90.0f };
    constexpr Knob tune  { 672.0f, 728.0f, 90.0f };
    constexpr float knobHitScale = 1.15f; // clickable square = radius * 2 * this

    // dry/wet slider (bounds around the glass slot on the artwork)
    constexpr float mixX = 886.0f, mixY = 386.0f, mixW = 78.0f, mixH = 442.0f;
}

static const juce::Colour kMagenta(0xffff3dcb), kCyan(0xff3de8ff), kGold(0xffffd34d), kInk(0xff0a0712);

DiscoLAF::DiscoLAF()
{
    setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff17102a));
    setColour(juce::ComboBox::outlineColourId, kMagenta.withAlpha(0.7f));
    setColour(juce::ComboBox::textColourId, juce::Colours::white);
    setColour(juce::ComboBox::arrowColourId, kCyan);
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff17102a));
    setColour(juce::PopupMenu::textColourId, juce::Colours::white);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, kMagenta.withAlpha(0.8f));
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    setColour(juce::TextButton::textColourOnId, kInk);
    setColour(juce::BubbleComponent::backgroundColourId, juce::Colour(0xee17102a));
    setColour(juce::BubbleComponent::outlineColourId, kCyan);
    setColour(juce::TooltipWindow::textColourId, juce::Colours::white);
}

void DiscoLAF::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos,
                                float startAngle, float endAngle, juce::Slider& s)
{
    const float cx = (float) x + (float) w * 0.5f;
    const float cy = (float) y + (float) h * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);

    if (s.getProperties().contains("art"))
    {
        // Artwork already contains the knob + rainbow ring: dim the unused part of the ring
        // and add a bright marker so the knob visibly responds.
        const float R = (float) juce::jmin(w, h) / (2.0f * layout::knobHitScale);
        const float rm = R * 0.88f, th = R * 0.26f;

        juce::Path unlit;
        unlit.addCentredArc(cx, cy, rm, rm, 0.0f, angle, endAngle, true);
        g.setColour(juce::Colour(0xdd07050c));
        g.strokePath(unlit, juce::PathStrokeType(th, juce::PathStrokeType::curved, juce::PathStrokeType::butt));

        const float sn = std::sin(angle), cs = std::cos(angle);
        const juce::Point<float> dot(cx + sn * rm, cy - cs * rm);
        g.setColour(juce::Colours::white.withAlpha(0.25f));
        g.fillEllipse(dot.x - R * 0.17f, dot.y - R * 0.17f, R * 0.34f, R * 0.34f);
        g.setColour(juce::Colours::white);
        g.fillEllipse(dot.x - R * 0.085f, dot.y - R * 0.085f, R * 0.17f, R * 0.17f);
        g.setColour(kInk);
        g.drawEllipse(dot.x - R * 0.085f, dot.y - R * 0.085f, R * 0.17f, R * 0.17f, 1.2f);

        // pointer line on the knob face
        const juce::Point<float> a(cx - R * 0.10f + sn * R * 0.22f, cy - R * 0.02f - cs * R * 0.22f);
        const juce::Point<float> b(cx - R * 0.10f + sn * R * 0.50f, cy - R * 0.02f - cs * R * 0.50f);
        g.setColour(kInk.withAlpha(0.8f));
        g.drawLine(a.x, a.y, b.x, b.y, R * 0.075f);
        g.setColour(kGold);
        g.drawLine(a.x, a.y, b.x, b.y, R * 0.04f);
    }
    else
    {
        const float r = (float) juce::jmin(w, h) * 0.5f - 3.0f;
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3b2f5c), cx, cy - r, juce::Colour(0xff0d0918), cx, cy + r, false));
        g.fillEllipse(cx - r * 0.78f, cy - r * 0.78f, r * 1.56f, r * 1.56f);
        g.setColour(juce::Colours::white.withAlpha(0.35f));
        g.drawEllipse(cx - r * 0.78f, cy - r * 0.78f, r * 1.56f, r * 1.56f, 1.2f);

        juce::Path track, value;
        const float ra = r * 0.92f;
        track.addCentredArc(cx, cy, ra, ra, 0.0f, startAngle, endAngle, true);
        value.addCentredArc(cx, cy, ra, ra, 0.0f, startAngle, angle, true);
        g.setColour(juce::Colour(0xff2a2142));
        g.strokePath(track, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setGradientFill(juce::ColourGradient(kCyan, cx - r, cy, kMagenta, cx + r, cy, false));
        g.strokePath(value, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour(kGold);
        g.drawLine(cx + std::sin(angle) * r * 0.25f, cy - std::cos(angle) * r * 0.25f,
                   cx + std::sin(angle) * r * 0.70f, cy - std::cos(angle) * r * 0.70f, 3.0f);
    }
}

void DiscoLAF::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float sliderPos,
                                float, float, juce::Slider::SliderStyle, juce::Slider&)
{
    const float cx = (float) x + (float) w * 0.5f;
    const float bottom = (float) (y + h) - 12.0f;

    // glowing fill from the bottom of the slot up to the thumb
    if (bottom > sliderPos)
    {
        juce::Rectangle<float> fill(cx - 7.0f, sliderPos, 14.0f, bottom - sliderPos);
        g.setGradientFill(juce::ColourGradient(kMagenta, cx, sliderPos, kCyan, cx, bottom, false));
        g.setOpacity(0.85f);
        g.fillRoundedRectangle(fill, 6.0f);
    }

    // thumb
    juce::Rectangle<float> thumb(cx - 26.0f, sliderPos - 11.0f, 52.0f, 22.0f);
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.fillRoundedRectangle(thumb.translated(0.0f, 2.0f), 7.0f);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xfff4f1ff), cx, thumb.getY(),
                                           juce::Colour(0xff8c84ad), cx, thumb.getBottom(), false));
    g.fillRoundedRectangle(thumb, 7.0f);
    g.setColour(kMagenta);
    g.drawRoundedRectangle(thumb, 7.0f, 1.6f);
    g.setColour(kInk.withAlpha(0.7f));
    g.fillRect(thumb.getX() + 8.0f, thumb.getCentreY() - 1.0f, thumb.getWidth() - 16.0f, 2.0f);
}

void DiscoLAF::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                    bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced(1.5f);
    const bool on = b.getToggleState();
    if (on)
    {
        g.setGradientFill(juce::ColourGradient(kGold, r.getX(), r.getY(), kMagenta, r.getRight(), r.getBottom(), false));
        g.fillRoundedRectangle(r, 9.0f);
    }
    else
    {
        g.setColour(juce::Colour(0xdd17102a).brighter(down ? 0.25f : (highlighted ? 0.12f : 0.0f)));
        g.fillRoundedRectangle(r, 9.0f);
        g.setColour(kCyan.withAlpha(highlighted ? 0.95f : 0.6f));
        g.drawRoundedRectangle(r, 9.0f, 1.6f);
    }
}

// ------------------------------------------------------------------------------------------------

void DiscoSauceEditor::setupKnob(juce::Slider& s, bool artStyle)
{
    s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    s.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    s.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    s.setMouseDragSensitivity(180);
    s.setPopupDisplayEnabled(true, true, this);
    if (artStyle) s.getProperties().set("art", true);
    addAndMakeVisible(s);
}

DiscoSauceEditor::DiscoSauceEditor(DiscoSauceProcessor& p)
    : juce::AudioProcessorEditor(&p), proc(p)
{
    setLookAndFeel(&laf);
    art = juce::ImageFileFormat::loadFrom(BinaryData::design_jpg, (size_t) BinaryData::design_jpgSize);

    setupKnob(sComp, true);  setupKnob(sRev, true);   setupKnob(sDelay, true);
    setupKnob(sEq, true);    setupKnob(sTune, true);
    setupKnob(sRetune, false); setupKnob(sOut, false);

    sMix.setSliderStyle(juce::Slider::LinearVertical);
    sMix.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    sMix.setPopupDisplayEnabled(true, true, this);
    addAndMakeVisible(sMix);

    auto& apvts = proc.apvts;
    aComp = std::make_unique<SA>(apvts, "comp", sComp);
    aRev = std::make_unique<SA>(apvts, "reverb", sRev);
    aDelay = std::make_unique<SA>(apvts, "delay", sDelay);
    aEq = std::make_unique<SA>(apvts, "eq", sEq);
    aTune = std::make_unique<SA>(apvts, "tune", sTune);
    aMix = std::make_unique<SA>(apvts, "mix", sMix);
    aRetune = std::make_unique<SA>(apvts, "retune", sRetune);
    aOut = std::make_unique<SA>(apvts, "output", sOut);

    juce::StringArray keys, scales;
    for (int i = 0; i < 12; ++i) { keys.add(keyNames()[i]); scales.add(scaleNames()[i]); }
    cbKey.addItemList(keys, 1);
    cbScale.addItemList(scales, 1);
    addAndMakeVisible(cbKey);
    addAndMakeVisible(cbScale);
    aKey = std::make_unique<CA>(apvts, "key", cbKey);
    aScale = std::make_unique<CA>(apvts, "scale", cbScale);

    static const char* names[4] = { "CRISP", "T-PAIN", "DARK TRAP", "DISCO PRO" };
    for (int i = 0; i < 4; ++i)
    {
        pb[i].setButtonText(names[i]);
        pb[i].setClickingTogglesState(true);
        pb[i].setRadioGroupId(777);
        pb[i].onClick = [this, i] { proc.applyPreset(i); };
        addAndMakeVisible(pb[i]);
    }
    const int idx = juce::jlimit(0, 3, (int) std::lround(apvts.getRawParameterValue("preset")->load()));
    pb[idx].setToggleState(true, juce::dontSendNotification);

    setResizeLimits(480, 480, 1600, 1600);
    getConstrainer()->setFixedAspectRatio((double) (layout::artW / layout::totalH));
    setResizable(true, true);
    setSize(820, (int) (820.0f * layout::totalH / layout::artW));

    startTimerHz(8);
}

DiscoSauceEditor::~DiscoSauceEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void DiscoSauceEditor::timerCallback()
{
    const int idx = juce::jlimit(0, 3, (int) std::lround(proc.apvts.getRawParameterValue("preset")->load()));
    if (!pb[idx].getToggleState())
        pb[idx].setToggleState(true, juce::dontSendNotification);
}

void DiscoSauceEditor::paint(juce::Graphics& g)
{
    const float s = (float) getWidth() / layout::artW;
    g.fillAll(juce::Colour(0xff07050c));

    if (art.isValid())
    {
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        g.drawImage(art, juce::Rectangle<float>(0.0f, 0.0f, layout::artW * s, layout::artH * s));
    }

    const juce::Rectangle<float> strip(0.0f, layout::artH * s, layout::artW * s, layout::stripH * s);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff1d1236), 0.0f, strip.getY(),
                                           juce::Colour(0xff07050c), 0.0f, strip.getBottom(), false));
    g.fillRect(strip);
    g.setColour(kMagenta.withAlpha(0.6f));
    g.fillRect(0.0f, strip.getY(), strip.getWidth(), 2.0f);

    g.setColour(juce::Colour(0xffc9bcff));
    g.setFont(15.0f * s * 1.1f);
    auto label = [&](const char* t, float x, float y, float w, juce::Justification j)
    {
        g.drawText(t, juce::Rectangle<float>(x * s, y * s, w * s, 22.0f * s), j);
    };
    label("PRESETS", 24, 992, 200, juce::Justification::centredLeft);
    label("KEY", 610, 992, 70, juce::Justification::centred);
    label("SCALE", 692, 992, 170, juce::Justification::centred);
    label("RETUNE SPEED", 852, 1072, 96, juce::Justification::centred);
    label("OUTPUT", 962, 1072, 96, juce::Justification::centred);
}

void DiscoSauceEditor::resized()
{
    const float s = (float) getWidth() / layout::artW;
    auto rect = [&](float x, float y, float w, float h)
    {
        return juce::Rectangle<int>(juce::roundToInt(x * s), juce::roundToInt(y * s),
                                    juce::roundToInt(w * s), juce::roundToInt(h * s));
    };
    auto knobRect = [&](const layout::Knob& k)
    {
        const float half = k.r * layout::knobHitScale;
        return rect(k.cx - half, k.cy - half, half * 2.0f, half * 2.0f);
    };

    sComp.setBounds(knobRect(layout::comp));
    sRev.setBounds(knobRect(layout::rev));
    sDelay.setBounds(knobRect(layout::delay));
    sEq.setBounds(knobRect(layout::eq));
    sTune.setBounds(knobRect(layout::tune));
    sMix.setBounds(rect(layout::mixX, layout::mixY, layout::mixW, layout::mixH));

    pb[0].setBounds(rect(24, 1020, 132, 50));
    pb[1].setBounds(rect(164, 1020, 132, 50));
    pb[2].setBounds(rect(304, 1020, 132, 50));
    pb[3].setBounds(rect(444, 1020, 132, 50));

    cbKey.setBounds(rect(610, 1024, 70, 36));
    cbScale.setBounds(rect(692, 1024, 170, 36));

    sRetune.setBounds(rect(867, 1002, 66, 66));
    sOut.setBounds(rect(977, 1002, 66, 66));
}

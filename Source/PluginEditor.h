#pragma once

#include "PluginProcessor.h"

namespace Ink
{
    const juce::Colour plateTop    { 0xffece4d0 };
    const juce::Colour plateBottom { 0xffd4c9ae };
    const juce::Colour ink         { 0xff2b2622 };
    const juce::Colour inkSoft     { 0xff6d6457 };
    const juce::Colour red         { 0xffd23a24 };
    const juce::Colour ledOff      { 0xff5b2b22 };
    const juce::Colour lcdBg       { 0xff1a1410 };
    const juce::Colour lcdText     { 0xffff9a3c };
}

//==============================================================================
/** Bakelite pedal knob with a printed scale. */
class PedalKnob : public juce::Component
{
public:
    struct ScaleMark { float value; juce::String text; };

    PedalKnob (VoxShiftAudioProcessor&, const juce::String& paramId, float knobRadius,
               std::vector<ScaleMark> marks, int minorTicks);

    /** When the function returns true the knob shows and edits `other` instead (used by LINK). */
    void setRedirect (const juce::String& otherParamId, std::function<bool()> active);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    float getShownNorm() const;
    juce::RangedAudioParameter* shownParam() const;

private:
    struct Bound
    {
        juce::RangedAudioParameter* param = nullptr;
        std::unique_ptr<juce::ParameterAttachment> att;
        float norm = 0.0f;
    };
    Bound& active();
    void bind (Bound&, const juce::String& id);

    VoxShiftAudioProcessor& proc;
    Bound main, redirect;
    std::function<bool()> redirectActive;
    float radius;
    std::vector<ScaleMark> marks;
    int minorTicks;
    float dragStart = 0.0f;
    Bound* dragging = nullptr;
};

//==============================================================================
class LinkButton : public juce::Component
{
public:
    explicit LinkButton (VoxShiftAudioProcessor&);
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool isOn() const { return on; }
private:
    std::unique_ptr<juce::ParameterAttachment> att;
    bool on = false;
};

//==============================================================================
class ModeSwitch : public juce::Component
{
public:
    explicit ModeSwitch (VoxShiftAudioProcessor&);
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    int getMode() const { return mode; }
private:
    std::unique_ptr<juce::ParameterAttachment> att;
    int mode = 0;
};

//==============================================================================
class VoxShiftAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VoxShiftAudioProcessorEditor (VoxShiftAudioProcessor&);
    ~VoxShiftAudioProcessorEditor() override { stopTimer(); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override { repaint(); }
    void drawLcd (juce::Graphics&, juce::Rectangle<float>, const juce::String&);

    VoxShiftAudioProcessor& proc;
    PedalKnob pitch, formant, drive, mix, output;
    LinkButton link;
    ModeSwitch mode;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxShiftAudioProcessorEditor)
};

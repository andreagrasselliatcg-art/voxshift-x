#include "PluginEditor.h"

namespace
{
    constexpr float kStart = juce::MathConstants<float>::pi * 1.25f;
    constexpr float kEnd   = juce::MathConstants<float>::pi * 2.75f;
    constexpr int W = 640, H = 470;

    juce::Font font (float size, bool bold = true) { return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain)); }

    void drawLed (juce::Graphics& g, juce::Point<float> c, float r, bool on, float glow = 1.0f)
    {
        if (on)
        {
            g.setColour (Ink::red.withAlpha (0.25f * glow));
            g.fillEllipse (juce::Rectangle<float> (r * 4.0f, r * 4.0f).withCentre (c));
        }
        g.setColour (juce::Colour (0xff1c1714));
        g.fillEllipse (juce::Rectangle<float> (r * 2.6f, r * 2.6f).withCentre (c));
        g.setGradientFill (juce::ColourGradient (on ? juce::Colour (0xffff8a6a) : Ink::ledOff.brighter (0.2f), c.x - r * 0.4f, c.y - r * 0.4f,
                                                 on ? Ink::red : Ink::ledOff, c.x + r, c.y + r, true));
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
        g.setColour (juce::Colours::white.withAlpha (on ? 0.6f : 0.2f));
        g.fillEllipse (juce::Rectangle<float> (r * 0.7f, r * 0.5f).withCentre (c.translated (-r * 0.3f, -r * 0.4f)));
    }

    void drawScrew (juce::Graphics& g, juce::Point<float> c)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd9d6d0), c.x - 4, c.y - 4, juce::Colour (0xff8d8a84), c.x + 5, c.y + 5, false));
        g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (c));
        g.setColour (juce::Colour (0xff55524d));
        g.drawEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (c), 0.8f);
        g.drawLine (c.x - 4.0f, c.y + 1.5f, c.x + 4.0f, c.y - 1.5f, 1.4f);
    }

    juce::String noteName (int midi) { return juce::MidiMessage::getMidiNoteName (midi, true, true, 3); }
}

//==============================================================================
PedalKnob::PedalKnob (VoxShiftAudioProcessor& p, const juce::String& id, float r, std::vector<ScaleMark> m, int minor)
    : proc (p), radius (r), marks (std::move (m)), minorTicks (minor)
{
    bind (main, id);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

void PedalKnob::bind (Bound& b, const juce::String& id)
{
    b.param = proc.apvts.getParameter (id);
    b.att = std::make_unique<juce::ParameterAttachment> (*b.param, [this, &b] (float v) { b.norm = b.param->convertTo0to1 (v); repaint(); });
    b.att->sendInitialUpdate();
}

void PedalKnob::setRedirect (const juce::String& otherId, std::function<bool()> isActive)
{
    bind (redirect, otherId);
    redirectActive = std::move (isActive);
}

PedalKnob::Bound& PedalKnob::active() { return (redirectActive && redirectActive()) ? redirect : main; }
float PedalKnob::getShownNorm() const { return (redirectActive && redirectActive()) ? redirect.norm : main.norm; }
juce::RangedAudioParameter* PedalKnob::shownParam() const { return (redirectActive && redirectActive()) ? redirect.param : main.param; }

void PedalKnob::paint (juce::Graphics& g)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    const float r = radius;
    auto* prm = main.param;

    // ---- printed scale --------------------------------------------------------
    g.setColour (Ink::ink);
    for (int i = 0; i <= minorTicks; ++i)
    {
        const float a = kStart + (kEnd - kStart) * (float) i / (float) minorTicks;
        const auto p0 = c.getPointOnCircumference (r + 5.0f, a), p1 = c.getPointOnCircumference (r + 9.0f, a);
        g.drawLine ({ p0, p1 }, 1.0f);
    }
    for (auto& m : marks)
    {
        const float a = kStart + (kEnd - kStart) * prm->convertTo0to1 (m.value);
        const auto p0 = c.getPointOnCircumference (r + 5.0f, a), p1 = c.getPointOnCircumference (r + 13.0f, a);
        g.drawLine ({ p0, p1 }, 2.0f);
        const auto tp = c.getPointOnCircumference (r + (r > 40.0f ? 26.0f : 22.0f), a);
        g.setFont (font (r > 40.0f ? 13.0f : 10.5f));
        g.drawText (m.text, juce::Rectangle<float> (40.0f, 16.0f).withCentre (tp), juce::Justification::centred);
    }

    // ---- bakelite knob ----------------------------------------------------------
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillEllipse (juce::Rectangle<float> (r * 2.1f, r * 2.1f).withCentre (c.translated (2.0f, 5.0f)));

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3b3734), c.x, c.y - r, juce::Colour (0xff0d0c0b), c.x, c.y + r, false));
    g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));

    // knurled skirt
    const float val = getShownNorm();
    const float angle = kStart + (kEnd - kStart) * val;
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    const int ridges = r > 40.0f ? 60 : 36;
    for (int i = 0; i < ridges; ++i)
    {
        const float a = angle + juce::MathConstants<float>::twoPi * (float) i / (float) ridges;
        g.drawLine ({ c.getPointOnCircumference (r * 0.84f, a), c.getPointOnCircumference (r * 0.98f, a) }, 1.0f);
    }

    const float capR = r * 0.78f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff45403c), c.x - capR * 0.5f, c.y - capR * 0.6f,
                                             juce::Colour (0xff100f0e), c.x + capR * 0.6f, c.y + capR, true));
    g.fillEllipse (juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (c));
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawEllipse (juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (c), 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.fillEllipse (juce::Rectangle<float> (capR * 1.0f, capR * 0.6f).withCentre (c.translated (-capR * 0.3f, -capR * 0.45f)));

    // pointer line
    g.setColour (juce::Colour (0xfff4efe4));
    g.drawLine ({ c.getPointOnCircumference (r * 0.30f, angle), c.getPointOnCircumference (r * 0.97f, angle) }, juce::jmax (2.0f, r * 0.07f));

    if (redirectActive && redirectActive())
    {
        g.setColour (Ink::red.withAlpha (0.5f));
        g.drawEllipse (juce::Rectangle<float> (r * 2.0f + 2.0f, r * 2.0f + 2.0f).withCentre (c), 1.5f);
    }
}

void PedalKnob::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isAltDown()) { mouseDoubleClick (e); return; }
    dragging = &active();
    dragStart = dragging->norm;
    dragging->att->beginGesture();
}

void PedalKnob::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    const float sens = (e.mods.isShiftDown() ? 0.15f : 1.0f) / (radius > 40.0f ? 260.0f : 200.0f);
    const float v = juce::jlimit (0.0f, 1.0f, dragStart + (float) (e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY()) * sens);
    dragging->att->setValueAsPartOfGesture (dragging->param->convertFrom0to1 (v));
}

void PedalKnob::mouseUp (const juce::MouseEvent&)
{
    if (dragging) dragging->att->endGesture();
    dragging = nullptr;
}

void PedalKnob::mouseDoubleClick (const juce::MouseEvent&)
{
    auto& b = active();
    b.att->setValueAsCompleteGesture (b.param->convertFrom0to1 (b.param->getDefaultValue()));
}

void PedalKnob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    auto& b = active();
    const float d = (w.isReversed ? -w.deltaY : w.deltaY) * (e.mods.isShiftDown() ? 0.01f : 0.05f);
    b.att->setValueAsCompleteGesture (b.param->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, b.norm + d)));
}

//==============================================================================
LinkButton::LinkButton (VoxShiftAudioProcessor& p)
{
    att = std::make_unique<juce::ParameterAttachment> (*p.apvts.getParameter (ParamIDs::link), [this] (float v) { on = v > 0.5f; repaint(); });
    att->sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void LinkButton::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    auto cap = b.withTrimmedTop (18.0f).reduced (8.0f, 0.0f);
    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.fillRoundedRectangle (cap.translated (0, 2), 5.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3633), cap.getX(), cap.getY(), juce::Colour (0xff141210), cap.getX(), cap.getBottom(), false));
    g.fillRoundedRectangle (cap, 5.0f);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (cap, 5.0f, 1.0f);

    // chain icon
    g.setColour (on ? juce::Colour (0xffffb08a) : juce::Colour (0xffbdb6aa));
    const auto c = cap.getCentre();
    g.drawRoundedRectangle (juce::Rectangle<float> (16.0f, 9.0f).withCentre (c.translated (-6.0f, 0.0f)), 4.5f, 2.0f);
    g.drawRoundedRectangle (juce::Rectangle<float> (16.0f, 9.0f).withCentre (c.translated (6.0f, 0.0f)), 4.5f, 2.0f);

    drawLed (g, { b.getCentreX(), b.getY() + 7.0f }, 4.0f, on);
}

void LinkButton::mouseUp (const juce::MouseEvent& e)
{
    if (getLocalBounds().contains (e.getPosition())) att->setValueAsCompleteGesture (on ? 0.0f : 1.0f);
}

//==============================================================================
ModeSwitch::ModeSwitch (VoxShiftAudioProcessor& p)
{
    att = std::make_unique<juce::ParameterAttachment> (*p.apvts.getParameter (ParamIDs::mode), [this] (float v) { mode = juce::roundToInt (v); repaint(); });
    att->sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void ModeSwitch::paint (juce::Graphics& g)
{
    const char* names[3] = { "TRANSPOSE", "QUANTIZE", "ROBOT" };
    const float rowH = (float) getHeight() / 3.0f;

    // slot for the lever
    auto slot = juce::Rectangle<float> ((float) getWidth() - 26.0f, 8.0f, 12.0f, (float) getHeight() - 16.0f);
    g.setColour (juce::Colour (0xff1c1916));
    g.fillRoundedRectangle (slot, 6.0f);
    g.setColour (juce::Colours::white.withAlpha (0.3f));
    g.drawRoundedRectangle (slot.translated (0, 1), 6.0f, 0.8f);

    for (int i = 0; i < 3; ++i)
    {
        const float cy = rowH * ((float) i + 0.5f);
        drawLed (g, { 10.0f, cy }, 4.5f, i == mode);
        g.setColour (i == mode ? Ink::ink : Ink::inkSoft);
        g.setFont (font (13.0f));
        g.drawText (names[i], juce::Rectangle<float> (24.0f, cy - 9.0f, (float) getWidth() - 56.0f, 18.0f), juce::Justification::centredLeft);
    }

    // chrome lever tip
    const float ly = rowH * ((float) mode + 0.5f);
    const auto lc = juce::Point<float> (slot.getCentreX(), ly);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillEllipse (juce::Rectangle<float> (18.0f, 18.0f).withCentre (lc.translated (1.0f, 3.0f)));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff5f3ef), lc.x - 5, lc.y - 6, juce::Colour (0xff7d7a75), lc.x + 6, lc.y + 7, false));
    g.fillEllipse (juce::Rectangle<float> (17.0f, 17.0f).withCentre (lc));
    g.setColour (juce::Colour (0xff4a4743));
    g.drawEllipse (juce::Rectangle<float> (17.0f, 17.0f).withCentre (lc), 0.8f);
}

void ModeSwitch::mouseUp (const juce::MouseEvent& e)
{
    if (! getLocalBounds().contains (e.getPosition())) return;
    const int m = juce::jlimit (0, 2, (int) ((float) e.y / ((float) getHeight() / 3.0f)));
    att->setValueAsCompleteGesture ((float) m);
}

//==============================================================================
VoxShiftAudioProcessorEditor::VoxShiftAudioProcessorEditor (VoxShiftAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      pitch   (p, ParamIDs::pitch,   62.0f, { { -12, "-12" }, { -7, "-7" }, { -5, "-5" }, { 0, "0" }, { 5, "+5" }, { 7, "+7" }, { 12, "+12" } }, 24),
      formant (p, ParamIDs::formant, 62.0f, { { -12, "-12" }, { -6, "-6" }, { 0, "0" }, { 6, "+6" }, { 12, "+12" } }, 24),
      drive   (p, ParamIDs::drive,   26.0f, { { 0, "0" }, { 50, "5" }, { 100, "10" } }, 10),
      mix     (p, ParamIDs::mix,     26.0f, { { 0, "DRY" }, { 100, "WET" } }, 10),
      output  (p, ParamIDs::output,  22.0f, { { -24, "-24" }, { 0, "0" }, { 12, "+12" } }, 12),
      link (p), mode (p)
{
    formant.setRedirect (ParamIDs::pitch, [this] { return link.isOn(); });
    for (auto* c : std::initializer_list<juce::Component*> { &pitch, &formant, &drive, &mix, &output, &link, &mode })
        addAndMakeVisible (c);
    setSize (W, H);
    startTimerHz (20);
}

void VoxShiftAudioProcessorEditor::drawLcd (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text)
{
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.drawRoundedRectangle (r.translated (0, 1), 4.0f, 1.0f);
    g.setColour (Ink::lcdBg);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (Ink::lcdText.withAlpha (0.15f));
    g.fillRoundedRectangle (r.reduced (2.0f), 3.0f);
    g.setColour (Ink::lcdText);
    g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::bold)));
    g.drawText (text, r, juce::Justification::centred);
}

void VoxShiftAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff141210));

    // ---- faceplate ----------------------------------------------------------------
    auto plate = juce::Rectangle<float> (12.0f, 12.0f, (float) W - 24.0f, (float) H - 24.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (plate.translated (0, 4), 14.0f);
    g.setGradientFill (juce::ColourGradient (Ink::plateTop, 0, plate.getY(), Ink::plateBottom, 0, plate.getBottom(), false));
    g.fillRoundedRectangle (plate, 14.0f);
    juce::Random rnd (7);
    for (int y = (int) plate.getY() + 2; y < (int) plate.getBottom() - 2; y += 2)
    {
        g.setColour (juce::Colours::black.withAlpha (0.012f + 0.012f * rnd.nextFloat()));
        g.drawHorizontalLine (y, plate.getX() + 4, plate.getRight() - 4);
    }
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.drawRoundedRectangle (plate.reduced (1.0f), 13.0f, 1.0f);
    g.setColour (Ink::ink.withAlpha (0.5f));
    g.drawRoundedRectangle (plate.reduced (9.0f), 9.0f, 1.2f);
    for (auto p : { juce::Point<float> (30, 30), { (float) W - 30, 30 }, { 30, (float) H - 30 }, { (float) W - 30, (float) H - 30 } })
        drawScrew (g, p);

    // ---- header ---------------------------------------------------------------------
    g.setColour (Ink::ink);
    g.setFont (font (36.0f));
    g.drawText ("VOXSHIFT", 52, 26, 230, 44, juce::Justification::centredLeft);
    auto badge = juce::Rectangle<float> (250.0f, 36.0f, 40.0f, 24.0f);
    g.setColour (Ink::red);
    g.fillRoundedRectangle (badge, 4.0f);
    g.setColour (Ink::plateTop);
    g.setFont (font (15.0f));
    g.drawText ("- X", badge, juce::Justification::centred);

    g.setColour (Ink::ink);
    g.setFont (font (15.0f));
    g.drawText ("TUNE TALKS", W - 260, 30, 208, 20, juce::Justification::centredRight);
    g.setColour (Ink::inkSoft);
    g.setFont (font (11.0f, false));
    g.drawText ("pitch & formant shifter", W - 260, 50, 208, 16, juce::Justification::centredRight);

    g.setColour (Ink::ink.withAlpha (0.35f));
    g.drawHorizontalLine (82, 40.0f, (float) W - 40.0f);
    g.drawHorizontalLine (352, 40.0f, (float) W - 40.0f);

    // ---- labels and displays ------------------------------------------------------------
    const int m = mode.getMode();
    const int note = proc.midiNote.load();
    const auto pitchParam = proc.apvts.getParameter (ParamIDs::pitch);
    const float pv = pitchParam->convertFrom0to1 (pitchParam->getValue());

    g.setColour (Ink::ink);
    g.setFont (font (17.0f));
    g.drawText ("PITCH", 105, 284, 120, 22, juce::Justification::centred);
    g.drawText ("FORMANT", 415, 284, 120, 22, juce::Justification::centred);

    juce::String pitchText;
    if (note >= 0)         pitchText = (m == vbx::modeRobot ? "MIDI " + noteName (note) : "MIDI " + juce::String (note - 60 >= 0 ? "+" : "") + juce::String (note - 60));
    else if (m == vbx::modeTranspose) pitchText = (pv > 0.005f ? "+" : "") + juce::String (pv, 2);
    else                   pitchText = (std::round (pv) > 0 ? "+" : "") + juce::String ((int) std::round (pv)) + (m == vbx::modeRobot ? "  ROBOT" : "  QNT");
    drawLcd (g, { 115.0f, 310.0f, 100.0f, 26.0f }, pitchText);

    const bool linked = link.isOn();
    const float fv = linked ? pv : proc.apvts.getParameter (ParamIDs::formant)->convertFrom0to1 (proc.apvts.getParameter (ParamIDs::formant)->getValue());
    drawLcd (g, { 425.0f, 310.0f, 100.0f, 26.0f }, (linked ? "L " : "") + juce::String (fv > 0.005f ? "+" : "") + juce::String (fv, 2));

    g.setFont (font (12.0f));
    g.setColour (Ink::ink);
    g.drawText ("LINK", 280, 92, 80, 16, juce::Justification::centred);
    g.drawText ("MODE", 262, 166, 116, 16, juce::Justification::centredLeft);

    // MIDI / tracking indicator
    drawLed (g, { 274.0f, 312.0f }, 4.0f, note >= 0);
    g.setColour (Ink::ink);
    g.setFont (font (11.0f));
    g.drawText ("MIDI", 284, 304, 40, 16, juce::Justification::centredLeft);
    const float hzIn = proc.inputHz.load();
    g.setColour (Ink::inkSoft);
    g.setFont (font (10.5f, false));
    g.drawText (hzIn > 0.0f ? "in " + noteName (juce::roundToInt (69.0f + 12.0f * std::log2 (hzIn / 440.0f))) : "in --",
                320, 304, 60, 16, juce::Justification::centredLeft);

    // bottom row
    g.setColour (Ink::ink);
    g.setFont (font (13.0f));
    g.drawText ("DRIVE", 90, 432, 80, 16, juce::Justification::centred);
    g.drawText ("MIX", 280, 432, 80, 16, juce::Justification::centred);
    g.drawText ("OUTPUT", 460, 432, 80, 16, juce::Justification::centred);

    const float d = proc.apvts.getParameter (ParamIDs::drive)->getValue();
    // red jewel drive lamp
    {
        const auto c = juce::Point<float> (208.0f, 398.0f);
        if (d > 0.001f)
        {
            g.setColour (Ink::red.withAlpha (0.15f + 0.35f * d));
            g.fillEllipse (juce::Rectangle<float> (26.0f + 18.0f * d, 26.0f + 18.0f * d).withCentre (c));
        }
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd8d4cc), c.x - 8, c.y - 8, juce::Colour (0xff6f6c66), c.x + 9, c.y + 9, false));
        g.fillEllipse (juce::Rectangle<float> (24.0f, 24.0f).withCentre (c));
        const auto lit = Ink::ledOff.interpolatedWith (juce::Colour (0xffff4a2a), juce::jlimit (0.0f, 1.0f, 0.15f + d));
        g.setGradientFill (juce::ColourGradient (lit.brighter (0.6f), c.x - 4, c.y - 5, lit.darker (0.5f), c.x + 6, c.y + 7, true));
        g.fillEllipse (juce::Rectangle<float> (16.0f, 16.0f).withCentre (c));
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.fillEllipse (juce::Rectangle<float> (5.0f, 3.5f).withCentre (c.translated (-2.5f, -3.5f)));
    }
}

void VoxShiftAudioProcessorEditor::resized()
{
    pitch.setBounds (165 - 100, 186 - 100, 200, 200);
    formant.setBounds (475 - 100, 186 - 100, 200, 200);
    link.setBounds (280, 110, 80, 44);
    mode.setBounds (262, 186, 130, 96);
    drive.setBounds (130 - 45, 398 - 45, 90, 90);
    mix.setBounds (320 - 45, 398 - 45, 90, 90);
    output.setBounds (500 - 40, 398 - 40, 80, 80);
}

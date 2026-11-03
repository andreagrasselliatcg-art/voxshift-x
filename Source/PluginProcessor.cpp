#include "PluginProcessor.h"
#include "PluginEditor.h"

VoxShiftAudioProcessor::VoxShiftAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "VoxShiftX", createParameterLayout())
{
    pMode    = apvts.getRawParameterValue (ParamIDs::mode);
    pPitch   = apvts.getRawParameterValue (ParamIDs::pitch);
    pFormant = apvts.getRawParameterValue (ParamIDs::formant);
    pLink    = apvts.getRawParameterValue (ParamIDs::link);
    pDrive   = apvts.getRawParameterValue (ParamIDs::drive);
    pMix     = apvts.getRawParameterValue (ParamIDs::mix);
    pOutput  = apvts.getRawParameterValue (ParamIDs::output);
}

juce::AudioProcessorValueTreeState::ParameterLayout VoxShiftAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    auto semis = [] (float v, int) { return (v > 0.005f ? "+" : "") + juce::String (v, std::abs (v - std::round (v)) < 0.005f ? 0 : 2) + " st"; };
    auto pct   = [] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; };

    params.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ParamIDs::mode, 1 }, "Mode",
                                                                    juce::StringArray { "Transpose", "Quantize", "Robot" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ParamIDs::pitch, 1 }, "Pitch",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("st").withStringFromValueFunction (semis)));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ParamIDs::formant, 1 }, "Formant",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("st").withStringFromValueFunction (semis)));
    params.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ParamIDs::link, 1 }, "Link", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ParamIDs::drive, 1 }, "Drive",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (pct)));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ParamIDs::mix, 1 }, "Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (pct)));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ParamIDs::output, 1 }, "Output",
        juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction ([] (float v, int)
            { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; })));

    return { params.begin(), params.end() };
}

bool VoxShiftAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    const auto mono = juce::AudioChannelSet::mono(), stereo = juce::AudioChannelSet::stereo();

    // mono -> mono, stereo -> stereo, and mono -> stereo (Logic mono tracks)
    if (out == stereo) return in == stereo || in == mono;
    if (out == mono)   return in == mono;
    return false;
}

void VoxShiftAudioProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    setLatencySamples (engine.getLatency());
    dryDelay.setSize (2, engine.getLatency());
    dryDelay.clear();
    dryWrite = 0;
    // Fixed-size work buffers: the host may send blocks of any size; we process in chunks.
    monoBuf.assign ((size_t) kMaxChunk, 0.0f);
    wetBuf.assign ((size_t) kMaxChunk, 0.0f);
    mixSmoothed.reset (sampleRate, 0.03);
    gainSmoothed.reset (sampleRate, 0.03);
    mixSmoothed.setCurrentAndTargetValue (pMix->load() * 0.01f);
    gainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pOutput->load()));
}

void VoxShiftAudioProcessor::reset()
{
    engine.reset();
    dryDelay.clear();
    dryWrite = 0;
}

void VoxShiftAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();
    if (n == 0) return;
    if (numIn == 0) { buffer.clear(); return; }

    // mono -> stereo: start the extra output channels from the input
    for (int ch = numIn; ch < numOut; ++ch) buffer.copyFrom (ch, 0, buffer, 0, 0, n);

    vbx::ShiftParams p;
    p.mode    = (int) pMode->load();
    p.pitch   = pPitch->load();
    p.formant = pFormant->load();
    p.link    = pLink->load() > 0.5f;
    p.drive   = pDrive->load() * 0.01f;
    engine.setParams (p);

    mixSmoothed.setTargetValue (pMix->load() * 0.01f);
    gainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (pOutput->load()));

    levelPeak = 0.0f;
    auto midiIt = midi.begin();
    for (int start = 0; start < n; start += kMaxChunk)
    {
        const int len = std::min (kMaxChunk, n - start);

        // mono sum of the real inputs
        for (int i = 0; i < len; ++i)
        {
            float s = 0.0f;
            for (int ch = 0; ch < numIn; ++ch) s += buffer.getSample (ch, start + i);
            monoBuf[(size_t) i] = s / (float) numIn;
            levelPeak = std::max (levelPeak, std::abs (monoBuf[(size_t) i]));
        }

        // render, splitting at MIDI events
        int pos = 0;
        while (midiIt != midi.end() && (*midiIt).samplePosition < start + len)
        {
            const auto meta = *midiIt;
            const int at = juce::jlimit (pos, len, meta.samplePosition - start);
            if (at > pos) engine.process (monoBuf.data() + pos, wetBuf.data() + pos, at - pos);
            pos = at;
            const auto m = meta.getMessage();
            if (m.isNoteOn())                                engine.noteOn (m.getNoteNumber());
            else if (m.isNoteOff())                          engine.noteOff (m.getNoteNumber());
            else if (m.isAllNotesOff() || m.isAllSoundOff()) engine.allNotesOff();
            ++midiIt;
        }
        if (len > pos) engine.process (monoBuf.data() + pos, wetBuf.data() + pos, len - pos);

        processRange (buffer, start, len);
    }

    midiNote.store (engine.heldNote());
    inputHz.store (engine.getDetectedHz());
    outputHz.store (engine.getOutputHz());
    inputLevel.store (std::max (levelPeak, inputLevel.load() * 0.85f));
}

void VoxShiftAudioProcessor::processRange (juce::AudioBuffer<float>& buffer, int start, int len)
{
    const int numOut = std::min (buffer.getNumChannels(), getTotalNumOutputChannels());
    const int dryLen = dryDelay.getNumSamples();

    for (int i = 0; i < len; ++i)
    {
        const float m = mixSmoothed.getNextValue();
        const float g = gainSmoothed.getNextValue();
        const float dryG = std::cos (m * juce::MathConstants<float>::halfPi) * g;
        const float wetG = std::sin (m * juce::MathConstants<float>::halfPi) * g;
        const float wet = wetBuf[(size_t) i] * wetG;

        for (int ch = 0; ch < numOut; ++ch)
        {
            const int dch = std::min (ch, 1);
            const float in = buffer.getSample (ch, start + i);
            const float delayed = dryDelay.getSample (dch, dryWrite);
            dryDelay.setSample (dch, dryWrite, in);
            buffer.setSample (ch, start + i, delayed * dryG + wet);
        }
        if (++dryWrite >= dryLen) dryWrite = 0;
    }
}

void VoxShiftAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}

void VoxShiftAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* VoxShiftAudioProcessor::createEditor() { return new VoxShiftAudioProcessorEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VoxShiftAudioProcessor(); }

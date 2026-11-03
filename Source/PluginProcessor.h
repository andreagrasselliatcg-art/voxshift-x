#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ShiftEngine.h"

namespace ParamIDs
{
    inline constexpr const char* mode    = "mode";
    inline constexpr const char* pitch   = "pitch";
    inline constexpr const char* formant = "formant";
    inline constexpr const char* link    = "link";
    inline constexpr const char* drive   = "drive";
    inline constexpr const char* mix     = "mix";
    inline constexpr const char* output  = "output";
}

class VoxShiftAudioProcessor : public juce::AudioProcessor
{
public:
    VoxShiftAudioProcessor();
    ~VoxShiftAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "VoxShift - X"; }
   #if JucePlugin_WantsMidiInput
    bool acceptsMidi() const override  { return true; }
   #else
    bool acceptsMidi() const override  { return false; }
   #endif
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.1; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;

    // Live values for the UI
    std::atomic<int>   midiNote { -1 };
    std::atomic<float> inputHz { 0.0f }, outputHz { 0.0f }, inputLevel { 0.0f };

private:
    void processRange (juce::AudioBuffer<float>&, int start, int len);

    static constexpr int kMaxChunk = 2048;
    std::atomic<float> *pMode, *pPitch, *pFormant, *pLink, *pDrive, *pMix, *pOutput;

    vbx::ShiftEngine engine;
    juce::AudioBuffer<float> dryDelay;
    int dryWrite = 0;
    std::vector<float> monoBuf, wetBuf;
    float levelPeak = 0.0f;
    juce::SmoothedValue<float> mixSmoothed, gainSmoothed;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxShiftAudioProcessor)
};

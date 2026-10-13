#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "dsp/Dsp.h"
#include "dsp/AutoTune.h"
#include "dsp/Presets.h"

class DiscoSauceProcessor : public juce::AudioProcessor
{
public:
    DiscoSauceProcessor();
    ~DiscoSauceProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Disco Sauce 1"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Loads a preset: sets the preset index, the five knobs and the retune speed.
    void applyPreset(int index);

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void setParam(const juce::String& id, float plainValue);

    double sr = 44100.0;
    ds::AutoTune tune;

    // EQ stage A (pre-compression): high-pass, low shelf, mud cut
    ds::Filter hpF[2], lowF[2], mudF[2];
    // EQ stage B (post-compression): presence, air
    ds::Filter presF[2], airF[2];
    // de-esser side-chain / split band
    ds::Filter essF[2];
    double essEnvHf = 0.0, essEnvFull = 0.0, essGain = 1.0;

    ds::Compressor comp1, comp2;
    ds::PingPongDelay delay;
    ds::FDNReverb reverb;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smMix, smOut, smDly, smRev;
    double duckEnv = 0.0;

    std::vector<float> dryL, dryR, mono, dlyL, dlyR, revIn, revL, revR;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DiscoSauceProcessor)
};

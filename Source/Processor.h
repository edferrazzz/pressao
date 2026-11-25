#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine.h"

namespace pressao {

class Processor final : public juce::AudioProcessor {
public:
    Processor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Pressao"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    // Presets are also exposed as host programs.
    int getNumPrograms() override { return numPresets; }
    int getCurrentProgram() override { return presetIndex(); }
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    float amount() const { return amountParam->get() / 100.0f; }       // 0..1
    int presetIndex() const { return presetParam->getIndex(); }
    bool isBypassed() const { return bypassParam->get(); }
    float level() const { return engine.level(); }
    void stepPreset(int delta);
    void choosePreset(int index);

    juce::AudioParameterFloat* amountParam = nullptr;
    juce::AudioParameterChoice* presetParam = nullptr;
    juce::AudioParameterBool* bypassParam = nullptr;

    static juce::String presetName(int index);   // "SÓ PRESSÃO" (UTF-8 safe)
    static juce::String versionString() { return JucePlugin_VersionString; }

private:
    Engine engine;
};

} // namespace pressao

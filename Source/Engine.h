#pragma once

// PRESSÃO audio engine. Pure DSP, no parameters tree, so the tests can drive it directly.
// Chain: compressor -> drive -> bitcrush -> clip (2x oversampled) -> low/high pass -> gate -> mono
//        -> reverb -> loudness match -> soft ceiling.
// With the knob at 0 every module is neutral.

#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include "Presets.h"

namespace pressao {

struct HostTime {
    double bpm = 120.0;
    double ppq = 0.0;
    bool hasPpq = false;
};

// Levels of every module for a preset at knob position p (0..1). Also used by the tests.
struct Levels {
    float drive = 0, comp = 0, clip = 0, crush = 0, lowpass = 0, highpass = 0, reverb = 0, mono = 0, gate = 0;
    float lowpassHz = 20000.0f, highpassHz = 20.0f;
};
Levels levelsFor(const Preset&, float p);

class Engine {
public:
    Engine();
    void prepare(double sampleRate, int maxBlock);
    void reset();
    // amount 0..1, preset index 0..9
    void process(juce::AudioBuffer<float>&, float amount, int preset, const HostTime&);
    int latencySamples() const { return latency; }
    // Output loudness (0..1-ish), for the flames to dance with the music.
    float level() const { return meter.load(); }

private:
    void updateFilters(const Levels&);

    double sr = 44100.0;
    int maxBlock = 512;
    int latency = 0;
    juce::dsp::Oversampling<float> os;
    juce::SmoothedValue<float> knob;
    int lastPreset = -1;

    // compressor (stereo-linked, feed-forward)
    float compEnv = 0.0f;
    // crush state (per channel)
    float crushHold[2] {}; float crushCount = 0.0f;
    // filters
    juce::dsp::StateVariableTPTFilter<float> lp[2], hp[2];
    // gate
    double gatePhase = 0.0; float gateGain = 1.0f;
    // reverb
    juce::dsp::Reverb reverb;
    juce::AudioBuffer<float> dry;
    // loudness match
    double dryMs = 0.0, wetMs = 0.0; float outGain = 1.0f;
    std::atomic<float> meter { 0.0f };
};

} // namespace pressao

#pragma once

// PRESSÃO audio engine: a pre-drop riser. Pure DSP, no parameters tree, so the tests can drive it directly.
// Chain: compressor -> drive -> bitcrush -> clip (2x oversampled) -> low/high pass -> gate -> noise riser
//        -> stereo (mono / wide) -> delay -> reverb -> level match -> soft ceiling.
// With the knob at 0 every module is neutral. When the knob drops back to 0 (the drop), the reverb and
// delay tails are cut so the drop comes in dry.

#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <vector>
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
    float delay = 0, noise = 0, widen = 0;
    float lowpassHz = 20000.0f, highpassHz = 20.0f;
};
Levels levelsFor(const Preset&, float p);

// Below this nothing goes into the reverb or the delay: the low end stays dry and tight.
constexpr float sendCutHz = 320.0f;

class Engine {
public:
    Engine();
    void prepare(double sampleRate, int maxBlock);
    void reset();
    // amount 0..1, preset index 0..9
    void process(juce::AudioBuffer<float>&, float amount, int preset, const HostTime&);
    // any preset (MEU PRESET); presetIndex only tells when the preset changed
    void process(juce::AudioBuffer<float>&, float amount, const Preset&, int presetIndex, const HostTime&);
    int latencySamples() const { return latency; }
    // Output loudness (0..1-ish), for the flames to dance with the music.
    float level() const { return meter.load(); }
    // How much reverb/riser is going on (0..1), for the steam.
    float steam() const { return steamMeter.load(); }

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
    juce::AudioBuffer<float> dry, wet;
    // loudness match
    double dryMs = 0.0, wetMs = 0.0; float outGain = 1.0f;
    std::atomic<float> meter { 0.0f };
    std::atomic<float> steamMeter { 0.0f };
    // drop detection
    float peakKnob = 0.0f;
    // delay
    std::vector<float> delayBuf[2];
    int delayPos = 0;
    // noise riser
    juce::dsp::StateVariableTPTFilter<float> noiseBp;
    juce::Random noiseRng { 12345 };
    float inputEnv = 0.0f;
    juce::dsp::StateVariableTPTFilter<float> sendHpA[2], sendHpB[2], sendLp[2], delayHp[2];
    // limiter
    int lookahead = 72;
    std::vector<float> limDelay[2], limPeaks;
    int limPos = 0;
    float limGain = 1.0f;
    static constexpr float ceiling = 0.966f;   // -0.3 dBFS
};

} // namespace pressao

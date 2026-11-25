#include "Engine.h"

#include <cmath>

namespace pressao {

namespace {
float smoothstep(float x) {
    x = juce::jlimit(0.0f, 1.0f, x);
    return x * x * (3.0f - 2.0f * x);
}

float ramp(const Ramp& r, float p) {
    if (r.amount <= 0.0f) return 0.0f;
    const float span = juce::jmax(0.001f, r.to - r.from);
    return r.amount * smoothstep((p - r.from) / span);
}

float softCeiling(float x) {
    const float a = std::abs(x);
    if (a <= 0.89f) return x;
    const float over = (a - 0.89f) / 0.11f;
    return std::copysign(0.89f + 0.11f * std::tanh(over), x);
}
} // namespace

Levels levelsFor(const Preset& pr, float p) {
    p = juce::jlimit(0.0f, 1.0f, p);
    Levels l;
    l.drive = ramp(pr.drive, p);
    l.comp = ramp(pr.comp, p);
    l.clip = ramp(pr.clip, p);
    l.crush = ramp(pr.crush, p);
    l.reverb = ramp(pr.reverb, p);
    l.mono = ramp(pr.mono, p);
    l.gate = ramp(pr.gate, p);
    if (pr.filter == FilterShape::closeThenRise) {
        // low pass closes over the first half, then reopens while the high pass rises
        const float split = 0.55f;
        l.lowpass = p < split ? smoothstep(p / split) : 1.0f - smoothstep((p - split) / 0.25f);
        l.highpass = smoothstep((p - split) / (1.0f - split));
    } else {
        l.lowpass = ramp(pr.lowpass, p);
        l.highpass = ramp(pr.highpass, p);
    }
    l.lowpassHz = 20000.0f * std::pow(pr.lowpassMinHz / 20000.0f, l.lowpass);
    l.highpassHz = 20.0f * std::pow(pr.highpassMaxHz / 20.0f, l.highpass);
    return l;
}

Engine::Engine() : os(2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false) {}

void Engine::prepare(double sampleRate, int block) {
    sr = sampleRate;
    maxBlock = juce::jmax(32, block);
    os.initProcessing((size_t) maxBlock);
    latency = (int) std::lround(os.getLatencyInSamples());
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) maxBlock, 1 };
    for (int c = 0; c < 2; ++c) {
        lp[c].prepare(spec); lp[c].setType(juce::dsp::StateVariableTPTFilterType::lowpass);
        hp[c].prepare(spec); hp[c].setType(juce::dsp::StateVariableTPTFilterType::highpass);
        lp[c].setResonance(0.9f); hp[c].setResonance(0.9f);
    }
    reverb.prepare({ sr, (juce::uint32) maxBlock, 2 });
    dry.setSize(2, maxBlock);
    knob.reset(sr, 0.05);
    reset();
}

void Engine::reset() {
    os.reset();
    compEnv = 0.0f;
    crushHold[0] = crushHold[1] = 0.0f; crushCount = 0.0f;
    for (int c = 0; c < 2; ++c) { lp[c].reset(); hp[c].reset(); }
    reverb.reset();
    gatePhase = 0.0; gateGain = 1.0f;
    dryMs = wetMs = 0.0; outGain = 1.0f;
}

void Engine::updateFilters(const Levels& l) {
    const float nyq = (float) sr * 0.45f;
    for (int c = 0; c < 2; ++c) {
        lp[c].setCutoffFrequency(juce::jmin(nyq, l.lowpassHz));
        hp[c].setCutoffFrequency(juce::jmin(nyq, l.highpassHz));
    }
}

void Engine::process(juce::AudioBuffer<float>& buffer, float amount, int presetIndex, const HostTime& time) {
    const int ch = juce::jmin(2, buffer.getNumChannels());
    const int n = buffer.getNumSamples();
    if (ch == 0 || n == 0) return;
    presetIndex = juce::jlimit(0, numPresets - 1, presetIndex);
    const Preset& pr = presets()[(size_t) presetIndex];
    if (presetIndex != lastPreset) {
        if (lastPreset >= 0) reverb.reset();
        lastPreset = presetIndex;
    }

    knob.setTargetValue(juce::jlimit(0.0f, 1.0f, amount));
    for (int c = 0; c < ch; ++c) dry.copyFrom(c, 0, buffer, c, 0, n);

    // The knob is smoothed per block (blocks are short), levels computed once per block.
    knob.skip(n);
    const float p = knob.getCurrentValue();
    const Levels l = levelsFor(pr, p);

    // ---- oversampled part: compressor, drive, crush, clip ----
    juce::dsp::AudioBlock<float> block(buffer.getArrayOfWritePointers(), (size_t) ch, (size_t) n);
    auto up = os.processSamplesUp(block);
    const int un = (int) up.getNumSamples();
    const double usr = sr * 2.0;
    const float att = (float) std::exp(-1.0 / (0.004 * usr)), rel = (float) std::exp(-1.0 / (0.09 * usr));
    const float thresholdDb = -l.comp * 32.0f;
    const float ratio = 1.0f + l.comp * 5.0f;
    const float driveGain = juce::Decibels::decibelsToGain(l.drive * pr.driveMaxDb);
    const float driveMix = juce::jmin(1.0f, l.drive * 4.0f);
    const float shapeNorm = 1.0f / std::tanh(juce::jmax(1.0f, driveGain));
    const float clipT = 1.0f - 0.75f * l.clip;
    const float crushStep = 1.0f + l.crush * 22.0f;
    const float crushQ = std::pow(2.0f, 15.0f - l.crush * 11.0f);
    float* u[2] { up.getChannelPointer(0), up.getChannelPointer((size_t) (ch > 1 ? 1 : 0)) };
    for (int i = 0; i < un; ++i) {
        // linked detector
        float peak = std::abs(u[0][i]);
        if (ch > 1) peak = juce::jmax(peak, std::abs(u[1][i]));
        compEnv = peak > compEnv ? att * compEnv + (1.0f - att) * peak : rel * compEnv + (1.0f - rel) * peak;
        float g = 1.0f;
        if (l.comp > 0.0f) {
            const float envDb = juce::Decibels::gainToDecibels(compEnv + 1.0e-9f);
            if (envDb > thresholdDb) g = juce::Decibels::decibelsToGain((thresholdDb - envDb) * (1.0f - 1.0f / ratio));
            g *= juce::Decibels::decibelsToGain(-thresholdDb * 0.5f * (1.0f - 1.0f / ratio));   // partial makeup
        }
        const bool hold = l.crush > 0.0f && (crushCount += 1.0f) < crushStep;
        if (!hold) crushCount = 0.0f;
        for (int c = 0; c < ch; ++c) {
            float x = u[c][i] * g;
            if (driveMix > 0.0f) {
                // asymmetric tanh: more even harmonics, the "fat" part
                const float y = (std::tanh(driveGain * x + 0.15f * l.drive) - std::tanh(0.15f * l.drive)) * shapeNorm;
                x += driveMix * (y - x);
            }
            if (l.crush > 0.0f) {
                if (!hold) crushHold[c] = std::round(x * crushQ) / crushQ;
                x += juce::jmin(1.0f, l.crush * 3.0f) * (crushHold[c] - x);
            }
            if (l.clip > 0.0f) {
                const float b = x * (1.0f + l.clip * 1.5f);
                x = juce::jlimit(-clipT, clipT, b) / clipT * 0.9f;
            }
            u[c][i] = x;
        }
    }
    os.processSamplesDown(block);

    // ---- base rate: filters, gate, mono ----
    updateFilters(l);
    const bool useLp = l.lowpassHz < 19000.0f, useHp = l.highpassHz > 22.0f;
    // gate: divisions per beat accelerate with the knob: 1/4 -> 1/8 -> 1/16 -> 1/32
    const double perBeat = std::pow(2.0, std::floor(juce::jmin(3.0f, l.gate * 3.99f)));
    const double beatsPerSample = time.bpm / 60.0 / sr;
    if (time.hasPpq) gatePhase = time.ppq * perBeat;
    const float gateSmooth = (float) std::exp(-1.0 / (0.002 * sr));
    const float depth = juce::jmin(1.0f, l.gate * 1.3f);
    const float width = 1.0f - l.mono;
    for (int i = 0; i < n; ++i) {
        for (int c = 0; c < ch; ++c) {
            float x = buffer.getSample(c, i);
            if (useHp) x = hp[c].processSample(0, x);
            if (useLp) x = lp[c].processSample(0, x);
            buffer.setSample(c, i, x);
        }
        if (l.gate > 0.0f) {
            const double ph = gatePhase - std::floor(gatePhase);
            const float target = ph < 0.55 ? 1.0f : 1.0f - depth;
            gateGain = gateSmooth * gateGain + (1.0f - gateSmooth) * target;
            for (int c = 0; c < ch; ++c) buffer.setSample(c, i, buffer.getSample(c, i) * gateGain);
        }
        gatePhase += beatsPerSample * perBeat;
        if (ch == 2 && l.mono > 0.0f) {
            const float a = buffer.getSample(0, i), b = buffer.getSample(1, i);
            const float m = 0.5f * (a + b), s = 0.5f * (a - b) * width;
            buffer.setSample(0, i, m + s);
            buffer.setSample(1, i, m - s);
        }
    }
    if (!useLp) for (auto& f : lp) f.reset();
    if (!useHp) for (auto& f : hp) f.reset();

    // ---- reverb ----
    if (l.reverb > 0.001f) {
        juce::Reverb::Parameters rp;
        rp.roomSize = 0.55f + 0.42f * l.reverb;
        rp.damping = 0.35f;
        rp.wetLevel = 0.55f * l.reverb;
        rp.dryLevel = 1.0f - 0.4f * l.reverb;
        rp.width = 1.0f;
        reverb.setParameters(rp);
        juce::dsp::AudioBlock<float> b(buffer.getArrayOfWritePointers(), (size_t) ch, (size_t) n);
        juce::dsp::ProcessContextReplacing<float> ctx(b);
        reverb.process(ctx);
    }

    // ---- loudness match: the knob changes the character, not just the volume ----
    double d = 0.0, w = 0.0;
    for (int c = 0; c < ch; ++c) {
        const float* a = dry.getReadPointer(c);
        const float* b = buffer.getReadPointer(c);
        for (int i = 0; i < n; ++i) { d += (double) a[i] * a[i]; w += (double) b[i] * b[i]; }
    }
    const double cnt = (double) (ch * n);
    const double k = 1.0 - std::exp(-(double) n / (0.25 * sr));
    dryMs += k * (d / cnt - dryMs);
    wetMs += k * (w / cnt - wetMs);
    float target = outGain;
    if (dryMs > 1.0e-9 && wetMs > 1.0e-12) target = juce::jlimit(0.2f, 3.0f, (float) std::sqrt(dryMs / wetMs));
    // a little louder as the pressure builds
    target *= juce::Decibels::decibelsToGain(2.0f * p);
    for (int c = 0; c < ch; ++c) buffer.applyGainRamp(c, 0, n, outGain, target);
    outGain = target;

    float peak = 0.0f;
    for (int c = 0; c < ch; ++c) {
        float* b = buffer.getWritePointer(c);
        for (int i = 0; i < n; ++i) { b[i] = softCeiling(b[i]); peak = juce::jmax(peak, std::abs(b[i])); }
    }
    meter.store(0.8f * meter.load() + 0.2f * peak);
}

} // namespace pressao

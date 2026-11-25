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
    l.delay = ramp(pr.delay, p);
    l.noise = ramp(pr.noise, p);
    l.widen = ramp(pr.widen, p);
    if (pr.filter == FilterShape::closeThenRise) {
        // low pass closes over the first half, then reopens while the high pass rises
        const float split = 0.55f;
        l.lowpass = pr.lowpass.amount * (p < split ? smoothstep(p / split) : 1.0f - smoothstep((p - split) / 0.25f));
        l.highpass = pr.highpass.amount * smoothstep((p - split) / (1.0f - split));
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
    lookahead = juce::jmax(8, (int) std::lround(sr * 0.0015));
    latency = (int) std::lround(os.getLatencyInSamples()) + lookahead;
    for (auto& d : limDelay) d.assign((size_t) lookahead + 1, 0.0f);
    limPeaks.assign((size_t) lookahead + 1, 0.0f);
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) maxBlock, 1 };
    for (int c = 0; c < 2; ++c) {
        lp[c].prepare(spec); lp[c].setType(juce::dsp::StateVariableTPTFilterType::lowpass);
        hp[c].prepare(spec); hp[c].setType(juce::dsp::StateVariableTPTFilterType::highpass);
        lp[c].setResonance(0.9f); hp[c].setResonance(0.9f);
    }
    reverb.prepare({ sr, (juce::uint32) maxBlock, 2 });
    for (auto& b : delayBuf) b.assign((size_t) (sr * 2.5), 0.0f);
    noiseBp.prepare(spec); noiseBp.setType(juce::dsp::StateVariableTPTFilterType::bandpass); noiseBp.setResonance(1.2f);
    // what goes into the reverb and the delay: nothing below the cut (no muddy low end), no harsh top
    for (int c = 0; c < 2; ++c) {
        for (auto* f : { &sendHpA[c], &sendHpB[c], &delayHp[c] }) {
            f->prepare(spec); f->setType(juce::dsp::StateVariableTPTFilterType::highpass);
            f->setCutoffFrequency(sendCutHz); f->setResonance(0.707f);
        }
        sendLp[c].prepare(spec); sendLp[c].setType(juce::dsp::StateVariableTPTFilterType::lowpass);
        sendLp[c].setCutoffFrequency(juce::jmin(11000.0f, (float) sr * 0.45f)); sendLp[c].setResonance(0.707f);
    }
    dry.setSize(2, maxBlock);
    wet.setSize(2, maxBlock);
    knob.reset(sr, 0.05);
    reset();
}

void Engine::reset() {
    os.reset();
    compEnv = 0.0f;
    crushHold[0] = crushHold[1] = 0.0f; crushCount = 0.0f;
    for (int c = 0; c < 2; ++c) { lp[c].reset(); hp[c].reset(); }
    reverb.reset();
    for (auto& b : delayBuf) std::fill(b.begin(), b.end(), 0.0f);
    delayPos = 0;
    noiseBp.reset(); inputEnv = 0.0f; peakKnob = 0.0f;
    for (int c = 0; c < 2; ++c) { sendHpA[c].reset(); sendHpB[c].reset(); delayHp[c].reset(); sendLp[c].reset(); }
    for (auto& d : limDelay) std::fill(d.begin(), d.end(), 0.0f);
    std::fill(limPeaks.begin(), limPeaks.end(), 0.0f);
    limPos = 0; limGain = 1.0f;
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
    process(buffer, amount, presets()[(size_t) juce::jlimit(0, numPresets - 1, presetIndex)], presetIndex, time);
}

void Engine::process(juce::AudioBuffer<float>& buffer, float amount, const Preset& pr, int presetIndex, const HostTime& time) {
    const int ch = juce::jmin(2, buffer.getNumChannels());
    const int n = buffer.getNumSamples();
    if (ch == 0 || n == 0) return;
    if (presetIndex != lastPreset) {
        if (lastPreset >= 0) reverb.reset();
        lastPreset = presetIndex;
    }

    amount = juce::jlimit(0.0f, 1.0f, amount);
    // The drop: the knob went up and is now back at (almost) 0 -> cut every tail, go dry right away.
    peakKnob = juce::jmax(peakKnob, amount);
    if (amount < 0.02f && peakKnob > 0.25f) {
        reverb.reset();
        for (auto& b : delayBuf) std::fill(b.begin(), b.end(), 0.0f);
        knob.setCurrentAndTargetValue(amount);
        peakKnob = 0.0f;
        outGain = 1.0f; dryMs = wetMs = 0.0;
        compEnv = 0.0f; gateGain = 1.0f;
    }
    if (amount < 0.02f) peakKnob = juce::jmin(peakKnob, 0.25f);
    knob.setTargetValue(amount);
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
    const float q = pr.resonant ? 2.6f : 0.9f;
    for (int c = 0; c < 2; ++c) { lp[c].setResonance(q); hp[c].setResonance(q); }
    const bool useLp = l.lowpassHz < 19000.0f, useHp = l.highpassHz > 22.0f;
    // gate: divisions per beat accelerate with the knob: 1/4 -> 1/8 -> 1/16 -> 1/32
    const double perBeat = std::pow(2.0, std::floor(juce::jmin(3.0f, l.gate * 3.99f)));
    const double beatsPerSample = time.bpm / 60.0 / sr;
    if (time.hasPpq) gatePhase = time.ppq * perBeat;
    const float gateSmooth = (float) std::exp(-1.0 / (0.002 * sr));
    const float depth = juce::jmin(1.0f, l.gate * 1.3f);
    const float width = (1.0f - l.mono) * (1.0f + 0.9f * l.widen);
    const float envRel = (float) std::exp(-1.0 / (0.4 * sr));
    const float noiseGain = 0.9f * l.noise * l.noise;
    if (l.noise > 0.0f) noiseBp.setCutoffFrequency(juce::jmin((float) sr * 0.4f, 600.0f * std::pow(16.0f, l.noise)));
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
        if (l.noise > 0.0f) {
            // white noise through a band pass that climbs, following the music's level
            float in = 0.0f;
            for (int c = 0; c < ch; ++c) in = juce::jmax(in, std::abs(dry.getSample(c, i)));
            inputEnv = in > inputEnv ? in : inputEnv * envRel;
            const float nz = noiseBp.processSample(0, noiseRng.nextFloat() * 2.0f - 1.0f) * noiseGain * inputEnv;
            for (int c = 0; c < ch; ++c) buffer.setSample(c, i, buffer.getSample(c, i) + nz * (c == 0 ? 1.0f : 0.9f));
        }
        if (ch == 2 && (l.mono > 0.0f || l.widen > 0.0f)) {
            const float a = buffer.getSample(0, i), b = buffer.getSample(1, i);
            const float m = 0.5f * (a + b), s = 0.5f * (a - b) * width;
            buffer.setSample(0, i, m + s);
            buffer.setSample(1, i, m - s);
        }
    }
    if (!useLp) for (auto& f : lp) f.reset();
    if (!useHp) for (auto& f : hp) f.reset();

    // ---- delay: dotted 1/8 ping-pong in time, feedback climbs ----
    if (l.delay > 0.001f) {
        const int len = (int) delayBuf[0].size();
        const int dt = juce::jlimit(1, len - 1, (int) std::lround(sr * 60.0 / juce::jmax(40.0, time.bpm) * (pr.delayDiv == 1 ? 1.0 : pr.delayDiv == 2 ? 0.25 : 0.75)));
        const float fb = 0.3f + 0.45f * l.delay, mix = 0.6f * l.delay;
        for (int i = 0; i < n; ++i) {
            int rp = delayPos - dt; if (rp < 0) rp += len;
            const float l0 = delayBuf[0][(size_t) rp], r0 = delayBuf[1][(size_t) rp];
            const float inL = buffer.getSample(0, i), inR = buffer.getSample(ch > 1 ? 1 : 0, i);
            const float send = delayHp[1].processSample(0, delayHp[0].processSample(0, 0.5f * (inL + inR)));
            delayBuf[0][(size_t) delayPos] = send * 0.7f + r0 * fb;   // ping-pong
            delayBuf[1][(size_t) delayPos] = l0 * fb;
            buffer.setSample(0, i, inL + mix * l0);
            if (ch > 1) buffer.setSample(1, i, inR + mix * r0);
            if (++delayPos == len) delayPos = 0;
        }
    }

    // ---- reverb: the riser wash, fed without the low end ----
    float wetShare = 0.0f;
    if (l.reverb > 0.001f) {
        juce::Reverb::Parameters rp;
        if (pr.reverbType == 0) { rp.roomSize = 0.45f + 0.3f * l.reverb; rp.damping = 0.5f; }        // SALA
        else if (pr.reverbType == 2) { rp.roomSize = 0.8f + 0.195f * l.reverb; rp.damping = 0.1f; } // CÉU
        else { rp.roomSize = 0.6f + 0.39f * l.reverb; rp.damping = 0.25f; }                          // HALL
        rp.wetLevel = 1.0f;
        rp.dryLevel = 0.0f;
        rp.width = 1.0f;
        reverb.setParameters(rp);
        for (int c = 0; c < ch; ++c) wet.copyFrom(c, 0, buffer, c, 0, n);
        for (int c = 0; c < ch; ++c) {
            float* w = wet.getWritePointer(c);
            for (int i = 0; i < n; ++i) w[i] = sendLp[c].processSample(0, sendHpB[c].processSample(0, sendHpA[c].processSample(0, w[i])));
        }
        juce::dsp::AudioBlock<float> b(wet.getArrayOfWritePointers(), (size_t) ch, (size_t) n);
        juce::dsp::ProcessContextReplacing<float> ctx(b);
        reverb.process(ctx);
        wetShare = l.reverb;
        const float dryG = 1.0f - 0.6f * l.reverb * l.reverb, wetG = 0.75f * l.reverb;
        for (int c = 0; c < ch; ++c) {
            buffer.applyGain(c, 0, n, dryG);
            buffer.addFrom(c, 0, wet, c, 0, n, wetG);
        }
    }
    steamMeter.store(juce::jmax(wetShare, l.delay * 0.8f, l.noise * 0.8f));

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
    if (dryMs > 1.0e-9 && wetMs > 1.0e-12) {
        // Never louder than the original. When the riser thins the sound out (high pass), give back
        // only part of it: a riser is allowed to get thinner, not to vanish.
        const float matchDb = juce::Decibels::gainToDecibels((float) std::sqrt(dryMs / wetMs));
        target = juce::Decibels::decibelsToGain(juce::jlimit(-18.0f, 8.0f, matchDb > 0.0f ? matchDb * 0.7f : matchDb));
    }
    for (int c = 0; c < ch; ++c) buffer.applyGainRamp(c, 0, n, outGain, target);
    outGain = target;

    // ---- limiter: look-ahead peak limiter, ceiling -0.3 dBFS ----
    float peak = 0.0f;
    const int L = lookahead, len = L + 1;
    const float limRel = (float) std::exp(-1.0 / (0.08 * sr));
    float* bl = buffer.getWritePointer(0);
    float* br = buffer.getWritePointer(ch > 1 ? 1 : 0);
    for (int i = 0; i < n; ++i) {
        const float in = juce::jmax(std::abs(bl[i]), std::abs(br[i]));
        limPeaks[(size_t) limPos] = in > ceiling ? ceiling / in : 1.0f;   // gain this sample needs
        limDelay[0][(size_t) limPos] = bl[i];
        limDelay[1][(size_t) limPos] = br[i];
        float need = 1.0f;
        for (float g : limPeaks) need = juce::jmin(need, g);              // anything over the next L samples
        // attack reaches the needed gain within the look-ahead window, release is smooth
        limGain = need < limGain ? limGain + (need - limGain) * (1.0f / (float) L) * 4.0f : limRel * limGain + (1.0f - limRel) * need;
        limGain = juce::jmin(limGain, 1.0f);
        int rp = limPos + 1; if (rp >= len) rp = 0;
        const float g = limGain;
        const float ol = juce::jlimit(-ceiling, ceiling, limDelay[0][(size_t) rp] * g);
        const float orr = juce::jlimit(-ceiling, ceiling, limDelay[1][(size_t) rp] * g);
        bl[i] = ol;
        if (ch > 1) br[i] = orr;
        peak = juce::jmax(peak, std::abs(ol), std::abs(orr));
        if (++limPos >= len) limPos = 0;
    }
    meter.store(0.8f * meter.load() + 0.2f * peak);
}

} // namespace pressao

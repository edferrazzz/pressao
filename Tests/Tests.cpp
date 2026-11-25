// Audio checks for every PRESSÃO preset and knob position.
// PRESSAO_SNAPSHOT_DIR=<dir> also writes PNGs of the window (run under xvfb-run on Linux).
// PRESSAO_DEMO_DIR=<dir> with PRESSAO_DEMO_INPUT=<wav> writes one pre-drop demo per preset.

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <cstdio>
#include <random>
#include "Editor.h"
#include "Engine.h"
#include "Processor.h"

using namespace pressao;

namespace {
int failures = 0, checks = 0;
void expect(bool ok, const juce::String& what) {
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL: %s\n", what.toRawUTF8()); }
}

constexpr double sr = 48000.0;
constexpr int block = 256;

// a little beat: kick every beat, a saw chord, hats
juce::AudioBuffer<float> testSignal(double seconds, float peak) {
    const int n = (int) (seconds * sr);
    juce::AudioBuffer<float> b(2, n);
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    const double beat = sr * 0.5;
    for (int i = 0; i < n; ++i) {
        const double t = i / sr;
        const double inBeat = std::fmod((double) i, beat) / sr;
        const float kick = (float) (std::sin(2.0 * juce::MathConstants<double>::pi * (50.0 + 90.0 * std::exp(-inBeat * 30.0)) * inBeat)
                                    * std::exp(-inBeat * 7.0));
        float chord = 0.0f;
        for (double f : { 220.0, 277.18, 329.63 }) chord += (float) (2.0 * std::fmod(t * f, 1.0) - 1.0) * 0.12f;
        const double inHat = std::fmod((double) i, beat * 0.5) / sr;
        const float hat = noise(rng) * (float) std::exp(-inHat * 60.0) * 0.25f;
        const float l = kick * 0.7f + chord + hat, r = kick * 0.7f + chord * 0.8f - hat * 0.6f;
        b.setSample(0, i, l * peak);
        b.setSample(1, i, r * peak);
    }
    return b;
}

double rms(const juce::AudioBuffer<float>& b, int from = 0) {
    double s = 0.0; int cnt = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = from; i < b.getNumSamples(); ++i) { s += (double) b.getSample(c, i) * b.getSample(c, i); ++cnt; }
    return std::sqrt(s / juce::jmax(1, cnt));
}

void run(Engine& e, juce::AudioBuffer<float>& b, float amount, int preset) {
    HostTime time;
    for (int start = 0; start < b.getNumSamples(); start += block) {
        const int n = juce::jmin(block, b.getNumSamples() - start);
        juce::AudioBuffer<float> view(b.getArrayOfWritePointers(), 2, start, n);
        time.ppq = (double) start / sr * 2.0;
        time.hasPpq = true;
        e.process(view, amount, preset, time);
    }
}

void testPresets() {
    const auto input = testSignal(4.0, 0.7f);
    const double inRms = rms(input, (int) sr);
    for (int p = 0; p < numPresets; ++p) {
        const auto name = Processor::presetName(p);
        for (float k : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }) {
            Engine e;
            e.prepare(sr, block);
            auto b = input;
            run(e, b, k, p);
            bool finite = true; float peak = 0.0f;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < b.getNumSamples(); ++i) {
                    const float v = b.getSample(c, i);
                    finite = finite && std::isfinite(v);
                    peak = juce::jmax(peak, std::abs(v));
                }
            const auto tag = name + " @" + juce::String((int) (k * 100));
            expect(finite, tag + " finite");
            expect(peak <= 0.967f, tag + " limiter holds -0.3 dBFS (peak " + juce::String(peak) + ")");
            const double outRms = rms(b, (int) sr);
            const double db = juce::Decibels::gainToDecibels(outRms / inRms);
            if (k == 0.0f) expect(std::abs(db) < 1.0, tag + " transparent at 0 (" + juce::String(db, 2) + " dB)");
            else expect(db > -12.0 && db < 0.6, tag + " never louder than the original (" + juce::String(db, 2) + " dB)");
            if (k == 1.0f) {
                // must clearly change the sound
                double num = 0.0, da = 0.0, db2 = 0.0;
                const int lat = e.latencySamples();
                for (int i = (int) sr; i < b.getNumSamples(); ++i) {
                    const double x = input.getSample(0, i - lat), y = b.getSample(0, i);
                    num += x * y; da += x * x; db2 += y * y;
                }
                const double corr = num / std::sqrt(da * db2 + 1e-12);
                expect(corr < 0.97, tag + " changes the sound (corr " + juce::String(corr, 3) + ")");
            }
        }
    }
    // FECHA E SOBE: low pass closed in the middle, high pass up at the end
    const auto l50 = levelsFor(presets()[1], 0.5f), l100 = levelsFor(presets()[1], 1.0f);
    for (int p = 0; p < numPresets; ++p) {
        const auto top = levelsFor(presets()[(size_t) p], 1.0f);
        expect(top.reverb > 0.5f || top.delay > 0.5f, Processor::presetName(p) + " is a riser (reverb/delay at the top)");
    }
    expect(l50.lowpassHz < 600.0f, "FECHA E SOBE low pass closes (" + juce::String(l50.lowpassHz) + " Hz)");
    expect(l100.lowpassHz > 15000.0f && l100.highpassHz > 1500.0f, "FECHA E SOBE ends with high pass up");
    // the drop: after riding the knob up, going back to 0 must give the dry sound right away (no reverb tail)
    for (int p = 0; p < numPresets; ++p) {
        Engine e; e.prepare(sr, block);
        auto b = testSignal(3.0, 0.7f);
        const auto ref = b;
        HostTime time; time.hasPpq = true;
        const int dropAt = (int) (2.0 * sr);
        for (int start = 0; start < b.getNumSamples(); start += block) {
            const int n = juce::jmin(block, b.getNumSamples() - start);
            juce::AudioBuffer<float> view(b.getArrayOfWritePointers(), 2, start, n);
            time.ppq = (double) start / sr * 2.0;
            e.process(view, start < dropAt ? (float) start / (float) dropAt : 0.0f, p, time);
        }
        // compare 50 ms after the drop with the input (latency-aligned)
        const int lat = e.latencySamples();
        double num = 0.0, da = 0.0, db2 = 0.0;
        for (int i = dropAt + (int) (0.05 * sr); i < b.getNumSamples(); ++i) {
            const double x = ref.getSample(0, i - lat), y = b.getSample(0, i);
            num += x * y; da += x * x; db2 += y * y;
        }
        const double corr = num / std::sqrt(da * db2 + 1e-12);
        const double lvl = juce::Decibels::gainToDecibels(std::sqrt(db2 / da));
        expect(corr > 0.98 && std::abs(lvl) < 1.5, Processor::presetName(p) + " drop is dry and clean (corr " + juce::String(corr, 3)
                                                        + ", " + juce::String(lvl, 2) + " dB)");
    }
    // MEU PRESET: every factory preset survives the trip to CRIAR, and a custom preset processes cleanly
    for (int p = 0; p < numPresets; ++p) {
        const auto c = fromPreset(presets()[(size_t) p]);
        const auto back = toPreset(c);
        expect(std::abs(levelsFor(back, 1.0f).reverb - levelsFor(presets()[(size_t) p], 1.0f).reverb) < 0.01f,
               Processor::presetName(p) + " reverb kept in CRIAR");
    }
    {
        Custom c; c.highpass = 1.0f; c.lowpass = 0.8f; c.lowpassType = 1; c.reverb = 1.0f; c.reverbType = 2; c.delay = 0.7f;
        c.delayDiv = 2; c.stereo = 1.0f; c.stereoType = 1; c.noise = 1.0f; c.gate = 0.5f; c.drive = 0.6f; c.highpassType = 1;
        const auto pr = toPreset(c);
        Engine e; e.prepare(sr, block);
        auto b = testSignal(3.0, 0.8f);
        HostTime time; time.hasPpq = true;
        bool finite = true; float peak = 0.0f;
        for (int start = 0; start < b.getNumSamples(); start += block) {
            const int n = juce::jmin(block, b.getNumSamples() - start);
            juce::AudioBuffer<float> view(b.getArrayOfWritePointers(), 2, start, n);
            time.ppq = (double) start / sr * 2.0;
            e.process(view, 1.0f, pr, customSlot, time);
        }
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < b.getNumSamples(); ++i) { finite = finite && std::isfinite(b.getSample(ch, i)); peak = juce::jmax(peak, std::abs(b.getSample(ch, i))); }
        expect(finite && peak <= 1.0f, "MEU PRESET with everything at max is clean (peak " + juce::String(peak) + ")");
    }
    // the low end never goes into the reverb: a 60 Hz burst leaves no tail
    {
        Custom c; c.highpass = 0.0f; c.reverb = 1.0f; c.reverbType = 2;
        const auto pr = toPreset(c);
        Engine e; e.prepare(sr, block);
        juce::AudioBuffer<float> b(2, (int) (3.0 * sr));
        b.clear();
        for (int i = 0; i < (int) sr; ++i)
            for (int ch = 0; ch < 2; ++ch) {
                const float fade = (float) juce::jmin(1.0, juce::jmin(i, (int) sr - i) / (0.05 * sr));   // no clicks
                b.setSample(ch, i, 0.6f * fade * (float) std::sin(2.0 * juce::MathConstants<double>::pi * 60.0 * i / sr));
            }
        HostTime time;
        for (int start = 0; start < b.getNumSamples(); start += block) {
            const int n = juce::jmin(block, b.getNumSamples() - start);
            juce::AudioBuffer<float> view(b.getArrayOfWritePointers(), 2, start, n);
            e.process(view, 1.0f, pr, customSlot, time);
        }
        juce::AudioBuffer<float> tail(2, (int) sr);
        for (int ch = 0; ch < 2; ++ch) tail.copyFrom(ch, 0, b, ch, (int) (1.3 * sr), (int) sr);
        const double tailDb = juce::Decibels::gainToDecibels(rms(tail) / 0.42);
        expect(tailDb < -45.0, "no reverb tail from the low end (" + juce::String(tailDb, 1) + " dB)");
    }
    // silence in, silence out
    {
        Engine e; e.prepare(sr, block);
        juce::AudioBuffer<float> b(2, (int) sr); b.clear();
        run(e, b, 1.0f, 9);
        expect(rms(b) < 1.0e-4, "silence stays silent");
    }
}

void writeSnapshots(const juce::File& dir) {
    dir.createDirectory();
    Processor proc;
    proc.prepareToPlay(sr, block);
    Editor ed(proc);
    const struct { float v; int preset; const char* file; } shots[] {
        { 0.08f, 0, "1-comeco.png" }, { 0.42f, 1, "2-subindo.png" }, { 0.72f, 4, "3-quase.png" }, { 1.0f, 2, "4-topo.png" },
    };
    for (auto& s : shots) {
        proc.presetParam->setValueNotifyingHost(proc.presetParam->convertTo0to1((float) s.preset));
        auto img = ed.renderFrame(s.v, 1.3 + s.v);
        juce::PNGImageFormat png;
        auto f = dir.getChildFile(s.file);
        f.deleteFile();
        if (auto out = f.createOutputStream()) png.writeImageToStream(img, *out);
    }
    proc.presetParam->setValueNotifyingHost(proc.presetParam->convertTo0to1(5.0f));
    ed.setCreating(true);
    {
        auto cimg = ed.renderFrame(0.5f, 2.0);
        auto cf = dir.getChildFile("6-criar.png");
        cf.deleteFile();
        juce::PNGImageFormat cpng;
        if (auto out = cf.createOutputStream()) cpng.writeImageToStream(cimg, *out);
    }
    ed.setCreating(false);
    auto drop = ed.renderFrame(0.0f, 3.0, false, 0.35);
    {
        auto df = dir.getChildFile("7-drop.png");
        df.deleteFile();
        juce::PNGImageFormat dpng;
        if (auto out = df.createOutputStream()) dpng.writeImageToStream(drop, *out);
    }
    proc.presetParam->setValueNotifyingHost(proc.presetParam->convertTo0to1(1.0f));
    auto img = ed.renderFrame(0.3f, 2.0, true);
    juce::PNGImageFormat png;
    auto f = dir.getChildFile("5-info.png");
    f.deleteFile();
    if (auto out = f.createOutputStream()) png.writeImageToStream(img, *out);
    // animation frames for a preview video: knob 0 -> 100 in 6 s, hold, back to 0
    if (std::getenv("PRESSAO_FRAMES") != nullptr) {
        auto fd = dir.getChildFile("frames");
        fd.createDirectory();
        proc.presetParam->setValueNotifyingHost(proc.presetParam->convertTo0to1(1.0f));
        for (int i = 0; i < 300; ++i) {
            const double t = i / 30.0;
            float v = 0.0f;
            if (t < 1.0) v = 0.03f; else if (t < 7.0) v = 0.03f + 0.97f * (float) std::pow((t - 1.0) / 6.0, 1.6); else if (t < 8.5) v = 1.0f; else v = 0.0f;
            auto fr = ed.renderFrame(v, t, false, t >= 8.5 ? t - 8.5 : -1.0);
            auto ff = fd.getChildFile("f" + juce::String(i).paddedLeft('0', 4) + ".png");
            ff.deleteFile();
            if (auto out = ff.createOutputStream()) png.writeImageToStream(fr, *out);
        }
    }
}

// One demo per preset: 8 bars of the loop with the knob rising 0 -> 100, then the drop at 0.
void writeDemos(const juce::File& dir, const juce::File& input, double bpm) {
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(input));
    if (reader == nullptr) { std::printf("demo input not readable\n"); return; }
    const double rate = reader->sampleRate;
    const int bar = (int) std::lround(rate * 60.0 / bpm * 4.0);
    const int buildLen = bar * 8, dropLen = bar * 4;
    juce::AudioBuffer<float> src(2, (int) reader->lengthInSamples);
    reader->read(&src, 0, src.getNumSamples(), 0, true, true);
    dir.createDirectory();
    for (int p = 0; p < numPresets; ++p) {
        juce::AudioBuffer<float> out(2, buildLen + dropLen);
        for (int i = 0; i < out.getNumSamples(); ++i)
            for (int c = 0; c < 2; ++c) out.setSample(c, i, src.getSample(c, i % src.getNumSamples()));
        Engine e;
        e.prepare(rate, block);
        HostTime time; time.bpm = bpm; time.hasPpq = true;
        for (int start = 0; start < out.getNumSamples(); start += block) {
            const int n = juce::jmin(block, out.getNumSamples() - start);
            juce::AudioBuffer<float> view(out.getArrayOfWritePointers(), 2, start, n);
            const float k = start < buildLen ? (float) start / (float) buildLen : 0.0f;
            time.ppq = (double) start / rate * bpm / 60.0;
            e.process(view, k, p, time);
        }
        auto name = juce::String(p + 1).paddedLeft('0', 2) + " " + Processor::presetName(p) + ".wav";
        auto f = dir.getChildFile(juce::File::createLegalFileName(name));
        f.deleteFile();
        std::unique_ptr<juce::OutputStream> os(f.createOutputStream());
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(os.get(), rate, 2, 16, {}, 0));
        if (w != nullptr) { os.release(); w->writeFromAudioSampleBuffer(out, 0, out.getNumSamples()); }
    }
}
} // namespace

int main() {
    juce::ScopedJuceInitialiser_GUI gui;
    testPresets();
    if (auto* d = std::getenv("PRESSAO_DEMO_DIR"))
        if (auto* in = std::getenv("PRESSAO_DEMO_INPUT"))
            writeDemos(juce::File(d), juce::File(in), std::getenv("PRESSAO_DEMO_BPM") ? std::atof(std::getenv("PRESSAO_DEMO_BPM")) : 120.0);
    if (auto* d = std::getenv("PRESSAO_SNAPSHOT_DIR")) writeSnapshots(juce::File(d));
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

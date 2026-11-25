#include "Presets.h"

#include <algorithm>
#include <cmath>

namespace pressao {

namespace {
Preset make(const char* name, std::initializer_list<const char*> tags, const char* help) {
    Preset p {};
    p.name = name;
    int i = 0;
    for (auto* t : tags) if (i < 4) p.tags[i++] = t;
    p.help = help;
    p.driveMaxDb = 26.0f;
    p.lowpassMinHz = 280.0f;
    p.highpassMaxHz = 1800.0f;
    p.filter = FilterShape::normal;
    // the riser base: high pass and reverb grow together
    p.highpass = { 1.0f, 0.0f, 1.0f };
    p.reverb = { 1.0f, 0.0f, 1.0f };
    return p;
}

std::array<Preset, numPresets> build() {
    std::array<Preset, numPresets> a {};

    a[0] = make("SUBIDA", { "HP", "REV" }, "O riser clássico: high pass e reverb crescendo juntos.");

    auto& p1 = a[1] = make("FECHA E SOBE", { "LP", "HP", "REV" }, "O low pass vai fechando e depois o high pass sobe.");
    p1.filter = FilterShape::closeThenRise;
    p1.lowpass = { 1.0f, 0.0f, 1.0f };
    p1.highpassMaxHz = 2200.0f;
    p1.lowpassMinHz = 320.0f;

    auto& p2 = a[2] = make("CÉU", { "HP", "REV", "WIDE" }, "Reverb gigante, high pass alto e estéreo abrindo.");
    p2.highpassMaxHz = 2600.0f;
    p2.widen = { 1.0f, 0.2f, 1.0f };
    p2.reverbType = 2;

    auto& p3 = a[3] = make("TÚNEL", { "LP", "REV" }, "Abafa e afunda no reverb, riser escuro.");
    p3.highpass = { 0.35f, 0.3f, 1.0f };
    p3.lowpass = { 1.0f, 0.0f, 1.0f };
    p3.lowpassMinHz = 300.0f;
    p3.resonant = true;

    auto& p4 = a[4] = make("ECO", { "HP", "DELAY", "REV" }, "Delay no tempo que vai repetindo cada vez mais.");
    p4.delay = { 1.0f, 0.0f, 1.0f };
    p4.reverb = { 0.7f, 0.1f, 1.0f };

    auto& p5 = a[5] = make("VENTANIA", { "HP", "NOISE", "REV" }, "Ruído branco subindo junto com o reverb.");
    p5.noise = { 1.0f, 0.1f, 1.0f };

    auto& p6 = a[6] = make("ASPIRADOR", { "HP", "MONO", "REV" }, "O estéreo fecha e o som é sugado (no drop abre tudo).");
    p6.mono = { 1.0f, 0.0f, 0.85f };
    p6.reverb = { 0.8f, 0.0f, 1.0f };

    auto& p7 = a[7] = make("TREMEDEIRA", { "HP", "GATE", "REV" }, "Corte rítmico que acelera no tempo da música.");
    p7.gate = { 1.0f, 0.0f, 1.0f };
    p7.reverbType = 0;
    p7.reverb = { 0.75f, 0.0f, 1.0f };

    auto& p8 = a[8] = make("RADINHO", { "HP", "LP", "REV" }, "Vira rádio velho e sobe pro reverb.");
    p8.lowpass = { 1.0f, 0.0f, 0.6f };
    p8.lowpassMinHz = 3200.0f;
    p8.drive = { 0.4f, 0.0f, 1.0f };
    p8.mono = { 0.6f, 0.0f, 0.6f };
    p8.reverb = { 0.8f, 0.2f, 1.0f };

    auto& p9 = a[9] = make("FERVURA", { "HP", "REV", "DIST" }, "O riser com gordura: satura e comprime enquanto sobe.");
    p9.drive = { 0.7f, 0.2f, 1.0f };
    p9.comp = { 0.7f, 0.0f, 1.0f };
    p9.noise = { 0.5f, 0.4f, 1.0f };
    return a;
}
} // namespace

Preset toPreset(const Custom& c) {
    Preset p {};
    p.name = "MEU PRESET";
    p.help = "O seu: o 100% do knob chega nos tetos que você escolheu.";
    int t = 0;
    auto tag = [&](bool on, const char* s) { if (on && t < 4) p.tags[t++] = s; };
    tag(c.highpass > 0.01f, "HP");
    tag(c.lowpass > 0.01f, "LP");
    tag(c.reverb > 0.01f, "REV");
    tag(c.delay > 0.01f, "DELAY");
    tag(c.stereo > 0.01f, c.stereoType == 0 ? "MONO" : "WIDE");
    tag(c.noise > 0.01f, "NOISE");
    tag(c.gate > 0.01f, "GATE");
    tag(c.drive > 0.01f, "DIST");
    p.highpass = { c.highpass, 0.0f, 1.0f };
    p.lowpass = { c.lowpass, 0.0f, 1.0f };
    p.reverb = { c.reverb, 0.0f, 1.0f };
    p.delay = { c.delay, 0.0f, 1.0f };
    if (c.stereoType == 0) p.mono = { c.stereo, 0.0f, 1.0f }; else p.widen = { c.stereo, 0.0f, 1.0f };
    p.noise = { c.noise, 0.1f, 1.0f };
    p.gate = { c.gate, 0.0f, 1.0f };
    p.drive = { c.drive, 0.0f, 1.0f };
    p.comp = { c.drive * 0.7f, 0.0f, 1.0f };
    p.driveMaxDb = 26.0f;
    // the slider is the ceiling: amount 1 = the widest range
    p.highpassMaxHz = customHighpassMaxHz;
    p.lowpassMinHz = customLowpassMinHz;
    p.filter = c.lowpassType == 1 ? FilterShape::closeThenRise : FilterShape::normal;
    p.reverbType = c.reverbType;
    p.resonant = c.highpassType == 1;
    p.delayDiv = c.delayDiv;
    return p;
}

Custom fromPreset(const Preset& p) {
    // express the preset's top in the custom ranges, so CRIAR starts from what you were hearing
    auto hpAt = [](float hz) { return std::log(hz / 20.0f) / std::log(customHighpassMaxHz / 20.0f); };
    auto lpAt = [](float hz) { return std::log(hz / 20000.0f) / std::log(customLowpassMinHz / 20000.0f); };
    Custom c;
    c.highpass = std::clamp(p.highpass.amount * hpAt(p.highpassMaxHz), 0.0f, 1.0f);
    c.lowpass = p.filter == FilterShape::closeThenRise ? std::clamp(lpAt(p.lowpassMinHz), 0.0f, 1.0f)
                                                      : std::clamp(p.lowpass.amount * lpAt(p.lowpassMinHz), 0.0f, 1.0f);
    c.lowpassType = p.filter == FilterShape::closeThenRise ? 1 : 0;
    c.reverb = p.reverb.amount;
    c.delay = p.delay.amount;
    c.stereoType = p.widen.amount > p.mono.amount ? 1 : 0;
    c.stereo = std::max(p.widen.amount, p.mono.amount);
    c.noise = p.noise.amount;
    c.gate = p.gate.amount;
    c.drive = p.drive.amount;
    c.highpassType = p.resonant ? 1 : 0;
    c.reverbType = p.reverbType;
    c.delayDiv = p.delayDiv;
    return c;
}

const std::array<Preset, numPresets>& presets() {
    static const auto table = build();
    return table;
}

} // namespace pressao

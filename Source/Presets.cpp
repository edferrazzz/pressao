#include "Presets.h"

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
    p.highpassMaxHz = 1400.0f;
    p.filter = FilterShape::normal;
    return p;
}

std::array<Preset, numPresets> build() {
    std::array<Preset, numPresets> a {};

    auto& p0 = a[0] = make("SÓ PRESSÃO", { "DIST", "COMP" }, "Gordura pura: satura, comprime e estoura.");
    p0.drive = { 1.0f, 0.0f, 1.0f };
    p0.comp = { 1.0f, 0.0f, 0.8f };
    p0.clip = { 0.35f, 0.6f, 1.0f };

    auto& p1 = a[1] = make("FECHA E SOBE", { "LP", "HP", "DIST" }, "O low pass vai fechando e depois o high pass sobe.");
    p1.filter = FilterShape::closeThenRise;
    p1.drive = { 0.55f, 0.1f, 1.0f };
    p1.comp = { 0.5f, 0.0f, 1.0f };
    p1.highpassMaxHz = 2200.0f;
    p1.lowpassMinHz = 320.0f;

    auto& p2 = a[2] = make("TÚNEL", { "LP", "REV" }, "Abafa e afunda no reverb.");
    p2.lowpass = { 1.0f, 0.0f, 1.0f };
    p2.reverb = { 0.85f, 0.2f, 1.0f };
    p2.drive = { 0.35f, 0.0f, 1.0f };
    p2.lowpassMinHz = 260.0f;

    auto& p3 = a[3] = make("RADINHO", { "HP", "LP", "DIST" }, "Vira rádio velho distorcido.");
    p3.highpass = { 1.0f, 0.0f, 0.7f };
    p3.lowpass = { 1.0f, 0.0f, 0.7f };
    p3.drive = { 0.85f, 0.0f, 1.0f };
    p3.mono = { 0.7f, 0.0f, 0.7f };
    p3.highpassMaxHz = 650.0f;
    p3.lowpassMinHz = 3200.0f;

    auto& p4 = a[4] = make("CÉU", { "HP", "REV" }, "Tira o grave e sobe pro reverb gigante.");
    p4.highpass = { 1.0f, 0.0f, 1.0f };
    p4.reverb = { 1.0f, 0.0f, 1.0f };
    p4.drive = { 0.25f, 0.0f, 1.0f };
    p4.highpassMaxHz = 1600.0f;

    auto& p5 = a[5] = make("ASPIRADOR", { "MONO", "HP", "COMP" }, "O estéreo fecha e o som é sugado.");
    p5.mono = { 1.0f, 0.0f, 0.8f };
    p5.highpass = { 0.75f, 0.2f, 1.0f };
    p5.comp = { 0.9f, 0.0f, 1.0f };
    p5.drive = { 0.3f, 0.0f, 1.0f };

    auto& p6 = a[6] = make("MOTOSERRA", { "CRUSH", "DIST" }, "Serra, bitcrush e clip.");
    p6.crush = { 1.0f, 0.15f, 1.0f };
    p6.drive = { 0.9f, 0.0f, 1.0f };
    p6.clip = { 0.7f, 0.3f, 1.0f };

    auto& p7 = a[7] = make("TREMEDEIRA", { "GATE", "HP" }, "Corte rítmico que acelera no tempo da música.");
    p7.gate = { 1.0f, 0.0f, 1.0f };
    p7.highpass = { 0.55f, 0.3f, 1.0f };
    p7.drive = { 0.4f, 0.0f, 1.0f };

    auto& p8 = a[8] = make("ESTOURO", { "DIST", "CLIP" }, "Distorção no talo até rasgar.");
    p8.drive = { 1.0f, 0.0f, 1.0f };
    p8.clip = { 1.0f, 0.2f, 1.0f };
    p8.comp = { 0.6f, 0.0f, 1.0f };
    p8.driveMaxDb = 38.0f;

    auto& p9 = a[9] = make("VULCÃO", { "LP", "HP", "REV", "DIST" }, "Tudo junto: filtro, reverb e fogo.");
    p9.lowpass = { 0.7f, 0.0f, 0.6f };
    p9.highpass = { 0.75f, 0.45f, 1.0f };
    p9.reverb = { 0.6f, 0.2f, 1.0f };
    p9.drive = { 1.0f, 0.0f, 1.0f };
    p9.clip = { 0.5f, 0.5f, 1.0f };
    p9.comp = { 0.6f, 0.0f, 1.0f };
    return a;
}
} // namespace

const std::array<Preset, numPresets>& presets() {
    static const auto table = build();
    return table;
}

} // namespace pressao

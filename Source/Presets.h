#pragma once

// The 10 PRESSÃO presets. Each one is a recipe: which modules turn on as the knob goes 0 -> 100.
// A module's level at knob position p is  amount * smoothstep((p - from) / (to - from)).

#include <array>

namespace pressao {

struct Ramp {
    float amount = 0.0f;   // 0 = module off in this preset
    float from = 0.0f;     // knob position where it starts
    float to = 1.0f;       // knob position where it is fully in
};

enum class FilterShape { normal, closeThenRise };

struct Preset {
    const char* name;
    const char* tags[4];        // short labels shown under the name (nullptr = unused)
    const char* help;           // one line, shown in the "i" panel
    Ramp drive, comp, clip, crush, lowpass, highpass, reverb, mono, gate;
    float driveMaxDb = 26.0f;   // input gain at full drive
    float lowpassMinHz = 280.0f;
    float highpassMaxHz = 1400.0f;
    FilterShape filter = FilterShape::normal;
};

constexpr int numPresets = 10;

const std::array<Preset, numPresets>& presets();

} // namespace pressao

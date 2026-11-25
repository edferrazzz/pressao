#pragma once

// The 10 PRESSÃO presets: pre-drop risers. Every one is built on high pass + reverb growing with the knob;
// the others modules (delay, noise, stereo, gate, filter, drive) give each preset its own character.
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
    Ramp drive, comp, clip, crush, lowpass, highpass, reverb, mono, gate, delay, noise, widen;
    float driveMaxDb = 26.0f;   // input gain at full drive
    float lowpassMinHz = 280.0f;
    float highpassMaxHz = 1800.0f;
    FilterShape filter = FilterShape::normal;
    int reverbType = 1;         // 0 SALA, 1 HALL, 2 CÉU
    bool resonant = false;      // filters with a resonant peak
    int delayDiv = 0;           // 0 dotted 1/8, 1 = 1/4, 2 = 1/16
};

// "MEU PRESET": what the person sets in CRIAR mode. Each value is the ceiling the module reaches at knob 100.
struct Custom {
    float highpass = 0.7f, lowpass = 0.0f, reverb = 0.8f, delay = 0.0f, stereo = 0.0f, noise = 0.0f, gate = 0.0f, drive = 0.0f;
    int highpassType = 0;   // 0 SUAVE, 1 RESSONANTE
    int lowpassType = 0;    // 0 FECHA, 1 FECHA E SOBE
    int reverbType = 1;     // 0 SALA, 1 HALL, 2 CÉU
    int delayDiv = 0;       // 0 1/8., 1 1/4, 2 1/16
    int stereoType = 0;     // 0 FECHA (mono), 1 ABRE (wide)
};
Preset toPreset(const Custom&);
Custom fromPreset(const Preset&);
constexpr float customHighpassMaxHz = 3000.0f, customLowpassMinHz = 250.0f;

constexpr int numPresets = 10;     // factory presets
constexpr int customSlot = 10;     // the 11th slot is MEU PRESET
constexpr int numSlots = 11;

const std::array<Preset, numPresets>& presets();

} // namespace pressao

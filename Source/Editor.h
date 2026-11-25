#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include "Processor.h"

namespace pressao {

// The PRESSÃO window: a pressure-cooker gauge on the fire, one knob, presets with arrows.
// Everything is painted here (no child components) so the animation stays in one place.
class Editor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit Editor(Processor&);
    ~Editor() override;

    void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static constexpr int W = 400, H = 680;

    // Tests: draw one frame at a fixed knob value and animation time.
    juce::Image renderFrame(float value, double time, bool showInfo = false, double burstAge = -1.0);
    void setCreating(bool);   // CRIAR mode (MEU PRESET ceilings)

private:
    void timerCallback() override;
    void drawBackground(juce::Graphics&, float v, juce::Colour heat);
    void drawHeader(juce::Graphics&, float v, juce::Colour heat, float sx, float sy);
    void drawPresetRow(juce::Graphics&);
    void drawGauge(juce::Graphics&, float v, float audio, float steam, juce::Colour heat, float sx, float sy);
    void drawFooter(juce::Graphics&, juce::Colour heat);
    void drawInfo(juce::Graphics&);
    void drawCreator(juce::Graphics&, juce::Colour heat);
    void updateFire(float v, float audio);
    int rowAt(juce::Point<float>, bool& onChip, bool& chipNext) const;
    void setRowFromX(int row, float x);
    void setAmount(float value01);

    Processor& proc;
    double t = 0.0;
    float shown = 0.0f;      // smoothed knob value for the drawing
    float audio = 0.0f;      // smoothed output level
    float steamLevel = 0.0f; // smoothed reverb/riser amount
    float lastTarget = 0.0f;
    double burstStart = -10.0;   // the drop: big steam release
    bool infoOpen = false;
    bool dragging = false;
    float dragStart = 0.0f;
    std::optional<float> overrideValue;
    bool creating = false;
    int dragRow = -1;
    const juce::Rectangle<float> criarBtn { 258, 20, 80, 32 };
    juce::Point<int> hover;
    static constexpr int fireRes = 150;
    static constexpr float fireRadius = 190.0f;
    juce::Image fireImg, fireGlow;
public:
    int tipRowOverride = -1;   // tests: show a tooltip in snapshots
private:

    const juce::Rectangle<float> infoBtn { 346, 20, 32, 32 };
    const juce::Rectangle<float> leftArrow { 22, 124, 46, 56 };
    const juce::Rectangle<float> rightArrow { 332, 124, 46, 56 };
    const juce::Rectangle<float> presetBox { 76, 124, 248, 56 };
    const juce::Rectangle<float> knobArea { 40, 220, 320, 400 };
    const juce::Rectangle<float> powerBtn { 22, 628, 36, 36 };
    const juce::Point<float> gaugeCentre { 200.0f, 448.0f };
};

} // namespace pressao

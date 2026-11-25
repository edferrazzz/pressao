#include "Editor.h"
#include "BinaryData.h"

#include <array>
#include <cmath>
#include <vector>

namespace pressao {

namespace {
constexpr float pi = juce::MathConstants<float>::pi;

juce::Font archivo(float h) {
    static auto tf = juce::Typeface::createSystemTypefaceFor(BinaryData::ArchivoBlackRegular_ttf, (size_t) BinaryData::ArchivoBlackRegular_ttfSize);
    return juce::Font(juce::FontOptions(tf).withHeight(h));
}
juce::Font interBold(float h) {
    static auto tf = juce::Typeface::createSystemTypefaceFor(BinaryData::InterBold_ttf, (size_t) BinaryData::InterBold_ttfSize);
    return juce::Font(juce::FontOptions(tf).withHeight(h));
}
juce::Font interRegular(float h) {
    static auto tf = juce::Typeface::createSystemTypefaceFor(BinaryData::InterRegular_ttf, (size_t) BinaryData::InterRegular_ttfSize);
    return juce::Font(juce::FontOptions(tf).withHeight(h));
}

juce::String utf8(const char* s) { return juce::String(juce::CharPointer_UTF8(s)); }

// amber -> orange -> red -> hot red, same stops as the concept
juce::Colour heatColour(float v) {
    struct Stop { float at; juce::uint32 c; };
    static const Stop stops[] { { 0.0f, 0xffffb02e }, { 0.5f, 0xffff6a1a }, { 0.8f, 0xffff2a1a }, { 1.0f, 0xffff1030 } };
    v = juce::jlimit(0.0f, 1.0f, v);
    for (int i = 1; i < 4; ++i)
        if (v <= stops[i].at) {
            const float u = (v - stops[i - 1].at) / (stops[i].at - stops[i - 1].at);
            return juce::Colour(stops[i - 1].c).interpolatedWith(juce::Colour(stops[i].c), u);
        }
    return juce::Colour(stops[3].c);
}

struct Rng {
    juce::int64 s;
    explicit Rng(juce::int64 seed) : s(seed) {}
    float next() { s = (s * 16807) % 2147483647; return (float) s / 2147483647.0f; }
};

struct Particle { float a, speed, phase, size, k; };
struct Puff { float phase, speed, dx, size, k; };

const std::vector<Particle>& embers() {
    static const std::vector<Particle> list = [] {
        std::vector<Particle> v; Rng r(99);
        for (int i = 0; i < 70; ++i) v.push_back({ r.next() * 2.0f * pi, 0.4f + r.next(), r.next(), 1.0f + r.next() * 2.5f, r.next() });
        return v;
    }();
    return list;
}

const std::vector<Puff>& steam() {
    static const std::vector<Puff> list = [] {
        std::vector<Puff> v; Rng r(31);
        for (int i = 0; i < 40; ++i) v.push_back({ r.next(), 0.6f + r.next() * 0.8f, r.next() - 0.5f, 6.0f + r.next() * 10.0f, r.next() });
        return v;
    }();
    return list;
}

// value noise + fbm: flowing flame tongues (same math as the concept page)
const std::array<int, 512>& perm() {
    static const std::array<int, 512> p = [] {
        std::array<int, 512> a {};
        for (int i = 0; i < 256; ++i) a[(size_t) i] = i;
        Rng r(11);
        for (int i = 255; i > 0; --i) std::swap(a[(size_t) i], a[(size_t) (int) (r.next() * (float) (i + 1)) % 256]);
        for (int i = 0; i < 256; ++i) a[(size_t) (i + 256)] = a[(size_t) i];
        return a;
    }();
    return p;
}
inline float hash2(int x, int y) { const auto& p = perm(); return (float) p[(size_t) ((p[(size_t) (x & 255)] + (y & 255)) & 511)] / 255.0f; }
float vnoise(float x, float y) {
    const float fx = std::floor(x), fy = std::floor(y);
    const int xi = (int) fx, yi = (int) fy;
    const float xf = x - fx, yf = y - fy;
    const float u = xf * xf * (3.0f - 2.0f * xf), w = yf * yf * (3.0f - 2.0f * yf);
    const float a = hash2(xi, yi), b = hash2(xi + 1, yi), c = hash2(xi, yi + 1), d = hash2(xi + 1, yi + 1);
    return (a + (b - a) * u) + ((c + (d - c) * u) - (a + (b - a) * u)) * w;
}
float fbm(float x, float y) {
    float s = 0.0f, a = 0.5f;
    for (int o = 0; o < 4; ++o) { s += a * vnoise(x, y); x *= 2.03f; y *= 2.03f; a *= 0.5f; }
    return s / 0.9375f;
}
inline float sstep(float a, float b, float x) { const float t = juce::jlimit(0.0f, 1.0f, (x - a) / (b - a)); return t * t * (3.0f - 2.0f * t); }

juce::Point<float> polar(float angle, float radius) { return { std::cos(angle) * radius, std::sin(angle) * radius }; }

void softBlob(juce::Graphics& g, float x, float y, float r, juce::Colour c) {
    g.setGradientFill(juce::ColourGradient(c, x, y, c.withAlpha(0.0f), x + r, y, true));
    g.fillEllipse(x - r, y - r, r * 2.0f, r * 2.0f);
}

void glowStroke(juce::Graphics& g, const juce::Path& p, juce::Colour c, float width, float glow) {
    for (int k = 3; k >= 1; --k) {
        g.setColour(c.withAlpha(0.12f * glow));
        g.strokePath(p, juce::PathStrokeType(width + (float) k * 5.0f * glow, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour(c);
    g.strokePath(p, juce::PathStrokeType(width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void chevron(juce::Graphics& g, juce::Rectangle<float> r, bool right, juce::Colour c) {
    const auto cx = r.getCentreX(), cy = r.getCentreY();
    juce::Path p;
    const float d = right ? 1.0f : -1.0f;
    p.startNewSubPath(cx - 3.5f * d, cy - 7.0f);
    p.lineTo(cx + 3.5f * d, cy);
    p.lineTo(cx - 3.5f * d, cy + 7.0f);
    g.setColour(c);
    g.strokePath(p, juce::PathStrokeType(2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void well(juce::Graphics& g, juce::Rectangle<float> r, float radius, bool over = false) {
    g.setColour(juce::Colour(0xf00a0a0c));
    g.fillRoundedRectangle(r, radius);
    g.setColour(over ? juce::Colour(0xff3a3a44) : juce::Colour(0xff26262c));
    g.drawRoundedRectangle(r.reduced(0.5f), radius, 1.0f);
}

struct RowDef {
    const char* label;
    float Custom::* value;
    int Custom::* type;
    std::vector<const char*> types;
};

const std::vector<RowDef>& rowDefs() {
    static const std::vector<RowDef> rows {
        { "HIGH PASS", &Custom::highpass, &Custom::highpassType, { "SUAVE", "RESSONA" } },
        { "LOW PASS", &Custom::lowpass, &Custom::lowpassType, { "FECHA", "FECHA+SOBE" } },
        { "REVERB", &Custom::reverb, &Custom::reverbType, { "SALA", "HALL", "C\xc3\x89U" } },
        { "DELAY", &Custom::delay, &Custom::delayDiv, { "1/8 PONTO", "1/4", "1/16" } },
        { "EST\xc3\x89REO", &Custom::stereo, &Custom::stereoType, { "FECHA", "ABRE" } },
        { "RU\xc3\x8d" "DO", &Custom::noise, nullptr, {} },
        { "GATE", &Custom::gate, nullptr, {} },
        { "GORDURA", &Custom::drive, nullptr, {} },
    };
    return rows;
}

constexpr float rowTop = 206.0f, rowH = 48.0f, trackX = 150.0f;
float trackWidth(const RowDef& r) { return r.type != nullptr ? 118.0f : 212.0f; }
juce::Rectangle<float> chipRect(int i) { return { 282.0f, rowTop + (float) i * rowH + 9.0f, 86.0f, 30.0f }; }

juce::String rowValue(int i, const Custom& c) {
    const auto& r = rowDefs()[(size_t) i];
    const float v = c.*(r.value);
    if (v < 0.005f) return "desligado";
    auto hz = [](float f) { return f >= 1000.0f ? juce::String(f / 1000.0f, 1) + " kHz" : juce::String((int) std::lround(f)) + " Hz"; };
    if (i == 0) return juce::String(juce::CharPointer_UTF8("at\xc3\xa9 ")) + hz(20.0f * std::pow(customHighpassMaxHz / 20.0f, v));
    if (i == 1) return juce::String(juce::CharPointer_UTF8("at\xc3\xa9 ")) + hz(20000.0f * std::pow(customLowpassMinHz / 20000.0f, v));
    return juce::String((int) std::lround(v * 100.0f)) + "%";
}
} // namespace

// ===========================================================================
Editor::Editor(Processor& p) : juce::AudioProcessorEditor(p), proc(p) {
    setSize(W, H);
    shown = proc.amount();
    setOpaque(true);
    startTimerHz(30);
}

Editor::~Editor() { stopTimer(); }

void Editor::timerCallback() {
    t += 1.0 / 30.0;
    const float target = proc.isBypassed() ? 0.0f : proc.amount();
    shown += (target - shown) * 0.35f;
    audio += (juce::jmin(1.0f, proc.level()) - audio) * 0.3f;
    steamLevel += (proc.steam() - steamLevel) * 0.15f;
    if (target < 0.02f && lastTarget > 0.3f) burstStart = t;   // the drop: release the valve
    lastTarget = target;
    repaint();
}

juce::Image Editor::renderFrame(float value, double time, bool showInfo, double burstAge) {
    overrideValue = value;
    t = time;
    infoOpen = showInfo;
    audio = 0.6f;
    burstStart = burstAge >= 0.0 ? time - burstAge : -10.0;
    juce::Image img(juce::Image::ARGB, W * 2, H * 2, true);
    {
        juce::Graphics g(img);
        g.addTransform(juce::AffineTransform::scale(2.0f));
        paint(g);
    }
    overrideValue.reset();
    infoOpen = false;
    return img;
}

void Editor::paint(juce::Graphics& g) {
    const float v = overrideValue ? *overrideValue : shown;
    const auto heat = heatColour(v);
    const float shakeAmt = std::pow(juce::jmax(0.0f, v - 0.75f) / 0.25f, 2.0f) * 2.5f;
    const float sx = (float) (std::sin(t * 91.0) + std::sin(t * 57.0)) * shakeAmt * 0.5f;
    const float sy = (float) (std::cos(t * 83.0) + std::sin(t * 67.0)) * shakeAmt * 0.5f;

    drawBackground(g, v, heat);
    const float st = overrideValue ? *overrideValue : juce::jmax(steamLevel, v * 0.6f);
    if (creating) drawCreator(g, heat);
    else drawGauge(g, v, overrideValue ? 0.6f : audio, st, heat, sx, sy);
    drawHeader(g, v, heat, sx, sy);
    drawPresetRow(g);
    drawFooter(g, heat);
    if (infoOpen) drawInfo(g);
}

void Editor::drawBackground(juce::Graphics& g, float v, juce::Colour heat) {
    const auto b = getLocalBounds().toFloat();
    g.fillAll(juce::Colour(0xff0f0f12));
    // heat behind the gauge
    {
        juce::ColourGradient cg(heat.withAlpha(0.06f + v * 0.24f), gaugeCentre.x, gaugeCentre.y + 20.0f,
                                heat.withAlpha(0.0f), gaugeCentre.x + 300.0f, gaugeCentre.y + 20.0f, true);
        g.setGradientFill(cg);
        g.fillRect(b);
    }
    // the whole window heats up at the edges
    if (v > 0.02f) {
        juce::ColourGradient cg(heat.withAlpha(0.0f), b.getCentreX(), b.getCentreY(),
                                heat.withAlpha(std::pow(v, 1.4f) * 0.55f), b.getCentreX() + b.getWidth() * 0.75f, b.getCentreY(), true);
        cg.addColour(0.45, heat.withAlpha(0.0f));
        g.setGradientFill(cg);
        g.fillRect(b);
    }
    g.setColour(heat.withAlpha(0.12f + v * 0.45f));
    g.drawRect(b, 1.0f);
}

void Editor::drawHeader(juce::Graphics& g, float v, juce::Colour heat, float sx, float sy) {
    // logo + name
    {
        const juce::Rectangle<float> icon(22, 27, 18, 18);
        g.setColour(juce::Colours::white);
        g.drawEllipse(icon.reduced(1.5f), 2.2f);
        g.drawLine(icon.getCentreX(), icon.getY() + 5.0f, icon.getCentreX(), icon.getCentreY() + 0.5f, 2.2f);
        g.setFont(interBold(16.0f));
        g.drawText("audiomakers", juce::Rectangle<float>(46, 22, 200, 28), juce::Justification::centredLeft);
    }
    // info button
    {
        const bool over = infoBtn.contains(hover.toFloat());
        g.setColour(juce::Colour(0xff121216));
        g.fillRoundedRectangle(infoBtn, 9.0f);
        g.setColour(over || infoOpen ? juce::Colour(0xff4a4a55) : juce::Colour(0xff2b2b33));
        g.drawRoundedRectangle(infoBtn.reduced(0.5f), 9.0f, 1.0f);
        g.setColour(juce::Colour(0xffb0b0bc));
        g.setFont(interBold(14.0f));
        g.drawText("i", infoBtn, juce::Justification::centred);
    }
    // CRIAR / PRONTO
    {
        const bool over = criarBtn.contains(hover.toFloat());
        g.setColour(creating ? heat : juce::Colour(0xff121216));
        g.fillRoundedRectangle(criarBtn, 9.0f);
        g.setColour(creating ? heat : (over ? juce::Colour(0xff4a4a55) : juce::Colour(0xff2b2b33)));
        g.drawRoundedRectangle(criarBtn.reduced(0.5f), 9.0f, 1.0f);
        g.setColour(creating ? juce::Colour(0xff160a02) : juce::Colour(0xffcfcfda));
        g.setFont(interBold(11.0f).withExtraKerningFactor(0.12f));
        g.drawText(creating ? "PRONTO" : "CRIAR", criarBtn, juce::Justification::centred);
    }
    // title with heat glow
    {
        const auto text = utf8("PRESSÃO");
        const float scale = 1.0f + v * 0.04f;
        juce::GlyphArrangement ga;
        ga.addLineOfText(archivo(52.0f), text, 0.0f, 0.0f);
        juce::Path path;
        ga.createPath(path);
        const auto pb = path.getBounds();
        path.applyTransform(juce::AffineTransform::translation(-pb.getCentreX(), -pb.getCentreY())
                                .scaled(scale)
                                .translated(200.0f + sx * 0.5f, 92.0f + sy * 0.5f));
        juce::DropShadow(heat.withAlpha(0.35f + v * 0.5f), (int) (14 + v * 26), {}).drawForPath(g, path);
        g.setColour(heat);
        g.fillPath(path);
    }
}

void Editor::drawPresetRow(juce::Graphics& g) {
    const int idx = proc.presetIndex();
    const auto pr = proc.slotPreset(idx);
    if (creating) {
        const auto box = presetBox.withX(leftArrow.getX()).withRight(rightArrow.getRight());
        well(g, box, 12.0f);
        g.setColour(juce::Colour(0xffeceef6));
        g.setFont(interBold(15.5f).withExtraKerningFactor(0.12f));
        g.drawText("MEU PRESET", box.withHeight(30.0f).translated(0, 5), juce::Justification::centred);
        g.setColour(juce::Colour(0xff9a9aaa));
        g.setFont(interRegular(11.5f));
        g.drawText(utf8("o 100% do knob chega nesses tetos"), box.withTrimmedTop(30.0f).withHeight(18.0f), juce::Justification::centred);
        return;
    }
    well(g, leftArrow, 12.0f, leftArrow.contains(hover.toFloat()));
    well(g, rightArrow, 12.0f, rightArrow.contains(hover.toFloat()));
    chevron(g, leftArrow, false, juce::Colour(0xffcfcfda));
    chevron(g, rightArrow, true, juce::Colour(0xffcfcfda));
    well(g, presetBox, 12.0f, presetBox.contains(hover.toFloat()));

    // "02  FECHA E SOBE"
    const auto num = idx == customSlot ? juce::String(juce::CharPointer_UTF8("\xe2\x98\x85")) : juce::String(idx + 1).paddedLeft('0', 2);
    const auto name = utf8(pr.name);
    const auto fNum = interBold(15.5f).withExtraKerningFactor(0.12f);
    const auto fName = interBold(15.5f).withExtraKerningFactor(0.12f);
    const float wNum = juce::GlyphArrangement::getStringWidth(fNum, num);
    const float wName = juce::GlyphArrangement::getStringWidth(fName, name);
    float x = presetBox.getCentreX() - (wNum + 8.0f + wName) * 0.5f;
    g.setFont(fNum);
    g.setColour(juce::Colour(0xff6c6c7a));
    g.drawText(num, juce::Rectangle<float>(x, presetBox.getY() + 9.0f, wNum + 4.0f, 20.0f), juce::Justification::centredLeft);
    g.setFont(fName);
    g.setColour(juce::Colour(0xffeceef6));
    g.drawText(name, juce::Rectangle<float>(x + wNum + 8.0f, presetBox.getY() + 9.0f, wName + 6.0f, 20.0f), juce::Justification::centredLeft);

    // module tags
    const auto fTag = interBold(9.5f).withExtraKerningFactor(0.08f);
    std::vector<juce::String> tags;
    for (auto* tg : pr.tags) if (tg != nullptr) tags.push_back(tg);
    float total = 0.0f;
    std::vector<float> widths;
    for (auto& tg : tags) { widths.push_back(juce::GlyphArrangement::getStringWidth(fTag, tg) + 12.0f); total += widths.back(); }
    total += 5.0f * (float) (tags.size() - 1);
    x = presetBox.getCentreX() - total * 0.5f;
    g.setFont(fTag);
    for (size_t i = 0; i < tags.size(); ++i) {
        const juce::Rectangle<float> r(x, presetBox.getY() + 33.0f, widths[i], 15.0f);
        g.setColour(juce::Colour(0xff1c1c22));
        g.fillRoundedRectangle(r, 4.0f);
        g.setColour(juce::Colour(0xffcfcfda));
        g.drawText(tags[i], r, juce::Justification::centred);
        x += widths[i] + 5.0f;
    }
}

void Editor::drawGauge(juce::Graphics& g, float v, float lvl, float st, juce::Colour c, float sx, float sy) {
    juce::Graphics::ScopedSaveState save(g);
    constexpr float S = 1.15f;
    g.addTransform(juce::AffineTransform::scale(S).translated(gaugeCentre.x + sx, gaugeCentre.y + sy));
    const float BR = 104.0f;
    const double tt = t;

    // glow
    softBlob(g, 0.0f, 10.0f, 150.0f, c.withAlpha(0.15f + v * 0.3f));

    // smoke: dark puffs rising from the flame tips at the sides, behind everything
    for (int i = 0; i < 18; ++i) {
        const auto& s = steam()[(size_t) i];
        if (s.k > v * 1.2f) continue;
        const float side = (i % 2 == 0) ? -1.0f : 1.0f;
        const float life = (float) std::fmod(tt * (0.18 + 0.12 * s.speed) + s.phase, 1.0);
        const float x = side * (BR + 18.0f + life * (20.0f + 30.0f * s.size / 16.0f)) + (float) std::sin(tt * 0.9 + s.phase * 20.0f) * 14.0f * life;
        const float y = -10.0f - life * (150.0f + v * 90.0f);
        const float r = 10.0f + life * (36.0f + s.size * 2.0f);
        const float a = std::sin(life * pi) * (0.10f + v * 0.22f);
        softBlob(g, x, y, r, juce::Colour::fromFloatRGBA(0.12f, 0.11f, 0.11f, a));
    }

    // fire: a real flame field (noise rising), wrapping the gauge from below and licking up the sides
    {
        updateFire(v, lvl);
        const float R0 = fireRadius;
        g.setOpacity(0.55f);
        g.drawImage(fireGlow, juce::Rectangle<float>(-R0 - 12.0f, -R0 - 12.0f, R0 * 2 + 24.0f, R0 * 2 + 24.0f));
        g.setOpacity(1.0f);
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        g.drawImage(fireImg, juce::Rectangle<float>(-R0, -R0, R0 * 2, R0 * 2));
    }

    // sparks: bright embers thrown up by the fire, with a short trail
    for (const auto& p : embers()) {
        if (p.k > v * 1.1f + 0.05f) continue;
        const float life = (float) std::fmod(tt * p.speed * (0.55 + v * 0.5) + p.phase, 1.0);
        const float side = std::cos(p.a) >= 0.0f ? 1.0f : -1.0f;
        const float x0 = side * (BR * (0.55f + 0.45f * std::abs(std::cos(p.a)))) ;
        const float drift = (vnoise(p.phase * 50.0f, (float) tt * 1.5f) - 0.5f) * 60.0f * life;
        const float x = x0 + side * life * 26.0f + drift;
        const float y = BR * 0.35f - life * (150.0f + v * 140.0f) * p.speed;
        const float r = p.size * 0.55f * (1.0f - life * 0.6f);
        const float al = (1.0f - life) * (life < 0.05f ? life / 0.05f : 1.0f);
        const auto col = juce::Colour::fromFloatRGBA(1.0f, 0.85f - life * 0.5f, 0.35f - life * 0.3f, al);
        softBlob(g, x, y, r * 4.0f, col.withAlpha(al * 0.35f));
        g.setColour(col);
        g.drawLine(x, y, x - drift * 0.03f, y + 2.0f + p.speed * 2.5f, r * 1.1f);
        g.setColour(juce::Colour::fromFloatRGBA(1.0f, 0.97f, 0.85f, al));
        g.fillEllipse(x - r * 0.6f, y - r * 0.6f, r * 1.2f, r * 1.2f);
    }

    // pressure weight valve on top: rocks faster and faster
    {
        juce::Graphics::ScopedSaveState s2(g);
        const float rock = (float) std::sin(tt * (5.0 + v * 28.0)) * std::pow(v, 1.5f) * 0.35f;
        g.addTransform(juce::AffineTransform::translation(0.0f, -BR - 2.0f));
        g.setColour(juce::Colour(0xff3a3a42));
        g.fillRect(-7.0f, -14.0f, 14.0f, 16.0f);
        g.addTransform(juce::AffineTransform::rotation(rock));
        juce::Path body;
        body.startNewSubPath(-15, -14); body.lineTo(15, -14); body.lineTo(11, -34); body.lineTo(-11, -34); body.closeSubPath();
        juce::ColourGradient vg(juce::Colour(0xff55555f), -16, 0, juce::Colour(0xff44444c), 16, 0, false);
        vg.addColour(0.5, juce::Colour(0xff9a9aa6));
        g.setGradientFill(vg);
        g.fillPath(body);
        g.setColour(c.withAlpha(std::pow(v, 1.5f) * 0.6f));
        g.fillPath(body);
        g.setColour(juce::Colour(0xff2a2a30));
        g.fillEllipse(-5, -41, 10, 10);
    }

    // steam from the valve: this IS the reverb. More riser, more steam, filling the window.
    {
        const int count = 30 + (int) (st * 90.0f);
        for (int i = 0; i < count; ++i) {
            const auto& s = steam()[(size_t) (i % (int) steam().size())];
            const float ph = s.phase + (float) (i / (int) steam().size()) * 0.37f;
            const float life = (float) std::fmod(tt * s.speed * (0.45 + st * 0.9) + ph, 1.0);
            const float spread = 8.0f + life * (30.0f + st * 150.0f);
            const float x = s.dx * spread * 2.0f + (float) std::sin(tt * 1.3 + ph * 9.0f) * 10.0f * life;
            const float y = -BR - 40.0f - life * (60.0f + st * 190.0f);
            const float r = s.size * (0.5f + life * (1.5f + st * 3.0f)) * (0.6f + st * 0.7f);
            const float a = (1.0f - life) * (life < 0.08f ? life / 0.08f : 1.0f) * (0.10f + st * 0.32f);
            softBlob(g, x, y, r, juce::Colour::fromFloatRGBA(0.95f, 0.95f, 0.98f, a));
        }
        // a haze hanging over everything when the reverb is big
        softBlob(g, 0.0f, -BR - 120.0f, 260.0f, juce::Colour::fromFloatRGBA(0.92f, 0.92f, 0.96f, st * st * 0.10f));
        // the whistle: sound lines when it is at the top
        if (v > 0.85f) {
            const float wv = (v - 0.85f) / 0.15f;
            for (int k = 0; k < 3; ++k) {
                const float ph = (float) std::fmod(tt * 2.2 + k / 3.0, 1.0);
                const float r = 14.0f + ph * 34.0f;
                juce::Path arcL, arcR;
                arcL.addCentredArc(0, -BR - 34.0f, r, r, 0.0f, -2.3f, -1.4f, true);
                arcR.addCentredArc(0, -BR - 34.0f, r, r, 0.0f, 1.4f, 2.3f, true);
                g.setColour(juce::Colours::white.withAlpha((1.0f - ph) * 0.6f * wv));
                g.strokePath(arcL, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.strokePath(arcR, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }
        // the drop: the valve lets everything out at once
        const double age = tt - burstStart;
        if (age >= 0.0 && age < 1.2) {
            const float u = (float) (age / 1.2);
            for (int i = 0; i < 26; ++i) {
                const auto& s = steam()[(size_t) i];
                const float ang = -pi * 0.5f + (s.dx) * 2.4f;
                const float dist = 20.0f + u * (140.0f + s.size * 8.0f);
                const auto p = polar(ang, dist);
                softBlob(g, p.x, -BR - 30.0f + p.y, 14.0f + u * 46.0f, juce::Colour::fromFloatRGBA(0.97f, 0.97f, 1.0f, (1.0f - u) * 0.45f));
            }
        }
    }

    // steel bezel, heating up
    {
        juce::ColourGradient bz(juce::Colour(0xffc9c9d2), -BR, -BR, juce::Colour(0xff3b3b44), BR, BR, false);
        bz.addColour(0.35, juce::Colour(0xff6b6b76));
        bz.addColour(0.6, juce::Colour(0xff9b9ba6));
        g.setGradientFill(bz);
        g.fillEllipse(-BR, -BR, BR * 2, BR * 2);
        g.setColour(c.withAlpha(std::pow(v, 1.3f) * 0.55f));
        g.fillEllipse(-BR, -BR, BR * 2, BR * 2);
        for (int i = 0; i < 8; ++i) {
            const auto p = polar((float) i / 8.0f * 2.0f * pi + pi / 8.0f, BR - 8.0f);
            g.setColour(juce::Colour(0xff2b2b31));
            g.fillEllipse(p.x - 3.6f, p.y - 3.6f, 7.2f, 7.2f);
            g.setColour(juce::Colour(0xff888888));
            g.drawLine(p.x - 2.0f, p.y, p.x + 2.0f, p.y, 1.0f);
        }
    }

    // face
    const float FR = BR - 17.0f;
    {
        juce::ColourGradient fg(juce::Colour(0xff1d1d22), 0, -20, juce::Colour(0xff0c0c0f), 0, -20 + FR, true);
        g.setGradientFill(fg);
        g.fillEllipse(-FR, -FR, FR * 2, FR * 2);
        softBlob(g, 0.0f, 0.0f, FR, c.withAlpha(v * v * 0.25f));
    }
    const float a0 = pi * 0.75f, a1 = pi * 2.25f, av = a0 + (a1 - a0) * v;
    auto arc = [&](float from, float to, float r) {
        juce::Path p;
        p.addCentredArc(0, 0, r, r, 0.0f, from + pi * 0.5f, to + pi * 0.5f, true);
        return p;
    };
    g.setColour(juce::Colour(0xff26262c));
    g.strokePath(arc(a0, a1, FR - 10.0f), juce::PathStrokeType(7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(juce::Colour::fromFloatRGBA(1.0f, 0.16f, 0.16f, 0.55f));
    g.strokePath(arc(a0 + (a1 - a0) * 0.8f, a1, FR - 10.0f), juce::PathStrokeType(7.0f));
    if (v > 0.003f) glowStroke(g, arc(a0, av, FR - 10.0f), c, 7.0f, 0.4f + v);

    g.setFont(interBold(10.0f));
    for (int k = 0; k <= 20; ++k) {
        const float a = a0 + (a1 - a0) * (float) k / 20.0f;
        const bool big = k % 5 == 0;
        const auto p1 = polar(a, FR - 17.0f), p2 = polar(a, FR - (big ? 28.0f : 23.0f));
        g.setColour(k >= 16 ? juce::Colour(0xffff5a4a) : juce::Colour(0xff8a8a98));
        g.drawLine(p1.x, p1.y, p2.x, p2.y, big ? 2.2f : 1.2f);
        if (big && k > 0 && k < 20) {
            const auto tp = polar(a, FR - 39.0f);
            g.setColour(k >= 16 ? juce::Colour(0xffff6a5a) : juce::Colour(0xffb8b8c4));
            g.drawText(juce::String(k * 5), juce::Rectangle<float>(tp.x - 14, tp.y - 7, 28, 14), juce::Justification::centred);
        }
    }

    // readout
    {
        const juce::Rectangle<float> r(-30, 26, 60, 30);
        g.setColour(juce::Colour(0xff08080a));
        g.fillRoundedRectangle(r, 7.0f);
        g.setColour(juce::Colour(0xff2a2a30));
        g.drawRoundedRectangle(r, 7.0f, 1.0f);
        g.setColour(juce::Colours::white);
        g.setFont(interBold(21.0f));
        g.drawText(juce::String((int) std::lround(v * 100.0f)), r, juce::Justification::centred);
        const bool danger = v > 0.95f;   // at the top: tell them what to do next
        const bool blink = std::sin(tt * 20.0) > 0.0;
        g.setColour(danger ? (blink ? juce::Colours::white : c) : juce::Colour(0xff7a7a88));
        g.setFont(interBold(8.5f));
        g.drawText(danger ? "SOLTA NO DROP" : utf8("PRESSÃO"), juce::Rectangle<float>(-50, 62, 100, 12), juce::Justification::centred);
    }

    // needle, trembling near the top
    {
        const float jit = (float) (std::sin(tt * 63.0) * 0.06 + std::sin(tt * 37.0) * 0.03) * v * v * v;
        const float na = av + jit;
        const auto from = polar(na, -14.0f), to = polar(na, FR - 20.0f);
        g.setColour(juce::Colours::black.withAlpha(0.4f));
        g.drawLine(from.x + 1, from.y + 2, to.x + 1, to.y + 2, 4.0f);
        g.setColour(juce::Colour(0xffff3b2f));
        g.drawLine(from.x, from.y, to.x, to.y, 3.2f);
        g.setColour(juce::Colour(0xffd8d8e0));
        g.fillEllipse(-8, -8, 16, 16);
        g.setColour(juce::Colour(0xff2a2a30));
        g.fillEllipse(-3, -3, 6, 6);
    }

    // glass: cracks near the top, highlight, steam fog
    {
        juce::Graphics::ScopedSaveState s3(g);
        juce::Path face;
        face.addEllipse(-FR, -FR, FR * 2, FR * 2);
        g.reduceClipRegion(face);
        juce::Path hl;
        hl.addEllipse(-FR * 0.75f, -FR * 0.42f, FR * 1.5f, FR * 0.84f);
        hl.applyTransform(juce::AffineTransform::rotation(-0.5f).translated(-18.0f, -34.0f));
        g.setColour(juce::Colours::white.withAlpha(0.06f));
        g.fillPath(hl);
        g.setColour(juce::Colour::fromFloatRGBA(0.9f, 0.9f, 0.94f, st * st * 0.12f));
        g.fillEllipse(-FR, -FR, FR * 2, FR * 2);
    }

    // steam leaking from the sides when it is about to blow
    for (int lk = 0; lk < 6; ++lk) {
        const float th = 0.7f + (float) lk * 0.05f;
        if (v < th) continue;
        const float ph = (float) lk * 0.37f - std::floor((float) lk * 0.37f);
        const float a = -pi * 0.5f + (ph - 0.5f) * 2.6f;
        for (int m = 0; m < 5; ++m) {
            const float life = (float) std::fmod(tt * 1.6 + ph + m / 5.0, 1.0);
            const auto p = polar(a, BR + life * (30.0f + v * 30.0f));
            softBlob(g, p.x, p.y - life * 10.0f, 4.0f + life * 12.0f, juce::Colour::fromFloatRGBA(0.94f, 0.94f, 0.97f, (1.0f - life) * 0.35f));
        }
    }
}

void Editor::updateFire(float v, float lvl) {
    constexpr int N = fireRes;
    if (!fireImg.isValid()) {
        fireImg = juce::Image(juce::Image::ARGB, N, N, true);
        fireGlow = juce::Image(juce::Image::ARGB, N / 4, N / 4, true);
    }
    const float BR = 104.0f, R0 = fireRadius;
    const float tt = (float) std::fmod(t, 1000.0);
    const float react = 0.85f + 0.3f * lvl;
    const float maxLen = (22.0f + 87.0f * std::pow(v, 0.8f)) * react;
    const float sp = 1.6f + v * 2.2f;
    const float blue = juce::jlimit(0.0f, 1.0f, 1.0f - v * 1.6f);
    static const float stops[5][4] { { 0.0f, 90, 10, 0 }, { 0.25f, 200, 40, 5 }, { 0.5f, 255, 120, 20 }, { 0.75f, 255, 200, 70 }, { 1.0f, 255, 250, 215 } };
    juce::Image::BitmapData bd(fireImg, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < N; ++y) {
        const float ly = ((float) y / (float) (N - 1) * 2.0f - 1.0f) * R0;
        for (int x = 0; x < N; ++x) {
            const float lx = ((float) x / (float) (N - 1) * 2.0f - 1.0f) * R0;
            float q = 0.0f;
            const float r0 = std::hypot(lx, ly);
            const float d0 = r0 - BR + 6.0f;
            if (d0 > -2.0f && d0 < maxLen * 1.6f + 10.0f) {
                // flames bend upward: the source of a point is a bit below it
                const float py = ly + 0.9f * juce::jlimit(0.0f, 50.0f, d0);
                const float rr = std::hypot(lx, py);
                const float d = rr - BR + 6.0f;
                const float sinA = py / juce::jmax(1.0f, rr);
                const float m = 0.08f + 0.92f * sstep(-0.7f, 0.45f, sinA);
                const float n = fbm(lx * 0.05f, ly * 0.016f + tt * sp) * 0.7f + fbm(lx * 0.11f, ly * 0.035f + tt * sp * 1.6f) * 0.3f;
                const float len = maxLen * m * (0.08f + 2.2f * n * n * n);
                q = d > 0.0f ? juce::jlimit(0.0f, 1.0f, 1.0f - d / juce::jmax(1.0f, len)) : juce::jlimit(0.0f, 1.0f, 0.6f + d * 0.1f);
                q *= sstep(-2.0f, 3.0f, d0) * sstep(-BR - maxLen * 0.5f, -BR * 0.2f, ly);
            }
            int k = 1;
            while (k < 4 && q > stops[k][0]) ++k;
            const float u = juce::jlimit(0.0f, 1.0f, (q - stops[k - 1][0]) / (stops[k][0] - stops[k - 1][0]));
            float R = stops[k - 1][1] + (stops[k][1] - stops[k - 1][1]) * u;
            float G = stops[k - 1][2] + (stops[k][2] - stops[k - 1][2]) * u;
            float B = stops[k - 1][3] + (stops[k][3] - stops[k - 1][3]) * u;
            // gas-blue at the root when the fire is low
            const float base = blue * sstep(10.0f, 0.0f, d0) * juce::jlimit(0.0f, 1.0f, q * 3.0f);
            R += (70.0f - R) * base; G += (130.0f - G) * base; B += (255.0f - B) * base;
            const float A = juce::jlimit(0.0f, 1.0f, q * 3.2f);
            bd.setPixelColour(x, y, juce::Colour::fromFloatRGBA(R / 255.0f, G / 255.0f, B / 255.0f, A));
        }
    }
    // soft glow: a tiny copy of the flames, scaled up later
    juce::Graphics gg(fireGlow);
    fireGlow.clear(fireGlow.getBounds());
    gg.setImageResamplingQuality(juce::Graphics::mediumResamplingQuality);
    gg.drawImage(fireImg, fireGlow.getBounds().toFloat());
}

void Editor::drawCreator(juce::Graphics& g, juce::Colour heat) {
    const juce::Rectangle<float> panel(22, 196, (float) W - 44, 404);
    well(g, panel, 14.0f);
    const auto c = proc.getCustom();
    const auto& rows = rowDefs();
    for (int i = 0; i < (int) rows.size(); ++i) {
        const auto& r = rows[(size_t) i];
        const float y0 = rowTop + (float) i * rowH;
        if (i > 0) {
            g.setColour(juce::Colour(0xff1a1a20));
            g.fillRect(34.0f, y0, panel.getWidth() - 24.0f, 1.0f);
        }
        const float v = c.*(r.value);
        g.setColour(v > 0.005f ? juce::Colour(0xffeceef6) : juce::Colour(0xff8a8a98));
        g.setFont(interBold(11.0f).withExtraKerningFactor(0.1f));
        g.drawText(utf8(r.label), juce::Rectangle<float>(36, y0 + 8, 112, 16), juce::Justification::centredLeft);
        g.setColour(v > 0.005f ? heat.brighter(0.2f) : juce::Colour(0xff5a5a66));
        g.setFont(interRegular(11.5f));
        g.drawText(rowValue(i, c), juce::Rectangle<float>(36, y0 + 24, 112, 16), juce::Justification::centredLeft);

        const float tw = trackWidth(r);
        const juce::Rectangle<float> track(trackX, y0 + 21.0f, tw, 6.0f);
        g.setColour(juce::Colour(0xff1d1d23));
        g.fillRoundedRectangle(track, 3.0f);
        if (v > 0.0f) {
            g.setGradientFill(juce::ColourGradient(heatColour(0.0f), track.getX(), 0, heatColour(v), track.getX() + tw * v, 0, false));
            g.fillRoundedRectangle(track.withWidth(juce::jmax(6.0f, tw * v)), 3.0f);
        }
        const float hx = track.getX() + tw * v;
        g.setColour(juce::Colours::white);
        g.fillEllipse(hx - 7.0f, track.getCentreY() - 7.0f, 14.0f, 14.0f);
        g.setColour(dragRow == i ? heat : juce::Colour(0xff2a2a30));
        g.drawEllipse(hx - 7.0f, track.getCentreY() - 7.0f, 14.0f, 14.0f, 1.5f);

        if (r.type != nullptr) {
            const auto chip = chipRect(i);
            g.setColour(juce::Colour(0xff15151a));
            g.fillRoundedRectangle(chip, 8.0f);
            g.setColour(juce::Colour(0xff2b2b33));
            g.drawRoundedRectangle(chip.reduced(0.5f), 8.0f, 1.0f);
            const int t = c.*(r.type);
            g.setColour(juce::Colour(0xffeceef6));
            g.setFont(interBold(10.0f).withExtraKerningFactor(0.06f));
            g.drawText(utf8(r.types[(size_t) juce::jlimit(0, (int) r.types.size() - 1, t)]), chip.reduced(14, 0), juce::Justification::centred);
            g.setColour(juce::Colour(0xff6c6c7a));
            g.setFont(interBold(12.0f));
            g.drawText(juce::String(juce::CharPointer_UTF8("\xe2\x80\xb9")), chip.withWidth(14).translated(3, -1), juce::Justification::centred);
            g.drawText(juce::String(juce::CharPointer_UTF8("\xe2\x80\xba")), chip.withTrimmedLeft(chip.getWidth() - 14).translated(-3, -1), juce::Justification::centred);
        }
    }
    // explanation only on mouse-over
    bool onChip = false, chipNext = false;
    const int hr = dragRow >= 0 ? -1 : rowAt(hover.toFloat(), onChip, chipNext);
    if (hr >= 0 || tipRowOverride >= 0) {
        const int i = tipRowOverride >= 0 ? tipRowOverride : hr;
        static const char* tips[] {
            "Corta o grave subindo. No 100% do knob chega at\xc3\xa9 aqui.",
            "Abafa o som. FECHA s\xc3\xb3 fecha, FECHA+SOBE fecha e depois abre com o high pass.",
            "Quanto de reverb no 100%. SALA, HALL ou C\xc3\x89U.",
            "Eco no tempo da m\xc3\xbasica.",
            "FECHA deixa mono, ABRE alarga.",
            "Chiado de riser que sobe junto.",
            "Picota o som no tempo.",
            "Satura e engorda.",
        };
        const auto text = utf8(tips[i]) + utf8("  Segure pra ouvir no 100%.");
        const float y = rowTop + (float) (i + 1) * rowH + 2.0f;
        juce::AttributedString as;
        as.append(text, interRegular(12.0f), juce::Colour(0xff16161a));
        as.setWordWrap(juce::AttributedString::byWord);
        juce::TextLayout tl;
        tl.createLayout(as, 236.0f);
        const juce::Rectangle<float> box(juce::jlimit(30.0f, (float) W - 30.0f - 256.0f, trackX - 40.0f), y, 256.0f, tl.getHeight() + 16.0f);
        g.setColour(juce::Colours::black.withAlpha(0.4f));
        g.fillRoundedRectangle(box.translated(0, 3), 8.0f);
        g.setColour(juce::Colour(0xfff4f4f7));
        g.fillRoundedRectangle(box, 8.0f);
        tl.draw(g, box.reduced(10, 8));
    }
}

int Editor::rowAt(juce::Point<float> p, bool& onChip, bool& chipNext) const {
    onChip = chipNext = false;
    if (p.x < 22 || p.x > W - 22) return -1;
    const int i = (int) std::floor((p.y - rowTop) / rowH);
    if (i < 0 || i >= (int) rowDefs().size()) return -1;
    const auto& r = rowDefs()[(size_t) i];
    if (r.type != nullptr && chipRect(i).expanded(2).contains(p)) {
        onChip = true;
        chipNext = p.x > chipRect(i).getCentreX();
        return i;
    }
    return p.x >= trackX - 14.0f ? i : -1;
}

void Editor::setRowFromX(int row, float x) {
    auto c = proc.getCustom();
    const auto& r = rowDefs()[(size_t) row];
    c.*(r.value) = juce::jlimit(0.0f, 1.0f, (x - trackX) / trackWidth(r));
    proc.setCustom(c);
}

void Editor::setCreating(bool on) {
    if (on && proc.presetIndex() != customSlot) {
        proc.setCustom(fromPreset(proc.slotPreset(proc.presetIndex())));   // start from what you were hearing
        proc.choosePreset(customSlot);
    }
    creating = on;
    proc.previewTop = false;
    dragRow = -1;
    repaint();
}

void Editor::drawFooter(juce::Graphics& g, juce::Colour heat) {
    const bool bypassed = proc.isBypassed();
    g.setColour(juce::Colour(0xff121216));
    g.fillEllipse(powerBtn);
    g.setColour(powerBtn.contains(hover.toFloat()) ? juce::Colour(0xff4a4a55) : juce::Colour(0xff2b2b33));
    g.drawEllipse(powerBtn.reduced(0.5f), 1.0f);
    juce::Path p;
    const auto cx = powerBtn.getCentreX(), cy = powerBtn.getCentreY();
    p.addCentredArc(cx, cy + 0.8f, 6.0f, 6.0f, 0.0f, 0.75f, 2.0f * pi - 0.75f, true);
    p.startNewSubPath(cx, cy - 7.5f);
    p.lineTo(cx, cy + 0.5f);
    g.setColour(bypassed ? juce::Colour(0xff5a5a66) : heat);
    g.strokePath(p, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const juce::Rectangle<float> row(0, powerBtn.getY(), (float) W, powerBtn.getHeight());
    const auto by = juce::String("by "), name = juce::String("audiomakers");
    const float w1 = juce::GlyphArrangement::getStringWidth(interRegular(13.0f), by);
    const float w2 = juce::GlyphArrangement::getStringWidth(interBold(13.0f), name);
    const float x = row.getCentreX() - (w1 + w2) * 0.5f;
    g.setColour(juce::Colour(0xff8a8a9a));
    g.setFont(interRegular(13.0f));
    g.drawText(by, juce::Rectangle<float>(x, row.getY(), w1 + 2, row.getHeight()), juce::Justification::centredLeft);
    g.setColour(juce::Colours::white);
    g.setFont(interBold(13.0f));
    g.drawText(name, juce::Rectangle<float>(x + w1, row.getY(), w2 + 4, row.getHeight()), juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xff8a8a9a));
    g.setFont(interRegular(13.0f));
    const auto ver = Processor::versionString().upToLastOccurrenceOf(".", false, false);
    g.drawText(ver, juce::Rectangle<float>((float) W - 80, row.getY(), 58, row.getHeight()), juce::Justification::centredRight);
}

void Editor::drawInfo(juce::Graphics& g) {
    const juce::Rectangle<float> r(22, 196, (float) W - 44, 420);
    g.setColour(juce::Colour(0xf20b0b0e));
    g.fillRoundedRectangle(r, 14.0f);
    g.setColour(juce::Colour(0xff2b2b33));
    g.drawRoundedRectangle(r.reduced(0.5f), 14.0f, 1.0f);
    auto area = r.reduced(22, 20);
    g.setColour(juce::Colours::white);
    g.setFont(interBold(15.0f));
    g.drawText(utf8("Como usar"), area.removeFromTop(24), juce::Justification::centredLeft);
    area.removeFromTop(6);
    g.setColour(juce::Colour(0xffc4c4d2));
    g.setFont(interRegular(13.0f));
    const auto how = utf8("Automatize o knob subindo no pré-drop e volte pro 0 no 1 do drop: o reverb é cortado na hora "
                          "e o drop entra seco. Setinhas trocam o preset. CRIAR monta o seu: escolha até onde cada "
                          "efeito vai no 100% do knob.");
    g.drawFittedText(how, area.removeFromTop(66).toNearestInt(), juce::Justification::topLeft, 6, 1.0f);
    area.removeFromTop(6);
    g.setColour(juce::Colours::white);
    g.setFont(interBold(13.0f));
    g.drawText("Presets", area.removeFromTop(20), juce::Justification::centredLeft);
    for (int i = 0; i < numPresets; ++i) {
        auto line = area.removeFromTop(23);
        g.setColour(i == proc.presetIndex() ? heatColour(0.4f) : juce::Colour(0xffeceef6));
        g.setFont(interBold(11.5f));
        g.drawText(utf8(presets()[(size_t) i].name), line.removeFromLeft(108), juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff9a9aaa));
        g.setFont(interRegular(11.5f));
        g.drawText(utf8(presets()[(size_t) i].help), line, juce::Justification::centredLeft, true);
    }
}

// ===========================================================================
void Editor::setAmount(float value01) {
    proc.amountParam->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, value01));
}

void Editor::mouseMove(const juce::MouseEvent& e) {
    hover = e.getPosition();
    const auto p = e.position;
    const bool clickable = infoBtn.contains(p) || leftArrow.contains(p) || rightArrow.contains(p) || presetBox.contains(p)
                           || powerBtn.contains(p) || criarBtn.contains(p);
    setMouseCursor(clickable ? juce::MouseCursor::PointingHandCursor
                             : (knobArea.contains(p) && !creating ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor));
}

void Editor::mouseDown(const juce::MouseEvent& e) {
    const auto p = e.position;
    if (infoOpen) { infoOpen = false; repaint(); return; }
    if (infoBtn.contains(p)) { infoOpen = true; repaint(); return; }
    if (criarBtn.contains(p)) { setCreating(!creating); return; }
    if (creating) {
        bool onChip = false, next = false;
        const int row = rowAt(p, onChip, next);
        if (row < 0) return;
        const auto& r = rowDefs()[(size_t) row];
        if (onChip) {
            auto c = proc.getCustom();
            const int n = (int) r.types.size();
            c.*(r.type) = ((c.*(r.type)) + (next ? 1 : -1) + n) % n;
            proc.setCustom(c);
            repaint();
            return;
        }
        dragRow = row;
        setRowFromX(row, p.x);
        proc.previewTop = true;
        repaint();
        return;
    }
    if (leftArrow.contains(p)) { proc.stepPreset(-1); return; }  // (not reached while creating)
    if (rightArrow.contains(p)) { proc.stepPreset(1); return; }
    if (powerBtn.contains(p)) {
        proc.bypassParam->beginChangeGesture();
        proc.bypassParam->setValueNotifyingHost(proc.isBypassed() ? 0.0f : 1.0f);
        proc.bypassParam->endChangeGesture();
        return;
    }
    if (presetBox.contains(p)) {
        juce::PopupMenu m;
        for (int i = 0; i < numPresets; ++i)
            m.addItem(i + 1, juce::String(i + 1).paddedLeft('0', 2) + "  " + Processor::presetName(i), true, i == proc.presetIndex());
        m.addSeparator();
        m.addItem(customSlot + 1, Processor::presetName(customSlot), true, proc.presetIndex() == customSlot);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(localAreaToGlobal(presetBox.toNearestInt())).withMinimumWidth(248),
                        [this](int r) { if (r > 0) proc.choosePreset(r - 1); });
        return;
    }
    if (knobArea.contains(p) && !creating) {
        dragging = true;
        dragStart = proc.amountParam->convertTo0to1(proc.amountParam->get());
        proc.amountParam->beginChangeGesture();
    }
}

void Editor::mouseDrag(const juce::MouseEvent& e) {
    if (dragRow >= 0) { setRowFromX(dragRow, e.position.x); repaint(); return; }
    if (!dragging) return;
    const float range = e.mods.isShiftDown() ? 1200.0f : 260.0f;
    setAmount(dragStart - (float) e.getDistanceFromDragStartY() / range);
}

void Editor::mouseUp(const juce::MouseEvent&) {
    if (dragRow >= 0) { dragRow = -1; proc.previewTop = false; repaint(); }
    if (dragging) proc.amountParam->endChangeGesture();
    dragging = false;
}

void Editor::mouseDoubleClick(const juce::MouseEvent& e) {
    if (creating) {
        bool onChip = false, next = false;
        const int row = rowAt(e.position, onChip, next);
        if (row >= 0 && !onChip) { auto c = proc.getCustom(); c.*(rowDefs()[(size_t) row].value) = 0.0f; proc.setCustom(c); repaint(); }
        return;
    }
    if (!knobArea.contains(e.position)) return;
    proc.amountParam->beginChangeGesture();
    setAmount(0.0f);
    proc.amountParam->endChangeGesture();
}

void Editor::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    if (creating || !knobArea.contains(e.position)) return;
    const float now = proc.amountParam->convertTo0to1(proc.amountParam->get());
    proc.amountParam->beginChangeGesture();
    setAmount(now + w.deltaY * 0.12f);
    proc.amountParam->endChangeGesture();
}

} // namespace pressao

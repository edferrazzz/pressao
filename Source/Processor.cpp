#include "Processor.h"
#include "Editor.h"

namespace pressao {

juce::String Processor::presetName(int index) {
    if (index == customSlot) return "MEU PRESET";
    return juce::String(juce::CharPointer_UTF8(presets()[(size_t) juce::jlimit(0, numPresets - 1, index)].name));
}

Custom Processor::getCustom() const {
    const juce::SpinLock::ScopedLockType l(customLock);
    return custom;
}

void Processor::setCustom(const Custom& c) {
    const auto p = toPreset(c);
    const juce::SpinLock::ScopedLockType l(customLock);
    custom = c;
    customPreset = p;
}

Preset Processor::slotPreset(int index) const {
    if (index == customSlot) {
        const juce::SpinLock::ScopedLockType l(customLock);
        return customPreset;
    }
    return presets()[(size_t) juce::jlimit(0, numPresets - 1, index)];
}

Processor::Processor()
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Input", juce::AudioChannelSet::stereo(), true)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
    juce::StringArray names;
    for (int i = 0; i < numSlots; ++i) names.add(presetName(i));
    addParameter(amountParam = new juce::AudioParameterFloat(juce::ParameterID { "amount", 1 }, juce::CharPointer_UTF8("Pressão"),
                                                             juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f,
                                                             juce::AudioParameterFloatAttributes().withLabel("%")));
    addParameter(presetParam = new juce::AudioParameterChoice(juce::ParameterID { "preset", 1 }, "Preset", names, 0));
    addParameter(bypassParam = new juce::AudioParameterBool(juce::ParameterID { "bypass", 1 }, "Bypass", false));
}

bool Processor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void Processor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    juce::ignoreUnused(samplesPerBlock);
    engine.prepare(sampleRate, 256);   // processBlock feeds the engine in chunks of at most 256
    setLatencySamples(engine.latencySamples());
}

void Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c) buffer.clear(c, 0, total);

    HostTime time;
    if (auto* ph = getPlayHead()) {
        if (auto pos = ph->getPosition()) {
            if (auto bpm = pos->getBpm()) time.bpm = *bpm;
            if (auto ppq = pos->getPpqPosition()) { time.ppq = *ppq; time.hasPpq = pos->getIsPlaying(); }
        }
    }
    // Bypass keeps running with the knob at 0 (transparent, same latency, reverb tail fades out).
    const float a = isBypassed() ? 0.0f : (previewTop.load() ? 1.0f : amount());
    const int preset = presetIndex();
    if (preset == customSlot) {
        const juce::SpinLock::ScopedTryLockType l(customLock);
        if (l.isLocked()) audioCustom = customPreset;
    }
    const Preset& pr = preset == customSlot ? audioCustom : presets()[(size_t) juce::jlimit(0, numPresets - 1, preset)];
    // split long host blocks into the size we prepared for
    constexpr int chunk = 256;
    const int ch = buffer.getNumChannels();
    for (int start = 0; start < total; start += chunk) {
        const int n = juce::jmin(chunk, total - start);
        juce::AudioBuffer<float> view(buffer.getArrayOfWritePointers(), ch, start, n);
        HostTime t = time;
        if (t.hasPpq) t.ppq += (double) start / getSampleRate() * t.bpm / 60.0;
        engine.process(view, a, pr, preset, t);
    }
}

void Processor::setCurrentProgram(int index) { choosePreset(index); }

const juce::String Processor::getProgramName(int index) { return presetName(index); }

void Processor::choosePreset(int index) {
    index = (index % numSlots + numSlots) % numSlots;
    presetParam->beginChangeGesture();
    presetParam->setValueNotifyingHost(presetParam->convertTo0to1((float) index));
    presetParam->endChangeGesture();
}

void Processor::stepPreset(int delta) { choosePreset(presetIndex() + delta); }

void Processor::getStateInformation(juce::MemoryBlock& dest) {
    juce::XmlElement xml("PRESSAO");
    xml.setAttribute("amount", (double) amountParam->get());
    xml.setAttribute("preset", presetIndex());
    xml.setAttribute("bypass", isBypassed());
    const auto c = getCustom();
    auto* m = xml.createNewChildElement("MEU");
    m->setAttribute("hp", c.highpass); m->setAttribute("lp", c.lowpass); m->setAttribute("rev", c.reverb);
    m->setAttribute("delay", c.delay); m->setAttribute("stereo", c.stereo); m->setAttribute("noise", c.noise);
    m->setAttribute("gate", c.gate); m->setAttribute("drive", c.drive);
    m->setAttribute("hpType", c.highpassType); m->setAttribute("lpType", c.lowpassType); m->setAttribute("revType", c.reverbType);
    m->setAttribute("delayDiv", c.delayDiv); m->setAttribute("stereoType", c.stereoType);
    copyXmlToBinary(xml, dest);
}

void Processor::setStateInformation(const void* data, int size) {
    if (auto xml = getXmlFromBinary(data, size)) {
        if (!xml->hasTagName("PRESSAO")) return;
        *amountParam = (float) xml->getDoubleAttribute("amount", 0.0);
        if (auto* m = xml->getChildByName("MEU")) {
            Custom c;
            auto f = [&](const char* k, float d) { return (float) juce::jlimit(0.0, 1.0, m->getDoubleAttribute(k, d)); };
            c.highpass = f("hp", c.highpass); c.lowpass = f("lp", c.lowpass); c.reverb = f("rev", c.reverb);
            c.delay = f("delay", c.delay); c.stereo = f("stereo", c.stereo); c.noise = f("noise", c.noise);
            c.gate = f("gate", c.gate); c.drive = f("drive", c.drive);
            c.highpassType = juce::jlimit(0, 1, m->getIntAttribute("hpType", 0));
            c.lowpassType = juce::jlimit(0, 1, m->getIntAttribute("lpType", 0));
            c.reverbType = juce::jlimit(0, 2, m->getIntAttribute("revType", 1));
            c.delayDiv = juce::jlimit(0, 2, m->getIntAttribute("delayDiv", 0));
            c.stereoType = juce::jlimit(0, 1, m->getIntAttribute("stereoType", 0));
            setCustom(c);
        }
        *presetParam = juce::jlimit(0, numSlots - 1, xml->getIntAttribute("preset", 0));
        *bypassParam = xml->getBoolAttribute("bypass", false);
    }
}

juce::AudioProcessorEditor* Processor::createEditor() { return new Editor(*this); }

} // namespace pressao

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new pressao::Processor(); }

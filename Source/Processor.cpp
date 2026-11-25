#include "Processor.h"
#include "Editor.h"

namespace pressao {

juce::String Processor::presetName(int index) {
    return juce::String(juce::CharPointer_UTF8(presets()[(size_t) juce::jlimit(0, numPresets - 1, index)].name));
}

Processor::Processor()
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Input", juce::AudioChannelSet::stereo(), true)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
    juce::StringArray names;
    for (int i = 0; i < numPresets; ++i) names.add(presetName(i));
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
    const float a = isBypassed() ? 0.0f : amount();
    const int preset = presetIndex();
    // split long host blocks into the size we prepared for
    constexpr int chunk = 256;
    const int ch = buffer.getNumChannels();
    for (int start = 0; start < total; start += chunk) {
        const int n = juce::jmin(chunk, total - start);
        juce::AudioBuffer<float> view(buffer.getArrayOfWritePointers(), ch, start, n);
        HostTime t = time;
        if (t.hasPpq) t.ppq += (double) start / getSampleRate() * t.bpm / 60.0;
        engine.process(view, a, preset, t);
    }
}

void Processor::setCurrentProgram(int index) { choosePreset(index); }

const juce::String Processor::getProgramName(int index) { return presetName(index); }

void Processor::choosePreset(int index) {
    index = (index % numPresets + numPresets) % numPresets;
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
    copyXmlToBinary(xml, dest);
}

void Processor::setStateInformation(const void* data, int size) {
    if (auto xml = getXmlFromBinary(data, size)) {
        if (!xml->hasTagName("PRESSAO")) return;
        *amountParam = (float) xml->getDoubleAttribute("amount", 0.0);
        *presetParam = juce::jlimit(0, numPresets - 1, xml->getIntAttribute("preset", 0));
        *bypassParam = xml->getBoolAttribute("bypass", false);
    }
}

juce::AudioProcessorEditor* Processor::createEditor() { return new Editor(*this); }

} // namespace pressao

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new pressao::Processor(); }

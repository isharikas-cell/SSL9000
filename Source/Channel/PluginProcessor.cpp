#include <juce_audio_utils/juce_audio_utils.h>
#include "ConsoleDSP.h"

class SSL9000ChannelProcessor final : public juce::AudioProcessor {
public:
    SSL9000ChannelProcessor()
      : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
        state(*this, nullptr, "PARAMETERS", createLayout()) {}
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout() {
        juce::AudioProcessorValueTreeState::ParameterLayout p;
        p.add(std::make_unique<juce::AudioParameterFloat>("drive", "Saturation", juce::NormalisableRange<float>(0.0f, 18.0f, 0.01f), 0.0f));
        p.add(std::make_unique<juce::AudioParameterFloat>("width", "Stereo Width", juce::NormalisableRange<float>(0.0f, 2.0f, 0.001f), 1.0f));
        p.add(std::make_unique<juce::AudioParameterFloat>("cross", "Crosstalk", juce::NormalisableRange<float>(0.0f, 0.08f, 0.0001f), 0.015f));
        return p;
    }
    const juce::String getName() const override { return "SSL9000 Channel"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& l) const override { return l.getMainInputChannelSet() == juce::AudioChannelSet::stereo() && l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
    void processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer&) override {
        juce::ScopedNoDenormals noDenormals;
        ssl9000::process(b, state.getRawParameterValue("drive")->load(), state.getRawParameterValue("width")->load(), state.getRawParameterValue("cross")->load());
    }
    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor(*this); }
    bool hasEditor() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& d) override { auto x = state.copyState(); std::unique_ptr<juce::XmlElement> xml(x.createXml()); copyXmlToBinary(*xml, d); }
    void setStateInformation(const void* d, int n) override { std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(d, n)); if (xml && xml->hasTagName(state.state.getType())) state.replaceState(juce::ValueTree::fromXml(*xml)); }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    juce::AudioProcessorValueTreeState state;
};
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SSL9000ChannelProcessor(); }

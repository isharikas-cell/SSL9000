#include <juce_audio_utils/juce_audio_utils.h>
#include "ConsoleDSP.h"

#include <array>
#include <cstdint>
#include <memory>

#if defined(_WIN32)
#include "ChannelAudioPublisher.h"
#include "WindowsConsoleLinkSession.h"
#endif

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
        // Console Link configuration is persisted with the plugin state. The
        // session key is deliberately explicit: separate plugin instances must
        // be given the same project token by the user.
        p.add(std::make_unique<juce::AudioParameterString>("sessionKey", "Console Link Session Key", juce::String{}));
        p.add(std::make_unique<juce::AudioParameterInt>("channelId", "Console Link Channel ID", 0, 31, 0));
        p.add(std::make_unique<juce::AudioParameterInt>("busId", "Console Link Bus ID", 0, 31, 0));
        return p;
    }

    const juce::String getName() const override { return "SSL9000 Channel"; }

    void prepareToPlay(double sampleRate, int) override {
        saturation.prepare(sampleRate);
#if defined(_WIN32)
        publisher.reset();
        const auto key = state.getParameter("sessionKey")->getValueAsText().trim();
        const auto channel = static_cast<std::uint32_t>(state.getRawParameterValue("channelId")->load());
        const auto bus = static_cast<std::uint32_t>(state.getRawParameterValue("busId")->load());
        if (key.isNotEmpty() && channel < ssl9000::link::maxChannels
            && bus < ssl9000::link::maxBuses) {
            const auto result = linkSession.attach(key.toWideCharPointer(),
                                                   static_cast<std::uint32_t>(sampleRate));
            if ((result == ssl9000::link::WindowsConsoleLinkSession::AttachResult::created
                 || result == ssl9000::link::WindowsConsoleLinkSession::AttachResult::attached)
                && linkSession.get() != nullptr) {
                publisher = std::make_unique<ssl9000::link::ChannelAudioPublisher>(
                    linkSession.get()->channelToBus[channel], channel, bus);
            }
        }
#endif
    }

    void releaseResources() override {
#if defined(_WIN32)
        publisher.reset();
        linkSession.close();
#endif
    }

    bool isBusesLayoutSupported(const BusesLayout& l) const override {
        return l.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
            && l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }

    void processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer&) override {
        juce::ScopedNoDenormals noDenormals;
        saturation.process(b, state.getRawParameterValue("drive")->load(),
                           state.getRawParameterValue("width")->load(),
                           state.getRawParameterValue("cross")->load());
#if defined(_WIN32)
        // The ordinary Studio One signal path is always processed above.
        // Console Link publication is best-effort and never blocks audio.
        const auto samples = b.getNumSamples();
        if (publisher && samples > 0
            && samples <= static_cast<int>(ssl9000::link::maxSamplesPerBlock)
            && b.getNumChannels() >= 2) {
            const auto position = getPlayHead() != nullptr
                ? getPlayHead()->getPosition() : std::nullopt;
            if (position.has_value() && position->getTimeInSamples().has_value()) {
                std::array<float, ssl9000::link::maxSamplesPerBlock * 2> interleaved {};
                const auto* left = b.getReadPointer(0);
                const auto* right = b.getReadPointer(1);
                for (int i = 0; i < samples; ++i) {
                    interleaved[static_cast<std::size_t>(i) * 2] = left[i];
                    interleaved[static_cast<std::size_t>(i) * 2 + 1] = right[i];
                }
                (void) publisher->publish(*position->getTimeInSamples(),
                                          static_cast<std::uint32_t>(samples),
                                          interleaved.data());
            }
        }
#endif
    }

    juce::AudioProcessorEditor* createEditor() override {
        return new juce::GenericAudioProcessorEditor(*this);
    }
    bool hasEditor() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& d) override {
        auto x = state.copyState();
        std::unique_ptr<juce::XmlElement> xml(x.createXml());
        copyXmlToBinary(*xml, d);
    }
    void setStateInformation(const void* d, int n) override {
        std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(d, n));
        if (xml && xml->hasTagName(state.state.getType()))
            state.replaceState(juce::ValueTree::fromXml(*xml));
    }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }

    ssl9000::DynamicSaturation saturation;
    juce::AudioProcessorValueTreeState state;
#if defined(_WIN32)
private:
    ssl9000::link::WindowsConsoleLinkSession linkSession;
    std::unique_ptr<ssl9000::link::ChannelAudioPublisher> publisher;
#endif
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new SSL9000ChannelProcessor();
}

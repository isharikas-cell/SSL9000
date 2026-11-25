#include <juce_audio_utils/juce_audio_utils.h>
#include "ConsoleDSP.h"

#include <array>
#include <cstdint>
#include <memory>

#if defined(_WIN32)
#include "ChannelAudioPublisher.h"
#include "WindowsConsoleLinkSession.h"
#endif


class SSL9000ChannelProcessor;

class SSL9000ChannelEditor final : public juce::AudioProcessorEditor {
public:
    explicit SSL9000ChannelEditor(SSL9000ChannelProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    SSL9000ChannelProcessor& processor;
    juce::Label titleLabel, sessionLabel, channelLabel, busLabel;
    juce::TextEditor sessionKeyEditor;
    juce::Slider channelSlider, busSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> channelAttachment, busAttachment;
};

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
        p.add(std::make_unique<juce::AudioParameterInt>("channelId", "Console Link Channel ID", 0, 31, 0));
        p.add(std::make_unique<juce::AudioParameterInt>("busId", "Console Link Bus ID", 0, 31, 0));
        return p;
    }

    const juce::String getName() const override { return "SSL9000 Channel"; }

    void prepareToPlay(double sampleRate, int) override {
        saturation.prepare(sampleRate);
#if defined(_WIN32)
        publisher.reset();
        const auto key = state.state.getProperty("sessionKey").toString().trim();
        const auto channel = static_cast<std::uint32_t>(state.getRawParameterValue("channelId")->load());
        const auto bus = static_cast<std::uint32_t>(state.getRawParameterValue("busId")->load());
        if (key.isNotEmpty() && channel < ssl9000::link::maxChannels
            && bus < ssl9000::link::maxBuses) {
            const auto result = linkSession.attach(std::wstring(key.toWideCharPointer()),
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
            if (auto* playHead = getPlayHead()) {
                const auto position = playHead->getPosition();
                if (position) {
                    const auto timeSamples = position->getTimeInSamples();
                    if (timeSamples) {
                        std::array<float, ssl9000::link::maxSamplesPerBlock * 2> interleaved {};
                        const auto* left = b.getReadPointer(0);
                        const auto* right = b.getReadPointer(1);
                        for (int i = 0; i < samples; ++i) {
                            interleaved[static_cast<std::size_t>(i) * 2] = left[i];
                            interleaved[static_cast<std::size_t>(i) * 2 + 1] = right[i];
                        }
                        (void) publisher->publish(*timeSamples,
                                                  static_cast<std::uint32_t>(samples),
                                                  interleaved.data());
                    }
                }
            }
        }
#endif
    }

    juce::AudioProcessorEditor* createEditor() override {
        return new SSL9000ChannelEditor(*this);
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


SSL9000ChannelEditor::SSL9000ChannelEditor(SSL9000ChannelProcessor& p)
    : AudioProcessorEditor(p), processor(p) {
    setSize(440, 250);
    titleLabel.setText("SSL9000 Channel", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(22.0f, juce::Font::bold));
    addAndMakeVisible(titleLabel);
    sessionLabel.setText("Project Session Key", juce::dontSendNotification);
    addAndMakeVisible(sessionLabel);
    sessionKeyEditor.setText(processor.state.state.getProperty("sessionKey").toString(), false);
    sessionKeyEditor.setTextToShowWhenEmpty("Paste the same key into every linked plugin", juce::Colours::grey);
    sessionKeyEditor.setInputRestrictions(64, "0123456789abcdefABCDEF-");
    sessionKeyEditor.onFocusLost = [this] {
        processor.state.state.setProperty("sessionKey", sessionKeyEditor.getText().trim(), nullptr);
    };
    sessionKeyEditor.onReturnKey = [this] {
        processor.state.state.setProperty("sessionKey", sessionKeyEditor.getText().trim(), nullptr);
        sessionKeyEditor.giveAwayKeyboardFocus();
    };
    addAndMakeVisible(sessionKeyEditor);
    channelLabel.setText("Channel ID (0–31)", juce::dontSendNotification);
    addAndMakeVisible(channelLabel);
    channelSlider.setSliderStyle(juce::Slider::IncDecButtons);
    channelSlider.setRange(0, 31, 1);
    channelSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 55, 24);
    addAndMakeVisible(channelSlider);
    channelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.state, "channelId", channelSlider);
    busLabel.setText("Bus ID (0–31)", juce::dontSendNotification);
    addAndMakeVisible(busLabel);
    busSlider.setSliderStyle(juce::Slider::IncDecButtons);
    busSlider.setRange(0, 31, 1);
    busSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 55, 24);
    addAndMakeVisible(busSlider);
    busAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.state, "busId", busSlider);
}

void SSL9000ChannelEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff20242a));
    g.setColour(juce::Colours::white);
    g.drawText("Console Link routing", 20, 118, getWidth() - 40, 24, juce::Justification::centredLeft);
    g.setColour(juce::Colours::lightgrey);
    g.drawText("Use the same project key on all linked SSL9000 instances.",
               20, 218, getWidth() - 40, 20, juce::Justification::centredLeft);
}

void SSL9000ChannelEditor::resized() {
    auto area = getLocalBounds().reduced(20);
    titleLabel.setBounds(area.removeFromTop(34));
    area.removeFromTop(12);
    sessionLabel.setBounds(area.removeFromTop(20));
    sessionKeyEditor.setBounds(area.removeFromTop(30));
    channelLabel.setBounds(20, 151, 150, 28);
    channelSlider.setBounds(175, 151, 220, 28);
    busLabel.setBounds(20, 184, 150, 28);
    busSlider.setBounds(175, 184, 220, 28);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new SSL9000ChannelProcessor();
}

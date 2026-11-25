#include <juce_audio_utils/juce_audio_utils.h>
#include "ConsoleLinkProtocol.h"
#include "WindowsConsoleLinkSession.h"
#include "BusAudioReceiver.h"
#include "BusAudioPublisher.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#if !defined(SSL9000_BUS_SEND)
 #define SSL9000_BUS_SEND 0
#endif

class SSL9000BusProcessor;

class SSL9000BusEditor final : public juce::AudioProcessorEditor {
public:
    explicit SSL9000BusEditor(SSL9000BusProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    SSL9000BusProcessor& processor;
    juce::Label title, keyLabel, busLabel, sourcesLabel;
    juce::TextEditor keyEditor, sourcesEditor;
    juce::Slider busSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> busAttachment;
};

class SSL9000BusProcessor final : public juce::AudioProcessor {
public:
    SSL9000BusProcessor()
      : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
        state(*this, nullptr, "PARAMETERS", createLayout()) {}

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout() {
        juce::AudioProcessorValueTreeState::ParameterLayout p;
        p.add(std::make_unique<juce::AudioParameterInt>("busId", "Bus ID", 0, 31, 0));
        return p;
    }

    const juce::String getName() const override {
#if SSL9000_BUS_SEND
        return "SSL9000 Bus Send";
#else
        return "SSL9000 Bus Receive";
#endif
    }

    void prepareToPlay(double sampleRate, int) override {
#if defined(_WIN32)
        receiver.reset();
        publisher.reset();
        linkSession.close();
        const auto key = state.state.getProperty("sessionKey").toString().trim();
        if (key.isEmpty() || sampleRate < 1.0 || sampleRate > 768000.0)
            return;
        const auto bus = static_cast<std::uint32_t>(state.getRawParameterValue("busId")->load());
        const auto result = linkSession.attach(std::wstring(key.toWideCharPointer()),
                                                static_cast<std::uint32_t>(sampleRate));
        if ((result != ssl9000::link::WindowsConsoleLinkSession::AttachResult::created
             && result != ssl9000::link::WindowsConsoleLinkSession::AttachResult::attached)
            || linkSession.get() == nullptr)
            return;
#if SSL9000_BUS_SEND
        publisher = std::make_unique<ssl9000::link::BusAudioPublisher>(
            linkSession.get()->busToMaster[bus], bus);
#else
        std::array<bool, ssl9000::link::maxChannels> assigned {};
        const auto sourceText = state.state.getProperty("sources", "0-31").toString();
        juce::StringArray tokens;
        tokens.addTokens(sourceText, ",", "");
        for (auto token : tokens) {
            token = token.trim();
            const auto dash = token.indexOfChar('-');
            const auto first = (dash < 0 ? token : token.substring(0, dash)).getIntValue();
            auto last = first;
            if (dash >= 0)
                last = token.substring(dash + 1).getIntValue();
            if (first < 0 || last < first || last >= static_cast<int>(ssl9000::link::maxChannels))
                continue;
            for (int id = first; id <= last; ++id)
                assigned[static_cast<std::size_t>(id)] = true;
        }
        receiver = std::make_unique<ssl9000::link::BusAudioReceiver>(*linkSession.get());
        if (!receiver->configure(bus, assigned))
            receiver.reset();
#endif
#endif
    }

    void releaseResources() override {
#if defined(_WIN32)
        receiver.reset();
        publisher.reset();
        linkSession.close();
#endif
    }

    bool isBusesLayoutSupported(const BusesLayout& l) const override {
        return l.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
            && l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        juce::ScopedNoDenormals noDenormals;
#if defined(_WIN32) && !SSL9000_BUS_SEND
        // Bus Receive is a source, not an insert-through effect. Never leave
        // the host input in place when the shared receiver is unavailable.
        buffer.clear();
        const auto count = buffer.getNumSamples();
        if (receiver && count > 0 && count <= static_cast<int>(ssl9000::link::maxSamplesPerBlock)
            && buffer.getNumChannels() >= 2) {
            if (auto* playHead = getPlayHead()) {
                const auto position = playHead->getPosition();
                if (position) {
                    const auto samplePosition = position->getTimeInSamples();
                    if (samplePosition) {
                        std::array<float, ssl9000::link::maxSamplesPerBlock> left {}, right {};
                        if (receiver->process(*samplePosition, static_cast<std::uint32_t>(count),
                                              left.data(), right.data())) {
                            buffer.copyFrom(0, 0, left.data(), count);
                            buffer.copyFrom(1, 0, right.data(), count);
                        }
                    }
                }
            }
        }
#elif defined(_WIN32) && SSL9000_BUS_SEND
        const auto count = buffer.getNumSamples();
        if (publisher && count > 0
            && count <= static_cast<int>(ssl9000::link::maxSamplesPerBlock)
            && buffer.getNumChannels() >= 2) {
            if (auto* playHead = getPlayHead()) {
                const auto position = playHead->getPosition();
                if (position) {
                    const auto samplePosition = position->getTimeInSamples();
                    if (samplePosition) {
                        std::array<float, ssl9000::link::maxSamplesPerBlock * 2> interleaved {};
                        const auto* left = buffer.getReadPointer(0);
                        const auto* right = buffer.getReadPointer(1);
                        for (int i = 0; i < count; ++i) {
                            interleaved[static_cast<std::size_t>(i) * 2] = left[i];
                            interleaved[static_cast<std::size_t>(i) * 2 + 1] = right[i];
                        }
                        (void) publisher->publish(*samplePosition,
                                                  static_cast<std::uint32_t>(count),
                                                  interleaved.data());
                    }
                }
            }
        }
#endif
    }

    juce::AudioProcessorEditor* createEditor() override { return new SSL9000BusEditor(*this); }
    bool hasEditor() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& d) override {
        auto tree = state.copyState();
        std::unique_ptr<juce::XmlElement> xml(tree.createXml());
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

    juce::AudioProcessorValueTreeState state;
#if defined(_WIN32)
private:
    ssl9000::link::WindowsConsoleLinkSession linkSession;
    std::unique_ptr<ssl9000::link::BusAudioReceiver> receiver;
    std::unique_ptr<ssl9000::link::BusAudioPublisher> publisher;
#endif
};

SSL9000BusEditor::SSL9000BusEditor(SSL9000BusProcessor& p)
    : AudioProcessorEditor(p), processor(p) {
    setSize(440, 270);
#if SSL9000_BUS_SEND
    title.setText("SSL9000 Bus Send", juce::dontSendNotification);
#else
    title.setText("SSL9000 Bus Receive", juce::dontSendNotification);
#endif
    title.setFont(juce::Font(22.0f, juce::Font::bold));
    addAndMakeVisible(title);
    keyLabel.setText("Project Session Key", juce::dontSendNotification);
    addAndMakeVisible(keyLabel);
    keyEditor.setText(processor.state.state.getProperty("sessionKey").toString(), false);
    keyEditor.setTextToShowWhenEmpty("Same key as SSL9000 Channel and Master", juce::Colours::grey);
    keyEditor.setInputRestrictions(64, "0123456789abcdefABCDEF-");
    keyEditor.onFocusLost = [this] {
        processor.state.state.setProperty("sessionKey", keyEditor.getText().trim(), nullptr);
    };
    keyEditor.onReturnKey = [this] {
        processor.state.state.setProperty("sessionKey", keyEditor.getText().trim(), nullptr);
        keyEditor.giveAwayKeyboardFocus();
    };
    addAndMakeVisible(keyEditor);
    busLabel.setText("Bus ID (0–31)", juce::dontSendNotification);
    addAndMakeVisible(busLabel);
    busSlider.setSliderStyle(juce::Slider::IncDecButtons);
    busSlider.setRange(0, 31, 1);
    busSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 55, 24);
    addAndMakeVisible(busSlider);
    busAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.state, "busId", busSlider);
#if !SSL9000_BUS_SEND
    sourcesLabel.setText("Channel IDs (e.g. 0-7, 9, 12-15)", juce::dontSendNotification);
    addAndMakeVisible(sourcesLabel);
    sourcesEditor.setText(processor.state.state.getProperty("sources", "0-31").toString(), false);
    sourcesEditor.setInputRestrictions(96, "0123456789-, ");
    sourcesEditor.onFocusLost = [this] {
        processor.state.state.setProperty("sources", sourcesEditor.getText().trim(), nullptr);
    };
    sourcesEditor.onReturnKey = [this] {
        processor.state.state.setProperty("sources", sourcesEditor.getText().trim(), nullptr);
        sourcesEditor.giveAwayKeyboardFocus();
    };
    addAndMakeVisible(sourcesEditor);
#endif
}

void SSL9000BusEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff20242a));
    g.setColour(juce::Colours::white);
    g.drawText("Console Link routing", 20, 118, getWidth() - 40, 24, juce::Justification::centredLeft);
    g.setColour(juce::Colours::lightgrey);
#if SSL9000_BUS_SEND
    g.drawText("Place last on the Studio One group bus.", 20, 238, getWidth() - 40, 20, juce::Justification::centredLeft);
#else
    g.drawText("Place first on the group bus, before third-party effects.", 20, 238, getWidth() - 40, 20, juce::Justification::centredLeft);
#endif
}

void SSL9000BusEditor::resized() {
    title.setBounds(20, 20, 400, 34);
    keyLabel.setBounds(20, 68, 400, 20);
    keyEditor.setBounds(20, 90, 400, 30);
    busLabel.setBounds(20, 132, 150, 26);
    busSlider.setBounds(175, 132, 220, 28);
#if !SSL9000_BUS_SEND
    sourcesLabel.setBounds(20, 174, 400, 22);
    sourcesEditor.setBounds(20, 198, 400, 28);
#endif
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new SSL9000BusProcessor();
}

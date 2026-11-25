#include <juce_audio_utils/juce_audio_utils.h>
#include "ConsoleDSP.h"
#include "ConsoleLinkProtocol.h"
#include "WindowsConsoleLinkSession.h"
#include "MasterAudioReceiver.h"

#include <array>
#include <memory>
#include <string>

class SSL9000MasterProcessor final : public juce::AudioProcessor {
public:
    SSL9000MasterProcessor()
      : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
        state(*this, nullptr, "PARAMETERS", createLayout()) {}

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout() {
        juce::AudioProcessorValueTreeState::ParameterLayout p;
        p.add(std::make_unique<juce::AudioParameterFloat>("drive", "Master Saturation", juce::NormalisableRange<float>(0.0f, 12.0f, 0.01f), 0.0f));
        p.add(std::make_unique<juce::AudioParameterFloat>("width", "Stereo Width", juce::NormalisableRange<float>(0.0f, 2.0f, 0.001f), 1.0f));
        p.add(std::make_unique<juce::AudioParameterFloat>("cross", "Crosstalk", juce::NormalisableRange<float>(0.0f, 0.08f, 0.0001f), 0.015f));
        return p;
    }

    const juce::String getName() const override { return "SSL9000 Master"; }

    void prepareToPlay(double sampleRate, int) override {
        saturation.prepare(sampleRate);
#if defined(_WIN32)
        receiver.reset();
        linkSession.close();
        const auto key = state.state.getProperty("sessionKey").toString().trim();
        if (key.isEmpty() || sampleRate < 1.0 || sampleRate > 768000.0)
            return;
        const auto result = linkSession.attach(std::wstring(key.toWideCharPointer()),
                                                static_cast<std::uint32_t>(sampleRate));
        if ((result != ssl9000::link::WindowsConsoleLinkSession::AttachResult::created
             && result != ssl9000::link::WindowsConsoleLinkSession::AttachResult::attached)
            || linkSession.get() == nullptr)
            return;

        std::array<bool, ssl9000::link::maxBuses> selected {};
        auto text = state.state.getProperty("buses", "0-31").toString();
        juce::StringArray tokens;
        tokens.addTokens(text, ",", "");
        for (auto token : tokens) {
            token = token.trim();
            if (token.isEmpty())
                continue;
            const auto dash = token.indexOfChar('-');
            const auto first = (dash < 0 ? token : token.substring(0, dash)).getIntValue();
            auto last = first;
            if (dash >= 0)
                last = token.substring(dash + 1).getIntValue();
            if (first < 0 || last < first || last >= static_cast<int>(ssl9000::link::maxBuses))
                continue;
            for (int id = first; id <= last; ++id)
                selected[static_cast<std::size_t>(id)] = true;
        }
        receiver = std::make_unique<ssl9000::link::MasterAudioReceiver>(*linkSession.get());
        if (!receiver->configure(selected))
            receiver.reset();
#endif
    }

    void releaseResources() override {
#if defined(_WIN32)
        receiver.reset();
        linkSession.close();
#endif
    }

    bool isBusesLayoutSupported(const BusesLayout& l) const override {
        return l.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
            && l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        juce::ScopedNoDenormals noDenormals;
#if defined(_WIN32)
        const auto count = buffer.getNumSamples();
        if (receiver && count > 0
            && count <= static_cast<int>(ssl9000::link::maxSamplesPerBlock)
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
#endif
        saturation.process(buffer, state.getRawParameterValue("drive")->load(),
                           state.getRawParameterValue("width")->load(),
                           state.getRawParameterValue("cross")->load(), -30.0f);
    }

    class Editor final : public juce::AudioProcessorEditor {
    public:
        explicit Editor(SSL9000MasterProcessor& p) : AudioProcessorEditor(p), processor(p) {
            setSize(440, 250);
            title.setText("SSL9000 Master", juce::dontSendNotification);
            title.setFont(juce::Font(22.0f, juce::Font::bold));
            addAndMakeVisible(title);
            keyLabel.setText("Project Session Key", juce::dontSendNotification);
            addAndMakeVisible(keyLabel);
            key.setText(processor.state.state.getProperty("sessionKey").toString(), false);
            key.setTextToShowWhenEmpty("Same key as Channel and Bus plugins", juce::Colours::grey);
            key.setInputRestrictions(64, "0123456789abcdefABCDEF-");
            key.onFocusLost = [this] { processor.state.state.setProperty("sessionKey", key.getText().trim(), nullptr); };
            key.onReturnKey = [this] { processor.state.state.setProperty("sessionKey", key.getText().trim(), nullptr); key.giveAwayKeyboardFocus(); };
            addAndMakeVisible(key);
            busesLabel.setText("Bus IDs to sum (e.g. 0-3, 5, 8-9)", juce::dontSendNotification);
            addAndMakeVisible(busesLabel);
            buses.setText(processor.state.state.getProperty("buses", "0-31").toString(), false);
            buses.setInputRestrictions(96, "0123456789-, ");
            buses.onFocusLost = [this] { processor.state.state.setProperty("buses", buses.getText().trim(), nullptr); };
            buses.onReturnKey = [this] { processor.state.state.setProperty("buses", buses.getText().trim(), nullptr); buses.giveAwayKeyboardFocus(); };
            addAndMakeVisible(buses);
            addSlider(drive, driveAttachment, "drive");
            addSlider(width, widthAttachment, "width");
            addSlider(cross, crossAttachment, "cross");
        }
        void paint(juce::Graphics& g) override {
            g.fillAll(juce::Colour(0xff20242a));
            g.setColour(juce::Colours::white);
            g.drawText("Receives selected Bus Send streams and sums them before master processing.",
                       20, 214, getWidth() - 40, 24, juce::Justification::centredLeft);
        }
        void resized() override {
            title.setBounds(20, 14, 400, 32);
            keyLabel.setBounds(20, 52, 400, 18);
            key.setBounds(20, 72, 400, 28);
            busesLabel.setBounds(20, 106, 400, 18);
            buses.setBounds(20, 126, 400, 28);
            drive.setBounds(20, 164, 125, 28);
            width.setBounds(155, 164, 125, 28);
            cross.setBounds(290, 164, 125, 28);
        }
    private:
        void addSlider(juce::Slider& slider,
                       std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attachment,
                       const juce::String& id) {
            slider.setSliderStyle(juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 18);
            addAndMakeVisible(slider);
            attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                processor.state, id, slider);
        }
        SSL9000MasterProcessor& processor;
        juce::Label title, keyLabel, busesLabel;
        juce::TextEditor key, buses;
        juce::Slider drive, width, cross;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> driveAttachment, widthAttachment, crossAttachment;
    };

    juce::AudioProcessorEditor* createEditor() override { return new Editor(*this); }
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
    std::unique_ptr<ssl9000::link::MasterAudioReceiver> receiver;
#endif
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SSL9000MasterProcessor(); }

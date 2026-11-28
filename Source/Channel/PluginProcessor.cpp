#include <juce_audio_utils/juce_audio_utils.h>
#include "ConsoleDSP.h"

#include <memory>

class SSL9000ChannelProcessor final : public juce::AudioProcessor {
public:
    SSL9000ChannelProcessor()
        : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          state(*this, nullptr, "PARAMETERS", createLayout()) {}

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout() {
        juce::AudioProcessorValueTreeState::ParameterLayout p;
        p.add(std::make_unique<juce::AudioParameterFloat>(
            "drive", "Saturation", juce::NormalisableRange<float>(0.0f, 18.0f, 0.01f), 0.0f));
        p.add(std::make_unique<juce::AudioParameterFloat>(
            "width", "Stereo Width", juce::NormalisableRange<float>(0.0f, 2.0f, 0.001f), 1.0f));
        return p;
    }

    const juce::String getName() const override { return "SSL9000 Channel"; }
    void prepareToPlay(double sampleRate, int) override { saturation.prepare(sampleRate); }
    void releaseResources() override {}

    bool isBusesLayoutSupported(const BusesLayout& l) const override {
        return l.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
            && l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }
    bool supportsDoublePrecisionProcessing() const override { return true; }

    void processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer&) override {
        juce::ScopedNoDenormals noDenormals;
        processAudio(b);
    }
    void processBlock(juce::AudioBuffer<double>& b, juce::MidiBuffer&) override {
        juce::ScopedNoDenormals noDenormals;
        processAudio(b);
    }

    template <typename SampleType>
    void processAudio(juce::AudioBuffer<SampleType>& b) noexcept {
        saturation.process(b,
            static_cast<double>(state.getRawParameterValue("drive")->load()),
            static_cast<double>(state.getRawParameterValue("width")->load()));
    }

    class Editor final : public juce::AudioProcessorEditor {
    public:
        explicit Editor(SSL9000ChannelProcessor& p) : AudioProcessorEditor(p), processor(p) {
            setSize(360, 190);
            title.setText("SSL9000 Channel", juce::dontSendNotification);
            title.setFont(juce::Font(22.0f, juce::Font::bold));
            addAndMakeVisible(title);
            addControl(drive, driveAttachment, "drive");
            addControl(width, widthAttachment, "width");
        }
        void paint(juce::Graphics& g) override {
            g.fillAll(juce::Colour(0xff20242a));
            g.setColour(juce::Colours::lightgrey);
            g.drawText("Stereo channel processing • double-precision DSP",
                       20, 158, getWidth() - 40, 20, juce::Justification::centredLeft);
        }
        void resized() override {
            title.setBounds(20, 14, 320, 32);
            drive.setBounds(65, 66, 100, 68);
            width.setBounds(195, 66, 100, 68);
        }
    private:
        void addControl(juce::Slider& s,
                        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& a,
                        const juce::String& id) {
            s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 18);
            addAndMakeVisible(s);
            a = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.state, id, s);
        }
        SSL9000ChannelProcessor& processor;
        juce::Label title;
        juce::Slider drive, width;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> driveAttachment, widthAttachment;
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

    ssl9000::DynamicSaturation saturation;
    juce::AudioProcessorValueTreeState state;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SSL9000ChannelProcessor(); }

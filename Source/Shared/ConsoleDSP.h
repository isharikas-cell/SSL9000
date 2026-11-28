#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

namespace ssl9000 {

// DSP state and intermediate calculations use double precision.
class DynamicSaturation {
public:
    void prepare(double sampleRate) noexcept {
        const double sr = sampleRate > 1.0 ? sampleRate : 44100.0;
        attackCoeff = std::exp(-1.0 / (0.008 * sr));
        releaseCoeff = std::exp(-1.0 / (0.120 * sr));
        envelope = 0.0;
    }

    template <typename SampleType>
    void process(juce::AudioBuffer<SampleType>& buffer,
                 double drive, double width,
                 double thresholdDb = -24.0) noexcept {
        if (buffer.getNumChannels() < 2)
            return;

        auto* left = buffer.getWritePointer(0);
        auto* right = buffer.getWritePointer(1);
        const double driveAmount = juce::jlimit(0.0, 1.0, drive / 18.0);
        const double maxCurve = 1.0 + driveAmount * 8.0;
        const double sideGain = juce::jlimit(0.0, 2.0, width);
        const double threshold = std::pow(10.0, juce::jlimit(-60.0, 0.0, thresholdDb) / 20.0);
        constexpr double kneeRange = 0.12;

        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            const double inL = static_cast<double>(left[i]);
            const double inR = static_cast<double>(right[i]);
            const double detector = juce::jmax(std::abs(inL), std::abs(inR));
            const double coeff = detector > envelope ? attackCoeff : releaseCoeff;
            envelope = coeff * envelope + (1.0 - coeff) * detector;

            const double activity = juce::jlimit(0.0, 1.0, (envelope - threshold) / kneeRange);
            const double saturation = driveAmount * activity;
            const double curve = 1.0 + saturation * (maxCurve - 1.0);
            const double mid = 0.5 * (inL + inR);
            const double side = 0.5 * (inL - inR) * sideGain;
            const double dryL = mid + side;
            const double dryR = mid - side;
            const double shapedL = std::tanh(dryL * curve) / curve;
            const double shapedR = std::tanh(dryR * curve) / curve;
            left[i] = static_cast<SampleType>(dryL + saturation * (shapedL - dryL));
            right[i] = static_cast<SampleType>(dryR + saturation * (shapedR - dryR));
        }
    }

private:
    double envelope = 0.0;
    double attackCoeff = 0.0;
    double releaseCoeff = 0.0;
};

} // namespace ssl9000

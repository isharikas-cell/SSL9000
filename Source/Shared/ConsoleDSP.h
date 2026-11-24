#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

namespace ssl9000 {
class DynamicSaturation {
public:
    void prepare(double sampleRate) noexcept {
        const float sr = static_cast<float>(sampleRate > 1.0 ? sampleRate : 44100.0);
        attackCoeff = std::exp(-1.0f / (0.008f * sr));
        releaseCoeff = std::exp(-1.0f / (0.120f * sr));
        envelope = 0.0f;
    }
    void process(juce::AudioBuffer<float>& buffer, float drive, float width, float crosstalk) noexcept {
        if (buffer.getNumChannels() < 2) return;
        auto* left = buffer.getWritePointer(0);
        auto* right = buffer.getWritePointer(1);
        const float driveAmount = juce::jlimit(0.0f, 1.0f, drive / 18.0f);
        const float maxCurve = 1.0f + driveAmount * 8.0f;
        const float sideGain = juce::jlimit(0.0f, 2.0f, width);
        const float cross = juce::jlimit(0.0f, 0.08f, crosstalk);
        constexpr float threshold = 0.0630957f; // -24 dBFS
        // A narrower knee makes saturation clearly engage on material around -20 dBFS.
        // At -20 dBFS (linear peak 0.1), activity is approximately 0.31.
        constexpr float kneeRange = 0.12f;
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            const float inL = left[i], inR = right[i];
            const float detector = juce::jmax(std::abs(inL), std::abs(inR));
            const float coeff = detector > envelope ? attackCoeff : releaseCoeff;
            envelope = coeff * envelope + (1.0f - coeff) * detector;
            const float activity = juce::jlimit(0.0f, 1.0f, (envelope - threshold) / kneeRange);
            const float saturation = driveAmount * activity;
            const float curve = 1.0f + saturation * (maxCurve - 1.0f);
            const float mid = 0.5f * (inL + inR);
            const float side = 0.5f * (inL - inR) * sideGain;
            const float dryL = mid + side, dryR = mid - side;
            const float shapedL = std::tanh(dryL * curve) / curve;
            const float shapedR = std::tanh(dryR * curve) / curve;
            const float outL = dryL + saturation * (shapedL - dryL);
            const float outR = dryR + saturation * (shapedR - dryR);
            left[i] = outL * (1.0f - cross) + outR * cross;
            right[i] = outR * (1.0f - cross) + outL * cross;
        }
    }
private:
    float envelope = 0.0f, attackCoeff = 0.0f, releaseCoeff = 0.0f;
};
} // namespace ssl9000

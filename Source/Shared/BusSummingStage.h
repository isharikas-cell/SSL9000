#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace ssl9000 {

// Fixed-capacity post-summing stage. Inputs and outputs are planar stereo.
// The caller owns all buffers; this class performs no allocation or locking.
class BusSummingStage {
public:
    static constexpr std::uint32_t maxSources = 32;

    void prepare(double sampleRate) noexcept {
        const float sr = static_cast<float>(sampleRate > 1.0 ? sampleRate : 44100.0);
        attackCoeff = std::exp(-1.0f / (0.008f * sr));
        releaseCoeff = std::exp(-1.0f / (0.120f * sr));
        envelope = 0.0f;
    }

    void reset() noexcept { envelope = 0.0f; }

    void process(const float* const* leftInputs,
                 const float* const* rightInputs,
                 std::uint32_t sourceCount,
                 std::uint32_t sampleCount,
                 float* outputLeft,
                 float* outputRight,
                 float drive,
                 float thresholdDb = -24.0f,
                 float outputGain = 1.0f) noexcept {
        if (outputLeft == nullptr || outputRight == nullptr)
            return;

        const auto count = std::min(sourceCount, maxSources);
        const float amount = std::clamp(drive, 0.0f, 1.0f);
        const float gain = std::clamp(outputGain, 0.0f, 16.0f);
        const float threshold = std::pow(10.0f, std::clamp(thresholdDb, -60.0f, 0.0f) / 20.0f);
        constexpr float kneeRange = 0.12f;

        for (std::uint32_t i = 0; i < sampleCount; ++i) {
            float sumL = 0.0f;
            float sumR = 0.0f;
            for (std::uint32_t source = 0; source < count; ++source) {
                if (leftInputs != nullptr && leftInputs[source] != nullptr)
                    sumL += leftInputs[source][i];
                if (rightInputs != nullptr && rightInputs[source] != nullptr)
                    sumR += rightInputs[source][i];
            }

            const float detector = std::max(std::abs(sumL), std::abs(sumR));
            const float coeff = detector > envelope ? attackCoeff : releaseCoeff;
            envelope = coeff * envelope + (1.0f - coeff) * detector;

            const float activity = std::clamp((envelope - threshold) / kneeRange, 0.0f, 1.0f);
            const float saturation = amount * activity;
            const float curve = 1.0f + saturation * 7.0f;
            const float shapedL = std::tanh(sumL * curve) / curve;
            const float shapedR = std::tanh(sumR * curve) / curve;

            outputLeft[i] = (sumL + saturation * (shapedL - sumL)) * gain;
            outputRight[i] = (sumR + saturation * (shapedR - sumR)) * gain;
        }
    }

private:
    float envelope = 0.0f;
    float attackCoeff = 0.0f;
    float releaseCoeff = 0.0f;
};

} // namespace ssl9000

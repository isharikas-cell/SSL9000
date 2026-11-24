#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

namespace ssl9000 {
inline void process(juce::AudioBuffer<float>& buffer, float driveDb, float width, float crosstalk) noexcept {
    if (buffer.getNumChannels() < 2) return;
    const int samples = buffer.getNumSamples();
    const float amount = juce::jlimit(0.0f, 18.0f, driveDb) / 18.0f;
    const float drive = 1.0f + amount * 7.0f;
    const float normaliser = std::tanh(drive);
    const float cross = juce::jlimit(0.0f, 0.08f, crosstalk);
    const float sideGain = juce::jlimit(0.0f, 2.0f, width);
    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getWritePointer(1);
    for (int i = 0; i < samples; ++i) {
        const float mid = 0.5f * (left[i] + right[i]);
        const float side = 0.5f * (left[i] - right[i]) * sideGain;
        const float dryL = mid + side;
        const float dryR = mid - side;
        const float wetL = std::tanh(dryL * drive) / normaliser;
        const float wetR = std::tanh(dryR * drive) / normaliser;
        const float l = dryL + amount * (wetL - dryL);
        const float r = dryR + amount * (wetR - dryR);
        left[i] = l * (1.0f - cross) + r * cross;
        right[i] = r * (1.0f - cross) + l * cross;
    }
}
}

#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

namespace ssl9000 {
inline void process(juce::AudioBuffer<float>& buffer, float driveDb, float width, float crosstalk) noexcept {
    const int channels = buffer.getNumChannels();
    const int samples = buffer.getNumSamples();
    if (channels < 2) return;
    const float drive = std::pow(10.0f, juce::jlimit(0.0f, 18.0f, driveDb) / 20.0f);
    const float cross = juce::jlimit(0.0f, 0.08f, crosstalk);
    const float midGain = 1.0f;
    const float sideGain = juce::jlimit(0.0f, 2.0f, width);
    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getWritePointer(1);
    for (int i = 0; i < samples; ++i) {
        const float mid = 0.5f * (left[i] + right[i]) * midGain;
        const float side = 0.5f * (left[i] - right[i]) * sideGain;
        float l = (mid + side) * drive;
        float r = (mid - side) * drive;
        l = std::tanh(l) / std::tanh(drive);
        r = std::tanh(r) / std::tanh(drive);
        const float outL = l * (1.0f - cross) + r * cross;
        const float outR = r * (1.0f - cross) + l * cross;
        left[i] = outL;
        right[i] = outR;
    }
}
}

#include "puddle_dsp.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
constexpr float kTwoPi = 6.28318530717958647692f;

PuddleDSP::Config makeConfig() {
    PuddleDSP::Config config;
    config.sampleRate = 48000.0f;
    config.rate = 0.5f;
    config.damp = 0.5f;
    config.depth = 0.5f;
    config.lpg = 0.5f;
    config.mix = 0.5f;
    config.volume = 1.0f;
    config.randomSeed = 12345U;
    return config;
}

std::vector<float> makeSine(uint32_t numSamples, float sampleRate, float frequency, float amplitude) {
    std::vector<float> signal(numSamples);
    for (uint32_t i = 0; i < numSamples; ++i) {
        signal[i] = std::sin(kTwoPi * frequency * static_cast<float>(i) / sampleRate) * amplitude;
    }
    return signal;
}
}

void testInitialization() {
    PuddleDSP dsp;
    dsp.initialize(makeConfig());

    float input[4] = {0.0f, 0.25f, -0.25f, 0.0f};
    float output[4] = {};
    dsp.process(input, output, 4);

    assert(std::fabs(output[1]) > 0.0f);
    std::cout << "Initialization test passed\n";
}

void testAudioProcessing() {
    PuddleDSP dsp;
    dsp.initialize(makeConfig());

    const uint32_t numSamples = 48000U;
    const std::vector<float> input = makeSine(numSamples, 48000.0f, 440.0f, 0.5f);
    std::vector<float> output(numSamples);

    dsp.process(input.data(), output.data(), numSamples);

    float maxLevel = 0.0f;
    for (uint32_t i = 0; i < numSamples; ++i) {
        maxLevel = std::max(maxLevel, std::fabs(output[i]));
    }

    assert(maxLevel > 0.01f);
    assert(maxLevel < 2.0f);
    std::cout << "Audio processing test passed\n";
}

void testParameterChanges() {
    PuddleDSP dsp;
    dsp.initialize(makeConfig());

    dsp.setRate(0.8f);
    dsp.setDepth(0.3f);
    dsp.setMix(0.7f);
    dsp.setVolume(1.2f);

    const float input[8] = {0.0f, 0.1f, 0.2f, 0.1f, 0.0f, -0.1f, -0.2f, -0.1f};
    float output[8] = {};
    dsp.process(input, output, 8);

    assert(std::fabs(output[2]) > 0.0f);
    std::cout << "Parameter change test passed\n";
}

void testParameterSmoothing() {
    PuddleDSP dsp;
    PuddleDSP::Config config = makeConfig();
    config.mix = 0.0f;
    config.volume = 1.0f;
    dsp.initialize(config);

    const uint32_t numSamples = 256U;
    std::vector<float> input(numSamples, 1.0f);
    std::vector<float> output(numSamples, 0.0f);

    dsp.setVolume(0.0f);
    dsp.process(input.data(), output.data(), numSamples);

    assert(output.front() > 0.9f);
    assert(output.back() < 0.7f);
    assert(output.back() > 0.0f);
    std::cout << "Parameter smoothing test passed\n";
}

void testDeterminism() {
    PuddleDSP dsp1;
    PuddleDSP dsp2;

    const PuddleDSP::Config config = makeConfig();
    dsp1.initialize(config);
    dsp2.initialize(config);

    const uint32_t numSamples = 4800U;
    const std::vector<float> input = makeSine(numSamples, 48000.0f, 440.0f, 0.5f);
    std::vector<float> output1(numSamples);
    std::vector<float> output2(numSamples);

    dsp1.process(input.data(), output1.data(), numSamples);
    dsp2.process(input.data(), output2.data(), numSamples);

    for (uint32_t i = 0; i < numSamples; ++i) {
        assert(std::fabs(output1[i] - output2[i]) < 1e-7f);
    }

    std::cout << "Determinism test passed\n";
}

void testExternalDelayStorage() {
    PuddleDSP dsp;
    const PuddleDSP::Config config = makeConfig();
    std::vector<float> delayStorage(PuddleDSP::requiredDelayBufferSamples(config.sampleRate));
    const std::vector<float> input = makeSine(4096U, 48000.0f, 330.0f, 0.35f);
    std::vector<float> output(input.size());

    dsp.initialize(config, delayStorage.data(), static_cast<uint32_t>(delayStorage.size()));
    dsp.process(input.data(), output.data(), static_cast<uint32_t>(output.size()));

    float maxLevel = 0.0f;
    for (float sample : output) {
        maxLevel = std::max(maxLevel, std::fabs(sample));
    }

    assert(maxLevel > 0.01f);
    std::cout << "External storage test passed\n";
}

void testResetRewindsState() {
    PuddleDSP dsp;
    dsp.initialize(makeConfig());

    const uint32_t numSamples = 4096U;
    const std::vector<float> input = makeSine(numSamples, 48000.0f, 220.0f, 0.4f);
    std::vector<float> firstPass(numSamples);
    std::vector<float> secondPass(numSamples);

    dsp.process(input.data(), firstPass.data(), numSamples);
    dsp.reset();
    dsp.process(input.data(), secondPass.data(), numSamples);

    for (uint32_t i = 0; i < numSamples; ++i) {
        assert(std::fabs(firstPass[i] - secondPass[i]) < 1e-7f);
    }

    std::cout << "Reset test passed\n";
}

int main() {
    std::cout << "Running Puddle DSP tests...\n";

    testInitialization();
    testAudioProcessing();
    testParameterChanges();
    testParameterSmoothing();
    testDeterminism();
    testExternalDelayStorage();
    testResetRewindsState();

    std::cout << "All tests passed\n";
    return 0;
}

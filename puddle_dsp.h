#pragma once

#include <cstdint>

#ifndef PUDDLE_NO_HEAP
#include <vector>
#endif

class PuddleDSP {
public:
    struct Config {
        float sampleRate = 48000.0f;
        float rate = 0.5f;
        float damp = 0.5f;
        float depth = 0.5f;
        float lpg = 0.5f;
        float feedback = 0.15f;
        float mix = 0.5f;
        float volume = 1.0f;
        uint32_t randomSeed = 0x5044444Cu;
    };

    PuddleDSP();
    ~PuddleDSP();

    void initialize(const Config& config);
    void initialize(const Config& config, float* delayBuffer, uint32_t delayBufferSamples);
    static uint32_t requiredDelayBufferSamples(float sampleRate);

    void setRate(float value);
    void setDamp(float value);
    void setDepth(float value);
    void setLpg(float value);
    void setFeedback(float value);
    void setMix(float value);
    void setVolume(float value);

    void process(const float* input, float* output, uint32_t numSamples);
    void reset();

private:
    struct DelayLine {
        float* buffer = nullptr;
        uint32_t writePos = 0;
        uint32_t size = 0;
        float baseDelayMs = 35.0f;
        float maxModulationMs = 15.0f;
    };

    struct RandomCVGenerator {
        uint32_t state = 0;
        float currentValue = 0.0f;
        float targetValue = 0.0f;
        float slewRate = 1.0f;
        uint32_t updateInterval = 1;
        uint32_t samplesUntilUpdate = 0;
    };

    struct EnvelopeFollower {
        float level = 0.0f;
        float attackCoeff = 0.0f;
        float releaseCoeff = 0.0f;
    };

    struct LowPassFilter {
        float state = 0.0f;
        float baseCutoffHz = 8000.0f;
        float modulationDepthHz = 12000.0f;
    };

    struct DCBlocker {
        float x1 = 0.0f;
        float y1 = 0.0f;
    };

    struct SmoothedParameters {
        float rate = 0.5f;
        float damp = 0.5f;
        float depth = 0.5f;
        float lpg = 0.5f;
        float feedback = 0.15f;
        float mix = 0.5f;
        float volume = 1.0f;
        float coeff = 1.0f;
    };

    static float clamp(float value, float minValue, float maxValue);

    void clearDelayBuffer();
    void updateDerivedState();
    void updateParameterSmoothingCoeff();
    void updateRandomInterval();
    void updateRandomSlew();
    void updateEnvelopeCoefficients();
    void syncSmoothedParameters();
    void smoothParameters();

    float readDelay(float modulation) const;
    void writeDelay(float sample);
    float generateRandomCV();
    float trackEnvelope(float inputSample);
    float filterSample(float input, float envLevel);
    float blockDC(float input);
    static float softClip(float input);
    float nextRandomSigned();

    Config m_config;
    DelayLine m_delay;
    RandomCVGenerator m_randomCV;
    EnvelopeFollower m_envFollower;
    LowPassFilter m_filter;
    DCBlocker m_dcBlocker;
    SmoothedParameters m_smoothed;

#ifndef PUDDLE_NO_HEAP
    std::vector<float> m_ownedDelayBuffer;
#endif

    bool m_initialized = false;
};

#include "puddle_dsp.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kMinDelayMs = 1.0f;
constexpr float kMaxDelayMs = 49.0f;
constexpr float kMinRateIntervalMs = 50.0f;
constexpr float kMaxRateIntervalMs = 2000.0f;
constexpr float kMinSlewTimeMs = 2.0f;
constexpr float kMaxSlewTimeMs = 400.0f;
constexpr float kAttackTimeMs = 0.75f;
constexpr float kReleaseTimeMs = 55.0f;
constexpr float kUint32ToUnit = 1.0f / 4294967295.0f;
constexpr float kMaxLinearVolume = 15.848932f;
constexpr float kMaxFeedback = 0.95f;
constexpr float kDCBlockerR = 0.9975f;
constexpr float kParameterSmoothingMs = 10.0f;
constexpr float kPiOver2 = 1.57079632679489661923f;

uint32_t roundToUInt(float value) {
    return static_cast<uint32_t>(value + 0.5f);
}

float expApproxNegative(float value) {
    // All current call sites feed negative coefficients. Keep the approximation simple
    // and self-contained so the plugin does not depend on libm symbols at load time.
    if (value >= 0.0f) {
        return 1.0f;
    }
    if (value <= -10.0f) {
        return 0.0f;
    }

    const float x = value * 0.125f;
    const float x2 = x * x;
    const float poly =
        1.0f + x + (0.5f * x2) + ((x2 * x) * (1.0f / 6.0f)) +
        ((x2 * x2) * (1.0f / 24.0f)) + ((x2 * x2 * x) * (1.0f / 120.0f));

    float result = poly;
    result *= result;
    result *= result;
    result *= result;
    return std::max(0.0f, result);
}

bool smoothToward(float& current, float target, float coeff) {
    const float delta = target - current;
    if (std::fabs(delta) <= 1e-6f) {
        if (current != target) {
            current = target;
            return true;
        }
        return false;
    }

    current += coeff * delta;
    if (std::fabs(target - current) <= 1e-6f) {
        current = target;
    }
    return true;
}
}

PuddleDSP::PuddleDSP() = default;

PuddleDSP::~PuddleDSP() = default;

float PuddleDSP::clamp(float value, float minValue, float maxValue) {
    return std::max(minValue, std::min(value, maxValue));
}

uint32_t PuddleDSP::requiredDelayBufferSamples(float sampleRate) {
    const float safeSampleRate = std::max(1.0f, sampleRate);
    return std::max<uint32_t>(2U, roundToUInt(50.0f * safeSampleRate / 1000.0f) + 2U);
}

void PuddleDSP::initialize(const Config& config) {
#ifdef PUDDLE_NO_HEAP
    (void)config;
    m_delay.buffer = nullptr;
    m_delay.size = 0;
    m_initialized = false;
#else
    m_ownedDelayBuffer.assign(requiredDelayBufferSamples(config.sampleRate), 0.0f);
    initialize(config, m_ownedDelayBuffer.data(), static_cast<uint32_t>(m_ownedDelayBuffer.size()));
#endif
}

void PuddleDSP::initialize(const Config& config, float* delayBuffer, uint32_t delayBufferSamples) {
    m_config = config;
    m_config.sampleRate = std::max(1.0f, config.sampleRate);
    m_config.rate = clamp(config.rate, 0.0f, 1.0f);
    m_config.damp = clamp(config.damp, 0.0f, 1.0f);
    m_config.depth = clamp(config.depth, 0.0f, 1.0f);
    m_config.lpg = clamp(config.lpg, 0.0f, 1.0f);
    m_config.feedback = clamp(config.feedback, 0.0f, kMaxFeedback);
    m_config.mix = clamp(config.mix, 0.0f, 1.0f);
    m_config.volume = clamp(config.volume, 0.0f, kMaxLinearVolume);

    const uint32_t requiredSamples = requiredDelayBufferSamples(m_config.sampleRate);
    if (delayBuffer == nullptr || delayBufferSamples < requiredSamples) {
        m_delay.buffer = nullptr;
        m_delay.size = 0;
        m_initialized = false;
        return;
    }

    m_delay.buffer = delayBuffer;
    m_delay.size = delayBufferSamples;
    m_delay.writePos = 0;
    m_delay.baseDelayMs = 35.0f;
    m_delay.maxModulationMs = 15.0f;

    m_filter.state = 0.0f;
    m_filter.baseCutoffHz = 8000.0f;
    m_filter.modulationDepthHz = 12000.0f;

    m_dcBlocker.x1 = 0.0f;
    m_dcBlocker.y1 = 0.0f;

    syncSmoothedParameters();
    m_initialized = true;
    updateDerivedState();
    reset();
}

void PuddleDSP::setRate(float value) {
    m_config.rate = clamp(value, 0.0f, 1.0f);
}

void PuddleDSP::setDamp(float value) {
    m_config.damp = clamp(value, 0.0f, 1.0f);
}

void PuddleDSP::setDepth(float value) {
    m_config.depth = clamp(value, 0.0f, 1.0f);
}

void PuddleDSP::setLpg(float value) {
    m_config.lpg = clamp(value, 0.0f, 1.0f);
}

void PuddleDSP::setFeedback(float value) {
    m_config.feedback = clamp(value, 0.0f, kMaxFeedback);
}

void PuddleDSP::setMix(float value) {
    m_config.mix = clamp(value, 0.0f, 1.0f);
}

void PuddleDSP::setVolume(float value) {
    m_config.volume = clamp(value, 0.0f, kMaxLinearVolume);
}

void PuddleDSP::updateDerivedState() {
    updateParameterSmoothingCoeff();
    updateRandomInterval();
    updateRandomSlew();
    updateEnvelopeCoefficients();
}

void PuddleDSP::updateParameterSmoothingCoeff() {
    const float smoothingSamples =
        std::max(1.0f, kParameterSmoothingMs * m_config.sampleRate / 1000.0f);
    m_smoothed.coeff = 1.0f - expApproxNegative(-1.0f / smoothingSamples);
}

void PuddleDSP::updateRandomInterval() {
    const float targetIntervalMs =
        kMaxRateIntervalMs - (m_smoothed.rate * (kMaxRateIntervalMs - kMinRateIntervalMs));
    const float intervalSamples = targetIntervalMs * m_config.sampleRate / 1000.0f;
    m_randomCV.updateInterval = std::max<uint32_t>(1U, roundToUInt(intervalSamples));
    if (m_randomCV.samplesUntilUpdate > m_randomCV.updateInterval) {
        m_randomCV.samplesUntilUpdate = m_randomCV.updateInterval;
    }
}

void PuddleDSP::updateRandomSlew() {
    const float slewTimeMs =
        kMinSlewTimeMs + (m_smoothed.damp * (kMaxSlewTimeMs - kMinSlewTimeMs));
    const float slewSamples = std::max(1.0f, slewTimeMs * m_config.sampleRate / 1000.0f);
    m_randomCV.slewRate = 1.0f - expApproxNegative(-1.0f / slewSamples);
}

void PuddleDSP::updateEnvelopeCoefficients() {
    const float attackSamples = std::max(1.0f, kAttackTimeMs * m_config.sampleRate / 1000.0f);
    const float releaseSamples = std::max(1.0f, kReleaseTimeMs * m_config.sampleRate / 1000.0f);
    m_envFollower.attackCoeff = 1.0f - expApproxNegative(-1.0f / attackSamples);
    m_envFollower.releaseCoeff = 1.0f - expApproxNegative(-1.0f / releaseSamples);
}

void PuddleDSP::syncSmoothedParameters() {
    m_smoothed.rate = m_config.rate;
    m_smoothed.damp = m_config.damp;
    m_smoothed.depth = m_config.depth;
    m_smoothed.lpg = m_config.lpg;
    m_smoothed.feedback = m_config.feedback;
    m_smoothed.mix = m_config.mix;
    m_smoothed.volume = m_config.volume;
}

void PuddleDSP::smoothParameters() {
    const bool rateChanged = smoothToward(m_smoothed.rate, m_config.rate, m_smoothed.coeff);
    const bool dampChanged = smoothToward(m_smoothed.damp, m_config.damp, m_smoothed.coeff);

    smoothToward(m_smoothed.depth, m_config.depth, m_smoothed.coeff);
    smoothToward(m_smoothed.lpg, m_config.lpg, m_smoothed.coeff);
    smoothToward(m_smoothed.feedback, m_config.feedback, m_smoothed.coeff);
    smoothToward(m_smoothed.mix, m_config.mix, m_smoothed.coeff);
    smoothToward(m_smoothed.volume, m_config.volume, m_smoothed.coeff);

    if (rateChanged) {
        updateRandomInterval();
    }
    if (dampChanged) {
        updateRandomSlew();
    }
}

void PuddleDSP::clearDelayBuffer() {
    if (m_delay.buffer == nullptr || m_delay.size == 0U) {
        return;
    }

    for (uint32_t i = 0; i < m_delay.size; ++i) {
        m_delay.buffer[i] = 0.0f;
    }
}

float PuddleDSP::readDelay(float modulation) const {
    if (!m_initialized || m_delay.buffer == nullptr || m_delay.size == 0U) {
        return 0.0f;
    }

    const float baseDelaySamples = m_delay.baseDelayMs * m_config.sampleRate / 1000.0f;
    const float modulationSamples =
        modulation * m_smoothed.depth * m_delay.maxModulationMs * m_config.sampleRate / 1000.0f;

    const float totalDelay =
        clamp(baseDelaySamples + modulationSamples,
              kMinDelayMs * m_config.sampleRate / 1000.0f,
              kMaxDelayMs * m_config.sampleRate / 1000.0f);

    const uint32_t delayInt = static_cast<uint32_t>(totalDelay);
    const float frac = totalDelay - static_cast<float>(delayInt);

    const uint32_t newerIndex =
        (m_delay.writePos + m_delay.size - delayInt) % m_delay.size;
    const uint32_t olderIndex =
        (newerIndex + m_delay.size - 1U) % m_delay.size;

    const float newerSample = m_delay.buffer[newerIndex];
    const float olderSample = m_delay.buffer[olderIndex];
    return (newerSample * (1.0f - frac)) + (olderSample * frac);
}

void PuddleDSP::writeDelay(float sample) {
    if (!m_initialized || m_delay.buffer == nullptr || m_delay.size == 0U) {
        return;
    }

    m_delay.buffer[m_delay.writePos] = sample;
    m_delay.writePos = (m_delay.writePos + 1U) % m_delay.size;
}

float PuddleDSP::nextRandomSigned() {
    m_randomCV.state = (m_randomCV.state * 1664525U) + 1013904223U;
    return (static_cast<float>(m_randomCV.state) * kUint32ToUnit * 2.0f) - 1.0f;
}

float PuddleDSP::generateRandomCV() {
    if (!m_initialized) {
        return 0.0f;
    }

    if (m_randomCV.samplesUntilUpdate == 0U) {
        m_randomCV.targetValue = nextRandomSigned();
        m_randomCV.samplesUntilUpdate = m_randomCV.updateInterval;
    } else {
        --m_randomCV.samplesUntilUpdate;
    }

    m_randomCV.currentValue +=
        (m_randomCV.targetValue - m_randomCV.currentValue) * m_randomCV.slewRate;

    return m_randomCV.currentValue;
}

float PuddleDSP::trackEnvelope(float inputSample) {
    if (!m_initialized) {
        return 0.0f;
    }

    const float absInput = std::fabs(inputSample);
    const float coeff = (absInput > m_envFollower.level)
        ? m_envFollower.attackCoeff
        : m_envFollower.releaseCoeff;

    m_envFollower.level += (absInput - m_envFollower.level) * coeff;
    return m_envFollower.level;
}

float PuddleDSP::filterSample(float input, float envLevel) {
    if (!m_initialized) {
        return input;
    }

    const float nyquistLimitedMaxCutoff = std::min(20000.0f, m_config.sampleRate * 0.45f);
    const float lpgAmount = m_smoothed.lpg;
    const float clampedEnv = clamp(envLevel, 0.0f, 1.0f);
    const float shapedEnv = (2.0f * clampedEnv) - (clampedEnv * clampedEnv);
    const float baseCutoffHz = 8000.0f - (6800.0f * lpgAmount);
    const float cutoffHz = clamp(
        baseCutoffHz + (shapedEnv * lpgAmount * 18000.0f),
        350.0f,
        nyquistLimitedMaxCutoff);

    const float coeff = 1.0f - expApproxNegative((-kTwoPi * cutoffHz) / m_config.sampleRate);
    m_filter.state += coeff * (input - m_filter.state);

    // High LPG settings also let the wet level sag between transients, which makes the
    // bright attack / murky tail contrast much easier to hear.
    const float lpgGain = 1.0f - (lpgAmount * 0.55f * (1.0f - shapedEnv));
    return m_filter.state * lpgGain;
}

float PuddleDSP::blockDC(float input) {
    const float y = input - m_dcBlocker.x1 + kDCBlockerR * m_dcBlocker.y1;
    m_dcBlocker.x1 = input;
    m_dcBlocker.y1 = y;
    return y;
}

float PuddleDSP::softClip(float input) {
    if (input > 1.0f) {
        return 1.0f - (1.0f / (1.0f + (input - 1.0f)));
    }
    if (input < -1.0f) {
        return -1.0f + (1.0f / (1.0f - (input + 1.0f)));
    }
    return input;
}

void PuddleDSP::process(const float* input, float* output, uint32_t numSamples) {
    if (input == nullptr || output == nullptr) {
        return;
    }

    if (!m_initialized) {
        for (uint32_t i = 0; i < numSamples; ++i) {
            output[i] = input[i];
        }
        return;
    }

    for (uint32_t i = 0; i < numSamples; ++i) {
        smoothParameters();

        const float inSample = input[i];
        const float envLevel = trackEnvelope(inSample);
        const float modCV = generateRandomCV();
        const float delayedSample = readDelay(modCV);
        const float filteredSample = filterSample(delayedSample, envLevel);

        const float fbSignal = blockDC(softClip(filteredSample * m_smoothed.feedback));
        writeDelay(inSample + fbSignal);

        const float mixAngle = m_smoothed.mix * kPiOver2;
        const float sinMix = std::sin(mixAngle);
        const float cosMix = std::cos(mixAngle);
        const float mixedSample = (inSample * cosMix) + (filteredSample * sinMix);
        output[i] = mixedSample * m_smoothed.volume;
    }
}

void PuddleDSP::reset() {
    if (!m_initialized) {
        return;
    }

    clearDelayBuffer();
    m_delay.writePos = 0;

    m_randomCV.state = m_config.randomSeed;
    m_randomCV.currentValue = 0.0f;
    m_randomCV.targetValue = 0.0f;
    m_randomCV.samplesUntilUpdate = 0;

    m_envFollower.level = 0.0f;
    m_filter.state = 0.0f;
    m_dcBlocker.x1 = 0.0f;
    m_dcBlocker.y1 = 0.0f;
    syncSmoothedParameters();
    updateRandomInterval();
    updateRandomSlew();
}

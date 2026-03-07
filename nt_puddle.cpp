#include <algorithm>
#include <cstddef>
#include <distingnt/api.h>

#include "puddle_dsp.h"

#include <new>

enum {
    kParamInput,
    kParamOutput,
    kParamOutputMode,
    kParamRate,
    kParamDamp,
    kParamDepth,
    kParamLpg,
    kParamMix,
    kParamVolume,
    kNumParams
};

namespace {
constexpr float kDefaultSampleRateHz = 48000.0f;
constexpr float kMaxSupportedSampleRateHz = 96000.0f;
constexpr int kMinPercentHundredths = 0;
constexpr int kDefaultPercentHundredths = 5000;
constexpr int kDefaultMixPercentHundredths = 10000;
constexpr int kMaxPercentHundredths = 10000;
constexpr int kMinVolumeDbHundredths = -6000;
constexpr int kDefaultVolumeDbHundredths = -1000;
constexpr int kMaxVolumeDbHundredths = 2400;
constexpr float kLn10Over20 = 0.11512925464970228f;
constexpr float kLn2 = 0.6931471805599453f;

float effectiveSampleRate() {
    const float raw = (NT_globals.sampleRate > 0U)
        ? static_cast<float>(NT_globals.sampleRate)
        : kDefaultSampleRateHz;
    return std::min(raw, kMaxSupportedSampleRateHz);
}

uint32_t requiredDelayBytes(float sampleRate) {
    return PuddleDSP::requiredDelayBufferSamples(sampleRate) * sizeof(float);
}

float expApprox(float value) {
    const float scaled = value / kLn2;
    int exponent = static_cast<int>(scaled + ((scaled >= 0.0f) ? 0.5f : -0.5f));
    const float remainder = value - (static_cast<float>(exponent) * kLn2);
    const float r2 = remainder * remainder;
    float result =
        1.0f + remainder + (0.5f * r2) + ((r2 * remainder) * (1.0f / 6.0f)) +
        ((r2 * r2) * (1.0f / 24.0f)) + ((r2 * r2 * remainder) * (1.0f / 120.0f));

    while (exponent > 0) {
        result *= 2.0f;
        --exponent;
    }
    while (exponent < 0) {
        result *= 0.5f;
        ++exponent;
    }
    return result;
}

float percentHundredthsToUnit(int valueHundredths) {
    return static_cast<float>(valueHundredths) * 0.0001f;
}

float volumeDbHundredthsToLinear(int valueHundredthsDb) {
    return expApprox(static_cast<float>(valueHundredthsDb) * 0.01f * kLn10Over20);
}

void formatHundredths(char* buffer, int valueHundredths) {
    char* out = buffer;
    int value = valueHundredths;
    if (value < 0) {
        *out++ = '-';
        value = -value;
    }

    int whole = value / 100;
    const int frac = value % 100;
    char digits[8];
    int numDigits = 0;
    do {
        digits[numDigits++] = static_cast<char>('0' + (whole % 10));
        whole /= 10;
    } while (whole > 0);

    while (numDigits > 0) {
        *out++ = digits[--numDigits];
    }

    *out++ = '.';
    *out++ = static_cast<char>('0' + (frac / 10));
    *out++ = static_cast<char>('0' + (frac % 10));
    *out = '\0';
}
}

static const _NT_parameter parameters[] = {
    NT_PARAMETER_AUDIO_INPUT("Input", 1, 1)
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE("Output", 1, 13)

    { .name = "RATE", .min = kMinPercentHundredths, .max = kMaxPercentHundredths,
      .def = kDefaultPercentHundredths, .unit = kNT_unitPercent, .scaling = kNT_scaling100 },
    { .name = "DAMP", .min = kMinPercentHundredths, .max = kMaxPercentHundredths,
      .def = kDefaultPercentHundredths, .unit = kNT_unitPercent, .scaling = kNT_scaling100 },
    { .name = "DEPTH", .min = kMinPercentHundredths, .max = kMaxPercentHundredths,
      .def = kDefaultPercentHundredths, .unit = kNT_unitPercent, .scaling = kNT_scaling100 },
    { .name = "LPG", .min = kMinPercentHundredths, .max = kMaxPercentHundredths,
      .def = kDefaultPercentHundredths, .unit = kNT_unitPercent, .scaling = kNT_scaling100 },
    { .name = "MIX", .min = kMinPercentHundredths, .max = kMaxPercentHundredths,
      .def = kDefaultMixPercentHundredths, .unit = kNT_unitPercent, .scaling = kNT_scaling100 },
    { .name = "VOLUME", .min = kMinVolumeDbHundredths, .max = kMaxVolumeDbHundredths,
      .def = kDefaultVolumeDbHundredths, .unit = kNT_unitDb, .scaling = kNT_scaling100 },
};

static const uint8_t pageMain[] = {
    kParamRate,
    kParamDamp,
    kParamDepth,
    kParamLpg,
    kParamMix,
    kParamVolume,
};

static const uint8_t pageRouting[] = {
    kParamInput,
    kParamOutput,
    kParamOutputMode,
};

static const _NT_parameterPage pages[] = {
    { .name = "Main", .numParams = ARRAY_SIZE(pageMain), .params = pageMain },
    { .name = "Routing", .numParams = ARRAY_SIZE(pageRouting), .params = pageRouting },
};

static const _NT_parameterPages parameterPages = {
    .numPages = ARRAY_SIZE(pages),
    .pages = pages,
};

struct _puddleAlgorithmDTC {
    PuddleDSP dsp;
};

struct _puddleAlgorithm : public _NT_algorithm {
    _puddleAlgorithmDTC* dtc;
};

static void accumulateProcessed(float* output,
                                const float* input,
                                uint32_t numFrames,
                                PuddleDSP& dsp);

void calculateRequirements(_NT_algorithmRequirements& req, const int32_t* specifications) {
    (void)specifications;

    req.numParameters = ARRAY_SIZE(parameters);
    req.sram = sizeof(_puddleAlgorithm);
    req.dram = requiredDelayBytes(effectiveSampleRate());
    req.dtc = sizeof(_puddleAlgorithmDTC);
    req.itc = 0;
}

_NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs,
                         const _NT_algorithmRequirements& req,
                         const int32_t* specifications) {
    (void)req;
    (void)specifications;

    _puddleAlgorithm* alg = new (ptrs.sram) _puddleAlgorithm();
    alg->dtc = new (ptrs.dtc) _puddleAlgorithmDTC();
    alg->parameters = parameters;
    alg->parameterPages = &parameterPages;

    const float sampleRate = effectiveSampleRate();

    PuddleDSP::Config config;
    config.sampleRate = sampleRate;
    config.rate = 0.5f;
    config.damp = 0.5f;
    config.depth = 0.5f;
    config.lpg = 0.5f;
    config.mix = 1.0f;
    config.volume = volumeDbHundredthsToLinear(kDefaultVolumeDbHundredths);
    config.randomSeed = 0x5044444CU ^ static_cast<uint32_t>(sampleRate);

    const uint32_t delaySamples = PuddleDSP::requiredDelayBufferSamples(sampleRate);
    alg->dtc->dsp.initialize(config, reinterpret_cast<float*>(ptrs.dram), delaySamples);
    return alg;
}

void parameterChanged(_NT_algorithm* self, int p) {
    _puddleAlgorithm* pThis = static_cast<_puddleAlgorithm*>(self);

    switch (p) {
    case kParamRate:
        pThis->dtc->dsp.setRate(percentHundredthsToUnit(pThis->v[kParamRate]));
        break;
    case kParamDamp:
        pThis->dtc->dsp.setDamp(percentHundredthsToUnit(pThis->v[kParamDamp]));
        break;
    case kParamDepth:
        pThis->dtc->dsp.setDepth(percentHundredthsToUnit(pThis->v[kParamDepth]));
        break;
    case kParamLpg:
        pThis->dtc->dsp.setLpg(percentHundredthsToUnit(pThis->v[kParamLpg]));
        break;
    case kParamMix:
        pThis->dtc->dsp.setMix(percentHundredthsToUnit(pThis->v[kParamMix]));
        break;
    case kParamVolume:
        pThis->dtc->dsp.setVolume(volumeDbHundredthsToLinear(pThis->v[kParamVolume]));
        break;
    default:
        break;
    }
}

void step(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
    _puddleAlgorithm* pThis = static_cast<_puddleAlgorithm*>(self);
    const uint32_t numFrames = static_cast<uint32_t>(numFramesBy4 * 4);
    if (numFrames == 0U) {
        return;
    }

    const float* input = busFrames + ((pThis->v[kParamInput] - 1) * numFrames);
    float* output = busFrames + ((pThis->v[kParamOutput] - 1) * numFrames);
    const bool replaceMode = pThis->v[kParamOutputMode] != 0;

    if (replaceMode) {
        pThis->dtc->dsp.process(input, output, numFrames);
        return;
    }

    accumulateProcessed(output, input, numFrames, pThis->dtc->dsp);
}

bool draw(_NT_algorithm* self) {
    _puddleAlgorithm* pThis = static_cast<_puddleAlgorithm*>(self);

    NT_drawText(0, 8, "Puddle", 15, kNT_textLeft, kNT_textLarge);

    char buf[24];

    NT_drawText(0, 24, "RATE:");
    formatHundredths(buf, pThis->v[kParamRate]);
    NT_drawText(40, 24, buf);

    NT_drawText(128, 24, "DAMP:");
    formatHundredths(buf, pThis->v[kParamDamp]);
    NT_drawText(168, 24, buf);

    NT_drawText(0, 36, "DEPTH:");
    formatHundredths(buf, pThis->v[kParamDepth]);
    NT_drawText(48, 36, buf);

    NT_drawText(128, 36, "LPG:");
    formatHundredths(buf, pThis->v[kParamLpg]);
    NT_drawText(160, 36, buf);

    NT_drawText(0, 48, "MIX:");
    formatHundredths(buf, pThis->v[kParamMix]);
    NT_drawText(32, 48, buf);

    NT_drawText(128, 48, "VOL:");
    formatHundredths(buf, pThis->v[kParamVolume]);
    NT_drawText(160, 48, buf);

    return false;
}

static void accumulateProcessed(float* output,
                                const float* input,
                                uint32_t numFrames,
                                PuddleDSP& dsp) {
    float localScratch[64];
    float* scratch = localScratch;
    uint32_t scratchCapacity = ARRAY_SIZE(localScratch);

    if (NT_globals.workBuffer != nullptr) {
        const uint32_t workFrames =
            NT_globals.workBufferSizeBytes / static_cast<uint32_t>(sizeof(float));
        if (workFrames > 0U) {
            scratch = NT_globals.workBuffer;
            scratchCapacity = workFrames;
        }
    }

    uint32_t offset = 0U;
    while (offset < numFrames) {
        const uint32_t framesThisPass = std::min(scratchCapacity, numFrames - offset);
        dsp.process(input + offset, scratch, framesThisPass);
        for (uint32_t i = 0; i < framesThisPass; ++i) {
            output[offset + i] += scratch[i];
        }
        offset += framesThisPass;
    }
}

static const _NT_factory factory = {
    .guid = NT_MULTICHAR('P', 'D', 'D', 'L'),
    .name = "Puddle",
    .description = "K-Field modulator emulation",
    .calculateRequirements = calculateRequirements,
    .construct = construct,
    .parameterChanged = parameterChanged,
    .step = step,
    .draw = draw,
    .tags = kNT_tagEffect | kNT_tagDelay,
};

extern "C" uintptr_t pluginEntry(_NT_selector selector, uint32_t data) {
    switch (selector) {
    case kNT_selector_version:
        return kNT_apiVersionCurrent;
    case kNT_selector_numFactories:
        return 1;
    case kNT_selector_factoryInfo:
        return reinterpret_cast<uintptr_t>((data == 0U) ? &factory : nullptr);
    default:
        return 0;
    }
}

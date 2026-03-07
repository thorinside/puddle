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

static const _NT_parameter parameters[] = {
    NT_PARAMETER_AUDIO_INPUT("Input", 1, 1)
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE("Output", 1, 13)

    { .name = "RATE", .min = 0, .max = 100, .def = 50, .unit = kNT_unitPercent },
    { .name = "DAMP", .min = 0, .max = 100, .def = 50, .unit = kNT_unitPercent },
    { .name = "DEPTH", .min = 0, .max = 100, .def = 50, .unit = kNT_unitPercent },
    { .name = "LPG", .min = 0, .max = 100, .def = 50, .unit = kNT_unitPercent },
    { .name = "MIX", .min = 0, .max = 100, .def = 50, .unit = kNT_unitPercent },
    { .name = "VOLUME", .min = 0, .max = 200, .def = 100, .unit = kNT_unitPercent },
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

struct _puddleAlgorithm : public _NT_algorithm {
    PuddleDSP dsp;
};

void calculateRequirements(_NT_algorithmRequirements& req, const int32_t* specifications) {
    (void)specifications;

    const float sampleRate = (NT_globals.sampleRate > 0U)
        ? static_cast<float>(NT_globals.sampleRate)
        : 48000.0f;

    req.numParameters = ARRAY_SIZE(parameters);
    req.sram = sizeof(_puddleAlgorithm);
    req.dram = PuddleDSP::requiredDelayBufferSamples(sampleRate) * sizeof(float);
    req.dtc = 0;
    req.itc = 0;
}

_NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs,
                         const _NT_algorithmRequirements& req,
                         const int32_t* specifications) {
    (void)req;
    (void)specifications;

    _puddleAlgorithm* alg = new (ptrs.sram) _puddleAlgorithm();
    alg->parameters = parameters;
    alg->parameterPages = &parameterPages;

    const float sampleRate = (NT_globals.sampleRate > 0U)
        ? static_cast<float>(NT_globals.sampleRate)
        : 48000.0f;

    PuddleDSP::Config config;
    config.sampleRate = sampleRate;
    config.rate = 0.5f;
    config.damp = 0.5f;
    config.depth = 0.5f;
    config.lpg = 0.5f;
    config.mix = 0.5f;
    config.volume = 1.0f;
    config.randomSeed = 0x5044444CU ^ static_cast<uint32_t>(sampleRate);

    const uint32_t delaySamples = PuddleDSP::requiredDelayBufferSamples(sampleRate);
    alg->dsp.initialize(config, reinterpret_cast<float*>(ptrs.dram), delaySamples);
    return alg;
}

void parameterChanged(_NT_algorithm* self, int p) {
    _puddleAlgorithm* pThis = static_cast<_puddleAlgorithm*>(self);

    switch (p) {
    case kParamRate:
        pThis->dsp.setRate(pThis->v[kParamRate] / 100.0f);
        break;
    case kParamDamp:
        pThis->dsp.setDamp(pThis->v[kParamDamp] / 100.0f);
        break;
    case kParamDepth:
        pThis->dsp.setDepth(pThis->v[kParamDepth] / 100.0f);
        break;
    case kParamLpg:
        pThis->dsp.setLpg(pThis->v[kParamLpg] / 100.0f);
        break;
    case kParamMix:
        pThis->dsp.setMix(pThis->v[kParamMix] / 100.0f);
        break;
    case kParamVolume:
        pThis->dsp.setVolume(pThis->v[kParamVolume] / 100.0f);
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
        pThis->dsp.process(input, output, numFrames);
        return;
    }

    for (uint32_t i = 0; i < numFrames; ++i) {
        float wet = 0.0f;
        pThis->dsp.process(input + i, &wet, 1U);
        output[i] += wet;
    }
}

bool draw(_NT_algorithm* self) {
    _puddleAlgorithm* pThis = static_cast<_puddleAlgorithm*>(self);

    NT_drawText(0, 8, "Puddle", 15, kNT_textLeft, kNT_textLarge);

    char buf[16];

    NT_drawText(0, 24, "RATE:");
    NT_intToString(buf, pThis->v[kParamRate]);
    NT_drawText(40, 24, buf);

    NT_drawText(128, 24, "DAMP:");
    NT_intToString(buf, pThis->v[kParamDamp]);
    NT_drawText(168, 24, buf);

    NT_drawText(0, 36, "DEPTH:");
    NT_intToString(buf, pThis->v[kParamDepth]);
    NT_drawText(48, 36, buf);

    NT_drawText(128, 36, "LPG:");
    NT_intToString(buf, pThis->v[kParamLpg]);
    NT_drawText(160, 36, buf);

    NT_drawText(0, 48, "MIX:");
    NT_intToString(buf, pThis->v[kParamMix]);
    NT_drawText(32, 48, buf);

    NT_drawText(128, 48, "VOL:");
    NT_intToString(buf, pThis->v[kParamVolume]);
    NT_drawText(160, 48, buf);

    return false;
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

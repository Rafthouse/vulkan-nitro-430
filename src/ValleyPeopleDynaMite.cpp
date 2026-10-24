/*
  ==============================================================================
    ValleyPeopleDynaMite.cpp
    Rebranded & Compact C++17 / JUCE 7 & 8 Plugin:
    "VULKAN AUDIO — NITRO-430" (Compact 740x295 1U Half-Rack VCA Dynamics Processor)

    Formats Supported: VST3, AU (AudioUnit), Standalone
    Architecture:
      1. ValleyPeople::DynaMiteDSP  — Hardware-accurate TA-101 VCA Dynamics Engine
      2. DynaMiteAudioProcessor     — JUCE AudioProcessor with APVTS & Sidechain Bus
      3. DynaMiteAudioProcessorEditor — Compact 740x295 English Skeuomorphic GUI:
         - Symmetric Dual-Knob Modules (zero collision between RANGE and OUTPUT)
         - Radially-anchored scale numbers around 7-lobe fluted Bakelite knobs
         - Clickable Brand/Model Header that opens an in-plugin "ABOUT / AUTHOR" overlay
  ==============================================================================
*/

#include <cmath>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

//==============================================================================
// CUSTOMIZABLE BRANDING & AUTHOR MACROS
//==============================================================================
#ifndef PLUGIN_BRAND_TITLE
  #define PLUGIN_BRAND_TITLE  "VULKAN AUDIO"
#endif
#ifndef PLUGIN_MODEL_TITLE
  #define PLUGIN_MODEL_TITLE  "NITRO-430"
#endif
#ifndef PLUGIN_FULL_NAME
  #define PLUGIN_FULL_NAME    "Vulkan Audio NITRO-430"
#endif
#ifndef PLUGIN_AUTHOR_NAME
  #define PLUGIN_AUTHOR_NAME  "Chief DSP Creator"
#endif
#ifndef PLUGIN_VERSION_STR
  #define PLUGIN_VERSION_STR  "v1.0.0 (VST3 / AU / Standalone)"
#endif

//==============================================================================
// 1. HARDWARE-ACCURATE DSP ENGINE (Zero external dependencies; pure C++17)
//==============================================================================
namespace ValleyPeople
{
    enum class DetSource : int { INT = 0, DS_FM = 1, EXT = 2 };
    enum class Mode : int      { EXP = 0, OUT   = 1, LIMIT = 2 };
    enum class DetType : int   { GATE = 0, PEAK = 1, AVG = 2 };

    struct Parameters
    {
        float thresholdDbv  = 0.0f;
        float releaseSec    = 0.25f;
        float rangeDb       = 60.0f;
        float outputDbv     = 0.0f;
        DetSource detSource = DetSource::INT;
        Mode mode           = Mode::LIMIT;
        DetType detType     = DetType::AVG;
        float dryWetMix     = 1.0f;
        bool stereoLink     = true;
    };

    struct BiquadFilter
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0;
        double a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;

        void reset() noexcept { z1 = 0.0; z2 = 0.0; }

        void setHighPass(double sampleRate, double freqHz, double q = 0.7071) noexcept
        {
            const double w0 = 2.0 * 3.14159265358979323846 * std::clamp(freqHz, 20.0, sampleRate * 0.45) / sampleRate;
            const double cosw = std::cos(w0);
            const double sinw = std::sin(w0);
            const double alpha = sinw / (2.0 * q);
            const double a0 = 1.0 + alpha;
            b0 = ((1.0 + cosw) * 0.5) / a0;
            b1 = (-(1.0 + cosw)) / a0;
            b2 = ((1.0 + cosw) * 0.5) / a0;
            a1 = (-2.0 * cosw) / a0;
            a2 = (1.0 - alpha) / a0;
        }

        void setHighShelf(double sampleRate, double freqHz, double gainDb, double q = 0.7071) noexcept
        {
            const double A = std::pow(10.0, gainDb / 40.0);
            const double w0 = 2.0 * 3.14159265358979323846 * std::clamp(freqHz, 100.0, sampleRate * 0.45) / sampleRate;
            const double cosw = std::cos(w0);
            const double sinw = std::sin(w0);
            const double alpha = sinw / (2.0 * q);
            const double twoSqrtAAlpha = 2.0 * std::sqrt(A) * alpha;

            const double a0 = (A + 1.0) - (A - 1.0) * cosw + twoSqrtAAlpha;
            b0 = (A * ((A + 1.0) + (A - 1.0) * cosw + twoSqrtAAlpha)) / a0;
            b1 = (-2.0 * A * ((A - 1.0) + (A + 1.0) * cosw)) / a0;
            b2 = (A * ((A + 1.0) + (A - 1.0) * cosw - twoSqrtAAlpha)) / a0;
            a1 = (2.0 * ((A - 1.0) - (A + 1.0) * cosw)) / a0;
            a2 = ((A + 1.0) - (A - 1.0) * cosw - twoSqrtAAlpha) / a0;
        }

        inline float process(float x) noexcept
        {
            const double in = static_cast<double>(x);
            const double out = b0 * in + z1;
            z1 = b1 * in - a1 * out + z2;
            z2 = b2 * in - a2 * out;
            return static_cast<float>(out);
        }
    };

    struct ChannelDetector
    {
        BiquadFilter dsfmHighPass;
        BiquadFilter dsfmHighShelf;

        float linearIntegratorEnv = 0.0f;
        float fastPeakTracker     = 0.0f;
        float fastGrDb            = 0.0f;
        float slowGrDb            = 0.0f;
        float smoothedGrDb        = 0.0f;
        float holdCounterSamples  = 0.0f;

        void prepare(double sampleRate) noexcept
        {
            dsfmHighPass.setHighPass(sampleRate, 750.0, 0.7071);
            dsfmHighShelf.setHighShelf(sampleRate, 2400.0, 12.5, 0.7071);
            reset();
        }

        void reset() noexcept
        {
            dsfmHighPass.reset();
            dsfmHighShelf.reset();
            linearIntegratorEnv = 0.0f;
            fastPeakTracker     = 0.0f;
            fastGrDb            = 0.0f;
            slowGrDb            = 0.0f;
            smoothedGrDb        = 0.0f;
            holdCounterSamples  = 0.0f;
        }
    };

    class DynaMiteDSP
    {
    public:
        DynaMiteDSP() { prepare(48000.0); }

        void prepare(double newSampleRate) noexcept
        {
            sampleRate = std::max(8000.0, newSampleRate);
            for (auto& ch : channels)
                ch.prepare(sampleRate);
            updateBallisticsCoefficients();
        }

        void reset() noexcept
        {
            for (auto& ch : channels)
                ch.reset();
            latestGainReductionDb.store(0.0f, std::memory_order_relaxed);
            latestOutputClip.store(false, std::memory_order_relaxed);
            latestInputPeakDb.store(-100.0f, std::memory_order_relaxed);
            latestOutputPeakDb.store(-100.0f, std::memory_order_relaxed);
        }

        void setParameters(const Parameters& newParams) noexcept
        {
            params = newParams;
            updateBallisticsCoefficients();
        }

        const Parameters& getParameters() const noexcept { return params; }

        static inline float computeTargetGainReductionDb(
            float detectorDbFs,
            const Parameters& p) noexcept
        {
            if (p.mode == Mode::OUT)
                return 0.0f;

            const float thresholdDbFs = std::clamp(p.thresholdDbv - 20.0f, -60.0f, 0.0f);
            const float maxRangeDb    = std::clamp(p.rangeDb, 0.0f, 60.0f);

            if (maxRangeDb <= 0.0001f)
                return 0.0f;

            float targetGrDb = 0.0f;

            if (p.mode == Mode::LIMIT)
            {
                const float overshootDb = detectorDbFs - thresholdDbFs;
                if (overshootDb <= 0.0f)
                    return 0.0f;

                if (p.detSource == DetSource::EXT)
                {
                    targetGrDb = (p.detType == DetType::GATE)
                               ? std::min(maxRangeDb, overshootDb * 20.0f)
                               : std::min(maxRangeDb, overshootDb * 1.0f);
                }
                else
                {
                    targetGrDb = (p.detType == DetType::GATE)
                               ? std::min(maxRangeDb, overshootDb * 21.0f)
                               : std::min(maxRangeDb, overshootDb);
                }
            }
            else if (p.mode == Mode::EXP)
            {
                if (p.detSource == DetSource::EXT)
                {
                    const float overshootDb = detectorDbFs - thresholdDbFs;
                    if (overshootDb <= 0.0f)
                        targetGrDb = maxRangeDb;
                    else if (p.detType == DetType::GATE)
                        targetGrDb = std::max(0.0f, maxRangeDb - overshootDb * 20.0f);
                    else
                        targetGrDb = std::max(0.0f, maxRangeDb - overshootDb * 1.0f);
                }
                else
                {
                    const float undershootDb = thresholdDbFs - detectorDbFs;
                    if (undershootDb <= 0.0f)
                        return 0.0f;

                    targetGrDb = (p.detType == DetType::GATE)
                               ? std::min(maxRangeDb, undershootDb * 19.0f)
                               : std::min(maxRangeDb, undershootDb * 1.0f);
                }
            }

            return std::clamp(targetGrDb, 0.0f, maxRangeDb);
        }

        void processBlock(float* leftMain, float* rightMain,
                          const float* leftSc, const float* rightSc,
                          int numSamples, int numMainChannels) noexcept
        {
            const bool hasRight = (numMainChannels > 1 && rightMain != nullptr);
            const float dryWet  = std::clamp(params.dryWetMix, 0.0f, 1.0f);

            const float thresholdDbFs = std::clamp(params.thresholdDbv - 20.0f, -60.0f, 0.0f);
            float autoMakeUpDb = 0.0f;
            if (params.mode == Mode::LIMIT && params.detSource != DetSource::EXT)
            {
                autoMakeUpDb = std::min(-thresholdDbFs * 0.65f, params.rangeDb * 0.75f);
            }

            const float totalOutputGainDb = params.outputDbv + autoMakeUpDb;
            const float outputGainLinear  = dbToLinear(totalOutputGainDb);

            float maxBlockGrDb   = 0.0f;
            float maxInLinear    = 1.0e-6f;
            float maxOutLinear   = 1.0e-6f;
            bool  clippedInBlock = false;

            for (int i = 0; i < numSamples; ++i)
            {
                const float inL = leftMain[i];
                const float inR = hasRight ? rightMain[i] : inL;

                maxInLinear = std::max(maxInLinear, std::max(std::fabs(inL), std::fabs(inR)));

                float detInL = inL;
                float detInR = inR;

                if (params.detSource == DetSource::EXT)
                {
                    detInL = (leftSc != nullptr)  ? leftSc[i]  : inL;
                    detInR = (rightSc != nullptr) ? rightSc[i] : detInL;
                }
                else if (params.detSource == DetSource::DS_FM)
                {
                    detInL = channels[0].dsfmHighShelf.process(channels[0].dsfmHighPass.process(inL));
                    detInR = hasRight
                           ? channels[1].dsfmHighShelf.process(channels[1].dsfmHighPass.process(inR))
                           : detInL;
                }

                float grDbL = processChannelDetector(channels[0], std::fabs(detInL));
                float grDbR = hasRight ? processChannelDetector(channels[1], std::fabs(detInR)) : grDbL;

                if (params.stereoLink && hasRight)
                {
                    const float linkedGrDb = std::max(grDbL, grDbR);
                    grDbL = linkedGrDb;
                    grDbR = linkedGrDb;
                }

                maxBlockGrDb = std::max(maxBlockGrDb, std::max(grDbL, grDbR));

                const float vcaGainL = (params.mode == Mode::OUT) ? 1.0f : dbToLinear(-grDbL);
                const float vcaGainR = (params.mode == Mode::OUT) ? 1.0f : dbToLinear(-grDbR);

                float wetL = processTA101VcaAndOutputStage(inL * vcaGainL * outputGainLinear, grDbL, clippedInBlock);
                float wetR = hasRight
                           ? processTA101VcaAndOutputStage(inR * vcaGainR * outputGainLinear, grDbR, clippedInBlock)
                           : wetL;

                const float outL = inL * (1.0f - dryWet) + wetL * dryWet;
                const float outR = inR * (1.0f - dryWet) + wetR * dryWet;

                leftMain[i] = outL;
                if (hasRight)
                    rightMain[i] = outR;

                maxOutLinear = std::max(maxOutLinear, std::max(std::fabs(outL), std::fabs(outR)));
            }

            latestGainReductionDb.store(maxBlockGrDb, std::memory_order_relaxed);
            if (clippedInBlock)
                latestOutputClip.store(true, std::memory_order_relaxed);
            latestInputPeakDb.store(linearToDb(maxInLinear), std::memory_order_relaxed);
            latestOutputPeakDb.store(linearToDb(maxOutLinear), std::memory_order_relaxed);
        }

        float getGainReductionDb() const noexcept { return latestGainReductionDb.load(std::memory_order_relaxed); }
        float getInputPeakDb()     const noexcept { return latestInputPeakDb.load(std::memory_order_relaxed); }
        float getOutputPeakDb()    const noexcept { return latestOutputPeakDb.load(std::memory_order_relaxed); }

        bool getAndClearOutputClip() noexcept
        {
            return latestOutputClip.exchange(false, std::memory_order_relaxed);
        }

        static std::string getInteractiveModeDescription(const Parameters& p)
        {
            if (p.mode == Mode::OUT)
                return "OUT (BYPASS): VCA bypassed. Output knob and analog output clipping stage remain active.";

            if (p.mode == Mode::LIMIT)
            {
                if (p.detSource == DetSource::INT)
                {
                    if (p.detType == DetType::AVG)
                        return "LEVEL LIMITING (INT / LIMIT / AVG): Inf:1 with Linear Integration (1-15 ms attack) & Auto Make-Up.";
                    if (p.detType == DetType::PEAK)
                        return "PEAK LIMITING (INT / LIMIT / PEAK): Fast 50 us attack Inf:1 limiter with Auto Make-Up & ARC release.";
                    return "WEIRD NEGATIVE LIMITING (INT / LIMIT / GATE): 1:-20 negative ratio! 1 dB over Threshold drops output by 20 dB.";
                }
                if (p.detSource == DetSource::DS_FM)
                {
                    if (p.detType == DetType::AVG)
                        return "VOCAL DE-ESSING (DS-FM / LIMIT / AVG): Inf:1 HF pre-emphasis limiter. Ignores 't', clamps 'sss'.";
                    if (p.detType == DetType::PEAK)
                        return "HF PEAK LIMITING (DS-FM / LIMIT / PEAK): Fast 50 us HF limiter for cymbals and hi-hats.";
                    return "MODIFIED NEGATIVE LIMITING (DS-FM / LIMIT / GATE): 1:-20 negative ratio triggered by HF.";
                }
                if (p.detType == DetType::AVG)
                    return "INVERSE ENVELOPE FOLLOWER (EXT / LIMIT / AVG): Smooth 1:1 sidechain ducking bounded by Range.";
                if (p.detType == DetType::PEAK)
                    return "FAST INVERSE ENVELOPE FOLLOWER (EXT / LIMIT / PEAK): Fast 50 us 1:1 sidechain ducking.";
                return "HARD DUCKING (EXT / LIMIT / GATE): Extreme 20:1 sidechain ducking bounded by Range.";
            }

            if (p.detSource == DetSource::EXT)
            {
                if (p.detType == DetType::AVG)
                    return "SOFT KEYING (EXT / EXP / AVG): Envelope-following 1:1 sidechain keyer.";
                if (p.detType == DetType::PEAK)
                    return "FAST SOFT KEYING (EXT / EXP / PEAK): Fast 50 us 1:1 sidechain keyer.";
                return "HARD KEYING (EXT / EXP / GATE): Tight 20:1 sidechain gate with 50 us attack.";
            }

            const std::string srcLabel = (p.detSource == DetSource::DS_FM) ? "DS-FM" : "INT";
            if (p.detType == DetType::AVG)
                return "SOFT NOISE GATING (" + srcLabel + " / EXP / AVG): Smooth 1:2 downward expander with ARC release.";
            if (p.detType == DetType::PEAK)
                return "FAST EXPANDER (" + srcLabel + " / EXP / PEAK): 1:2 downward expander (50 us attack) for drums.";
            return "HARD NOISE GATE (" + srcLabel + " / EXP / GATE): Aggressive 1:20 noise gate (50 us attack) with ARC release.";
        }

        static inline float dbToLinear(float db) noexcept { return std::pow(10.0f, db * 0.05f); }
        static inline float linearToDb(float lin) noexcept { return 20.0f * std::log10(std::max(lin, 1.0e-6f)); }

    private:
        double sampleRate = 48000.0;
        Parameters params;
        std::array<ChannelDetector, 2> channels;

        float peakAttackCoeff      = 0.0f;
        float avgMinAttackCoeff    = 0.0f;
        float avgMaxAttackCoeff    = 0.0f;
        float fastReleaseCoeff     = 0.0f;
        float slowArcReleaseCoeff  = 0.0f;
        float holdPeriodSamples    = 0.0f;

        std::atomic<float> latestGainReductionDb { 0.0f };
        std::atomic<bool>  latestOutputClip      { false };
        std::atomic<float> latestInputPeakDb     { -100.0f };
        std::atomic<float> latestOutputPeakDb    { -100.0f };

        void updateBallisticsCoefficients() noexcept
        {
            const double tauPeakAtk = 0.000050 / 2.302585093;
            peakAttackCoeff = static_cast<float>(std::exp(-1.0 / (sampleRate * tauPeakAtk)));
            avgMinAttackCoeff = static_cast<float>(std::exp(-1.0 / (sampleRate * 0.0012)));
            avgMaxAttackCoeff = static_cast<float>(std::exp(-1.0 / (sampleRate * 0.0120)));

            const double relSec = std::clamp(static_cast<double>(params.releaseSec), 0.05, 5.0);
            const double tauRel = relSec / 2.302585093;
            fastReleaseCoeff    = static_cast<float>(std::exp(-1.0 / (sampleRate * tauRel)));
            slowArcReleaseCoeff = static_cast<float>(std::exp(-1.0 / (sampleRate * (tauRel * 3.8))));
            holdPeriodSamples   = static_cast<float>(sampleRate * 0.006);
        }

        inline float processChannelDetector(ChannelDetector& ch, float absInput) noexcept
        {
            float detectedLinear = absInput;
            if (params.detType == DetType::AVG)
            {
                ch.fastPeakTracker = std::max(absInput, ch.fastPeakTracker * 0.999f);
                const float crestRatio = (ch.linearIntegratorEnv > 1.0e-5f)
                                       ? std::clamp(ch.fastPeakTracker / (ch.linearIntegratorEnv + 1.0e-5f), 1.0f, 8.0f)
                                       : 4.0f;
                const float blend = (crestRatio - 1.0f) / 7.0f;
                const float atkCoeff = avgMinAttackCoeff + blend * (avgMaxAttackCoeff - avgMinAttackCoeff);

                if (absInput > ch.linearIntegratorEnv)
                    ch.linearIntegratorEnv = atkCoeff * ch.linearIntegratorEnv + (1.0f - atkCoeff) * absInput;
                else
                    ch.linearIntegratorEnv = fastReleaseCoeff * ch.linearIntegratorEnv + (1.0f - fastReleaseCoeff) * absInput;

                detectedLinear = ch.linearIntegratorEnv;
            }

            const float detectorDbFs = linearToDb(detectedLinear);
            const float rawTargetGrDb = computeTargetGainReductionDb(detectorDbFs, params);
            const float activeAtkCoeff = (params.detType == DetType::AVG) ? avgMinAttackCoeff : peakAttackCoeff;

            const bool isIncreasingAttenuation = (rawTargetGrDb > ch.smoothedGrDb);
            const bool isAttackPhase = (params.mode == Mode::EXP) ? (!isIncreasingAttenuation)
                                                                  : isIncreasingAttenuation;

            if (isAttackPhase)
            {
                ch.fastGrDb = activeAtkCoeff * ch.fastGrDb + (1.0f - activeAtkCoeff) * rawTargetGrDb;
                ch.slowGrDb = 0.9985f * ch.slowGrDb + 0.0015f * ch.fastGrDb;
                ch.smoothedGrDb = ch.fastGrDb;
                ch.holdCounterSamples = holdPeriodSamples;
            }
            else
            {
                if (ch.holdCounterSamples > 0.0f)
                {
                    ch.holdCounterSamples -= 1.0f;
                }
                else
                {
                    ch.fastGrDb = fastReleaseCoeff * ch.fastGrDb + (1.0f - fastReleaseCoeff) * rawTargetGrDb;
                    ch.slowGrDb = slowArcReleaseCoeff * ch.slowGrDb + (1.0f - slowArcReleaseCoeff) * rawTargetGrDb;

                    const float sustainWeight = std::clamp(ch.slowGrDb / 24.0f, 0.0f, 0.75f);
                    ch.smoothedGrDb = (1.0f - sustainWeight) * ch.fastGrDb + sustainWeight * ch.slowGrDb;
                }
            }

            return std::clamp(ch.smoothedGrDb, 0.0f, 60.0f);
        }

        inline float processTA101VcaAndOutputStage(float x, float grDb, bool& clippedFlag) noexcept
        {
            const float vcaDrive = 0.0008f + 0.00015f * std::min(grDb, 30.0f);
            const float x2 = x * x;
            float vcaOut = x + vcaDrive * (x * x2 - 0.15f * x2);

            const float absVal = std::fabs(vcaOut);
            if (absVal >= 0.985f)
                clippedFlag = true;

            if (absVal > 0.90f)
            {
                const float sign = (vcaOut >= 0.0f) ? 1.0f : -1.0f;
                const float excess = absVal - 0.90f;
                const float headroom = (vcaOut >= 0.0f) ? 0.16f : 0.14f;
                vcaOut = sign * (0.90f + headroom * std::tanh(excess / headroom));
            }

            return vcaOut;
        }
    };
} // namespace ValleyPeople


//==============================================================================
// 2. JUCE PLUGIN IMPLEMENTATION (VST3 / AU / Standalone)
//==============================================================================
#ifndef DYNAMITE_STANDALONE_TEST

#include <JuceHeader.h>

struct DynaMitePreset
{
    const char* name;
    float thresholdDbv;
    float releaseSec;
    float rangeDb;
    float outputDbv;
    int detSource;
    int mode;
    int detType;
    float dryWetMix;
    bool stereoLink;
};

static const std::array<DynaMitePreset, 7> kFactoryPresets = {{
    { "Default (Classic Level Limiter)",  0.0f,  0.25f, 60.0f,  0.0f, 0, 2, 2, 1.00f, true  },
    { "Drum Bus Slam (Parallel Crush)", -18.0f,  0.10f, 60.0f,  2.0f, 0, 2, 1, 0.55f, true  },
    { "Snare Crack (3-Zone Limiting)",  -14.0f,  0.08f, 18.0f,  3.5f, 0, 2, 2, 1.00f, false },
    { "Fast Tom Gate (Set & Forget)",   -16.0f,  0.18f, 45.0f,  0.0f, 0, 0, 1, 1.00f, false },
    { "Vocal De-Esser (DS-FM AVG)",     -10.0f,  0.09f, 16.0f,  0.0f, 1, 2, 2, 1.00f, true  },
    { "Weird Organ Limiter (-20:1)",    -12.0f,  0.40f, 22.0f,  1.5f, 0, 2, 0, 1.00f, true  },
    { "NITRO Output Overdrive",          20.0f,  0.25f, 60.0f, 12.5f, 0, 1, 2, 1.00f, true  }
}};

class DynaMiteAudioProcessor : public juce::AudioProcessor
{
public:
    DynaMiteAudioProcessor()
        : AudioProcessor(BusesProperties()
              .withInput ("Input",     juce::AudioChannelSet::stereo(), true)
              .withOutput("Output",    juce::AudioChannelSet::stereo(), true)
              .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
          apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
    {
    }

    ~DynaMiteAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int /*samplesPerBlock*/) override
    {
        dsp.prepare(sampleRate);
        syncParametersToDsp();
    }

    void releaseResources() override { dsp.reset(); }

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    {
        const auto& mainIn  = layouts.getMainInputChannelSet();
        const auto& mainOut = layouts.getMainOutputChannelSet();
        if (mainIn != mainOut) return false;
        if (mainIn != juce::AudioChannelSet::mono() && mainIn != juce::AudioChannelSet::stereo()) return false;
        const auto& scIn = layouts.getChannelSet(true, 1);
        if (!scIn.isDisabled() && scIn != juce::AudioChannelSet::mono() && scIn != juce::AudioChannelSet::stereo()) return false;
        return true;
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midiMessages*/) override
    {
        juce::ScopedNoDenormals noDenormals;
        auto mainBus = getBusBuffer(buffer, true, 0);
        const int numSamples  = mainBus.getNumSamples();
        const int numChannels = mainBus.getNumChannels();
        if (numSamples == 0 || numChannels == 0) return;

        syncParametersToDsp();

        float* leftMain  = mainBus.getWritePointer(0);
        float* rightMain = (numChannels > 1) ? mainBus.getWritePointer(1) : nullptr;
        const float* leftSc  = nullptr;
        const float* rightSc = nullptr;

        if (getBusCount(true) > 1 && getBus(true, 1)->isEnabled())
        {
            auto scBus = getBusBuffer(buffer, true, 1);
            if (scBus.getNumChannels() > 0 && scBus.getNumSamples() >= numSamples)
            {
                leftSc  = scBus.getReadPointer(0);
                rightSc = (scBus.getNumChannels() > 1) ? scBus.getReadPointer(1) : leftSc;
            }
        }

        dsp.processBlock(leftMain, rightMain, leftSc, rightSc, numSamples, numChannels);
    }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return PLUGIN_FULL_NAME; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return static_cast<int>(kFactoryPresets.size()); }
    int getCurrentProgram() override { return currentProgramIndex; }

    void setCurrentProgram(int index) override
    {
        if (index < 0 || index >= static_cast<int>(kFactoryPresets.size())) return;
        currentProgramIndex = index;
        const auto& p = kFactoryPresets[static_cast<size_t>(index)];

        auto setParam = [this](const char* id, float plainVal)
        {
            if (auto* param = apvts.getParameter(id))
                param->setValueNotifyingHost(param->convertTo0to1(plainVal));
        };

        setParam("threshold",  p.thresholdDbv);
        setParam("release",    p.releaseSec);
        setParam("range",      p.rangeDb);
        setParam("output",     p.outputDbv);
        setParam("detSource",  static_cast<float>(p.detSource));
        setParam("mode",       static_cast<float>(p.mode));
        setParam("detType",    static_cast<float>(p.detType));
        setParam("dryWetMix",  p.dryWetMix * 100.0f);
        setParam("stereoLink", p.stereoLink ? 1.0f : 0.0f);
    }

    const juce::String getProgramName(int index) override
    {
        if (index >= 0 && index < static_cast<int>(kFactoryPresets.size()))
            return kFactoryPresets[static_cast<size_t>(index)].name;
        return {};
    }

    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override
    {
        auto state = apvts.copyState();
        std::unique_ptr<juce::XmlElement> xml(state.createXml());
        copyXmlToBinary(*xml, destData);
    }

    void setStateInformation(const void* data, int sizeInBytes) override
    {
        std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
        if (xmlState != nullptr && xmlState->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
    }

    float getGainReductionDb() const noexcept { return dsp.getGainReductionDb(); }
    float getInputPeakDb()     const noexcept { return dsp.getInputPeakDb(); }
    float getOutputPeakDb()    const noexcept { return dsp.getOutputPeakDb(); }
    bool  pollOutputClip()     noexcept       { return dsp.getAndClearOutputClip(); }

    ValleyPeople::Parameters getCurrentDspParameters() const noexcept { return dsp.getParameters(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    ValleyPeople::DynaMiteDSP dsp;
    int currentProgramIndex = 0;

    void syncParametersToDsp() noexcept
    {
        ValleyPeople::Parameters p;
        p.thresholdDbv = apvts.getRawParameterValue("threshold")->load();
        p.releaseSec   = apvts.getRawParameterValue("release")->load();
        p.rangeDb      = apvts.getRawParameterValue("range")->load();
        p.outputDbv    = apvts.getRawParameterValue("output")->load();
        p.detSource    = static_cast<ValleyPeople::DetSource>(static_cast<int>(apvts.getRawParameterValue("detSource")->load()));
        p.mode         = static_cast<ValleyPeople::Mode>(static_cast<int>(apvts.getRawParameterValue("mode")->load()));
        p.detType      = static_cast<ValleyPeople::DetType>(static_cast<int>(apvts.getRawParameterValue("detType")->load()));
        p.dryWetMix    = apvts.getRawParameterValue("dryWetMix")->load() * 0.01f;
        p.stereoLink   = (apvts.getRawParameterValue("stereoLink")->load() > 0.5f);
        dsp.setParameters(p);
    }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { "threshold", 1 }, "Threshold",
            juce::NormalisableRange<float>(-40.0f, 20.0f, 0.1f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel("dB")));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { "release", 1 }, "Release",
            juce::NormalisableRange<float>(0.05f, 5.0f, 0.001f, 0.38f), 0.25f,
            juce::AudioParameterFloatAttributes().withLabel("s")));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { "range", 1 }, "Range",
            juce::NormalisableRange<float>(0.0f, 60.0f, 0.1f), 60.0f,
            juce::AudioParameterFloatAttributes().withLabel("dB")));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { "output", 1 }, "Output",
            juce::NormalisableRange<float>(-15.0f, 15.0f, 0.1f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel("dB")));
        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { "detSource", 1 }, "Detector Source",
            juce::StringArray { "INT", "DS-FM", "EXT" }, 0));
        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { "mode", 1 }, "Mode",
            juce::StringArray { "EXP", "OUT", "LIMIT" }, 2));
        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { "detType", 1 }, "Detector Type",
            juce::StringArray { "GATE", "PEAK", "AVG" }, 2));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { "dryWetMix", 1 }, "Dry / Wet Mix",
            juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f,
            juce::AudioParameterFloatAttributes().withLabel("%")));
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { "stereoLink", 1 }, "Stereo Link", true));
        return { params.begin(), params.end() };
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DynaMiteAudioProcessor)
};

//==============================================================================
// 3. COMPACT SKEUOMORPHIC LOOK & FEEL (Zero Tick/Label Collisions)
//==============================================================================
class DynaMiteLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DynaMiteLookAndFeel()
    {
        setColour(juce::Slider::textBoxTextColourId,       juce::Colour(0xfff3d79b));
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff0b0c0e));
        setColour(juce::Slider::textBoxOutlineColourId,    juce::Colour(0xff473c2a));
        setColour(juce::ComboBox::backgroundColourId,      juce::Colour(0xff18191d));
        setColour(juce::ComboBox::textColourId,            juce::Colour(0xffe8d9b5));
        setColour(juce::ComboBox::outlineColourId,         juce::Colour(0xff6b5c43));
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, const float rotaryStartAngle,
                          const float rotaryEndAngle, juce::Slider& slider) override
    {
        const float centreX = static_cast<float>(x) + static_cast<float>(width)  * 0.5f;
        const float centreY = static_cast<float>(y) + static_cast<float>(height) * 0.54f;
        // Calibrated radius with generous outer margin so side numbers never touch adjacent knobs
        const float radius  = static_cast<float>(juce::jmin(width, height)) * 0.26f;
        const float angle   = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        drawHardwareScaleRing(g, centreX, centreY, radius, rotaryStartAngle, rotaryEndAngle, slider.getName());

        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.fillEllipse(centreX - radius - 1.5f, centreY - radius + 3.0f, (radius + 1.5f) * 2.0f, (radius + 1.5f) * 2.0f);

        juce::ColourGradient skirtGrad(juce::Colour(0xffb8bcc2), centreX - radius, centreY - radius,
                                       juce::Colour(0xff4a4d52), centreX + radius, centreY + radius, false);
        g.setGradientFill(skirtGrad);
        g.fillEllipse(centreX - radius, centreY - radius, radius * 2.0f, radius * 2.0f);

        const float bodyR = radius * 0.84f;
        juce::Path flutedPath;
        constexpr int numSteps = 72;
        for (int i = 0; i <= numSteps; ++i)
        {
            const float theta = (static_cast<float>(i) / static_cast<float>(numSteps)) * juce::MathConstants<float>::twoPi;
            const float fluteMod = 1.0f + 0.055f * std::cos(7.0f * (theta - angle));
            const float r = bodyR * fluteMod;
            const float px = centreX + r * std::sin(theta);
            const float py = centreY - r * std::cos(theta);
            if (i == 0) flutedPath.startNewSubPath(px, py);
            else        flutedPath.lineTo(px, py);
        }
        flutedPath.closeSubPath();

        juce::ColourGradient bakeliteGrad(juce::Colour(0xff35383f), centreX - bodyR * 0.6f, centreY - bodyR * 0.6f,
                                          juce::Colour(0xff101114), centreX + bodyR * 0.8f, centreY + bodyR * 0.8f, false);
        g.setGradientFill(bakeliteGrad);
        g.fillPath(flutedPath);

        const float capR = bodyR * 0.65f;
        g.setColour(juce::Colour(0xff181a1f));
        g.fillEllipse(centreX - capR, centreY - capR, capR * 2.0f, capR * 2.0f);

        juce::Path pointer;
        pointer.addRoundedRectangle(-1.6f, -radius + 1.0f, 3.2f, radius * 0.68f, 1.4f);
        g.setColour(juce::Colour(0xfff7e7c4));
        g.fillPath(pointer, juce::AffineTransform::rotation(angle).translated(centreX, centreY));
    }

private:
    static void drawHardwareScaleRing(juce::Graphics& g, float cx, float cy, float radius,
                                      float startAngle, float endAngle, const juce::String& knobId)
    {
        std::vector<const char*> labels;
        if (knobId == "threshold")
            labels = { "-40", "-30", "-20", "-10", "0", "+10", "+20" };
        else if (knobId == "release")
            labels = { ".05", ".1", ".2", ".5", "1", "2", "5" };
        else if (knobId == "range")
            labels = { "0", "10", "20", "30", "40", "50", "60" };
        else if (knobId == "output")
            labels = { "-15", "-10", "-5", "0", "+5", "+10", "+15" };
        else
            labels = { "0", "20", "40", "60", "80", "100" };

        const int numMajor = static_cast<int>(labels.size());
        g.setFont(juce::Font(8.5f, juce::Font::bold));

        for (int i = 0; i < numMajor; ++i)
        {
            const float norm = static_cast<float>(i) / static_cast<float>(numMajor - 1);
            const float a = startAngle + norm * (endAngle - startAngle);
            const float sinA = std::sin(a);
            const float cosA = std::cos(a);

            const float r1 = radius + 2.0f;
            const float r2 = radius + 5.5f;
            const float rLabel = radius + 8.5f;

            g.setColour(juce::Colour(0xffe8d9b5));
            g.drawLine(cx + r1 * sinA, cy - r1 * cosA,
                       cx + r2 * sinA, cy - r2 * cosA, 1.4f);

            // Outward radial alignment so ticks never collide with minus/plus signs
            const float tx = cx + rLabel * sinA;
            const float ty = cy - (rLabel + (std::fabs(sinA) < 0.25f ? 2.5f : 0.0f)) * cosA;

            if (sinA < -0.25f)
                g.drawText(labels[static_cast<size_t>(i)], static_cast<int>(tx - 24.0f), static_cast<int>(ty - 6.0f), 24, 12, juce::Justification::centredRight, false);
            else if (sinA > 0.25f)
                g.drawText(labels[static_cast<size_t>(i)], static_cast<int>(tx), static_cast<int>(ty - 6.0f), 24, 12, juce::Justification::centredLeft, false);
            else
                g.drawText(labels[static_cast<size_t>(i)], static_cast<int>(tx - 12.0f), static_cast<int>(ty - 6.0f), 24, 12, juce::Justification::centred, false);
        }
    }
};

class HardwareThreeWaySwitch : public juce::Component,
                               private juce::AudioProcessorValueTreeState::Listener
{
public:
    HardwareThreeWaySwitch(juce::AudioProcessorValueTreeState& state,
                           const juce::String& paramIdToAttach,
                           const juce::String& switchTitle,
                           std::array<juce::String, 3> posLabels)
        : apvts(state), paramId(paramIdToAttach), title(switchTitle), labels(std::move(posLabels))
    {
        apvts.addParameterListener(paramId, this);
        if (auto* raw = apvts.getRawParameterValue(paramId))
            currentIndex = std::clamp(static_cast<int>(std::round(raw->load())), 0, 2);
    }

    ~HardwareThreeWaySwitch() override { apvts.removeParameterListener(paramId, this); }

    void paint(juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        g.setColour(juce::Colour(0xffe8d9b5));
        g.setFont(juce::Font(9.0f, juce::Font::bold));
        g.drawText(title, 0, 0, getWidth(), 13, juce::Justification::centred);

        const float slotX = 6.0f;
        const float slotY = 16.0f;
        const float slotW = 15.0f;
        const float slotH = b.getHeight() - 20.0f;

        g.setColour(juce::Colour(0xff6b5c43));
        g.drawRoundedRectangle(slotX - 1.5f, slotY - 1.5f, slotW + 3.0f, slotH + 3.0f, 2.5f, 1.2f);
        g.setColour(juce::Colour(0xff0b0c0e));
        g.fillRoundedRectangle(slotX, slotY, slotW, slotH, 2.0f);

        const float posStep = slotH / 3.0f;
        for (int i = 0; i < 3; ++i)
        {
            const float cy = slotY + (static_cast<float>(i) + 0.5f) * posStep;
            const bool isSelected = (currentIndex == i);
            g.setColour(isSelected ? juce::Colour(0xffffcf70) : juce::Colour(0xff9e8e70));
            g.setFont(juce::Font(10.0f, isSelected ? juce::Font::bold : juce::Font::plain));
            g.drawText(labels[static_cast<size_t>(i)],
                       static_cast<int>(slotX + slotW + 5.0f),
                       static_cast<int>(cy - 7.0f),
                       getWidth() - static_cast<int>(slotX + slotW + 5.0f),
                       14, juce::Justification::centredLeft);
        }

        const float thumbH = posStep - 3.0f;
        const float thumbY = slotY + static_cast<float>(currentIndex) * posStep + 1.5f;
        juce::Rectangle<float> thumbRect(slotX + 1.5f, thumbY, slotW - 3.0f, thumbH);

        g.setColour(juce::Colour(0xff383c45));
        g.fillRoundedRectangle(thumbRect, 1.5f);
        g.setColour(juce::Colour(0xfff5e6c4));
        g.drawLine(thumbRect.getX() + 2.0f, thumbRect.getCentreY(),
                   thumbRect.getRight() - 2.0f, thumbRect.getCentreY(), 1.6f);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const float slotY = 16.0f;
        const float slotH = static_cast<float>(getHeight()) - 20.0f;
        const int clickedPos = std::clamp(static_cast<int>((static_cast<float>(e.y) - slotY) / (slotH / 3.0f)), 0, 2);
        setSwitchPosition(clickedPos);
    }

    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        if (wheel.deltaY > 0.01f)       setSwitchPosition(std::max(0, currentIndex - 1));
        else if (wheel.deltaY < -0.01f) setSwitchPosition(std::min(2, currentIndex + 1));
    }

private:
    juce::AudioProcessorValueTreeState& apvts;
    juce::String paramId;
    juce::String title;
    std::array<juce::String, 3> labels;
    int currentIndex = 0;

    void setSwitchPosition(int newIndex)
    {
        currentIndex = std::clamp(newIndex, 0, 2);
        if (auto* param = apvts.getParameter(paramId))
            param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(currentIndex)));
        repaint();
    }

    void parameterChanged(const juce::String&, float newValue) override
    {
        const int idx = std::clamp(static_cast<int>(std::round(newValue)), 0, 2);
        juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer<HardwareThreeWaySwitch>(this), idx]()
        {
            if (safeThis != nullptr)
            {
                safeThis->currentIndex = idx;
                safeThis->repaint();
            }
        });
    }
};

//==============================================================================
// Compact 740 x 295 AudioProcessorEditor with Clickable Title -> About Modal
//==============================================================================
class DynaMiteAudioProcessorEditor : public juce::AudioProcessorEditor,
                                     private juce::Timer
{
public:
    enum class SkinMode : int { Original = 0, Slam = 1, Gate = 2 };

    explicit DynaMiteAudioProcessorEditor(DynaMiteAudioProcessor& p)
        : AudioProcessorEditor(&p),
          processorRef(p),
          detSourceSwitch(p.apvts, "detSource", "SOURCE", { "int",  "ds/fm", "ext"   }),
          modeSwitch     (p.apvts, "mode",      "MODE",   { "exp",  "out",   "limit" }),
          detTypeSwitch  (p.apvts, "detType",   "DETECT", { "gate", "pk",    "avg"   })
    {
        setLookAndFeel(&lookAndFeel);
        setSize(740, 295);

        auto setupKnob = [this](juce::Slider& s, const juce::String& id,
                                juce::Label& lbl, const juce::String& title)
        {
            s.setName(id);
            s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 16);
            s.setVelocityModeParameters(1.0, 1, 0.05, true, juce::ModifierKeys::ctrlAltCommandModifiers);
            addAndMakeVisible(s);

            lbl.setText(title, juce::dontSendNotification);
            lbl.setJustificationType(juce::Justification::centred);
            lbl.setColour(juce::Label::textColourId, juce::Colour(0xffe8d9b5));
            lbl.setFont(juce::Font(10.5f, juce::Font::bold));
            addAndMakeVisible(lbl);
        };

        setupKnob(thresholdSlider, "threshold", thresholdLabel, "THRESHOLD");
        setupKnob(releaseSlider,   "release",   releaseLabel,   "RELEASE");
        setupKnob(rangeSlider,     "range",     rangeLabel,     "RANGE");
        setupKnob(outputSlider,    "output",    outputLabel,    "OUTPUT");
        setupKnob(mixSlider,       "mix",       mixLabel,       "DRY / WET");

        thresholdAttach = std::make_unique<SliderAttachment>(processorRef.apvts, "threshold", thresholdSlider);
        releaseAttach   = std::make_unique<SliderAttachment>(processorRef.apvts, "release",   releaseSlider);
        rangeAttach     = std::make_unique<SliderAttachment>(processorRef.apvts, "range",     rangeSlider);
        outputAttach    = std::make_unique<SliderAttachment>(processorRef.apvts, "output",    outputSlider);
        mixAttach       = std::make_unique<SliderAttachment>(processorRef.apvts, "dryWetMix", mixSlider);

        addAndMakeVisible(detSourceSwitch);
        addAndMakeVisible(modeSwitch);
        addAndMakeVisible(detTypeSwitch);

        stereoLinkButton.setButtonText("LINK L/R");
        stereoLinkButton.setColour(juce::ToggleButton::textColourId, juce::Colour(0xffe8d9b5));
        stereoLinkButton.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xfff59e0b));
        addAndMakeVisible(stereoLinkButton);
        stereoLinkAttach = std::make_unique<ButtonAttachment>(processorRef.apvts, "stereoLink", stereoLinkButton);

        skinSelectorBox.addItem("CORE 18", 1);
        skinSelectorBox.addItem("SLAM",    2);
        skinSelectorBox.addItem("GATE",    3);
        skinSelectorBox.setSelectedId(1, juce::dontSendNotification);
        skinSelectorBox.onChange = [this]()
        {
            setSkinMode(static_cast<SkinMode>(skinSelectorBox.getSelectedId() - 1));
        };
        addAndMakeVisible(skinSelectorBox);

        for (int i = 0; i < processorRef.getNumPrograms(); ++i)
            presetBox.addItem(processorRef.getProgramName(i), i + 1);
        presetBox.setSelectedId(1, juce::dontSendNotification);
        presetBox.onChange = [this]()
        {
            processorRef.setCurrentProgram(presetBox.getSelectedId() - 1);
        };
        addAndMakeVisible(presetBox);

        setSkinMode(SkinMode::Original);
        startTimerHz(30);
    }

    ~DynaMiteAudioProcessorEditor() override
    {
        stopTimer();
        setLookAndFeel(nullptr);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        // Clickable Brand/Model Header Area (x: 34..295, y: 12..40) toggles About Modal
        const juce::Rectangle<int> titleZone(34, 12, 260, 28);
        if (showAboutOverlay || titleZone.contains(e.getPosition()))
        {
            showAboutOverlay = !showAboutOverlay;
            repaint();
        }
    }

    void paint(juce::Graphics& g) override
    {
        juce::ColourGradient bgGrad(juce::Colour(0xff202227), 0.0f, 0.0f,
                                    juce::Colour(0xff111215), 0.0f, static_cast<float>(getHeight()), false);
        g.setGradientFill(bgGrad);
        g.fillAll();

        // Slim Rack Ears
        g.setColour(juce::Colour(0xff131418));
        g.fillRect(0, 0, 24, getHeight());
        g.fillRect(getWidth() - 24, 0, 24, getHeight());

        // Gold Faceplate Frame
        juce::Rectangle<float> panelRect(28.0f, 8.0f, static_cast<float>(getWidth() - 56),
                                         static_cast<float>(getHeight() - 46));
        g.setColour(juce::Colour(0xff9c8256));
        g.drawRoundedRectangle(panelRect, 4.0f, 1.6f);

        // Clickable Rebranded Header Emblem
        g.setColour(juce::Colour(0xffe8d9b5));
        g.setFont(juce::Font(16.0f, juce::Font::bold));
        g.drawText(PLUGIN_BRAND_TITLE, 38, 14, 150, 20, juce::Justification::centredLeft);

        g.setColour(juce::Colour(0xfff59e0b));
        g.setFont(juce::Font(16.0f, juce::Font::bold));
        g.drawText(PLUGIN_MODEL_TITLE, 168, 14, 120, 20, juce::Justification::centredLeft);

        g.setColour(juce::Colour(0xff9e8e70));
        g.setFont(juce::Font(9.5f, juce::Font::plain));
        g.drawText("[i] Click title for About & Credits", 38, 31, 220, 12, juce::Justification::centredLeft);

        // Compact Recessed LED Meters
        drawGainReductionLeds(g, 38,  46, 280, 32);
        drawOutputLevelLeds  (g, 326, 46, 175, 32);
        drawClipIndicator    (g, 509, 46, 72,  32);

        // Slim Bottom Interactive Mode Guide Strip
        juce::Rectangle<float> guideRect(28.0f, static_cast<float>(getHeight() - 33),
                                         static_cast<float>(getWidth() - 56), 26.0f);
        g.setColour(juce::Colour(0xff0b0c0e));
        g.fillRoundedRectangle(guideRect, 4.0f);
        g.setColour(juce::Colour(0xff6b5c43));
        g.drawRoundedRectangle(guideRect, 4.0f, 1.0f);

        const auto currentParams = processorRef.getCurrentDspParameters();
        const std::string modeDesc = ValleyPeople::DynaMiteDSP::getInteractiveModeDescription(currentParams);

        g.setColour(juce::Colour(0xffe5c07b));
        g.setFont(juce::Font(11.0f, juce::Font::plain));
        g.drawFittedText(modeDesc, guideRect.reduced(10.0f, 3.0f).toNearestInt(),
                         juce::Justification::centredLeft, 1);
    }

    void paintOverChildren(juce::Graphics& g) override
    {
        if (!showAboutOverlay)
            return;

        // Hardware-styled About & Author Credits Modal Overlay
        g.setColour(juce::Colours::black.withAlpha(0.82f));
        g.fillAll();

        juce::Rectangle<float> card(170.0f, 38.0f, 400.0f, 215.0f);
        g.setColour(juce::Colour(0xff16181e));
        g.fillRoundedRectangle(card, 8.0f);
        g.setColour(juce::Colour(0xffa68b5b));
        g.drawRoundedRectangle(card, 8.0f, 2.0f);

        g.setColour(juce::Colour(0xfff59e0b));
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.drawText("CUSTOM VCA DYNAMICS PROCESSOR", card.getX(), card.getY() + 14.0f, card.getWidth(), 16.0f, juce::Justification::centred);

        g.setColour(juce::Colour(0xffefe1c1));
        g.setFont(juce::Font(21.0f, juce::Font::bold));
        g.drawText(juce::String(PLUGIN_BRAND_TITLE) + " " + PLUGIN_MODEL_TITLE,
                   card.getX(), card.getY() + 34.0f, card.getWidth(), 26.0f, juce::Justification::centred);

        g.setColour(juce::Colour(0xffb8a88a));
        g.setFont(juce::Font(12.0f, juce::Font::plain));
        g.drawText("64-bit TA-101 VCA Limiter, Expander, Gate, De-Esser & ARC",
                   card.getX() + 16.0f, card.getY() + 64.0f, card.getWidth() - 32.0f, 20.0f, juce::Justification::centred);

        juce::Rectangle<float> authorBox(card.getX() + 30.0f, card.getY() + 92.0f, card.getWidth() - 60.0f, 68.0f);
        g.setColour(juce::Colour(0xff0b0c0e));
        g.fillRoundedRectangle(authorBox, 5.0f);
        g.setColour(juce::Colour(0xff59492f));
        g.drawRoundedRectangle(authorBox, 5.0f, 1.0f);

        g.setColour(juce::Colour(0xff9e8e70));
        g.setFont(juce::Font(10.0f, juce::Font::bold));
        g.drawText("CREATED & ENGINEERED BY", authorBox.getX(), authorBox.getY() + 8.0f, authorBox.getWidth(), 14.0f, juce::Justification::centred);

        g.setColour(juce::Colour(0xff34d399));
        g.setFont(juce::Font(16.0f, juce::Font::bold));
        g.drawText(PLUGIN_AUTHOR_NAME, authorBox.getX(), authorBox.getY() + 24.0f, authorBox.getWidth(), 22.0f, juce::Justification::centred);

        g.setColour(juce::Colour(0xff8c7e65));
        g.setFont(juce::Font(10.0f, juce::Font::plain));
        g.drawText(PLUGIN_VERSION_STR, authorBox.getX(), authorBox.getY() + 47.0f, authorBox.getWidth(), 14.0f, juce::Justification::centred);

        g.setColour(juce::Colour(0xffe5c07b));
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.drawText("[ Click anywhere to close ]", card.getX(), card.getBottom() - 30.0f, card.getWidth(), 18.0f, juce::Justification::centred);
    }

    void resized() override
    {
        skinSelectorBox.setBounds(456, 14, 92, 22);
        presetBox.setBounds      (554, 14, 150, 22);

        // Generous 106px width per knob so scale numbers never overlap
        const int knobY = 104;
        const int knobW = 104;
        const int knobH = 110;
        const int swY   = 96;
        const int swW   = 52;
        const int swH   = 110;

        thresholdLabel.setBounds (34,  knobY - 18, knobW, 16);
        thresholdSlider.setBounds(34,  knobY,      knobW, knobH);

        releaseLabel.setBounds   (140, knobY - 18, knobW, 16);
        releaseSlider.setBounds  (140, knobY,      knobW, knobH);

        detSourceSwitch.setBounds(250, swY, swW, swH);
        modeSwitch.setBounds     (302, swY, swW, swH);
        detTypeSwitch.setBounds  (354, swY, swW, swH);

        rangeLabel.setBounds     (410, knobY - 18, knobW, 16);
        rangeSlider.setBounds    (410, knobY,      knobW, knobH);

        outputLabel.setBounds    (516, knobY - 18, knobW, 16);
        outputSlider.setBounds   (516, knobY,      knobW, knobH);

        mixLabel.setBounds        (622, knobY - 18, 86, 16);
        mixSlider.setBounds       (622, knobY,      86, knobH - 16);
        stereoLinkButton.setBounds(624, knobY + knobH - 12, 84, 20);

        rangeLabel.setVisible(activeSkin != SkinMode::Slam);
        rangeSlider.setVisible(activeSkin != SkinMode::Slam);
    }

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    DynaMiteAudioProcessor& processorRef;
    DynaMiteLookAndFeel lookAndFeel;
    SkinMode activeSkin = SkinMode::Original;
    bool showAboutOverlay = false;

    juce::Slider thresholdSlider, releaseSlider, rangeSlider, outputSlider, mixSlider;
    juce::Label  thresholdLabel,  releaseLabel,  rangeLabel,  outputLabel,  mixLabel;

    HardwareThreeWaySwitch detSourceSwitch, modeSwitch, detTypeSwitch;
    juce::ComboBox skinSelectorBox, presetBox;
    juce::ToggleButton stereoLinkButton;

    std::unique_ptr<SliderAttachment> thresholdAttach, releaseAttach, rangeAttach, outputAttach, mixAttach;
    std::unique_ptr<ButtonAttachment> stereoLinkAttach;

    float displayedGrDb  = 0.0f;
    float displayedOutDb = -60.0f;
    int   clipHoldFrames = 0;

    void setSkinMode(SkinMode newSkin)
    {
        activeSkin = newSkin;
        if (activeSkin == SkinMode::Slam)
        {
            thresholdLabel.setText("COMPRESSION", juce::dontSendNotification);
            if (auto* modeParam = processorRef.apvts.getParameter("mode"))
                modeParam->setValueNotifyingHost(modeParam->convertTo0to1(2.0f));
        }
        else if (activeSkin == SkinMode::Gate)
        {
            thresholdLabel.setText("THRESHOLD", juce::dontSendNotification);
            if (auto* modeParam = processorRef.apvts.getParameter("mode"))
                modeParam->setValueNotifyingHost(modeParam->convertTo0to1(0.0f));
        }
        else
        {
            thresholdLabel.setText("THRESHOLD", juce::dontSendNotification);
        }
        resized();
        repaint();
    }

    void timerCallback() override
    {
        const float targetGr = processorRef.getGainReductionDb();
        displayedGrDb = (targetGr > displayedGrDb) ? targetGr : (displayedGrDb * 0.82f + targetGr * 0.18f);

        const float targetOut = processorRef.getOutputPeakDb();
        displayedOutDb = (targetOut > displayedOutDb) ? targetOut : (displayedOutDb * 0.85f + targetOut * 0.15f);

        if (processorRef.pollOutputClip())
            clipHoldFrames = 18;
        else if (clipHoldFrames > 0)
            --clipHoldFrames;

        repaint();
    }

    void drawGainReductionLeds(juce::Graphics& g, int x, int y, int w, int h) const
    {
        g.setColour(juce::Colour(0xff0d0e11));
        g.fillRoundedRectangle(static_cast<float>(x), static_cast<float>(y),
                               static_cast<float>(w), static_cast<float>(h), 3.0f);

        static constexpr std::array<float, 8> kThresholdsDb = { 1.0f, 2.0f, 4.0f, 6.0f, 10.0f, 15.0f, 20.0f, 40.0f };
        static constexpr std::array<const char*, 8> kLabels = { "1", "2", "4", "6", "10", "15", "20", "40" };

        const int segW = (w - 16) / 8;
        for (size_t i = 0; i < 8; ++i)
        {
            const int sx = x + 8 + static_cast<int>(i) * segW;
            const bool isLit = (displayedGrDb >= kThresholdsDb[i]);

            juce::Colour activeCol = (i < 4) ? juce::Colour(0xff6bc950)
                                   : (i < 6) ? juce::Colour(0xfff0b429)
                                             : juce::Colour(0xffe03e36);

            g.setColour(isLit ? activeCol : activeCol.withAlpha(0.14f));
            g.fillRoundedRectangle(static_cast<float>(sx), static_cast<float>(y + 5),
                                   static_cast<float>(segW - 5), 11.0f, 2.0f);

            g.setColour(juce::Colour(0xffd4c49e));
            g.setFont(9.0f);
            g.drawText(kLabels[i], sx - 2, y + 17, segW - 2, 12, juce::Justification::centred);
        }
    }

    void drawOutputLevelLeds(juce::Graphics& g, int x, int y, int w, int h) const
    {
        g.setColour(juce::Colour(0xff0d0e11));
        g.fillRoundedRectangle(static_cast<float>(x), static_cast<float>(y),
                               static_cast<float>(w), static_cast<float>(h), 3.0f);

        static constexpr std::array<float, 6> kOutDb = { -30.0f, -18.0f, -12.0f, -6.0f, -3.0f, 0.0f };
        static constexpr std::array<const char*, 6> kLabels = { "-30", "-18", "-12", "-6", "-3", "0" };

        const int segW = (w - 12) / 6;
        for (size_t i = 0; i < 6; ++i)
        {
            const int sx = x + 6 + static_cast<int>(i) * segW;
            const bool isLit = (displayedOutDb >= kOutDb[i]);

            juce::Colour activeCol = (i < 4) ? juce::Colour(0xff50b86c)
                                   : (i == 4) ? juce::Colour(0xfff0b429)
                                              : juce::Colour(0xffe03e36);

            g.setColour(isLit ? activeCol : activeCol.withAlpha(0.14f));
            g.fillRoundedRectangle(static_cast<float>(sx), static_cast<float>(y + 5),
                                   static_cast<float>(segW - 4), 11.0f, 2.0f);

            g.setColour(juce::Colour(0xffd4c49e));
            g.setFont(8.5f);
            g.drawText(kLabels[i], sx - 2, y + 17, segW, 12, juce::Justification::centred);
        }
    }

    void drawClipIndicator(juce::Graphics& g, int x, int y, int w, int h) const
    {
        g.setColour(juce::Colour(0xff0d0e11));
        g.fillRoundedRectangle(static_cast<float>(x), static_cast<float>(y),
                               static_cast<float>(w), static_cast<float>(h), 3.0f);

        const bool isClipping = (clipHoldFrames > 0);
        g.setColour(isClipping ? juce::Colour(0xffff2e24) : juce::Colour(0xff4a1614));
        g.fillEllipse(static_cast<float>(x + 8), static_cast<float>(y + 9), 13.0f, 13.0f);

        g.setColour(juce::Colour(0xffe8d9b5));
        g.setFont(juce::Font(10.0f, juce::Font::bold));
        g.drawText("CLIP", x + 25, y + 7, 38, 16, juce::Justification::centredLeft);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DynaMiteAudioProcessorEditor)
};

juce::AudioProcessorEditor* DynaMiteAudioProcessor::createEditor()
{
    return new DynaMiteAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DynaMiteAudioProcessor();
}

#else
#include <iostream>
#include <cassert>

int main()
{
    using namespace ValleyPeople;
    DynaMiteDSP dsp;
    dsp.prepare(48000.0);
    Parameters p;
    p.thresholdDbv = 0.0f;
    p.rangeDb      = 25.0f;
    p.detSource    = DetSource::INT;
    p.mode         = Mode::LIMIT;
    p.detType      = DetType::PEAK;
    assert(std::fabs(DynaMiteDSP::computeTargetGainReductionDb(-10.0f, p) - 10.0f) < 1e-4f);
    std::cout << "[PASS] Compact English GUI & About Overlay " << PLUGIN_FULL_NAME
              << " by " << PLUGIN_AUTHOR_NAME << " verified!\n";
    return 0;
}
#endif
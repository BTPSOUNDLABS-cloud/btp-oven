#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace btp;

namespace
{
    inline float softClip (float x) noexcept
    {
        const float a = std::abs (x);

        if (a <= 0.9f)
            return x;

        const float y = 0.9f + std::tanh ((a - 0.9f) * 2.0f) * 0.1f;
        return x < 0.0f ? -y : y;
    }

    void addFloat (juce::AudioProcessorValueTreeState::ParameterLayout& layout,
                   btp::P p, const char* name, float minV, float maxV, float def,
                   float centre = 0.0f, int decimals = 2, const char* suffix = nullptr,
                   float interval = 0.0f)
    {
        juce::NormalisableRange<float> range (minV, maxV, interval);

        if (centre > minV && centre < maxV)
            range.setSkewForCentre (centre);

        const juce::String unit (suffix != nullptr ? suffix : "");

        auto attributes = juce::AudioParameterFloatAttributes()
                              .withStringFromValueFunction ([decimals, unit] (float value, int)
                                                            {
                                                                if (decimals <= 0)
                                                                    return juce::String (juce::roundToInt (value)) + unit;

                                                                return juce::String (value, decimals) + unit;
                                                            });

        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { btp::keyOf (p), 1 },
                                                                 name, range, def, attributes));
    }

    void addBool (juce::AudioProcessorValueTreeState::ParameterLayout& layout,
                  btp::P p, const char* name, bool def)
    {
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { btp::keyOf (p), 1 },
                                                                name, def));
    }

    void addInt (juce::AudioProcessorValueTreeState::ParameterLayout& layout,
                 btp::P p, const char* name, int minV, int maxV, int def)
    {
        layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { btp::keyOf (p), 1 },
                                                               name, minV, maxV, def));
    }

    void addChoice (juce::AudioProcessorValueTreeState::ParameterLayout& layout,
                    btp::P p, const char* name, const juce::StringArray& choices, int def)
    {
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { btp::keyOf (p), 1 },
                                                                  name, choices, def));
    }

    juce::String loopLabel (int index)
    {
        static const char* names[] = { "A", "B", "C" };
        return (index >= 0 && index < 3) ? names[index] : "?";
    }
}

//==============================================================================
BtPOvenAudioProcessor::BtPOvenAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    formatManager.registerBasicFormats();
    refreshRawPointers();
    setLatencySamples (0);
    startTimerHz (30);
}

BtPOvenAudioProcessor::~BtPOvenAudioProcessor()
{
    stopTimer();
    RvcEngine::getInstance().shutdown();
   #if BTP_HAS_RAVE
    RaveEngine::getInstance().shutdown();
   #endif
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout BtPOvenAudioProcessor::createParameterLayout()
{
    using namespace btp;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Global ---------------------------------------------------------------
    addFloat (layout, P::InGain,  "Input Gain",  0.0f, 2.0f, 1.0f, 0.0f, 2);
    addFloat (layout, P::OutGain, "Output Gain", 0.0f, 2.0f, 0.9f, 0.0f, 2);

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { keyOf (P::ActiveLoop), 1 }, "Active Loop",
        juce::StringArray { "A", "B", "C" }, 0));

    addBool (layout, P::Playing, "Play", true);

    // Per-loop transport ---------------------------------------------------
    addBool (layout, P::AReverse, "A Reverse", false);
    addBool (layout, P::AHalf,    "A Half Time", false);
    addBool (layout, P::ADouble,  "A Double Time", false);
    addBool (layout, P::AStutter, "A Stutter", false);

    addBool (layout, P::BReverse, "B Reverse", false);
    addBool (layout, P::BHalf,    "B Half Time", false);
    addBool (layout, P::BDouble,  "B Double Time", false);
    addBool (layout, P::BStutter, "B Stutter", false);

    // Cook mode ------------------------------------------------------------
    addBool (layout, P::CookOn, "Cook Mode", false);

    addFloat (layout, P::GrainSize,    "Grain Size",    5.0f, 500.0f, 90.0f, 90.0f, 0, " ms");
    addFloat (layout, P::GrainSpray,   "Spray",         0.0f, 1.0f,   0.15f, 0.0f, 2);
    addFloat (layout, P::GrainPitch,   "Pitch Random",  0.0f, 24.0f,  2.0f, 0.0f, 1, " st");
    addFloat (layout, P::GrainStretch, "Time Stretch",  0.25f, 4.0f,  1.0f, 1.0f, 2, "x");
    addFloat (layout, P::GrainReverse, "Reverse Grains",0.0f, 1.0f,   0.0f, 0.0f, 2);
    addFloat (layout, P::GrainDensity, "Density",       0.0f, 1.0f,   0.5f, 0.0f, 2);
    addFloat (layout, P::GrainBurn,    "Burn",          0.0f, 1.0f,   0.0f, 0.0f, 2);
    addFloat (layout, P::GrainWidth,   "Width",         0.0f, 1.0f,   0.6f, 0.0f, 2);

    // Trap timing ----------------------------------------------------------
    addFloat (layout, P::ChopDepth,  "Gate Chop",   0.0f, 1.0f,  0.0f, 0.0f, 2);
    addFloat (layout, P::ChopRate,   "Chop Rate",   1.0f, 24.0f, 8.0f, 8.0f, 1);
    addBool  (layout, P::ChopTriplet,"Triplet",     false);
    addFloat (layout, P::StutterRate,"Stutter Rate",1.0f, 32.0f, 8.0f, 8.0f, 1);
    addFloat (layout, P::Drip,       "Drip",        0.0f, 1.0f,  0.0f, 0.0f, 2);
    addFloat (layout, P::DripRate,   "Drip Rate",   0.05f, 2.0f, 0.25f, 0.3f, 2);

    // Pitch + texture ------------------------------------------------------
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { keyOf (P::Octave), 1 }, "Octave", -2, 2, 0));

    addFloat (layout, P::Detune,     "Detune",     0.0f, 50.0f, 0.0f, 0.0f, 1, " ct");
    addFloat (layout, P::DetuneRate, "Detune Rate",0.05f, 8.0f, 0.6f, 1.0f, 2);
    addFloat (layout, P::Formant,    "Formant",    -12.0f, 12.0f, 0.0f, 0.0f, 1, " st");
    addFloat (layout, P::Wobble,     "Tape Wobble",0.0f, 1.0f,  0.0f, 0.0f, 2);
    addFloat (layout, P::Vinyl,      "Vinyl Noise",0.0f, 1.0f,  0.0f, 0.0f, 2);
    addFloat (layout, P::Air,        "Air Hiss",   0.0f, 1.0f,  0.0f, 0.0f, 2);
    addFloat (layout, P::Ghost,      "Ghost Pitch",0.0f, 1.0f,  0.0f, 0.0f, 2);

    // Dark filters ---------------------------------------------------------
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { keyOf (P::FilterType), 1 }, "Filter Type",
        juce::StringArray { "OFF", "LOWPASS", "HIGHPASS", "BANDPASS", "NOTCH",
                            "PEAK", "COMB", "SMOKE", "BASEMENT" }, 0));

    addFloat (layout, P::Cutoff,    "Cutoff",     30.0f, 18000.0f, 4000.0f, 900.0f, 0, " Hz");
    addFloat (layout, P::Reso,      "Resonance",  0.0f, 1.0f, 0.35f, 0.0f, 2);
    addFloat (layout, P::Sweep,     "Notch Sweep",0.0f, 1.0f, 0.0f, 0.0f, 2);
    addFloat (layout, P::SweepRate, "Sweep Rate", 0.05f, 5.0f, 0.3f, 0.5f, 2);

    // FX recorder ----------------------------------------------------------
    addBool  (layout, P::FxPlay,  "FX Recorder Play", false);
    addFloat (layout, P::FxSpeed, "FX Recorder Speed", 0.25f, 2.0f, 1.0f, 1.0f, 2, "x");

    // Dual-loop mix / region ------------------------------------------------
    addBool  (layout, P::APlay,   "Loop A Play",   true);
    addBool  (layout, P::BPlay,   "Loop B Play",   true);
    addBool  (layout, P::ABypass, "Loop A Bypass", false);
    addBool  (layout, P::BBypass, "Loop B Bypass", false);

    addFloat (layout, P::AStart,  "Loop A Start",  0.0f, 1.0f, 0.0f, 0.0f, 3);
    addFloat (layout, P::AEnd,    "Loop A End",    0.0f, 1.0f, 1.0f, 0.0f, 3);
    addBool  (layout, P::ALoop,   "Loop A Loop",   true);

    addFloat (layout, P::BStart,  "Loop B Start",  0.0f, 1.0f, 0.0f, 0.0f, 3);
    addFloat (layout, P::BEnd,    "Loop B End",    0.0f, 1.0f, 1.0f, 0.0f, 3);
    addBool  (layout, P::BLoop,   "Loop B Loop",   true);

    // Loop C (AI take) ------------------------------------------------------
    addBool  (layout, P::CPlay,   "Loop C Play",   true);
    addBool  (layout, P::CBypass, "Loop C Bypass", false);

    addFloat (layout, P::CStart,  "Loop C Start",  0.0f, 1.0f, 0.0f, 0.0f, 3);
    addFloat (layout, P::CEnd,    "Loop C End",    0.0f, 1.0f, 1.0f, 0.0f, 3);
    addBool  (layout, P::CLoop,   "Loop C Loop",   true);

    addBool  (layout, P::CReverse, "C Reverse", false);
    addBool  (layout, P::CHalf,    "C Half Time", false);
    addBool  (layout, P::CDouble,  "C Double Time", false);
    addBool  (layout, P::CStutter, "C Stutter", false);

    // Auto chop / rearrange -------------------------------------------------
    addBool  (layout, P::AutoChop, "Auto Chop", false);
    addChoice (layout, P::ChopDiv, "Chop Division",
               juce::StringArray { "1/16", "1/8", "1/4", "1/2", "1 BAR", "2 BAR", "4 BAR" }, 2);
    addBool  (layout, P::Rearrange, "Rearrange", false);
    addInt   (layout, P::ShuffleSeed, "Rearrange Seed", 0, 999, 1);
    addFloat (layout, P::Bpm, "Tempo", 60.0f, 200.0f, 140.0f, 140.0f, 0, " BPM", 1.0f);

    // Five-band EQ ----------------------------------------------------------
    addBool  (layout, P::EqOn, "EQ On", false);
    addFloat (layout, P::EqLow,     "EQ Low",      -12.0f, 12.0f, 0.0f, 0.0f, 1, " dB");
    addFloat (layout, P::EqLowMid,  "EQ Low Mid",  -12.0f, 12.0f, 0.0f, 0.0f, 1, " dB");
    addFloat (layout, P::EqMid,     "EQ Mid",      -12.0f, 12.0f, 0.0f, 0.0f, 1, " dB");
    addFloat (layout, P::EqHighMid, "EQ High Mid", -12.0f, 12.0f, 0.0f, 0.0f, 1, " dB");
    addFloat (layout, P::EqHigh,    "EQ High",     -12.0f, 12.0f, 0.0f, 0.0f, 1, " dB");

    // Full FX rack ----------------------------------------------------------
    addChoice (layout, P::RackRoute, "Rack Routing",
               juce::StringArray { "SERIES", "PARALLEL" }, 0);
    addFloat (layout, P::RackMix, "Rack Mix", 0.0f, 1.0f, 1.0f, 0.0f, 2);

    addBool  (layout, P::DelayOn, "Delay On", false);
    addFloat (layout, P::DelayTime, "Delay Time", 0.01f, 1.5f, 0.35f, 0.35f, 2, " s");
    addFloat (layout, P::DelayFeedback, "Delay Feedback", 0.0f, 0.95f, 0.35f, 0.0f, 2);
    addFloat (layout, P::DelayTone, "Delay Tone", 0.0f, 1.0f, 0.55f, 0.0f, 2);
    addFloat (layout, P::DelayMix, "Delay Mix", 0.0f, 1.0f, 0.25f, 0.0f, 2);
    addBool  (layout, P::DelayPingPong, "Delay Ping Pong", false);

    addBool  (layout, P::DistOn, "Distortion On", false);
    addFloat (layout, P::DistDrive, "Distortion Drive", 0.0f, 1.0f, 0.35f, 0.0f, 2);
    addFloat (layout, P::DistTone, "Distortion Tone", 0.0f, 1.0f, 0.5f, 0.0f, 2);
    addFloat (layout, P::DistMix, "Distortion Mix", 0.0f, 1.0f, 0.6f, 0.0f, 2);

    addBool  (layout, P::CrushOn, "Bitcrush On", false);
    addInt   (layout, P::CrushBits, "Bitcrush Bits", 1, 16, 8);
    addFloat (layout, P::CrushMix, "Bitcrush Mix", 0.0f, 1.0f, 0.5f, 0.0f, 2);

    addBool  (layout, P::ChorusOn, "Chorus On", false);
    addFloat (layout, P::ChorusRate, "Chorus Rate", 0.05f, 5.0f, 0.8f, 0.8f, 2, " Hz");
    addFloat (layout, P::ChorusDepth, "Chorus Depth", 0.0f, 1.0f, 0.5f, 0.0f, 2);
    addFloat (layout, P::ChorusMix, "Chorus Mix", 0.0f, 1.0f, 0.35f, 0.0f, 2);

    addBool  (layout, P::FlangerOn, "Flanger On", false);
    addFloat (layout, P::FlangerRate, "Flanger Rate", 0.05f, 5.0f, 0.3f, 0.3f, 2, " Hz");
    addFloat (layout, P::FlangerDepth, "Flanger Depth", 0.0f, 1.0f, 0.6f, 0.0f, 2);
    addFloat (layout, P::FlangerFeedback, "Flanger Feedback", 0.0f, 0.9f, 0.45f, 0.0f, 2);
    addFloat (layout, P::FlangerMix, "Flanger Mix", 0.0f, 1.0f, 0.4f, 0.0f, 2);

    addBool  (layout, P::PhaserOn, "Phaser On", false);
    addFloat (layout, P::PhaserRate, "Phaser Rate", 0.05f, 5.0f, 0.5f, 0.5f, 2, " Hz");
    addFloat (layout, P::PhaserDepth, "Phaser Depth", 0.0f, 1.0f, 0.7f, 0.0f, 2);
    addFloat (layout, P::PhaserMix, "Phaser Mix", 0.0f, 1.0f, 0.5f, 0.0f, 2);

    addBool  (layout, P::RackVerbOn, "Rack Reverb On", false);
    addFloat (layout, P::RackVerbSize, "Rack Reverb Size", 0.0f, 1.0f, 0.6f, 0.0f, 2);
    addFloat (layout, P::RackVerbMix, "Rack Reverb Mix", 0.0f, 1.0f, 0.25f, 0.0f, 2);

    // Key / scale -----------------------------------------------------------
    addChoice (layout, P::KeyRoot, "Key Root",
               juce::StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0);
    addChoice (layout, P::ScaleType, "Scale", juce::StringArray { "MAJOR", "MINOR" }, 0);
    addBool  (layout, P::KeyLock, "Key Lock", false);

    // Auto-tune -------------------------------------------------------------
    addBool  (layout, P::AutoTuneOn, "Autotune On", false);
    addFloat (layout, P::AutoTuneSpeed, "Autotune Speed", 0.5f, 200.0f, 8.0f, 20.0f, 1, " ms");
    addFloat (layout, P::AutoTuneMix,  "Autotune Mix",  0.0f, 1.0f, 1.0f, 0.0f, 2);
    addFloat (layout, P::AutoTuneSnap, "Autotune Snap", 0.0f, 1.0f, 1.0f, 0.0f, 2);

    return layout;
}

//==============================================================================
void BtPOvenAudioProcessor::refreshRawPointers()
{
    for (int i = 0; i < btp::numParams; ++i)
    {
        rawParams[(size_t) i] = apvts.getRawParameterValue (btp::kParamKeys[i]);
        jassert (rawParams[(size_t) i] != nullptr);
    }
}

double BtPOvenAudioProcessor::activeLoopLengthSeconds() const
{
    int index = juce::jlimit (0, 2, (int) std::lround (rawParams[(size_t) btp::P::ActiveLoop]
                                                          ? rawParams[(size_t) btp::P::ActiveLoop]->load() * 2.0f
                                                          : 0.0f));

    if (! loops[(size_t) index].hasContent())
    {
        for (int i = 0; i < 3; ++i)
        {
            if (loops[(size_t) i].hasContent())
            {
                index = i;
                break;
            }
        }
    }

    const double len = loops[(size_t) index].getLengthSeconds();
    return len > 0.05 ? len : 0.0;
}

//==============================================================================
void BtPOvenAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const bool sampleRateChanged = std::abs (sampleRate - currentSampleRate) > 0.5;
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;

    if (! loopsPrepared || sampleRateChanged)
    {
        for (auto& loop : loops)
            loop.prepare (currentSampleRate, btp::LoopBuffer::maxLoopSeconds);

        loopsPrepared = true;
    }

    for (auto& voice : voices)
        voice.prepare (currentSampleRate);

    voiceFX.prepare (currentSampleRate);
    rackFX.prepare (currentSampleRate);
    autoTune.prepare (currentSampleRate);

    dryBuffer.setSize (2, juce::jmax (64, samplesPerBlock), false, true, false);
    dryBuffer.clear();

    inGainSm.reset (currentSampleRate, 0.01);
    outGainSm.reset (currentSampleRate, 0.02);
    loopGainA.reset (currentSampleRate, 0.02);
    loopGainB.reset (currentSampleRate, 0.02);
    loopGainC.reset (currentSampleRate, 0.02);

    inGainSm.setCurrentAndTargetValue (1.0f);
    outGainSm.setCurrentAndTargetValue (0.9f);
    loopGainA.setCurrentAndTargetValue (0.0f);
    loopGainB.setCurrentAndTargetValue (0.0f);
    loopGainC.setCurrentAndTargetValue (0.0f);

    autoPhase = 0.0;
    automationPhase.store (0.0f);
    outputLevel.store (0.0f);
}

void BtPOvenAudioProcessor::releaseResources()
{
    // Buffers are intentionally kept: hosts call this on device changes and we
    // do not want to throw away a recorded loop.
}

bool BtPOvenAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in  = layouts.getMainInputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    if (in != out && ! (in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo()))
        return false;

    return true;
}

//==============================================================================
btp::Values BtPOvenAudioProcessor::readValues() const
{
    using namespace btp;

    Values v;

    auto load = [this] (P p) -> float
    {
        const auto* a = rawParams[(size_t) p];
        return a != nullptr ? a->load (std::memory_order_relaxed) : 0.0f;
    };

    v.inGain  = load (P::InGain);
    v.outGain = load (P::OutGain);

    v.activeLoop = juce::jlimit (0, 2, (int) std::lround (load (P::ActiveLoop) * 2.0f));
    v.playing    = load (P::Playing) > 0.5f;

    v.aReverse    = load (P::AReverse) > 0.5f;
    v.aHalf       = load (P::AHalf) > 0.5f;
    v.aDoubleTime = load (P::ADouble) > 0.5f;
    v.aStutter    = load (P::AStutter) > 0.5f;

    v.bReverse    = load (P::BReverse) > 0.5f;
    v.bHalf       = load (P::BHalf) > 0.5f;
    v.bDoubleTime = load (P::BDouble) > 0.5f;
    v.bStutter    = load (P::BStutter) > 0.5f;

    v.cReverse    = load (P::CReverse) > 0.5f;
    v.cHalf       = load (P::CHalf) > 0.5f;
    v.cDoubleTime = load (P::CDouble) > 0.5f;
    v.cStutter    = load (P::CStutter) > 0.5f;

    v.cookOn       = load (P::CookOn) > 0.5f;
    v.grainSizeMs  = load (P::GrainSize);
    v.grainSpray   = load (P::GrainSpray);
    v.grainPitch   = load (P::GrainPitch);
    v.grainStretch = load (P::GrainStretch);
    v.grainReverse = load (P::GrainReverse);
    v.grainDensity = load (P::GrainDensity);
    v.grainBurn    = load (P::GrainBurn);
    v.grainWidth   = load (P::GrainWidth);

    v.chopDepth   = load (P::ChopDepth);
    v.chopRate    = load (P::ChopRate);
    v.chopTriplet = load (P::ChopTriplet) > 0.5f;
    v.stutterRate = load (P::StutterRate);
    v.drip        = load (P::Drip);
    v.dripRate    = load (P::DripRate);

    v.octave     = juce::jlimit (-2, 2, (int) std::lround (load (P::Octave)));
    v.detune     = load (P::Detune);
    v.detuneRate = load (P::DetuneRate);
    v.formant    = load (P::Formant);
    v.wobble     = load (P::Wobble);
    v.vinyl      = load (P::Vinyl);
    v.air        = load (P::Air);
    v.ghost      = load (P::Ghost);

    v.filterType = juce::jlimit (0, (int) FilterMode::Basement, (int) std::lround (load (P::FilterType)));
    v.cutoff     = load (P::Cutoff);
    v.reso       = load (P::Reso);
    v.sweep      = load (P::Sweep);
    v.sweepRate  = load (P::SweepRate);

    v.fxPlay  = load (P::FxPlay) > 0.5f;
    v.fxSpeed = load (P::FxSpeed);

    v.aPlay   = load (P::APlay) > 0.5f;
    v.bPlay   = load (P::BPlay) > 0.5f;
    v.cPlay   = load (P::CPlay) > 0.5f;
    v.aBypass = load (P::ABypass) > 0.5f;
    v.bBypass = load (P::BBypass) > 0.5f;
    v.cBypass = load (P::CBypass) > 0.5f;

    v.aStart = load (P::AStart);
    v.aEnd   = load (P::AEnd);
    v.aLoop  = load (P::ALoop) > 0.5f;
    v.bStart = load (P::BStart);
    v.bEnd   = load (P::BEnd);
    v.bLoop  = load (P::BLoop) > 0.5f;
    v.cStart = load (P::CStart);
    v.cEnd   = load (P::CEnd);
    v.cLoop  = load (P::CLoop) > 0.5f;

    v.autoChop    = load (P::AutoChop) > 0.5f;
    v.chopDiv     = juce::jlimit (0, 6, (int) std::lround (load (P::ChopDiv)));
    v.rearrange   = load (P::Rearrange) > 0.5f;
    v.shuffleSeed = juce::jlimit (0, 999, (int) std::lround (load (P::ShuffleSeed)));
    v.bpm         = load (P::Bpm);

    v.eqOn      = load (P::EqOn) > 0.5f;
    v.eqLow     = load (P::EqLow);
    v.eqLowMid  = load (P::EqLowMid);
    v.eqMid     = load (P::EqMid);
    v.eqHighMid = load (P::EqHighMid);
    v.eqHigh    = load (P::EqHigh);

    v.rackRoute = juce::jlimit (0, 1, (int) std::lround (load (P::RackRoute)));
    v.rackMix   = load (P::RackMix);

    v.delayOn       = load (P::DelayOn) > 0.5f;
    v.delayTime     = load (P::DelayTime);
    v.delayFeedback = load (P::DelayFeedback);
    v.delayTone     = load (P::DelayTone);
    v.delayMix      = load (P::DelayMix);
    v.delayPingPong = load (P::DelayPingPong) > 0.5f;

    v.distOn    = load (P::DistOn) > 0.5f;
    v.distDrive = load (P::DistDrive);
    v.distTone  = load (P::DistTone);
    v.distMix   = load (P::DistMix);

    v.crushOn   = load (P::CrushOn) > 0.5f;
    v.crushBits = load (P::CrushBits);
    v.crushMix  = load (P::CrushMix);

    v.chorusOn    = load (P::ChorusOn) > 0.5f;
    v.chorusRate  = load (P::ChorusRate);
    v.chorusDepth = load (P::ChorusDepth);
    v.chorusMix   = load (P::ChorusMix);

    v.flangerOn       = load (P::FlangerOn) > 0.5f;
    v.flangerRate     = load (P::FlangerRate);
    v.flangerDepth    = load (P::FlangerDepth);
    v.flangerFeedback = load (P::FlangerFeedback);
    v.flangerMix      = load (P::FlangerMix);

    v.phaserOn    = load (P::PhaserOn) > 0.5f;
    v.phaserRate  = load (P::PhaserRate);
    v.phaserDepth = load (P::PhaserDepth);
    v.phaserMix   = load (P::PhaserMix);

    v.rackVerbOn   = load (P::RackVerbOn) > 0.5f;
    v.rackVerbSize = load (P::RackVerbSize);
    v.rackVerbMix  = load (P::RackVerbMix);

    v.keyRoot   = juce::jlimit (0, 11, (int) std::lround (load (P::KeyRoot)));
    v.scaleType = juce::jlimit (0, 1, (int) std::lround (load (P::ScaleType)));
    v.keyLock   = load (P::KeyLock) > 0.5f;

    v.autoTuneOn    = load (P::AutoTuneOn) > 0.5f;
    v.autoTuneSpeed = load (P::AutoTuneSpeed);
    v.autoTuneMix   = load (P::AutoTuneMix);
    v.autoTuneSnap  = load (P::AutoTuneSnap);

    return v;
}

void BtPOvenAudioProcessor::applyAutomation (Values& v, double phase) const
{
    using namespace btp;

    auto lane = [this, phase] (Lane l, float fallback) -> float
    {
        const float automated = fxRecorder.lookup ((int) l, phase);
        return std::isnan (automated) ? fallback : automated;
    };

    v.grainSizeMs  = lane (Lane::GrainSize,    v.grainSizeMs);
    v.grainSpray   = lane (Lane::GrainSpray,   v.grainSpray);
    v.grainPitch   = lane (Lane::GrainPitch,   v.grainPitch);
    v.grainStretch = lane (Lane::GrainStretch, v.grainStretch);
    v.grainDensity = lane (Lane::GrainDensity, v.grainDensity);
    v.grainBurn    = lane (Lane::GrainBurn,    v.grainBurn);

    v.chopDepth   = lane (Lane::ChopDepth, v.chopDepth);
    v.chopRate    = lane (Lane::ChopRate,  v.chopRate);
    v.stutterRate = lane (Lane::StutterRate, v.stutterRate);

    v.octave  = juce::jlimit (-2, 2, (int) std::lround (lane (Lane::Octave, (float) v.octave)));
    v.detune  = lane (Lane::Detune,  v.detune);
    v.wobble  = lane (Lane::Wobble,  v.wobble);
    v.formant = lane (Lane::Formant, v.formant);

    v.cutoff = lane (Lane::Cutoff, v.cutoff);
    v.reso   = lane (Lane::Reso,   v.reso);
    v.drip   = lane (Lane::Drip,   v.drip);
}

//==============================================================================
void BtPOvenAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused (midiMessages);

    const int numSamples = buffer.getNumSamples();
    const int numOut = buffer.getNumChannels();
    const int numIn = getTotalNumInputChannels();

    if (numSamples <= 0 || numOut <= 0)
        return;

    if (dryBuffer.getNumSamples() < numSamples)
        dryBuffer.setSize (2, numSamples, false, false, true);

    //==========================================================================
    Values v = readValues();

    const double phase = (double) automationPhase.load (std::memory_order_relaxed);

    if (v.fxPlay && fxRecorder.hasData())
        applyAutomation (v, phase);

    //==========================================================================
    // Copy the input (post input-gain) so we can overwrite the output buffer.
    inGainSm.setTargetValue (v.inGain);
    outGainSm.setTargetValue (v.outGain);

    const float* inL = numIn > 0 ? buffer.getReadPointer (0) : nullptr;
    const float* inR = numIn > 1 ? buffer.getReadPointer (1) : inL;

    float* dryL = dryBuffer.getWritePointer (0);
    float* dryR = dryBuffer.getWritePointer (1);

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = inGainSm.getNextValue();
        dryL[i] = (inL != nullptr ? inL[i] : 0.0f) * g;
        dryR[i] = (inR != nullptr ? inR[i] : 0.0f) * g;
    }

    //==========================================================================
    // Capture
    for (int li = 0; li < 3; ++li)
        if (loops[(size_t) li].isRecording())
            loops[(size_t) li].writeBlock (dryL, dryR, numSamples);

    //==========================================================================
    // Playback
    const bool contentA = loops[0].hasContent();
    const bool contentB = loops[1].hasContent();
    const bool contentC = loops[2].hasContent();
    const bool anyContent = contentA || contentB || contentC;
    const bool anyRecording = loops[0].isRecording() || loops[1].isRecording()
                           || loops[2].isRecording();

    btp::VoiceOptions opts[3];
    opts[0].reverse    = v.aReverse;
    opts[0].half       = v.aHalf;
    opts[0].doubleTime = v.aDoubleTime;
    opts[0].stutter    = v.aStutter;

    opts[1].reverse    = v.bReverse;
    opts[1].half       = v.bHalf;
    opts[1].doubleTime = v.bDoubleTime;
    opts[1].stutter    = v.bStutter;

    opts[2].reverse    = v.cReverse;
    opts[2].half       = v.cHalf;
    opts[2].doubleTime = v.cDoubleTime;
    opts[2].stutter    = v.cStutter;

    opts[0].start = v.aStart;
    opts[0].end   = v.aEnd;
    opts[0].looping = v.aLoop;
    opts[1].start = v.bStart;
    opts[1].end   = v.bEnd;
    opts[1].looping = v.bLoop;
    opts[2].start = v.cStart;
    opts[2].end   = v.cEnd;
    opts[2].looping = v.cLoop;

    for (int li = 0; li < 3; ++li)
    {
        opts[li].autoChop    = v.autoChop;
        opts[li].chopDiv     = v.chopDiv;
        opts[li].rearrange   = v.rearrange;
        opts[li].shuffleSeed = v.shuffleSeed;
        opts[li].bpm         = v.bpm;
    }

    btp::Values loopValues[3] = { v, v, v };

    // Bypass keeps the loop audible but skips its cook / timing / pitch tricks.
    auto bypassLoop = [] (btp::VoiceOptions& o, btp::Values& lv)
    {
        o.reverse = o.half = o.doubleTime = o.stutter = false;
        o.autoChop = o.rearrange = false;
        lv.cookOn = false;
        lv.chopDepth = 0.0f;
        lv.drip = 0.0f;
        lv.wobble = 0.0f;
        lv.detune = 0.0f;
        lv.ghost = 0.0f;
        lv.octave = 0;
        lv.grainStretch = 1.0f;
    };

    if (v.aBypass)
        bypassLoop (opts[0], loopValues[0]);

    if (v.bBypass)
        bypassLoop (opts[1], loopValues[1]);

    if (v.cBypass)
        bypassLoop (opts[2], loopValues[2]);

    const bool playA = v.playing && v.aPlay && contentA;
    const bool playB = v.playing && v.bPlay && contentB;
    const bool playC = v.playing && v.cPlay && contentC;

    int numPlaying = (playA ? 1 : 0) + (playB ? 1 : 0) + (playC ? 1 : 0);

    loopGainA.setTargetValue (playA ? 1.0f : 0.0f);
    loopGainB.setTargetValue (playB ? 1.0f : 0.0f);
    loopGainC.setTargetValue (playC ? 1.0f : 0.0f);

    const float bothScale = numPlaying >= 3 ? 0.60f : (numPlaying == 2 ? 0.72f : 1.0f);

    float* outL = buffer.getWritePointer (0);
    float* outR = numOut > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        float aL = 0.0f, aR = 0.0f, bL = 0.0f, bR = 0.0f, cL = 0.0f, cR = 0.0f;

        if (contentA)
            voices[0].renderNext (loops[0], loopValues[0], opts[0], currentSampleRate, aL, aR);

        if (contentB)
            voices[1].renderNext (loops[1], loopValues[1], opts[1], currentSampleRate, bL, bR);

        if (contentC)
            voices[2].renderNext (loops[2], loopValues[2], opts[2], currentSampleRate, cL, cR);

        const float ga = loopGainA.getNextValue() * bothScale;
        const float gb = loopGainB.getNextValue() * bothScale;
        const float gc = loopGainC.getNextValue() * bothScale;

        float l = aL * ga + bL * gb + cL * gc;
        float r = aR * ga + bR * gb + cR * gc;

        if (! anyContent)
        {
            l = dryL[i];
            r = dryR[i];
        }
        else if (anyRecording)
        {
            l += dryL[i] * 0.6f;
            r += dryR[i] * 0.6f;
        }

        outL[i] = l;

        if (outR != nullptr)
            outR[i] = r;
    }

    //==========================================================================
    voiceFX.processBlock (buffer, v, currentSampleRate);
    rackFX.processBlock (buffer, v, currentSampleRate);
    autoTune.processBlock (buffer, v, currentSampleRate);

    //==========================================================================
    // Output gain, soft clip, metering
    float peak = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = outGainSm.getNextValue();
        const float l = softClip (outL[i] * g);
        const float r = outR != nullptr ? softClip (outR[i] * g) : 0.0f;

        outL[i] = l;

        if (outR != nullptr)
            outR[i] = r;

        peak = juce::jmax (peak, std::abs (l), std::abs (r));
    }

    const float previous = outputLevel.load (std::memory_order_relaxed);
    outputLevel.store (juce::jmax (peak, previous * 0.82f), std::memory_order_relaxed);

    //==========================================================================
    // Automation transport
    const double cycleSeconds = activeLoopLengthSeconds();
    const double lengthForAutomation = cycleSeconds > 0.1 ? cycleSeconds : 4.0;
    const float speed = juce::jlimit (0.1f, 4.0f, v.fxSpeed);

    autoPhase += ((double) numSamples / (lengthForAutomation * currentSampleRate)) * (double) speed;

    while (autoPhase >= 1.0)
        autoPhase -= 1.0;

    automationPhase.store ((float) autoPhase, std::memory_order_relaxed);
}

//==============================================================================
void BtPOvenAudioProcessor::timerCallback()
{
    pollAiEngine();
    pollStemEngine();
    pollRvcEngine();
   #if BTP_HAS_RAVE
    pollRaveEngine();
   #endif

    if (! fxRecorder.isRecording())
        return;

    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;

    if (fxRecordStartTime < 0.0)
        fxRecordStartTime = now;

    const double loopLength = activeLoopLengthSeconds();
    const double cycle = loopLength > 0.15 ? loopLength : 4.0;
    const double elapsed = now - fxRecordStartTime;

    if (elapsed >= cycle)
    {
        stopFxRecording();
        return;
    }

    const double phase = juce::jlimit (0.0, 1.0, elapsed / cycle);

    for (int lane = 0; lane < btp::numLanes; ++lane)
    {
        const auto p = btp::paramForLane (lane);
        const auto* a = rawParams[(size_t) p];
        float value = a != nullptr ? a->load (std::memory_order_relaxed) : 0.0f;

        if (p == btp::P::Octave)
            value = std::round (value);

        fxRecorder.capture (lane, value, phase);
    }

    fxProgress.store ((float) phase, std::memory_order_relaxed);
}

//==============================================================================
void BtPOvenAudioProcessor::pollAiEngine()
{
    auto& engine = AiEngine::getInstance();

    AiEngine::Result result;

    if (! engine.takeResult (result))
        return;

    if (result.kind == AiEngine::Kind::OneShot)
    {
        lastOneShot = result.audio;
        oneShotSeconds = result.seconds;
        oneShotPrompt = result.prompt;
        setStatusMessage ("ONE-SHOT READY  /  " + juce::String (result.seconds, 1) + " s");
        return;
    }

    lastAiAudio = result.audio;
    aiResultSeconds = result.seconds;
    aiResultSeed = result.seed;
    aiResultPrompt = result.prompt;
    aiResultLyrics = result.lyrics;

    if (loadBufferIntoLoop (2, lastAiAudio, 48000.0))
    {
        const auto lyricHint = aiResultLyrics.isNotEmpty()
            ? "  /  " + aiResultLyrics.replace ("\n", " ").substring (0, 40)
            : juce::String();

        setStatusMessage ("AI TAKE LANDED IN LOOP C" + lyricHint);
    }
    else
        setStatusMessage ("AI TAKE READY - SEND TO A / B / C");
}

bool BtPOvenAudioProcessor::sendOneShotToLoop (int loopIndex)
{
    if (lastOneShot.getNumSamples() < 64)
    {
        setStatusMessage ("NO ONE-SHOT TO SEND YET");
        return false;
    }

    if (! loadBufferIntoLoop (loopIndex, lastOneShot, 48000.0))
        return false;

    setStatusMessage ("ONE-SHOT SENT TO LOOP " + loopLabel (loopIndex));
    return true;
}

bool BtPOvenAudioProcessor::sendAiResultToLoop (int loopIndex)
{
    if (lastAiAudio.getNumSamples() < 64)
    {
        setStatusMessage ("NO AI TAKE TO SEND YET");
        return false;
    }

    if (! loadBufferIntoLoop (loopIndex, lastAiAudio, 48000.0))
        return false;

    setStatusMessage ("AI TAKE SENT TO LOOP " + loopLabel (loopIndex)
                      + "  /  " + juce::String (aiResultSeconds, 1) + " s");
    return true;
}

//==============================================================================
void BtPOvenAudioProcessor::pollStemEngine()
{
    auto& engine = StemEngine::getInstance();

    StemEngine::Result result;

    if (! engine.takeResult (result))
        return;

    lastStems = result.stems;
    stemSeconds = result.seconds;

    setStatusMessage ("STEMS READY  /  VOCALS, DRUMS, BASS, OTHER  /  "
                      + juce::String (result.seconds, 1) + " s");
}

void BtPOvenAudioProcessor::startSeparation (const juce::AudioBuffer<float>& audio,
                                             double sourceSampleRate)
{
    StemEngine::getInstance().run (audio, sourceSampleRate);
    setStatusMessage ("SEPARATING STEMS - KEEP THE PLUGIN OPEN");
}

bool BtPOvenAudioProcessor::sendStemToLoop (int stemIndex, int loopIndex)
{
    stemIndex = juce::jlimit (0, 3, stemIndex);

    const auto& stem = lastStems[(size_t) stemIndex];

    if (stem.getNumSamples() < 64)
    {
        setStatusMessage ("NO STEM TO SEND YET");
        return false;
    }

    static const char* names[] = { "DRUMS", "BASS", "OTHER", "VOCALS" };

    if (! loadBufferIntoLoop (loopIndex, stem, 44100.0))
        return false;

    setStatusMessage (juce::String (names[stemIndex]) + " SENT TO LOOP " + loopLabel (loopIndex));
    return true;
}

//==============================================================================
void BtPOvenAudioProcessor::pollRvcEngine()
{
    auto& engine = RvcEngine::getInstance();

    RvcEngine::Result result;

    if (! engine.takeResult (result))
        return;

    lastRvcAudio = result.audio;
    rvcSeconds = result.seconds;

    setStatusMessage ("VOICE CONVERTED  /  " + juce::String (result.seconds, 1) + " s");
}

void BtPOvenAudioProcessor::startVoiceConversion (const juce::AudioBuffer<float>& audio,
                                                  double sourceSampleRate)
{
    RvcEngine::getInstance().convert (audio, sourceSampleRate);
    setStatusMessage ("CONVERTING VOICE...");
}

bool BtPOvenAudioProcessor::sendVoiceToLoop (int loopIndex)
{
    if (lastRvcAudio.getNumSamples() < 64)
    {
        setStatusMessage ("NO VOICE TAKE TO SEND YET");
        return false;
    }

    if (! loadBufferIntoLoop (loopIndex, lastRvcAudio, currentSampleRate))
        return false;

    setStatusMessage ("VOICE SENT TO LOOP " + loopLabel (loopIndex));
    return true;
}

//==============================================================================
void BtPOvenAudioProcessor::pollRaveEngine()
{
   #if BTP_HAS_RAVE
    auto& engine = RaveEngine::getInstance();

    RaveEngine::Result result;

    if (! engine.takeResult (result))
        return;

    lastRaveAudio = result.audio;
    raveSeconds = result.seconds;

    setStatusMessage ("RAVE VARIANT READY  /  " + juce::String (result.seconds, 1) + " s");
   #endif
}

void BtPOvenAudioProcessor::mutateLastOneShot (float variation)
{
   #if BTP_HAS_RAVE
    if (lastOneShot.getNumSamples() < 1024)
    {
        setStatusMessage ("GENERATE A ONE-SHOT FIRST");
        return;
    }

    RaveEngine::getInstance().mutate (lastOneShot, 48000.0, variation,
                                      (juce::int64) juce::Time::getCurrentTime().toMilliseconds());
    setStatusMessage ("MUTATING...");
   #else
    juce::ignoreUnused (variation);
   #endif
}

bool BtPOvenAudioProcessor::sendRaveToLoop (int loopIndex)
{
   #if BTP_HAS_RAVE
    if (lastRaveAudio.getNumSamples() < 64)
    {
        setStatusMessage ("NO VARIANT TO SEND YET");
        return false;
    }

    if (! loadBufferIntoLoop (loopIndex, lastRaveAudio, 48000.0))
        return false;

    setStatusMessage ("VARIANT SENT TO LOOP " + loopLabel (loopIndex));
    return true;
   #else
    juce::ignoreUnused (loopIndex);
    return false;
   #endif
}

//==============================================================================
void BtPOvenAudioProcessor::beginRecord (int loopIndex, bool overdub)
{
    if (loopIndex < 0 || loopIndex > 2)
        return;

    loops[(size_t) loopIndex].startRecording (overdub);
    voices[(size_t) loopIndex].reset();

    if (auto* play = apvts.getParameter (btp::keyOf (btp::P::Playing)))
        play->setValueNotifyingHost (1.0f);

    apvts.getParameter (btp::keyOf (btp::P::ActiveLoop))
         ->setValueNotifyingHost (loopIndex / 2.0f);

    setStatusMessage (overdub ? "OVERDUBBING LOOP " + loopLabel (loopIndex)
                              : "RECORDING LOOP " + loopLabel (loopIndex));
}

void BtPOvenAudioProcessor::stopRecord (int loopIndex)
{
    if (loopIndex < 0 || loopIndex > 2)
        return;

    auto& loop = loops[(size_t) loopIndex];
    const bool wasOverdub = loop.isOverdub();
    loop.stopRecording();
    voices[(size_t) loopIndex].reset();

    if (! loop.hasContent())
        setStatusMessage ("LOOP TOO SHORT - TRY AGAIN");
    else
        setStatusMessage ((wasOverdub ? "OVERDUB SAVED - " : "LOOP CAPTURED - ")
                          + juce::String (loop.getLengthSeconds(), 2) + " s");
}

void BtPOvenAudioProcessor::clearLoop (int loopIndex)
{
    if (loopIndex < 0 || loopIndex > 2)
        return;

    loops[(size_t) loopIndex].clear();
    voices[(size_t) loopIndex].reset();
    setStatusMessage ("LOOP " + loopLabel (loopIndex) + " CLEARED");
}

bool BtPOvenAudioProcessor::loadFileIntoLoop (int loopIndex, const juce::File& file)
{
    if (loopIndex < 0 || loopIndex > 2)
        return false;

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));

    if (reader == nullptr)
    {
        setStatusMessage ("COULD NOT READ FILE");
        return false;
    }

    const juce::int64 maxSourceSamples = (juce::int64) (currentSampleRate * btp::LoopBuffer::maxLoopSeconds);
    const bool wasTrimmed = reader->lengthInSamples > maxSourceSamples;
    const int numSamples = (int) juce::jmin (reader->lengthInSamples, maxSourceSamples);

    if (numSamples < 64)
    {
        setStatusMessage ("FILE TOO SHORT");
        return false;
    }

    juce::AudioBuffer<float> temp ((int) reader->numChannels, numSamples);
    reader->read (&temp, 0, numSamples, 0, true, true);

    if (! loops[(size_t) loopIndex].loadFromBuffer (temp, reader->sampleRate))
    {
        setStatusMessage ("FILE COULD NOT BE LOADED");
        return false;
    }

    voices[(size_t) loopIndex].reset();

    apvts.getParameter (btp::keyOf (btp::P::ActiveLoop))
         ->setValueNotifyingHost (loopIndex / 2.0f);

    apvts.getParameter (btp::keyOf (btp::P::Playing))->setValueNotifyingHost (1.0f);

    setStatusMessage (juce::String (wasTrimmed ? "LOADED (TRIMMED TO 60 S) " : "LOADED ")
                      + file.getFileName() + "  ->  LOOP "
                      + loopLabel (loopIndex));
    return true;
}

bool BtPOvenAudioProcessor::loadBufferIntoLoop (int loopIndex,
                                                const juce::AudioBuffer<float>& audio,
                                                double sourceSampleRate)
{
    if (loopIndex < 0 || loopIndex > 2)
        return false;

    if (audio.getNumSamples() < 64 || audio.getNumChannels() < 1)
    {
        setStatusMessage ("AI TAKE WAS EMPTY");
        return false;
    }

    if (! loops[(size_t) loopIndex].loadFromBuffer (audio, sourceSampleRate))
    {
        setStatusMessage ("COULD NOT LOAD AI TAKE");
        return false;
    }

    voices[(size_t) loopIndex].reset();

    apvts.getParameter (btp::keyOf (btp::P::ActiveLoop))
         ->setValueNotifyingHost (loopIndex / 2.0f);

    apvts.getParameter (btp::keyOf (btp::P::Playing))->setValueNotifyingHost (1.0f);

    return true;
}

bool BtPOvenAudioProcessor::getLoopAudio (int loopIndex, juce::AudioBuffer<float>& out,
                                          double& outSampleRate) const
{
    if (loopIndex < 0 || loopIndex > 2)
        return false;

    const auto& loop = loops[(size_t) loopIndex];
    const int len = loop.getLength();

    if (len < 64)
        return false;

    out.setSize (2, len, false, true, false);
    out.copyFrom (0, 0, loop.getChannelData (0), len);
    out.copyFrom (1, 0, loop.getChannelData (1), len);

    outSampleRate = loop.getSampleRate();
    return true;
}

bool BtPOvenAudioProcessor::exportLoop (int loopIndex, const juce::File& file)
{
    if (loopIndex < 0 || loopIndex > 2)
        return false;

    auto& loop = loops[(size_t) loopIndex];
    loop.stopRecording();

    if (! loop.hasContent())
    {
        setStatusMessage ("NOTHING TO EXPORT IN LOOP " + loopLabel (loopIndex));
        return false;
    }

    if (! loop.exportToFile (file))
    {
        setStatusMessage ("EXPORT FAILED");
        return false;
    }

    setStatusMessage ("EXPORTED " + file.getFileName());
    return true;
}

bool BtPOvenAudioProcessor::isLoopRecording (int loopIndex) const
{
    return loopIndex >= 0 && loopIndex <= 2 && loops[(size_t) loopIndex].isRecording();
}

bool BtPOvenAudioProcessor::isLoopOverdubbing (int loopIndex) const
{
    return loopIndex >= 0 && loopIndex <= 2 && loops[(size_t) loopIndex].isOverdub();
}

bool BtPOvenAudioProcessor::hasLoopContent (int loopIndex) const
{
    return loopIndex >= 0 && loopIndex <= 2 && loops[(size_t) loopIndex].hasContent();
}

double BtPOvenAudioProcessor::getLoopLengthSeconds (int loopIndex) const
{
    return (loopIndex >= 0 && loopIndex <= 2) ? loops[(size_t) loopIndex].getLengthSeconds() : 0.0;
}

float BtPOvenAudioProcessor::getWaveformPeak (int loopIndex, int point, int numPoints) const
{
    if (loopIndex < 0 || loopIndex > 2)
        return 0.0f;

    return loops[(size_t) loopIndex].getPeak (point, numPoints);
}

float BtPOvenAudioProcessor::getPlayheadPhase (int loopIndex) const
{
    return (loopIndex >= 0 && loopIndex <= 2) ? voices[(size_t) loopIndex].getPhase() : 0.0f;
}

//==============================================================================
void BtPOvenAudioProcessor::startFxRecording()
{
    fxRecorder.startRecording();
    fxRecordStartTime = -1.0;
    fxProgress.store (0.0f);

    if (auto* p = apvts.getParameter (btp::keyOf (btp::P::FxPlay)))
        p->setValueNotifyingHost (0.0f);

    setStatusMessage ("FX ARMED - TWIST KNOBS FOR ONE LOOP PASS");
}

void BtPOvenAudioProcessor::stopFxRecording()
{
    fxRecorder.stopRecording();
    fxRecordStartTime = -1.0;
    fxProgress.store (0.0f);
    setStatusMessage ("GESTURE CAPTURED - " + juce::String (fxRecorder.getTotalPoints()) + " POINTS");
}

void BtPOvenAudioProcessor::clearFx()
{
    fxRecorder.clear();
    fxProgress.store (0.0f);

    if (auto* p = apvts.getParameter (btp::keyOf (btp::P::FxPlay)))
        p->setValueNotifyingHost (0.0f);

    setStatusMessage ("FX GESTURES CLEARED");
}

void BtPOvenAudioProcessor::setStatusMessage (const juce::String& text)
{
    statusMessage = text;
    statusTime = juce::Time::getMillisecondCounterHiRes() * 0.001;
}

//==============================================================================
void BtPOvenAudioProcessor::nuke()
{
    using namespace btp;

    juce::Random rng ((juce::int64) (juce::Time::getMillisecondCounterHiRes() * 1000.0)
                      ^ (juce::int64) juce::Random::getSystemRandom().nextInt64());

    auto setValue = [this] (P p, float value)
    {
        if (auto* param = apvts.getParameter (keyOf (p)))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    };

    auto setBool = [&setValue] (P p, bool value) { setValue (p, value ? 1.0f : 0.0f); };
    auto chance = [&rng] (float probability) { return rng.nextFloat() < probability; };
    auto between = [&rng] (float a, float b) { return a + (b - a) * rng.nextFloat(); };

    // Key / scale -----------------------------------------------------------
    const int root = rng.nextInt (12);
    const int scale = rng.nextInt (2);
    setValue (P::KeyRoot, (float) root);
    setValue (P::ScaleType, (float) scale);
    setBool (P::KeyLock, chance (0.55f));

    // Auto-tune -------------------------------------------------------------
    setBool (P::AutoTuneOn, chance (0.35f));
    setValue (P::AutoTuneSpeed, between (2.0f, 60.0f));
    setValue (P::AutoTuneMix, between (0.4f, 1.0f));
    setValue (P::AutoTuneSnap, between (0.5f, 1.0f));

    // Both loops can play; sometimes one is parked. --------------------------
    const bool useA = chance (0.82f);
    const bool useB = chance (0.82f);

    setBool (P::APlay, useA || ! useB);
    setBool (P::BPlay, useB || ! useA);
    setBool (P::ABypass, chance (0.22f));
    setBool (P::BBypass, chance (0.22f));
    setBool (P::AReverse, chance (0.22f));
    setBool (P::AHalf, chance (0.18f));
    setBool (P::ADouble, chance (0.18f));
    setBool (P::AStutter, chance (0.18f));
    setBool (P::BReverse, chance (0.22f));
    setBool (P::BHalf, chance (0.18f));
    setBool (P::BDouble, chance (0.18f));
    setBool (P::BStutter, chance (0.18f));
    setBool (P::ALoop, chance (0.9f));
    setBool (P::BLoop, chance (0.9f));

    // Loop C (AI take) — randomised exactly like A/B, but nuke NEVER touches
    // the prompt, the dataset or the generator itself.
    setBool (P::CPlay, true);
    setBool (P::CBypass, chance (0.22f));
    setBool (P::CReverse, chance (0.22f));
    setBool (P::CHalf, chance (0.18f));
    setBool (P::CDouble, chance (0.18f));
    setBool (P::CStutter, chance (0.18f));
    setBool (P::CLoop, chance (0.9f));

    // Cook -----------------------------------------------------------------
    setBool (P::CookOn, chance (0.72f));
    setValue (P::GrainSize, between (18.0f, 230.0f));
    setValue (P::GrainSpray, between (0.0f, 0.65f));
    setValue (P::GrainPitch, chance (0.5f) ? (float) (2 * rng.nextInt (4)) : between (0.0f, 7.0f));
    setValue (P::GrainStretch, between (0.5f, 2.2f));
    setValue (P::GrainReverse, between (0.0f, 0.75f));
    setValue (P::GrainDensity, between (0.2f, 0.95f));
    setValue (P::GrainBurn, between (0.0f, 0.55f));
    setValue (P::GrainWidth, between (0.15f, 1.0f));

    // Trap timing -----------------------------------------------------------
    setValue (P::ChopDepth, chance (0.6f) ? between (0.0f, 0.6f) : 0.0f);
    setValue (P::ChopRate, (float) (2 + rng.nextInt (15)));
    setBool (P::ChopTriplet, chance (0.3f));
    setValue (P::StutterRate, (float) (2 + rng.nextInt (15)));
    setValue (P::Drip, chance (0.55f) ? between (0.0f, 0.55f) : 0.0f);
    setValue (P::DripRate, between (0.08f, 1.2f));

    setBool (P::AutoChop, chance (0.6f));
    setValue (P::ChopDiv, (float) rng.nextInt (7));
    setBool (P::Rearrange, chance (0.55f));
    setValue (P::ShuffleSeed, (float) rng.nextInt (1000));
    setValue (P::Bpm, between (120.0f, 160.0f));

    // Pitch + texture -------------------------------------------------------
    setValue (P::Octave, (float) (rng.nextInt (3) - 1));
    setValue (P::Detune, between (0.0f, 32.0f));
    setValue (P::DetuneRate, between (0.15f, 3.2f));
    setValue (P::Formant, between (-7.0f, 7.0f));
    setValue (P::Wobble, between (0.0f, 0.5f));
    setValue (P::Vinyl, between (0.0f, 0.35f));
    setValue (P::Air, between (0.0f, 0.35f));
    setValue (P::Ghost, between (0.0f, 0.55f));

    // Filters ---------------------------------------------------------------
    setValue (P::FilterType, chance (0.22f) ? 0.0f : (float) (1 + rng.nextInt (8)));
    setValue (P::Cutoff, 300.0f * std::pow (40.0f, rng.nextFloat()));
    setValue (P::Reso, between (0.1f, 0.82f));
    setValue (P::Sweep, between (0.0f, 0.5f));
    setValue (P::SweepRate, between (0.08f, 1.4f));

    // Five-band EQ ----------------------------------------------------------
    setBool (P::EqOn, chance (0.5f));
    setValue (P::EqLow, between (-7.0f, 7.0f));
    setValue (P::EqLowMid, between (-7.0f, 7.0f));
    setValue (P::EqMid, between (-7.0f, 7.0f));
    setValue (P::EqHighMid, between (-7.0f, 7.0f));
    setValue (P::EqHigh, between (-7.0f, 7.0f));

    // Full rack -------------------------------------------------------------
    setValue (P::RackRoute, (float) rng.nextInt (2));
    setValue (P::RackMix, between (0.65f, 1.0f));

    setBool (P::DelayOn, chance (0.6f));
    setValue (P::DelayTime, between (0.08f, 0.75f));
    setValue (P::DelayFeedback, between (0.18f, 0.68f));
    setValue (P::DelayTone, between (0.25f, 0.85f));
    setValue (P::DelayMix, between (0.12f, 0.45f));
    setBool (P::DelayPingPong, chance (0.6f));

    setBool (P::DistOn, chance (0.55f));
    setValue (P::DistDrive, between (0.1f, 0.7f));
    setValue (P::DistTone, between (0.2f, 0.85f));
    setValue (P::DistMix, between (0.3f, 0.8f));

    setBool (P::CrushOn, chance (0.4f));
    setValue (P::CrushBits, (float) (4 + rng.nextInt (9)));
    setValue (P::CrushMix, between (0.15f, 0.5f));

    setBool (P::ChorusOn, chance (0.5f));
    setValue (P::ChorusRate, between (0.2f, 2.5f));
    setValue (P::ChorusDepth, between (0.25f, 0.85f));
    setValue (P::ChorusMix, between (0.2f, 0.6f));

    setBool (P::FlangerOn, chance (0.45f));
    setValue (P::FlangerRate, between (0.1f, 1.4f));
    setValue (P::FlangerDepth, between (0.25f, 0.9f));
    setValue (P::FlangerFeedback, between (0.2f, 0.7f));
    setValue (P::FlangerMix, between (0.2f, 0.65f));

    setBool (P::PhaserOn, chance (0.4f));
    setValue (P::PhaserRate, between (0.1f, 1.8f));
    setValue (P::PhaserDepth, between (0.3f, 0.95f));
    setValue (P::PhaserMix, between (0.25f, 0.7f));

    setBool (P::RackVerbOn, chance (0.6f));
    setValue (P::RackVerbSize, between (0.3f, 0.9f));
    setValue (P::RackVerbMix, between (0.12f, 0.4f));

    setStatusMessage ("NUKED - NEW RECIPE  /  " + juce::String (noteName (root)) + " "
                      + (scale == 0 ? "MAJOR" : "MINOR"));
}

//==============================================================================
void BtPOvenAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("btp_oven_version", 1, nullptr);

    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void BtPOvenAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);

    if (tree.isValid())
        apvts.replaceState (tree);
}

//==============================================================================
juce::AudioProcessorEditor* BtPOvenAudioProcessor::createEditor()
{
    return new BtPOvenAudioProcessorEditor (*this);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BtPOvenAudioProcessor();
}

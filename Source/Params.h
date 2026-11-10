#pragma once

#include <juce_core/juce_core.h>

namespace btp
{

//==============================================================================
/** Parameter ids, in the same order as kParamKeys. */
enum class P : int
{
    InGain = 0, OutGain, ActiveLoop, Playing,
    AReverse, AHalf, ADouble, AStutter,
    BReverse, BHalf, BDouble, BStutter,
    CookOn, GrainSize, GrainSpray, GrainPitch, GrainStretch, GrainReverse,
    GrainDensity, GrainBurn, GrainWidth,
    ChopDepth, ChopRate, ChopTriplet, StutterRate, Drip, DripRate,
    Octave, Detune, DetuneRate, Formant, Wobble, Vinyl, Air, Ghost,
    FilterType, Cutoff, Reso, Sweep, SweepRate,
    FxPlay, FxSpeed,

    // Dual-loop mix / region ------------------------------------------------
    APlay, BPlay, ABypass, BBypass,
    AStart, AEnd, ALoop,
    BStart, BEnd, BLoop,

    // Auto chop / rearrange -------------------------------------------------
    AutoChop, ChopDiv, Rearrange, ShuffleSeed, Bpm, BpmSync,

    // Five-band EQ ----------------------------------------------------------
    EqOn, EqLow, EqLowMid, EqMid, EqHighMid, EqHigh,

    // Full FX rack ----------------------------------------------------------
    RackRoute, RackMix,
    DelayOn, DelayTime, DelayFeedback, DelayTone, DelayMix, DelayPingPong,
    DistOn, DistDrive, DistTone, DistMix,
    CrushOn, CrushBits, CrushMix,
    ChorusOn, ChorusRate, ChorusDepth, ChorusMix,
    FlangerOn, FlangerRate, FlangerDepth, FlangerFeedback, FlangerMix,
    PhaserOn, PhaserRate, PhaserDepth, PhaserMix,
    RackVerbOn, RackVerbSize, RackVerbMix,

    // Key / scale -----------------------------------------------------------
    KeyRoot, ScaleType, KeyLock,

    // Auto-tune (monophonic, key + scale locked) -----------------------------
    AutoTuneOn, AutoTuneSpeed, AutoTuneMix, AutoTuneSnap,

    // Loop C (AI take) -------------------------------------------------------
    CPlay, CBypass,
    CStart, CEnd, CLoop,
    CReverse, CHalf, CDouble, CStutter,

    Count
};

inline constexpr int numParams = (int) P::Count;

inline constexpr const char* kParamKeys[] =
{
    "in_gain", "out_gain", "active_loop", "playing",
    "a_reverse", "a_half", "a_double", "a_stutter",
    "b_reverse", "b_half", "b_double", "b_stutter",
    "cook_on", "grain_size", "grain_spray", "grain_pitch", "grain_stretch",
    "grain_reverse", "grain_density", "grain_burn", "grain_width",
    "chop_depth", "chop_rate", "chop_triplet", "stutter_rate", "drip", "drip_rate",
    "octave", "detune", "detune_rate", "formant", "wobble", "vinyl", "air", "ghost",
    "filter_type", "cutoff", "reso", "sweep", "sweep_rate",
    "fx_play", "fx_speed",

    "a_play", "b_play", "a_bypass", "b_bypass",
    "a_start", "a_end", "a_loop",
    "b_start", "b_end", "b_loop",

    "autochop", "chop_div", "rearrange", "shuffle_seed", "bpm", "bpm_sync",

    "eq_on", "eq_low", "eq_low_mid", "eq_mid", "eq_high_mid", "eq_high",

    "rack_route", "rack_mix",
    "delay_on", "delay_time", "delay_feedback", "delay_tone", "delay_mix", "delay_pingpong",
    "dist_on", "dist_drive", "dist_tone", "dist_mix",
    "crush_on", "crush_bits", "crush_mix",
    "chorus_on", "chorus_rate", "chorus_depth", "chorus_mix",
    "flanger_on", "flanger_rate", "flanger_depth", "flanger_feedback", "flanger_mix",
    "phaser_on", "phaser_rate", "phaser_depth", "phaser_mix",
    "rack_verb_on", "rack_verb_size", "rack_verb_mix",

    "key_root", "scale_type", "key_lock",

    "autotune_on", "autotune_speed", "autotune_mix", "autotune_snap",

    "c_play", "c_bypass",
    "c_start", "c_end", "c_loop",
    "c_reverse", "c_half", "c_double", "c_stutter"
};

static_assert ((int) (sizeof (kParamKeys) / sizeof (kParamKeys[0])) == numParams,
               "kParamKeys must stay in sync with enum P");

inline const char* keyOf (P p) noexcept { return kParamKeys[(int) p]; }

//==============================================================================
/** A plain snapshot of every DSP-relevant parameter, taken once per block. */
struct Values
{
    float inGain        = 1.0f;
    float outGain       = 0.9f;
    int   activeLoop    = 0;
    bool  playing       = true;

    bool  aReverse      = false;
    bool  aHalf         = false;
    bool  aDoubleTime   = false;
    bool  aStutter      = false;
    bool  bReverse      = false;
    bool  bHalf         = false;
    bool  bDoubleTime   = false;
    bool  bStutter      = false;

    bool  cookOn        = false;
    float grainSizeMs   = 90.0f;
    float grainSpray    = 0.15f;
    float grainPitch    = 2.0f;
    float grainStretch  = 1.0f;
    float grainReverse  = 0.0f;
    float grainDensity  = 0.5f;
    float grainBurn     = 0.0f;
    float grainWidth    = 0.6f;

    float chopDepth     = 0.0f;
    float chopRate      = 8.0f;
    bool  chopTriplet   = false;
    float stutterRate   = 8.0f;
    float drip          = 0.0f;
    float dripRate      = 0.25f;

    int   octave        = 0;
    float detune        = 0.0f;   // cents
    float detuneRate    = 0.6f;   // Hz
    float formant       = 0.0f;   // semitones
    float wobble        = 0.0f;
    float vinyl         = 0.0f;
    float air           = 0.0f;
    float ghost         = 0.0f;

    int   filterType    = 0;
    float cutoff        = 4000.0f;
    float reso          = 0.35f;
    float sweep         = 0.0f;
    float sweepRate     = 0.3f;

    bool  fxPlay        = false;
    float fxSpeed       = 1.0f;

    // Loop mix / region -----------------------------------------------------
    bool  aPlay         = true;
    bool  bPlay         = true;
    bool  aBypass       = false;
    bool  bBypass       = false;
    float aStart        = 0.0f;
    float aEnd          = 1.0f;
    bool  aLoop         = true;
    float bStart        = 0.0f;
    float bEnd          = 1.0f;
    bool  bLoop         = true;

    // Auto chop / rearrange --------------------------------------------------
    bool  autoChop      = false;
    int   chopDiv       = 2;
    bool  rearrange     = false;
    int   shuffleSeed   = 1;
    float bpm           = 140.0f;
    bool  bpmSync       = true;   // follow the host tempo when it is available

    // Five-band EQ -----------------------------------------------------------
    bool  eqOn          = false;
    float eqLow         = 0.0f;
    float eqLowMid      = 0.0f;
    float eqMid         = 0.0f;
    float eqHighMid     = 0.0f;
    float eqHigh        = 0.0f;

    // Full FX rack -----------------------------------------------------------
    int   rackRoute     = 0;      // 0 = series, 1 = parallel/send
    float rackMix       = 1.0f;

    bool  delayOn       = false;
    float delayTime     = 0.35f;
    float delayFeedback = 0.35f;
    float delayTone     = 0.55f;
    float delayMix      = 0.25f;
    bool  delayPingPong = false;

    bool  distOn        = false;
    float distDrive     = 0.35f;
    float distTone      = 0.5f;
    float distMix       = 0.6f;

    bool  crushOn       = false;
    float crushBits     = 8.0f;
    float crushMix      = 0.5f;

    bool  chorusOn      = false;
    float chorusRate    = 0.8f;
    float chorusDepth   = 0.5f;
    float chorusMix     = 0.35f;

    bool  flangerOn     = false;
    float flangerRate   = 0.3f;
    float flangerDepth  = 0.6f;
    float flangerFeedback = 0.45f;
    float flangerMix    = 0.4f;

    bool  phaserOn      = false;
    float phaserRate    = 0.5f;
    float phaserDepth   = 0.7f;
    float phaserMix     = 0.5f;

    bool  rackVerbOn    = false;
    float rackVerbSize  = 0.6f;
    float rackVerbMix   = 0.25f;

    int   keyRoot       = 0;      // 0 = C
    int   scaleType     = 0;      // 0 = major, 1 = minor
    bool  keyLock       = false;

    bool  autoTuneOn    = false;
    float autoTuneSpeed = 8.0f;   // retune time, ms
    float autoTuneMix   = 1.0f;
    float autoTuneSnap  = 1.0f;   // 0 = untouched, 1 = hard snap

    // Loop C (AI take) -------------------------------------------------------
    bool  cReverse      = false;
    bool  cHalf         = false;
    bool  cDoubleTime   = false;
    bool  cStutter      = false;
    bool  cPlay         = true;
    bool  cBypass       = false;
    float cStart        = 0.0f;
    float cEnd          = 1.0f;
    bool  cLoop         = true;
};

/** Per-loop transport / region flags. */
struct VoiceOptions
{
    bool   reverse    = false;
    bool   half       = false;
    bool   doubleTime = false;
    bool   stutter    = false;

    float  start      = 0.0f;
    float  end        = 1.0f;
    bool   looping    = true;

    bool   autoChop   = false;
    int    chopDiv    = 2;
    bool   rearrange  = false;
    int    shuffleSeed = 1;
    float  bpm        = 140.0f;
};

//==============================================================================
enum class FilterMode
{
    Off = 0,
    Lowpass,
    Highpass,
    Bandpass,
    Notch,
    Peak,
    Comb,
    Smoke,
    Basement
};

inline constexpr int numFilterModes = (int) FilterMode::Basement + 1;

inline const char* filterModeName (int index) noexcept
{
    static const char* names[] =
    {
        "OFF", "LOWPASS", "HIGHPASS", "BANDPASS", "NOTCH",
        "PEAK", "COMB", "SMOKE", "BASEMENT"
    };
    return (index >= 0 && index < numFilterModes) ? names[index] : "OFF";
}

//==============================================================================
// FX recorder lanes (order matters - it is used by the GUI strip too)
enum class Lane : int
{
    GrainSize = 0, GrainSpray, GrainPitch, GrainStretch, GrainDensity, GrainBurn,
    ChopDepth, ChopRate, StutterRate, Octave, Detune, Wobble, Formant,
    Cutoff, Reso, Drip, Count
};

inline constexpr int numLanes = (int) Lane::Count;

inline P paramForLane (int lane) noexcept
{
    switch ((Lane) lane)
    {
        case Lane::GrainSize:    return P::GrainSize;
        case Lane::GrainSpray:   return P::GrainSpray;
        case Lane::GrainPitch:   return P::GrainPitch;
        case Lane::GrainStretch: return P::GrainStretch;
        case Lane::GrainDensity: return P::GrainDensity;
        case Lane::GrainBurn:    return P::GrainBurn;
        case Lane::ChopDepth:    return P::ChopDepth;
        case Lane::ChopRate:     return P::ChopRate;
        case Lane::StutterRate:  return P::StutterRate;
        case Lane::Octave:       return P::Octave;
        case Lane::Detune:       return P::Detune;
        case Lane::Wobble:       return P::Wobble;
        case Lane::Formant:      return P::Formant;
        case Lane::Cutoff:       return P::Cutoff;
        case Lane::Reso:         return P::Reso;
        case Lane::Drip:         return P::Drip;
        case Lane::Count:        break;
    }
    return P::GrainSize;
}

inline const char* laneName (int lane) noexcept
{
    static const char* names[] =
    {
        "SIZE", "SPRAY", "PITCH", "STRETCH", "DENSITY", "BURN",
        "CHOP", "CHOP RT", "STUT RT", "OCTAVE", "DETUNE", "WOBBLE",
        "FORMANT", "CUTOFF", "RESO", "DRIP"
    };
    return (lane >= 0 && lane < numLanes) ? names[lane] : "";
}

//==============================================================================
/** Snap a semitone offset to the chosen key/scale. */
inline double quantiseToKey (double semitones, int root, int scaleType) noexcept
{
    static constexpr int major[] = { 0, 2, 4, 5, 7, 9, 11 };
    static constexpr int minor[] = { 0, 2, 3, 5, 7, 8, 10 };

    const int* degrees = scaleType == 1 ? minor : major;

    semitones = juce::jlimit (-48.0, 48.0, semitones);

    const double octave = std::floor (semitones / 12.0) * 12.0;
    const int pitchClass = juce::jlimit (0, 11, (int) std::lround (semitones - octave));

    int bestTarget = 0;
    int bestDistance = 99;

    for (int i = 0; i < 7; ++i)
    {
        const int target = (juce::jlimit (0, 11, root) + degrees[i]) % 12;
        int distance = std::abs (pitchClass - target);

        if (distance > 6)
            distance = 12 - distance;

        if (distance < bestDistance)
        {
            bestDistance = distance;
            bestTarget = target;
        }
    }

    double result = octave + (double) bestTarget;

    if (result - semitones > 6.0)
        result -= 12.0;
    else if (semitones - result > 6.0)
        result += 12.0;

    return result;
}

inline const char* noteName (int note) noexcept
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[juce::jlimit (0, 11, note)];
}

} // namespace btp

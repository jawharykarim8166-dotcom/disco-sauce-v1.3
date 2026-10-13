#pragma once

// Character data for the four main presets.
// The five section knobs (comp / reverb / delay / eq / tune) scale how much of each
// preset's character is applied, so the user stays in control of the amount.
struct Preset
{
    const char* name;
    // default knob positions + auto-tune retune speed (ms, 0 = instant "hard tune")
    float kComp, kRev, kDelay, kEq, kTune, retuneMs;
    // compressor: ratio, attack ms, release ms, knee dB, depth (threshold at knob=1, dB), saturation drive
    float ratio, attMs, relMs, kneeDb, depthDb, drive;
    // eq: high-pass Hz, low shelf dB, mud cut dB, presence dB, air dB, de-esser depth
    float hpHz, lowDb, mudDb, presDb, airDb, deEss;
    // reverb: size, decay seconds, damping Hz, pre-delay ms, max wet level
    float revSize, revDecay, revDamp, revPre, revWet;
    // delay: length in beats (0.5 = 1/8), feedback, feedback low-pass Hz, max wet level
    float dlyBeats, dlyFb, dlyLp, dlyWet;
};

inline const Preset* presetTable()
{
    static const Preset p[4] = {
        // 0: Crisp Studio - clean, glossy, "million dollar studio" vocal
        { "Crisp Studio",
          0.65f, 0.35f, 0.20f, 0.75f, 1.00f, 30.0f,
          3.5f, 12.0f, 140.0f, 6.0f, 20.0f, 0.15f,
          90.0f, 1.5f, -2.5f, 3.0f, 3.5f, 0.60f,
          0.9f, 1.4f, 6500.0f, 25.0f, 0.22f,
          0.5f, 0.25f, 5000.0f, 0.18f },
        // 1: T-Pain - instant hard tune, tight and bright
        { "T-Pain",
          0.70f, 0.25f, 0.20f, 0.60f, 1.00f, 0.0f,
          6.0f, 3.0f, 90.0f, 3.0f, 24.0f, 0.35f,
          110.0f, 1.0f, -1.5f, 4.0f, 2.5f, 0.50f,
          0.8f, 1.0f, 5000.0f, 15.0f, 0.18f,
          0.25f, 0.20f, 4500.0f, 0.15f },
        // 2: Dark Trap - big delay + reverb, forward presence
        { "Dark Trap",
          0.70f, 0.60f, 0.60f, 0.80f, 1.00f, 8.0f,
          4.0f, 8.0f, 120.0f, 4.0f, 22.0f, 0.30f,
          100.0f, 0.5f, -3.0f, 4.5f, 1.5f, 0.70f,
          1.3f, 3.2f, 3500.0f, 40.0f, 0.40f,
          0.75f, 0.45f, 3000.0f, 0.45f },
        // 3: Disco Pro - the full-quality, polished, wide, airy chain
        { "Disco Pro",
          0.80f, 0.50f, 0.40f, 0.85f, 1.00f, 12.0f,
          4.0f, 6.0f, 100.0f, 6.0f, 22.0f, 0.25f,
          80.0f, 2.0f, -2.5f, 3.5f, 4.5f, 0.80f,
          1.1f, 2.0f, 7500.0f, 30.0f, 0.30f,
          0.5f, 0.35f, 6000.0f, 0.30f }
    };
    return p;
}

inline const char* const* scaleNames()
{
    static const char* const n[12] = {
        "Chromatic", "Major", "Minor", "Harmonic Minor", "Pentatonic Major", "Pentatonic Minor",
        "Blues", "Dorian", "Phrygian", "Mixolydian", "Lydian", "Hijaz (Arabic)"
    };
    return n;
}

// bit i set = semitone i above the key's root is allowed
inline int scaleMask(int idx)
{
    static const int sets[12][8] = {
        { 0, 1, 2, 3, 4, 5, 6, 7 },   // placeholder, chromatic handled below
        { 0, 2, 4, 5, 7, 9, 11, -1 },
        { 0, 2, 3, 5, 7, 8, 10, -1 },
        { 0, 2, 3, 5, 7, 8, 11, -1 },
        { 0, 2, 4, 7, 9, -1, -1, -1 },
        { 0, 3, 5, 7, 10, -1, -1, -1 },
        { 0, 3, 5, 6, 7, 10, -1, -1 },
        { 0, 2, 3, 5, 7, 9, 10, -1 },
        { 0, 1, 3, 5, 7, 8, 10, -1 },
        { 0, 2, 4, 5, 7, 9, 10, -1 },
        { 0, 2, 4, 6, 7, 9, 11, -1 },
        { 0, 1, 4, 5, 7, 8, 10, -1 }
    };
    if (idx <= 0 || idx > 11) return 0xFFF;
    int m = 0;
    for (int i = 0; i < 8; ++i)
        if (sets[idx][i] >= 0) m |= (1 << sets[idx][i]);
    return m;
}

inline const char* const* keyNames()
{
    static const char* const n[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return n;
}

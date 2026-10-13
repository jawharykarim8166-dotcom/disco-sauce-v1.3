#pragma once
#include "Dsp.h"
#include "Presets.h"

namespace ds
{
// Real-time pitch correction.
//  - Detection: YIN (de Cheveigne & Kawahara, 2002) on a 16 kHz mono copy, 20 ms window, new reading every 4 ms,
//    sub-sample refined, with a very low level gate and a soft fallback so quiet / breathy notes are still caught.
//  - Note choice: nearest allowed note of the chosen key/scale, taken from a slowly averaged pitch with hysteresis,
//    so vibrato around a note boundary never makes the target flip. A real note change snaps immediately.
//  - Correction: exact pitch ratio every sample (retune speed 0 = instant hard tune), time-aligned to the audio
//    that is actually being output.
//  - Shifting: a delay-line reader whose delay moves at exactly (1 - ratio) samples per sample, and which splices
//    by a whole number of pitch periods (cross-faded) to stay centred on the reported latency. Splicing on whole
//    periods keeps the waveform coherent, so there is no comb filtering, flutter or pitch error from window changes.
class AutoTune
{
public:
    struct Settings
    {
        float amount = 1.0f;     // 0 = off, 1 = full (100%) correction
        float retuneMs = 20.0f;  // 0 = instant (hard tune)
        int key = 0;             // 0..11 (C..B)
        int scale = 0;           // index into scale list
    };

    void prepare(double sr)
    {
        sampleRate = sr;
        bufSize = nextPow2((int) (sr * 0.16));
        bufMask = bufSize - 1;
        buf[0].assign((size_t) bufSize, 0.f);
        buf[1].assign((size_t) bufSize, 0.f);
        corrRing.assign((size_t) bufSize, 0.f);
        wi = 0;
        dnom = (int) std::round(0.020 * sr);
        xfLen = std::max(32, (int) std::round(0.003 * sr));

        dec = std::max(1, (int) std::round(sr / 16000.0));
        anaRate = sr / dec;
        anaW = (int) std::round(anaRate * 0.020);
        tauMax = (int) std::round(anaRate / 65.0);
        tauMin = std::max(2, (int) std::round(anaRate / 1000.0));
        anaHop = std::max(8, (int) (anaRate * 0.004));
        ring.assign(2048, 0.f);
        ringW = 0;
        tmp.assign((size_t) (anaW + tauMax + 4), 0.f);
        diff.assign((size_t) (tauMax + 4), 0.0);
        cmnd.assign((size_t) (tauMax + 4), 0.0);
        detLagSamples = (int) std::round(detLagMs * 0.001 * sr);
        reset();
    }

    void reset()
    {
        std::fill(buf[0].begin(), buf[0].end(), 0.f);
        std::fill(buf[1].begin(), buf[1].end(), 0.f);
        std::fill(ring.begin(), ring.end(), 0.f);
        std::fill(corrRing.begin(), corrRing.end(), 0.f);
        accum = 0.f; accCount = 0; hopCount = 0;
        corr = 0.f; desired = 0.f; voicedG = 0.f; engage = 0.f;
        voicedHold = 0; haveDet = false; haveTarget = false; havePitchAvg = false;
        outlierCount = 0; lastTarget = 0; detSm = 60.f; pitchAvg = 60.f;
        dHead[0] = dHead[1] = (double) dnom;
        active = 0; crossing = false; fade = 0.0;
        lastP = sampleRate / 150.0;
    }

    void setDetLagMs(double ms) { detLagMs = ms; detLagSamples = (int) std::round(detLagMs * 0.001 * sampleRate); }
    int getLatency() const { return dnom; }
    bool isVoiced() const { return voicedHold > 0; }
    float getDetectedMidi() const { return detSm; }
    int getTargetNote() const { return lastTarget; }
    float getDesired() const { return desired; }
    float getCorr() const { return corr; }

    // L/R are processed in place; dryL/dryR receive the input delayed by the reported latency.
    void process(float* L, float* R, float* dryL, float* dryR, int n, const Settings& s)
    {
        const bool active_ = s.amount > 0.001f;
        const double engC = 1.0 - std::exp(-1.0 / (0.02 * sampleRate));
        const double voicC = 1.0 - std::exp(-1.0 / (0.008 * sampleRate));
        const double corrC = 1.0 - std::exp(-1.0 / (0.001 * std::max(1.0, (double) s.retuneMs) * sampleRate));
        const double releaseC = 1.0 - std::exp(-1.0 / (0.03 * sampleRate));
        curSettings = s;

        for (int i = 0; i < n; ++i)
        {
            const float inL = L[i], inR = R[i];
            buf[0][(size_t) wi] = inL;
            buf[1][(size_t) wi] = inR;

            const int di = (wi - dnom) & bufMask;
            const float dL = buf[0][(size_t) di];
            const float dR = buf[1][(size_t) di];
            dryL[i] = dL;
            dryR[i] = dR;

            // analysis path (decimate by averaging)
            accum += 0.5f * (inL + inR);
            if (++accCount >= dec)
            {
                ring[(size_t) (ringW & 2047)] = accum / (float) dec;
                ++ringW;
                accum = 0.f; accCount = 0;
                if (++hopCount >= anaHop) { hopCount = 0; analyse(); }
            }

            engage += (float) (((active_ ? 1.0f : 0.0f) - engage) * engC);
            voicedG += (float) (((voicedHold > 0 ? 1.0f : 0.0f) - voicedG) * voicC);

            if (voicedHold > 0) corr += (float) ((desired - corr) * corrC);
            else corr += (float) ((0.0f - corr) * std::max(corrC, releaseC));

            if (engage < 0.0005f && !active_)
            {
                corrRing[(size_t) wi] = 0.f;
                L[i] = dL; R[i] = dR;
                dHead[0] = dHead[1] = (double) dnom; crossing = false; fade = 0.0;
                wi = (wi + 1) & bufMask;
                continue;
            }

            // correction, delayed so it lines up with the audio actually being read
            corrRing[(size_t) wi] = corr;
            const int align = std::max(0, (int) std::lround(dHead[active]) - detLagSamples);
            const float corrNow = corrRing[(size_t) ((wi - align) & bufMask)];
            const double r = std::exp2((double) corrNow / 12.0);

            // move both read heads by exactly (1 - r) samples per sample
            const double step = 1.0 - r;
            dHead[0] += step;
            dHead[1] += step;

            // splice by a whole number of periods to stay centred on the reported latency
            if (!crossing)
            {
                const double dev = dHead[active] - (double) dnom;
                const int nSp = (int) std::lround(dev / lastP);
                if (nSp != 0 && (voicedHold > 0 || std::abs(dev) > 0.8 * dnom))
                {
                    dHead[1 - active] = dHead[active] - (double) nSp * lastP;
                    crossing = true;
                    fade = 0.0;
                }
            }
            const double dmax = (double) (bufSize - 8);
            dHead[0] = std::max(2.0, std::min(dHead[0], dmax));
            dHead[1] = std::max(2.0, std::min(dHead[1], dmax));

            float wB = 0.f;
            if (crossing) wB = (float) (0.5 - 0.5 * std::cos(kPi * fade));

            float out[2];
            for (int c = 0; c < 2; ++c)
            {
                float sh = readFrac(buf[c], dHead[active]);
                if (crossing)
                {
                    const float b = readFrac(buf[c], dHead[1 - active]);
                    sh += (b - sh) * wB;
                }
                const float tap = c == 0 ? dL : dR;                  // unvoiced: plain delayed audio
                const float tuned = voicedG * sh + (1.0f - voicedG) * tap;
                const float dry = c == 0 ? dL : dR;
                out[c] = engage * tuned + (1.0f - engage) * dry;
            }
            L[i] = out[0];
            R[i] = out[1];

            if (crossing)
            {
                fade += 1.0 / (double) xfLen;
                if (fade >= 1.0)
                {
                    crossing = false;
                    fade = 0.0;
                    active = 1 - active;
                    dHead[1 - active] = dHead[active];
                }
            }
            wi = (wi + 1) & bufMask;
        }
    }

private:
    inline float readFrac(const std::vector<float>& b, double d) const
    {
        const double rp = (double) wi - d;
        const int i1 = (int) std::floor(rp);
        const float f = (float) (rp - (double) i1);
        const float y0 = b[(size_t) ((i1 - 1) & bufMask)];
        const float y1 = b[(size_t) (i1 & bufMask)];
        const float y2 = b[(size_t) ((i1 + 1) & bufMask)];
        const float y3 = b[(size_t) ((i1 + 2) & bufMask)];
        const float c0 = y1;
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + c0;
    }

    bool isAllowed(int note) const
    {
        const int pc = (((note - curSettings.key) % 12) + 12) % 12;
        return ((scaleMask(curSettings.scale) >> pc) & 1) != 0;
    }

    int findTarget(float midi)
    {
        if (haveTarget && isAllowed(lastTarget) && std::abs(midi - (float) lastTarget) < 0.65f)
            return lastTarget;
        const int c = (int) std::lround(midi);
        int best = c;
        float bestD = 1.0e9f;
        for (int n = c - 7; n <= c + 7; ++n)
        {
            if (!isAllowed(n)) continue;
            const float d = std::abs(midi - (float) n);
            if (d < bestD) { bestD = d; best = n; }
        }
        haveTarget = true;
        lastTarget = best;
        return best;
    }

    // YIN pitch detection on the newest samples of the decimated ring buffer.
    bool detect(float& freq)
    {
        const int N = anaW + tauMax;
        for (int j = 0; j < N; ++j)
            tmp[(size_t) j] = ring[(size_t) ((ringW - N + j) & 2047)];

        double e = 0.0;
        for (int j = tauMax; j < N; ++j) e += (double) tmp[(size_t) j] * tmp[(size_t) j];
        if (std::sqrt(e / anaW) < 0.0005) return false;

        for (int tau = 1; tau <= tauMax; ++tau)
        {
            double d = 0.0;
            for (int j = 0; j < anaW; ++j)
            {
                const double v = (double) tmp[(size_t) (tauMax + j)] - (double) tmp[(size_t) (tauMax + j - tau)];
                d += v * v;
            }
            diff[(size_t) tau] = d;
        }
        cmnd[0] = 1.0;
        double run = 0.0;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            run += diff[(size_t) tau];
            cmnd[(size_t) tau] = run > 1.0e-12 ? diff[(size_t) tau] * tau / run : 1.0;
        }

        int est = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
        {
            if (cmnd[(size_t) tau] < 0.15)
            {
                while (tau + 1 < tauMax && cmnd[(size_t) (tau + 1)] < cmnd[(size_t) tau]) ++tau;
                est = tau;
                break;
            }
        }
        if (est < 2)
        {
            // softer fallback: accept the deepest dip if it is reasonably periodic
            double bestV = 1.0e9; int bestT = -1;
            for (int tau = tauMin; tau < tauMax; ++tau)
                if (cmnd[(size_t) tau] < bestV) { bestV = cmnd[(size_t) tau]; bestT = tau; }
            if (bestT < 2 || bestV > 0.28) return false;
            est = bestT;
        }

        const double s0 = cmnd[(size_t) (est - 1)], s1 = cmnd[(size_t) est], s2 = cmnd[(size_t) (est + 1)];
        const double den = s0 - 2.0 * s1 + s2;
        const double shift = std::abs(den) > 1.0e-9 ? 0.5 * (s0 - s2) / den : 0.0;
        const double better = (double) est + std::max(-1.0, std::min(1.0, shift));
        freq = (float) (anaRate / better);
        return freq > 60.f && freq < 1200.f;
    }

    void analyse()
    {
        float f = 0.f;
        if (detect(f))
        {
            const float midi = 69.0f + 12.0f * std::log2(f / 440.0f);
            if (haveDet && voicedHold > 0 && std::abs(midi - detSm) > 4.0f && outlierCount < 2)
            {
                ++outlierCount;          // ignore a one-off wild reading, keep previous pitch
                voicedHold = 5;
                return;
            }
            outlierCount = 0;
            detSm = midi;
            haveDet = true;

            // choose the note from a slowly averaged pitch so vibrato around a note boundary
            // doesn't make the target flip; large moves (a new note) reset the average at once
            hist[histIdx] = midi;
            histIdx = (histIdx + 1) & 3;
            float hMin = hist[0], hMax = hist[0];
            for (int h = 1; h < 4; ++h) { hMin = std::min(hMin, hist[h]); hMax = std::max(hMax, hist[h]); }
            const bool settled = (hMax - hMin) < 0.12f;     // pitch has stopped moving: a held note
            const float dev = std::abs(midi - pitchAvg);
            if (!havePitchAvg || dev > 2.0f || (dev > 0.6f && settled)) pitchAvg = midi;
            else pitchAvg += 0.05f * (midi - pitchAvg);
            if (!havePitchAvg) for (float& h : hist) h = midi;
            havePitchAvg = true;
            const int target = findTarget(pitchAvg);

            desired = ((float) target - detSm) * curSettings.amount;
            voicedHold = 5;
            lastP = sampleRate / (double) f;
        }
        else
        {
            if (voicedHold > 0) --voicedHold;
            if (voicedHold == 0) { haveDet = false; haveTarget = false; havePitchAvg = false; }
        }
    }

    double sampleRate = 48000.0, anaRate = 16000.0;
    int bufSize = 1, bufMask = 0, wi = 0, dnom = 960, xfLen = 144;
    std::vector<float> buf[2], corrRing;

    // read heads
    double dHead[2] = { 960.0, 960.0 }, fade = 0.0, lastP = 320.0;
    int active = 0;
    bool crossing = false;

    int dec = 3, anaW = 320, tauMax = 246, tauMin = 16, anaHop = 64;
    std::vector<float> ring, tmp;
    std::vector<double> diff, cmnd;
    int ringW = 0, accCount = 0, hopCount = 0;
    float accum = 0.f;

    float corr = 0.f, desired = 0.f, voicedG = 0.f, engage = 0.f, detSm = 60.f, pitchAvg = 60.f;
    float hist[4] = { 60.f, 60.f, 60.f, 60.f };
    int histIdx = 0;
    int voicedHold = 0, lastTarget = 0, outlierCount = 0, detLagSamples = 0;
    double detLagMs = 16.0;
    bool haveDet = false, haveTarget = false, havePitchAvg = false;
    Settings curSettings;
};

} // namespace ds

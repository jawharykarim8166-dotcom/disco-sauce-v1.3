#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstring>

namespace ds
{
static constexpr double kPi = 3.14159265358979323846;

inline int nextPow2(int v)
{
    int p = 1;
    while (p < v) p <<= 1;
    return p;
}

// ------------------------------------------------------------------ biquad (RBJ cookbook)
struct Coefs
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
};

struct Filter
{
    Coefs c;
    double z1 = 0, z2 = 0;
    inline float process(float x)
    {
        const double in = x;
        const double y = c.b0 * in + z1;
        z1 = c.b1 * in - c.a1 * y + z2;
        z2 = c.b2 * in - c.a2 * y;
        return (float) y;
    }
    void reset() { z1 = z2 = 0; }
};

inline Coefs normalise(double b0, double b1, double b2, double a0, double a1, double a2)
{
    Coefs c;
    c.b0 = b0 / a0; c.b1 = b1 / a0; c.b2 = b2 / a0;
    c.a1 = a1 / a0; c.a2 = a2 / a0;
    return c;
}

inline double clampFreq(double sr, double f) { return std::max(10.0, std::min(f, sr * 0.45)); }

inline Coefs makeHighPass(double sr, double f, double q)
{
    f = clampFreq(sr, f);
    const double w = 2 * kPi * f / sr, cs = std::cos(w), al = std::sin(w) / (2 * q);
    return normalise((1 + cs) / 2, -(1 + cs), (1 + cs) / 2, 1 + al, -2 * cs, 1 - al);
}

inline Coefs makeLowPass(double sr, double f, double q)
{
    f = clampFreq(sr, f);
    const double w = 2 * kPi * f / sr, cs = std::cos(w), al = std::sin(w) / (2 * q);
    return normalise((1 - cs) / 2, 1 - cs, (1 - cs) / 2, 1 + al, -2 * cs, 1 - al);
}

inline Coefs makePeak(double sr, double f, double q, double gDb)
{
    f = clampFreq(sr, f);
    const double A = std::pow(10.0, gDb / 40.0);
    const double w = 2 * kPi * f / sr, cs = std::cos(w), al = std::sin(w) / (2 * q);
    return normalise(1 + al * A, -2 * cs, 1 - al * A, 1 + al / A, -2 * cs, 1 - al / A);
}

inline Coefs makeLowShelf(double sr, double f, double q, double gDb)
{
    f = clampFreq(sr, f);
    const double A = std::pow(10.0, gDb / 40.0);
    const double w = 2 * kPi * f / sr, cs = std::cos(w), al = std::sin(w) / (2 * q);
    const double sq = 2 * std::sqrt(A) * al;
    return normalise(A * ((A + 1) - (A - 1) * cs + sq),
                     2 * A * ((A - 1) - (A + 1) * cs),
                     A * ((A + 1) - (A - 1) * cs - sq),
                     (A + 1) + (A - 1) * cs + sq,
                     -2 * ((A - 1) + (A + 1) * cs),
                     (A + 1) + (A - 1) * cs - sq);
}

inline Coefs makeHighShelf(double sr, double f, double q, double gDb)
{
    f = clampFreq(sr, f);
    const double A = std::pow(10.0, gDb / 40.0);
    const double w = 2 * kPi * f / sr, cs = std::cos(w), al = std::sin(w) / (2 * q);
    const double sq = 2 * std::sqrt(A) * al;
    return normalise(A * ((A + 1) + (A - 1) * cs + sq),
                     -2 * A * ((A - 1) + (A + 1) * cs),
                     A * ((A + 1) + (A - 1) * cs - sq),
                     (A + 1) - (A - 1) * cs + sq,
                     2 * ((A - 1) - (A + 1) * cs),
                     (A + 1) - (A - 1) * cs - sq);
}

// ------------------------------------------------------------------ compressor
// Feed-forward, stereo-linked, log-domain detector, soft knee.
struct Compressor
{
    double sr = 48000.0, state = 0.0, attC = 0.0, relC = 0.0;
    float thr = 0.f, ratio = 2.f, knee = 6.f, makeup = 0.f;

    void prepare(double s) { sr = s; state = 0.0; set(0.f, 2.f, 10.f, 100.f, 6.f, 0.f); }

    void set(float thrDb, float r, float attMs, float relMs, float kneeDb, float makeupDb)
    {
        thr = thrDb;
        ratio = std::max(1.0f, r);
        knee = std::max(0.01f, kneeDb);
        makeup = makeupDb;
        attC = std::exp(-1.0 / (0.001 * std::max(0.01f, attMs) * sr));
        relC = std::exp(-1.0 / (0.001 * std::max(0.01f, relMs) * sr));
    }

    void reset() { state = 0.0; }

    void process(float* l, float* r, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float lev = std::max(std::abs(l[i]), std::abs(r[i]));
            const double ldb = 20.0 * std::log10((double) std::max(lev, 1.0e-6f));
            const double over = ldb - thr;
            double out;
            if (2.0 * over < -knee) out = ldb;
            else if (2.0 * std::abs(over) <= knee)
            {
                const double t = over + knee * 0.5;
                out = ldb + (1.0 / ratio - 1.0) * t * t / (2.0 * knee);
            }
            else out = thr + over / ratio;

            const double red = ldb - out; // >= 0
            const double c = red > state ? attC : relC;
            state = c * state + (1.0 - c) * red;
            const float g = (float) std::pow(10.0, (-state + makeup) / 20.0);
            l[i] *= g;
            r[i] *= g;
        }
    }
};

// ------------------------------------------------------------------ stereo ping-pong delay
class PingPongDelay
{
public:
    void prepare(double s)
    {
        sr = s;
        size = nextPow2((int) (sr * 3.0));
        mask = size - 1;
        bufL.assign((size_t) size, 0.f);
        bufR.assign((size_t) size, 0.f);
        w = 0;
        cur = tgt = (float) (0.3 * sr);
        lpL = lpR = hpL = hpR = 0.f;
    }

    void set(float delaySamples, float feedback, float lpHz)
    {
        tgt = std::min(delaySamples, (float) (sr * 2.9));
        tgt = std::max(tgt, 32.f);
        fb = feedback;
        lpA = (float) (1.0 - std::exp(-2.0 * kPi * std::min((double) lpHz, sr * 0.45) / sr));
        hpA = (float) (1.0 - std::exp(-2.0 * kPi * 180.0 / sr));
    }

    void process(const float* inM, float* outL, float* outR, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            cur += (tgt - cur) * 0.0008f;
            const float dl = readFrac(bufL, cur);
            const float dr = readFrac(bufR, cur);

            lpL += lpA * (dl - lpL);
            lpR += lpA * (dr - lpR);
            hpL += hpA * (lpL - hpL);
            hpR += hpA * (lpR - hpR);
            const float fl = lpL - hpL;
            const float fr = lpR - hpR;

            bufL[(size_t) w] = inM[i] + fb * fr;
            bufR[(size_t) w] = fb * fl;
            w = (w + 1) & mask;

            outL[i] = dl;
            outR[i] = dr;
        }
    }

private:
    inline float readFrac(const std::vector<float>& b, float d) const
    {
        const float rp = (float) w - d;
        const int i0 = (int) std::floor(rp);
        const float fr = rp - (float) i0;
        const float a = b[(size_t) (i0 & mask)];
        const float c = b[(size_t) ((i0 + 1) & mask)];
        return a + (c - a) * fr;
    }

    double sr = 48000.0;
    int size = 1, mask = 0, w = 0;
    std::vector<float> bufL, bufR;
    float cur = 1000.f, tgt = 1000.f, fb = 0.3f, lpA = 0.3f, hpA = 0.01f;
    float lpL = 0, lpR = 0, hpL = 0, hpR = 0;
};

// ------------------------------------------------------------------ FDN reverb (8 lines, Householder feedback)
class FDNReverb
{
public:
    static constexpr int N = 8;

    void prepare(double s)
    {
        sr = s;
        const double sc = sr / 48000.0;
        static const int base[N] = { 1087, 1283, 1493, 1709, 1931, 2153, 2371, 2593 };
        static const float rates[N] = { 0.13f, 0.17f, 0.21f, 0.25f, 0.29f, 0.33f, 0.37f, 0.41f };
        size = nextPow2((int) (base[N - 1] * sc * 1.8 + 128));
        mask = size - 1;
        for (int i = 0; i < N; ++i)
        {
            line[i].assign((size_t) size, 0.f);
            baseLen[i] = (float) (base[i] * sc);
            curLen[i] = tgtLen[i] = baseLen[i];
            lp[i] = 0.f;
            lfoPh[i] = (float) i * 0.8f;
            lfoInc[i] = (float) (2.0 * kPi * rates[i] / sr);
            g[i] = 0.8f;
        }
        modDepth = (float) (5.0 * sc);
        w = 0;

        static const int apLen[4] = { 142, 107, 379, 277 };
        for (int i = 0; i < 4; ++i)
        {
            ap[i].b.assign((size_t) std::max(8, (int) (apLen[i] * sc)), 0.f);
            ap[i].idx = 0;
        }

        preSize = nextPow2((int) (sr * 0.25));
        preMask = preSize - 1;
        pre.assign((size_t) preSize, 0.f);
        pw = 0;
        preDelay = (int) (0.02 * sr);

        hp.c = makeHighPass(sr, 160.0, 0.707);
        hp.reset();
        set(1.0f, 1.5f, 6000.f, 20.f);
    }

    void set(float size_, float decaySec, float dampHz, float preMs)
    {
        decay = std::max(0.1f, decaySec);
        for (int i = 0; i < N; ++i) tgtLen[i] = baseLen[i] * std::max(0.4f, size_);
        dampA = (float) (1.0 - std::exp(-2.0 * kPi * std::min((double) dampHz, sr * 0.45) / sr));
        preDelay = std::min(preMask - 1, std::max(0, (int) (preMs * 0.001 * sr)));
    }

    // input: mono. output: stereo wet signal.
    void process(const float* in, float* outL, float* outR, int n)
    {
        // recompute per-line feedback gain from the current line lengths
        for (int i = 0; i < N; ++i)
        {
            const double t = (double) tgtLen[i] / sr;
            g[i] = (float) std::pow(10.0, -3.0 * t / (double) decay);
        }

        for (int k = 0; k < n; ++k)
        {
            // pre-delay
            const float x0 = hp.process(in[k]);
            pre[(size_t) pw] = x0;
            float x = pre[(size_t) ((pw - preDelay) & preMask)];
            pw = (pw + 1) & preMask;

            // input diffusion
            for (int a = 0; a < 4; ++a) x = ap[a].process(x);

            float d[N], v[N];
            float sum = 0.f;
            for (int i = 0; i < N; ++i)
            {
                curLen[i] += (tgtLen[i] - curLen[i]) * 0.0003f;
                const float mod = modDepth * std::sin(lfoPh[i]);
                lfoPh[i] += lfoInc[i];
                if (lfoPh[i] > (float) (2.0 * kPi)) lfoPh[i] -= (float) (2.0 * kPi);

                d[i] = readLine(i, curLen[i] + mod);
                lp[i] += dampA * (d[i] - lp[i]);
                v[i] = lp[i] * g[i];
                sum += v[i];
            }
            const float k2 = (2.0f / (float) N) * sum;
            for (int i = 0; i < N; ++i)
                line[i][(size_t) w] = v[i] - k2 + x * 0.4f;
            w = (w + 1) & mask;

            outL[k] = (d[0] - d[1] + d[2] - d[3] + d[4] - d[5] + d[6] - d[7]) * 0.5f;
            outR[k] = (d[0] + d[1] - d[2] - d[3] + d[4] + d[5] - d[6] - d[7]) * 0.5f;
        }
    }

private:
    struct AP
    {
        std::vector<float> b;
        int idx = 0;
        float gain = 0.6f;
        inline float process(float x)
        {
            const float d = b[(size_t) idx];
            const float v = x - gain * d;
            b[(size_t) idx] = v;
            if (++idx >= (int) b.size()) idx = 0;
            return gain * v + d;
        }
    };

    inline float readLine(int i, float d) const
    {
        const float rp = (float) w - d;
        const int i0 = (int) std::floor(rp);
        const float fr = rp - (float) i0;
        const float a = line[i][(size_t) (i0 & mask)];
        const float c = line[i][(size_t) ((i0 + 1) & mask)];
        return a + (c - a) * fr;
    }

    double sr = 48000.0;
    int size = 1, mask = 0, w = 0;
    std::vector<float> line[N];
    float baseLen[N], curLen[N], tgtLen[N], lp[N], lfoPh[N], lfoInc[N], g[N];
    float modDepth = 5.f, dampA = 0.3f, decay = 1.5f;
    AP ap[4];
    std::vector<float> pre;
    int preSize = 1, preMask = 0, pw = 0, preDelay = 0;
    Filter hp;
};

} // namespace ds

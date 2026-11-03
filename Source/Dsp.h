#pragma once

// Shared DSP building blocks for VoxBot - X (no JUCE dependency).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace vbx
{
constexpr float kPi    = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

//==============================================================================
// Topology-preserving-transform state variable filter (Cytomic / Simper).
struct SVF
{
    float ic1 = 0, ic2 = 0, a1 = 0, a2 = 0, a3 = 0, k = 1;

    void set (float fc, float q, float sr)
    {
        fc = std::clamp (fc, 20.0f, 0.45f * sr);
        const float g = std::tan (kPi * fc / sr);
        k  = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    inline void tick (float v0, float& bp, float& lp)
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        bp = v1; lp = v2;
    }

    inline float bandpass (float v0) { float b, l; tick (v0, b, l); return k * b; } // unity peak
    inline float lowpass  (float v0) { float b, l; tick (v0, b, l); return l; }
    inline float highpass (float v0) { float b, l; tick (v0, b, l); return v0 - k * b - l; }

    void reset() { ic1 = ic2 = 0; }
};

//==============================================================================
// YIN pitch tracker on a decimated copy of the signal, with selectable range.
//   0 = Low  (25 Hz - 700 Hz)
//   1 = Mid  (40 Hz - 1 kHz)
//   2 = High (80 Hz - 4 kHz)
class PitchTracker
{
public:
    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        // Allocate for the worst case of all ranges so setRange() never allocates.
        size_t maxWin = 0, maxTau = 0;
        for (int r = 0; r < 3; ++r)
        {
            Config c = configFor (r);
            maxWin = std::max (maxWin, (size_t) c.winLen);
            maxTau = std::max (maxTau, (size_t) c.tauMax);
        }
        ring.assign (maxWin, 0.0f);
        frame.assign (maxWin, 0.0f);
        cmnd.assign (maxTau + 2, 1.0f);
        range = -1;
        setRange (1);
    }

    void setRange (int newRange)
    {
        newRange = std::clamp (newRange, 0, 2);
        if (newRange == range) return;
        range = newRange;
        cfg = configFor (range);
        aa.set (cfg.aaHz, 0.707f, sr);  aa.reset();
        aa2.set (cfg.aaHz, 0.707f, sr); aa2.reset();
        std::fill (ring.begin(), ring.end(), 0.0f);
        writePos = 0; decimCount = 0; hopCount = 0;
        voiced = false;
    }

    int getRange() const { return range; }

    /** How far behind the newest input the pitch estimate is centred (in input samples). */
    int analysisDelaySamples() const { return (cfg.winLen * cfg.decim) / 2 + (cfg.hop * cfg.decim) / 2; }

    inline void push (float x)
    {
        x = aa2.lowpass (aa.lowpass (x));
        if (++decimCount < cfg.decim) return;
        decimCount = 0;

        ring[(size_t) writePos] = x;
        if (++writePos >= cfg.winLen) writePos = 0;

        if (++hopCount >= cfg.hop) { hopCount = 0; analyse(); }
    }

    float hz = 150.0f;      // last detected pitch (held when unvoiced)
    bool  voiced = false;
    float level = 0.0f;     // RMS of the analysis window

private:
    struct Config { int decim, tauMin, tauMax, integ, winLen, hop; float dsr, aaHz; };

    Config configFor (int r) const
    {
        const float fmin[3]   = { 25.0f, 40.0f, 80.0f };
        const float fmax[3]   = { 700.0f, 1000.0f, 4000.0f };
        const float target[3] = { 5512.0f, 11025.0f, 22050.0f };
        const float aaHz[3]   = { 1200.0f, 2200.0f, 6000.0f };

        Config c;
        c.decim  = std::max (1, (int) std::lround (sr / target[r]));
        c.dsr    = sr / (float) c.decim;
        c.tauMin = std::max (2, (int) std::floor (c.dsr / fmax[r]));
        c.tauMax = (int) std::ceil (c.dsr / fmin[r]);
        c.integ  = std::max (128, c.tauMax * 2);
        c.winLen = c.integ + c.tauMax + 2;
        c.hop    = std::max (16, (int) (c.dsr * 0.006f));
        c.aaHz   = std::min (aaHz[r], 0.4f * c.dsr);
        return c;
    }

    void analyse()
    {
        const int W = cfg.winLen;
        double energy = 0;
        for (int i = 0; i < W; ++i)
        {
            int idx = writePos + i; if (idx >= W) idx -= W;
            const float s = ring[(size_t) idx];
            frame[(size_t) i] = s;
            energy += (double) s * s;
        }

        level = (float) std::sqrt (energy / W);
        if (level < 0.003f) { voiced = false; return; }

        float running = 0.0f;
        cmnd[0] = 1.0f;
        for (int tau = 1; tau <= cfg.tauMax; ++tau)
        {
            float d = 0.0f;
            const float* a = frame.data();
            const float* b = frame.data() + tau;
            for (int j = 0; j < cfg.integ; ++j) { const float df = a[j] - b[j]; d += df * df; }
            running += d;
            cmnd[(size_t) tau] = running > 0.0f ? d * (float) tau / running : 1.0f;
        }

        int best = -1;
        for (int tau = cfg.tauMin; tau < cfg.tauMax; ++tau)
        {
            if (cmnd[(size_t) tau] < 0.18f)
            {
                while (tau + 1 < cfg.tauMax && cmnd[(size_t) tau + 1] < cmnd[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        }

        if (best < 0) { voiced = false; return; }

        float t = (float) best;
        if (best > 1 && best < cfg.tauMax)
        {
            const float s0 = cmnd[(size_t) best - 1], s1 = cmnd[(size_t) best], s2 = cmnd[(size_t) best + 1];
            const float den = s0 + s2 - 2.0f * s1;
            if (std::abs (den) > 1e-9f) t += 0.5f * (s0 - s2) / den;
        }

        hz = cfg.dsr / t;
        voiced = true;
    }

    float sr = 44100.0f;
    int range = -1;
    Config cfg {};
    SVF aa, aa2;
    int writePos = 0, decimCount = 0, hopCount = 0;
    std::vector<float> ring, frame, cmnd;
};

//==============================================================================
// 90-degree phase splitter (Olli Niemitalo's IIR Hilbert pair).
struct Hilbert
{
    struct AP
    {
        float a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        inline float tick (float x)
        {
            const float y = a2 * (x + y2) - x2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            return y;
        }
    };

    std::array<AP, 4> p1, p2;
    float delay = 0.0f;

    Hilbert()
    {
        const float c1[4] = { 0.6923877778065f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f };
        const float c2[4] = { 0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f };
        for (int i = 0; i < 4; ++i) { p1[(size_t) i].a2 = c1[i] * c1[i]; p2[(size_t) i].a2 = c2[i] * c2[i]; }
    }

    void reset()
    {
        for (auto& a : p1) a.x1 = a.x2 = a.y1 = a.y2 = 0;
        for (auto& a : p2) a.x1 = a.x2 = a.y1 = a.y2 = 0;
        delay = 0;
    }

    inline void tick (float x, float& re, float& im)
    {
        float a = x, b = x;
        for (auto& s : p1) a = s.tick (a);
        for (auto& s : p2) b = s.tick (b);
        re = delay; delay = a;
        im = b;
    }
};

//==============================================================================
// Hann window lookup.
struct HannTable
{
    static constexpr int N = 2048;
    std::array<float, N + 1> t {};
    HannTable()
    {
        for (int i = 0; i <= N; ++i)
            t[(size_t) i] = 0.5f - 0.5f * std::cos (kTwoPi * (float) i / (float) N);
    }
    inline float operator() (float phase) const   // phase 0..1
    {
        const float p = std::clamp (phase, 0.0f, 1.0f) * (float) N;
        const int i = (int) p;
        const float f = p - (float) i;
        return i >= N ? 0.0f : t[(size_t) i] + f * (t[(size_t) i + 1] - t[(size_t) i]);
    }
};

inline const HannTable& hann() { static const HannTable h; return h; }

//==============================================================================
// Simple fractional delay ring used by several effects.
struct DelayLine
{
    std::vector<float> buf;
    int mask = 0, w = 0;

    void prepare (int minSize)
    {
        int n = 1; while (n < minSize) n <<= 1;
        buf.assign ((size_t) n, 0.0f); mask = n - 1; w = 0;
    }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    inline void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
    inline float read (float delay) const   // delay in samples (>= 1)
    {
        const float rp = (float) w - delay;
        const int i0 = (int) std::floor (rp);
        const float f = rp - (float) i0;
        const float a = buf[(size_t) (i0 & mask)], b = buf[(size_t) ((i0 + 1) & mask)];
        return a + f * (b - a);
    }
};

inline float semisToRatio (float s) { return std::exp2 (s / 12.0f); }

} // namespace vbx

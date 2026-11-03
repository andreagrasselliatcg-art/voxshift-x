#pragma once

// VoxShift - X engine: monophonic pitch + formant shifter with
// Transpose / Quantize / Robot modes, MIDI note control and tube-style drive.
//
// Pitch-synchronous granular resynthesis (PSOLA): grains two source periods long
// are re-spaced to the new period (pitch) and read faster/slower (formant).

#include "Dsp.h"

namespace vbx
{
enum ShiftMode { modeTranspose = 0, modeQuantize, modeRobot };

struct ShiftParams
{
    int   mode = modeTranspose;
    float pitch = 0.0f;     // semitones -12..12
    float formant = 0.0f;   // semitones -12..12
    bool  link = false;     // formant follows pitch
    float drive = 0.0f;     // 0..1
};

class ShiftEngine
{
public:
    static constexpr int kGrains = 32;

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        latency = (int) std::ceil (0.070f * sr);
        ring.prepare (latency * 4 + 4096);
        ringAbs = 0;
        tracker.prepare (sampleRate);
        tracker.setRange (1);
        trackerDelay = std::max (1, latency - tracker.analysisDelaySamples());

        for (auto& g : grains) g.active = false;
        countdown = 0.0f; havePrev = false;
        outLog = std::log (150.0f); semisSmooth = 0.0f;
        robotNote = 48; quantNote = 57; newPhrase = true; silence = 0;
        bypass = 1.0f;
        coef20 = 1.0f - std::exp (-1.0f / (0.020f * sr));
        driveSmooth = 0.0f; toneLp = 0.0f; dcPrev = dcState = 0.0f;
        stackSize = 0;
    }

    int getLatency() const { return latency; }
    void setParams (const ShiftParams& p) { params = p; }

    void noteOn (int note)
    {
        removeNote (note);
        if (stackSize < (int) stack.size()) stack[(size_t) stackSize++] = note;
    }
    void noteOff (int note) { removeNote (note); }
    void allNotesOff() { stackSize = 0; }
    int  heldNote() const { return stackSize > 0 ? stack[(size_t) stackSize - 1] : -1; }

    float getDetectedHz() const { return tracker.voiced ? tracker.hz : 0.0f; }
    float getOutputHz() const { return lastOutHz; }

    //--------------------------------------------------------------------------
    void process (const float* in, float* out, int n)
    {
        const auto& P = params;
        const int note = heldNote();

        // Pitch offset in semitones, from the knob or from a held MIDI note (C3 = 60 = no shift)
        const bool  snapped = P.mode != modeTranspose;
        const float knob = snapped ? std::round (P.pitch) : P.pitch;
        const float semis = note >= 0 ? (float) (note - 60) : knob;
        const float formantSemis = P.link ? semis : P.formant;
        const float fr = semisToRatio (std::clamp (formantSemis, -24.0f, 24.0f));

        const bool neutral = P.mode == modeTranspose && std::abs (semis) < 0.01f && std::abs (formantSemis) < 0.01f;
        const float retune = 1.0f - std::exp (-1.0f / ((P.mode == modeRobot ? 0.012f : 0.006f) * sr));

        for (int i = 0; i < n; ++i)
        {
            ring.push (in[i]);
            ++ringAbs;
            tracker.push (ring.read ((float) trackerDelay));   // pitch estimate aligned with the grains

            // ---- target pitch ----------------------------------------------------
            const float fin = std::max (40.0f, tracker.hz);
            if (tracker.voiced) silence = 0;
            else if (++silence > (int) (0.3f * sr)) newPhrase = true;

            semisSmooth += coef20 * (semis - semisSmooth);

            if (P.mode != modeTranspose)
            {
                float targetHz;
                if (P.mode == modeQuantize)
                {
                    // nearest semitone, with a little hysteresis so notes don't flicker at the boundary
                    const float m = 69.0f + 12.0f * std::log2 (fin / 440.0f);
                    if (std::abs (m - (float) quantNote) > 0.6f) quantNote = (int) std::lround (m);
                    targetHz = midiHz ((float) quantNote) * semisToRatio (semis);
                }
                else
                {
                    if (note >= 0) targetHz = midiHz ((float) note);
                    else
                    {
                        // monotone "C": the octave is chosen at the start of each phrase and then held
                        if (newPhrase && tracker.voiced)
                        {
                            const float m = 69.0f + 12.0f * std::log2 (fin / 440.0f);
                            robotNote = 12 * (int) std::lround (m / 12.0f);
                            newPhrase = false;
                        }
                        targetHz = midiHz ((float) robotNote) * semisToRatio (knob);
                    }
                }
                if (tracker.voiced) outLog += retune * (std::log (targetHz) - outLog);
            }

            bypass += coef20 * ((neutral ? 1.0f : 0.0f) - bypass);

            // ---- grains ----------------------------------------------------------
            countdown -= 1.0f;
            if (countdown <= 0.0f) spawn (fin, fr);

            float y = 0.0f;
            const auto& win = hann();
            for (auto& g : grains)
            {
                if (! g.active) continue;
                y += win (g.age / g.len) * g.gain * readAbs (g.start + (double) (g.age * g.rate));
                g.age += 1.0f;
                if (g.age >= g.len) g.active = false;
            }

            const float dry = ring.read ((float) latency + 1.0f);
            y = y * (1.0f - bypass) + dry * bypass;

            out[i] = applyDrive (y);
        }
    }

private:
    struct Grain { bool active = false; double start = 0; float rate = 1, len = 1, age = 0, gain = 1; };

    static float midiHz (float m) { return 440.0f * std::exp2 ((m - 69.0f) / 12.0f); }

    void removeNote (int note)
    {
        int w = 0;
        for (int r = 0; r < stackSize; ++r) if (stack[(size_t) r] != note) stack[(size_t) w++] = stack[(size_t) r];
        stackSize = w;
    }

    void spawn (float fin, float fr)
    {
        const bool voiced = tracker.voiced;
        const float tin = voiced ? sr / fin : sr * 0.006f;

        float ratio;
        if (params.mode == modeTranspose) ratio = semisToRatio (semisSmooth);
        else                              ratio = voiced ? std::exp (outLog) / fin : 1.0f;
        ratio = std::clamp (ratio, 0.25f, 4.0f);

        const float tout = tin / ratio;
        lastOutHz = voiced ? sr / tout : 0.0f;

        float len = 2.0f * tin / fr;
        const float maxSpan = 2.0f * ((float) latency - tin) - 4.0f;
        if (len * fr > maxSpan) len = maxSpan / fr;
        len = std::max (len, 16.0f);

        const double target = (double) ringAbs - 1.0 - (double) latency;
        double centre = target;
        if (voiced && havePrev)
        {
            double m = std::round ((target - prevSrc) / (double) tin);
            if (m < 0) m = 0;
            centre = prevSrc + m * (double) tin;
            if (std::abs (centre - target) > (double) tin) centre = target;
        }
        prevSrc = centre; havePrev = voiced;

        const float gain = std::clamp (std::sqrt (2.0f * tout / len), 0.1f, 2.0f);
        for (auto& g : grains)
            if (! g.active)
            {
                g = { true, centre - 0.5 * (double) (len * fr), fr, len, 0.0f, gain };
                break;
            }

        countdown += tout;
        if (countdown < 1.0f) countdown = 1.0f;
    }

    inline float readAbs (double pos) const
    {
        const float d = (float) ((double) ringAbs - pos);
        if (d < 1.0f || d > (float) ring.mask) return 0.0f;
        return ring.read (d);
    }

    // Tube-style saturation: asymmetric soft clipping, level compensated, gently darkened.
    inline float applyDrive (float x)
    {
        driveSmooth += coef20 * (params.drive - driveSmooth);
        const float d = driveSmooth;
        if (d < 0.0005f) return x;

        const float pre  = 1.0f + 30.0f * d * d;
        const float bias = 0.25f * d;
        float y = std::tanh (pre * x + bias) - std::tanh (bias);
        // keep a typical vocal level (about -18 dBFS RMS) close to unity gain
        constexpr float ref = 0.12f;
        y *= ref / std::tanh (pre * ref);

        // remove the DC created by the asymmetry
        const float yb = y - dcPrev + 0.999f * dcState; dcPrev = y; dcState = yb; y = yb;

        // darken a little as the drive increases
        const float lpCoef = 1.0f - std::exp (-kTwoPi * (14000.0f - 8000.0f * d) / sr);
        toneLp += lpCoef * (y - toneLp);
        y = toneLp;

        return x * (1.0f - std::min (1.0f, d * 8.0f)) + y * std::min (1.0f, d * 8.0f);
    }

    float sr = 44100.0f;
    int latency = 3000, trackerDelay = 1000;
    ShiftParams params;

    DelayLine ring;
    long long ringAbs = 0;
    PitchTracker tracker;

    std::array<Grain, kGrains> grains;
    float countdown = 0.0f;
    double prevSrc = 0.0;
    bool havePrev = false;

    float outLog = 5.0f, semisSmooth = 0.0f, lastOutHz = 0.0f;
    int robotNote = 48, quantNote = 57, silence = 0;
    bool newPhrase = true;
    float bypass = 1.0f, coef20 = 0.001f;
    float driveSmooth = 0.0f, toneLp = 0.0f, dcPrev = 0.0f, dcState = 0.0f;

    std::array<int, 16> stack {};
    int stackSize = 0;
};
} // namespace vbx

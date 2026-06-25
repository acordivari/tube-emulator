#pragma once

#include <vector>
#include <cmath>
#include <algorithm>

//==============================================================================
// A compact spring-reverb model (mono).
//
// What makes a spring tank sound like a spring is *dispersion*: high frequencies
// travel down the spring faster than lows, giving the characteristic "boing"
// chirp. That is modelled here by a long cascade of first-order allpass filters
// (each contributes frequency-dependent group delay) sitting inside a damped
// feedback delay loop:
//
//   in --(+)--> [allpass cascade: dispersion] --> [delay] --+--> wet (tail)
//        ^                                                  |
//        +--------- feedback * [HF damping lowpass] <-------+
//
// JUCE-independent so it can be unit-tested offline (tools/reverb_check.cpp).
//==============================================================================
class SpringReverb
{
public:
    void prepare (double sampleRate, double delaySeconds)
    {
        fs       = sampleRate;
        delayLen = std::max (1, (int) (delaySeconds * sampleRate));
        delayBuf.assign ((size_t) delayLen, 0.0f);
        setDamping (2600.0f);   // springs are dark
        reset();
    }

    void reset()
    {
        std::fill (delayBuf.begin(), delayBuf.end(), 0.0f);
        for (int k = 0; k < kNumAllpass; ++k) apX[k] = apY[k] = 0.0f;
        dampState = 0.0f;
        writeIdx  = 0;
    }

    void  setFeedback (float fb)        { feedback = fb; }
    void  setDamping  (float cutoffHz)  { dampCoef = (float) (1.0 - std::exp (-2.0 * kPi * cutoffHz / fs)); }

    float processSample (float in)
    {
        // Read the tail (sample written delayLen ago) and damp its highs.
        const float read = delayBuf[(size_t) writeIdx];
        dampState += dampCoef * (read - dampState);

        float v = in + feedback * dampState;

        // Dispersion: cascade of identical first-order allpass sections,
        // H(z) = (a + z^-1) / (1 + a z^-1).
        for (int k = 0; k < kNumAllpass; ++k)
        {
            const float x = v;
            const float y = apCoef * x + apX[k] - apCoef * apY[k];
            apX[k] = x;
            apY[k] = y;
            v = y;
        }

        delayBuf[(size_t) writeIdx] = v;
        if (++writeIdx >= delayLen) writeIdx = 0;

        return read;   // wet = the decaying, dispersed tail
    }

private:
    static constexpr int    kNumAllpass = 120;
    static constexpr double kPi         = 3.14159265358979323846;

    double fs       = 48000.0;
    int    delayLen = 1;
    int    writeIdx = 0;

    std::vector<float> delayBuf;
    float feedback = 0.88f;
    float apCoef   = 0.65f;
    float apX[kNumAllpass] = {};
    float apY[kNumAllpass] = {};
    float dampState = 0.0f;
    float dampCoef  = 0.3f;
};

// Offline stability/decay check for the spring reverb.
//
//   c++ -std=c++17 -O2 tools/reverb_check.cpp -o /tmp/rev && /tmp/rev
//
// Feeds a single impulse and prints the tail level over time. Expect: a smooth
// decay toward silence (NOT growing) — i.e. the feedback loop is stable, and a
// musically useful decay time (roughly 1-2 s).
#include "../Source/SpringReverb.h"
#include <cmath>
#include <cstdio>

int main()
{
    const double fs = 48000.0;
    SpringReverb rev;
    rev.prepare (fs, 0.0371);

    const int total = (int) (3.0 * fs);
    const int win   = (int) (0.05 * fs);   // 50 ms RMS windows

    double acc = 0.0;
    int    count = 0;
    float  peak = 0.0f;

    std::printf (" time(ms)   RMS(dB)\n");
    for (int i = 0; i < total; ++i)
    {
        const float in  = (i == 0) ? 1.0f : 0.0f;
        const float out = rev.processSample (in);

        peak = std::fmax (peak, std::fabs (out));
        acc += (double) out * out;
        if (++count >= win)
        {
            const double rms = std::sqrt (acc / count);
            std::printf ("%8.0f  %8.1f\n", (i / fs) * 1000.0,
                         rms > 0.0 ? 20.0 * std::log10 (rms) : -200.0);
            acc = 0.0; count = 0;
        }
    }
    std::printf ("peak |out| = %.3f  (must stay finite / <~1 for stability)\n", peak);
    return 0;
}

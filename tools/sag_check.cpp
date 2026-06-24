// Offline sanity check for the power-amp sag dynamics. Mirrors applySag().
//
//   c++ -std=c++17 -O2 tools/sag_check.cpp -o /tmp/sag && /tmp/sag
//
// Feeds a 110 Hz note burst (0.3 s on, then silence) through the sag model and
// prints the supply "voltage" over time. Expect: a quick droop while the note
// rings, then a slow recovery back toward 1.0 (the bloom).
#include <cmath>
#include <cstdio>

int main()
{
    const double fs = 48000.0;
    auto coef = [fs] (double ms) { return (float) (1.0 - std::exp (-1.0 / ((ms * 0.001) * fs))); };

    const float envAtt = coef (1.0),  envRel = coef (20.0);
    const float droop  = coef (25.0), recover = coef (320.0);
    const float depth = 0.42f;        // ~Sag knob at 6
    const float minSupply = 0.3f;

    float supply = 1.0f, env = 0.0f;
    const int total = (int) (0.8 * fs);
    const int noteOff = (int) (0.3 * fs);

    std::printf (" time(ms)  supply  gain(dB)\n");
    for (int i = 0; i < total; ++i)
    {
        const double t = i / fs;
        const float sig = (i < noteOff) ? 0.6f * std::sin (2.0 * M_PI * 110.0 * t) : 0.0f;

        const float rect = std::fabs (sig);
        env += (rect - env) * (rect > env ? envAtt : envRel);
        const float target = std::fmax (minSupply, 1.0f - depth * env);
        supply += (target - supply) * (target < supply ? droop : recover);

        if (i % (int) (fs * 0.02) == 0)   // every 20 ms
            std::printf ("%8.0f  %6.3f  %+6.2f%s\n", t * 1000.0, supply,
                         20.0 * std::log10 (supply),
                         i == 0 ? "" : (i < noteOff ? "  <- note ringing" : "  <- released (bloom)"));
    }
    return 0;
}

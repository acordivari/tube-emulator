// Offline sanity check for the Fender tone-stack coefficients.
//
//   c++ -std=c++17 -O2 tools/tonestack_check.cpp -o /tmp/ts && /tmp/ts
//
// Prints the magnitude response (dB) at several frequencies for a few knob
// settings. Expect: a clear midrange scoop (~300-700 Hz dipping well below the
// bass and treble shelves) when all knobs are at noon — the Fender signature.
#include "../Source/ToneStack.h"
#include <cmath>
#include <complex>
#include <cstdio>

static double magDb (const tonestack::Coeffs& c, double f, double fs)
{
    const double w = 2.0 * M_PI * f / fs;
    const std::complex<double> z1 = std::exp (std::complex<double> (0.0, -w));
    const std::complex<double> z2 = z1 * z1;
    const std::complex<double> z3 = z2 * z1;

    const auto num = c.b[0] + c.b[1] * z1 + c.b[2] * z2 + c.b[3] * z3;
    const auto den = c.a[0] + c.a[1] * z1 + c.a[2] * z2 + c.a[3] * z3;
    return 20.0 * std::log10 (std::abs (num / den));
}

int main()
{
    const double fs = 48000.0;
    const double freqs[] = { 50, 100, 200, 400, 700, 1000, 2000, 4000, 8000 };

    struct Setting { const char* name; double t, l, m; };
    const Setting settings[] = {
        { "all noon (t=l=m=0.5)", 0.5, 0.5, 0.5 },
        { "blues  (t .6 l .5 m .4)", 0.6, 0.5, 0.4 },
        { "treble max / mid min", 1.0, 0.5, 0.0 },
        { "bass max",              0.5, 1.0, 0.5 },
    };

    for (const auto& s : settings)
    {
        const auto c = tonestack::computeDigital (fs, s.t, s.l, s.m);
        std::printf ("\n%s\n  ", s.name);
        for (double f : freqs)
            std::printf ("%5.0fHz % 6.1f  ", f, magDb (c, f, fs));
        std::printf ("\n");
    }
    return 0;
}

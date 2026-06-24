#pragma once

#include <algorithm>

//==============================================================================
// Fender / '59 Bassman tone stack.
//
// Analog transfer function H(s) = (b1 s + b2 s^2 + b3 s^3)
//                               / (a0 + a1 s + a2 s^2 + a3 s^3)
// with coefficients that depend on the three pot fractions, then mapped to a
// 3rd-order digital IIR via the bilinear transform.
//
// Source: D. T. Yeh & J. O. Smith, "Discretization of the '59 Fender Bassman
// Tone Stack", Proc. DAFx-06, Montreal. The symbolic coefficients and the
// '59 Bassman component values below are taken verbatim from that paper.
//
// This header is deliberately free of any JUCE dependency so the same math can
// be unit-tested offline (see tools/tonestack_check.cpp).
//==============================================================================
namespace tonestack
{
    // '59 Bassman component values (farads / ohms).
    constexpr double C1 = 0.25e-9;
    constexpr double C2 = 20.0e-9;
    constexpr double C3 = 20.0e-9;
    constexpr double R1 = 250.0e3;   // treble pot
    constexpr double R2 = 1.0e6;     // bass pot
    constexpr double R3 = 25.0e3;    // mid pot
    constexpr double R4 = 56.0e3;    // slope resistor

    // Digital biquad-style coefficients, denominator normalised so a[0] == 1.
    struct Coeffs
    {
        double b[4];   // numerator   (z^0 .. z^-3)
        double a[4];   // denominator (z^0 .. z^-3), a[0] == 1
    };

    // t = treble, l = bass ("low"), m = mid; each a pot fraction in [0, 1].
    inline Coeffs computeDigital (double fs, double t, double l, double m)
    {
        // Keep the pots just off the rails to avoid degenerate all-zero terms.
        t = std::clamp (t, 1.0e-5, 1.0 - 1.0e-5);
        l = std::clamp (l, 1.0e-5, 1.0 - 1.0e-5);
        m = std::clamp (m, 1.0e-5, 1.0 - 1.0e-5);

        // Handy products.
        const double C1C2   = C1 * C2;
        const double C1C3   = C1 * C3;
        const double C2C3   = C2 * C3;
        const double C1C2C3 = C1 * C2 * C3;
        const double R3sq   = R3 * R3;

        // --- analog numerator coefficients ----------------------------------
        const double b1 = t * C1 * R1
                        + m * C3 * R3
                        + l * (C1 * R2 + C2 * R2)
                        + (C1 * R3 + C2 * R3);

        const double b2 = t  * (C1C2 * R1 * R4 + C1C3 * R1 * R4)
                        - m * m * (C1C3 * R3sq + C2C3 * R3sq)
                        + m  * (C1C3 * R1 * R3 + C1C3 * R3sq + C2C3 * R3sq)
                        + l  * (C1C2 * R1 * R2 + C1C2 * R2 * R4 + C1C3 * R2 * R4)
                        + l * m * (C1C3 * R2 * R3 + C2C3 * R2 * R3)
                        + (C1C2 * R1 * R3 + C1C2 * R3 * R4 + C1C3 * R3 * R4);

        const double b3 = l * m * (C1C2C3 * R1 * R2 * R3 + C1C2C3 * R2 * R3 * R4)
                        - m * m * (C1C2C3 * R1 * R3sq + C1C2C3 * R3sq * R4)
                        + m     * (C1C2C3 * R1 * R3sq + C1C2C3 * R3sq * R4)
                        + t     *  C1C2C3 * R1 * R3 * R4
                        - t * m *  C1C2C3 * R1 * R3 * R4
                        + t * l *  C1C2C3 * R1 * R2 * R4;

        // --- analog denominator coefficients --------------------------------
        const double a0 = 1.0;

        const double a1 = (C1 * R1 + C1 * R3 + C2 * R3 + C2 * R4 + C3 * R4)
                        + m * C3 * R3
                        + l * (C1 * R2 + C2 * R2);

        const double a2 = m * (C1C3 * R1 * R3 - C2C3 * R3 * R4 + C1C3 * R3sq + C2C3 * R3sq)
                        + l * m * (C1C3 * R2 * R3 + C2C3 * R2 * R3)
                        - m * m * (C1C3 * R3sq + C2C3 * R3sq)
                        + l * (C1C2 * R2 * R4 + C1C2 * R1 * R2 + C1C3 * R2 * R4 + C2C3 * R2 * R4)
                        + (C1C2 * R1 * R4 + C1C3 * R1 * R4 + C1C2 * R3 * R4
                           + C1C2 * R1 * R3 + C1C3 * R3 * R4 + C2C3 * R3 * R4);

        const double a3 = l * m * (C1C2C3 * R1 * R2 * R3 + C1C2C3 * R2 * R3 * R4)
                        - m * m * (C1C2C3 * R1 * R3sq + C1C2C3 * R3sq * R4)
                        + m     * (C1C2C3 * R3sq * R4 + C1C2C3 * R1 * R3sq - C1C2C3 * R1 * R3 * R4)
                        + l     *  C1C2C3 * R1 * R2 * R4
                        +          C1C2C3 * R1 * R3 * R4;

        // --- bilinear transform: s -> c (1 - z^-1)/(1 + z^-1), c = 2 fs ------
        // Each power of s expands over a common (1 + z^-1)^3 denominator:
        //   (1-z)^k (1+z)^(3-k), k = 0..3, gives the fixed integer rows below.
        const double c  = 2.0 * fs;
        const double c2 = c * c;
        const double c3 = c2 * c;

        // Numerator (analog b0 == 0).
        const double n1 = b1 * c;
        const double n2 = b2 * c2;
        const double n3 = b3 * c3;

        Coeffs out;
        double bd0 =  n1 + n2 + n3;
        double bd1 =  n1 - n2 - 3.0 * n3;
        double bd2 = -n1 - n2 + 3.0 * n3;
        double bd3 = -n1 + n2 - n3;

        // Denominator.
        const double d0 = a0;
        const double d1 = a1 * c;
        const double d2 = a2 * c2;
        const double d3 = a3 * c3;

        double ad0 = d0 + d1 + d2 + d3;
        double ad1 = 3.0 * d0 + d1 - d2 - 3.0 * d3;
        double ad2 = 3.0 * d0 - d1 - d2 + 3.0 * d3;
        double ad3 = d0 - d1 + d2 - d3;

        // Normalise so a[0] == 1.
        const double inv = 1.0 / ad0;
        out.b[0] = bd0 * inv; out.b[1] = bd1 * inv;
        out.b[2] = bd2 * inv; out.b[3] = bd3 * inv;
        out.a[0] = 1.0;       out.a[1] = ad1 * inv;
        out.a[2] = ad2 * inv; out.a[3] = ad3 * inv;
        return out;
    }
}

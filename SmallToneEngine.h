#pragma once
// ============================================================================
//  Small Tone — moteur DSP (C++17 pur, sans JUCE : testé seul par dsp_test.cpp)
//
//  Modèle "composant par composant" de l'Electro-Harmonix Small Clone (EH-4600) :
//
//   LFO  : LM358 (IC2a intégrateur 2.2 µF tant. + IC2b comparateur à hystérésis
//          120k / 470k). Polarisations : 9 V -180k- pin3 (4.09 V) -68k- pin6 (2.23 V) -82k- masse.
//          RATE = potentiomètre 1M "ccw log" + 47k en série dans le courant de charge.
//          -> triangle 0.86 V … 2.79 V, montée un peu plus rapide que la descente
//             (sortie LM358 basse ≈ 0 V, haute ≈ Vcc-1.4 V), ≈ 0.42 Hz … 9.4 Hz.
//   DEPTH: sortie LFO -4.7k- nœud X -2.7k- [switch] -masse ; 10 µF sur X ; X -39k- VCO.
//          switch "DIN." (2.7k à la masse)  : diviseur ×0.365 + RC rapide (≈ 9 Hz)
//          switch "DEPTH" (2.7k en l'air)   : pleine amplitude + RC lent (≈ 3.4 Hz)
//          => en DEPTH, plus RATE est rapide, plus le balayage est arrondi/réduit
//             (c'est ce qui donne le côté "liquide" du Small Clone).
//          Le nœud X garde sa charge : basculer le switch fait glisser le délai (comme l'original).
//   VCO  : horloge du BBD pilotée par la tension de X (≈ 85 … 195 kHz).
//   BBD  : MN3207, 1024 étages -> délai = 1024 / (2 · f_clk) ≈ 2.6 … 6 ms.
//          Filtres anti-repliement / reconstruction (≈ 4e ordre, ~5.5-6 kHz),
//          légère saturation, petit souffle, liaison capacitive.
//   MIX  : sec + traité à parts égales (pas de réglage de mix sur la pédale).
// ============================================================================
#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>

namespace smalltone
{
constexpr double kPi = 3.14159265358979323846;

//------------------------------------------------------------------------------
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void lowpass (double fs, double fc, double q)
    {
        const double w = 2 * kPi * fc / fs, c = std::cos (w), s = std::sin (w), al = s / (2 * q);
        const double a0 = 1 + al;
        b0 = (1 - c) * 0.5 / a0; b1 = (1 - c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
    void highpass (double fs, double fc, double q)
    {
        const double w = 2 * kPi * fc / fs, c = std::cos (w), s = std::sin (w), al = s / (2 * q);
        const double a0 = 1 + al;
        b0 = (1 + c) * 0.5 / a0; b1 = -(1 + c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
    inline double process (double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0; }
};

//------------------------------------------------------------------------------
// Potentiomètre RATE : 1M "ccw log" (piste à variation rapide au début) + 47k
inline double rateResistance (double pos01)
{
    pos01 = std::clamp (pos01, 0.0, 1.0);
    constexpr double k = 3.2;
    const double e0 = std::exp (-k);
    const double r = 1.0e6 * (std::exp (-k * pos01) - e0) / (1.0 - e0);
    return 47.0e3 + r;
}

//------------------------------------------------------------------------------
// LFO analogique (intégrateur + comparateur à hystérésis)
struct AnalogLfo
{
    // tensions du circuit (alim 9 V)
    static constexpr double VH = 7.6, VL = 0.05;           // sorties LM358 haute / basse
    static constexpr double Vint = 9.0 * 150.0 / 330.0;    // pin 3 (référence intégrateur) 4.09 V
    static constexpr double Vcmp = 9.0 * 82.0 / 330.0;     // pin 6 (référence comparateur) 2.23 V
    static constexpr double C = 2.2e-6;
    static double thrUp()   { return (590.0 * Vcmp - 120.0 * VL) / 470.0; }   // ≈ 2.79 V
    static double thrDown() { return (590.0 * Vcmp - 120.0 * VH) / 470.0; }   // ≈ 0.86 V

    double v = 1.8;          // tension du triangle (sortie IC2a)
    bool high = true;        // état du comparateur

    // période pour une résistance R donnée (formule exacte du modèle)
    static double periodFor (double R)
    {
        const double dv = thrUp() - thrDown();
        return C * R * dv * (1.0 / (VH - Vint) + 1.0 / (Vint - VL));
    }
    static double resistanceForFrequency (double hz) { return 1.0 / (hz * periodFor (1.0)); }

    inline double tick (double R, double dt)
    {
        const double vout = high ? VH : VL;
        v -= (vout - Vint) / (R * C) * dt;          // intégrateur inverseur
        if (high && v <= thrDown()) { v = thrDown() + (thrDown() - v); high = false; }
        else if (! high && v >= thrUp()) { v = thrUp() - (v - thrUp()); high = true; }
        return v;
    }
    double normalised() const { return (v - thrDown()) / (thrUp() - thrDown()); }   // 0 … 1
};

//------------------------------------------------------------------------------
// Réseau DEPTH (4.7k / 2.7k commuté / 10 µF / 39k vers le VCO)
struct DepthNetwork
{
    double vx = 0.66;
    // deep = true : 2.7k en l'air (switch sur "DEPTH")
    inline double tick (double vin, bool deep, double dt)
    {
        constexpr double R1 = 4.7e3, R2 = 2.7e3, R3 = 39.0e3, C = 10.0e-6;
        constexpr double Rvco = 100.0e3;                     // charge vue par le 39k (entrée VCO)
        // C·dvx/dt = (vin - vx)/R1 - vx/(R3+Rvco) - [vx/R2]   (Euler implicite : stable à tout pas)
        const double g = 1.0 / R1 + 1.0 / (R3 + Rvco) + (deep ? 0.0 : 1.0 / R2);
        const double a = dt / C;
        vx = (vx + a * (vin / R1)) / (1.0 + a * g);
        return vx;   // tension de commande du VCO
    }
};

//------------------------------------------------------------------------------
// VCO + BBD : tension de commande -> délai
inline double clockFromControl (double vc)          // Hz
{
    // l'horloge ralentit quand la tension monte (délai plus long)
    return std::clamp (210.0e3 - 45.0e3 * vc, 40.0e3, 260.0e3);
}
inline double bbdDelaySeconds (double fclk) { return 1024.0 / (2.0 * fclk); }

//------------------------------------------------------------------------------
class Engine
{
public:
    struct Settings
    {
        double rate = 0.5;          // position du potard 0 … 1
        bool deep = true;           // switch DEPTH (true = "DEPTH", false = "DIN.")
        bool on = true;             // footswitch
        bool stereoSpread = false;  // canal droit avec LFO inversé (option, l'original est mono)
        bool noise = true;          // souffle du BBD
        double syncHz = 0.0;        // > 0 : fréquence du LFO imposée (synchro tempo)
    };

    void prepare (double sampleRate, int /*maxBlock*/)
    {
        fs = sampleRate;
        const int maxDelay = (int) std::ceil (0.012 * fs) + 8;
        size = 1; while (size < maxDelay) size <<= 1;
        mask = size - 1;
        for (auto& ch : chans) { ch.buf.assign ((size_t) size, 0.0f); ch.w = 0; }
        for (auto& ch : chans)
        {
            ch.pre1.lowpass (fs, std::min (6200.0, fs * 0.45), 0.54);
            ch.pre2.lowpass (fs, std::min (6200.0, fs * 0.45), 1.31);
            ch.post1.lowpass (fs, std::min (5600.0, fs * 0.45), 0.54);
            ch.post2.lowpass (fs, std::min (5600.0, fs * 0.45), 1.31);
            ch.hpWet.highpass (fs, 70.0, 0.6);
            ch.hpIn.highpass (fs, 12.0, 0.6);
            ch.pre1.reset(); ch.pre2.reset(); ch.post1.reset(); ch.post2.reset(); ch.hpWet.reset(); ch.hpIn.reset();
        }
        depthL = DepthNetwork{}; depthR = DepthNetwork{};
        // nœud X préchargé à sa valeur moyenne
        const double mid = 0.5 * (AnalogLfo::thrUp() + AnalogLfo::thrDown());
        depthL.vx = depthR.vx = mid;
        mix = 1.0;
        delaySmooth[0] = delaySmooth[1] = bbdDelaySeconds (clockFromControl (mid)) * fs;
        rateR = rateResistance (0.5);
    }

    // traite un bloc (in-place). nCh = 1 ou 2
    void process (float* const* io, int nCh, int n, const Settings& st)
    {
        const double dt = 1.0 / fs;
        const double targetR = st.syncHz > 0.0 ? AnalogLfo::resistanceForFrequency (st.syncHz)
                                                : rateResistance (st.rate);
        const double mixTarget = st.on ? 1.0 : 0.0;
        const double mixStep = 1.0 / (0.010 * fs);
        const double rCoef = std::exp (-1.0 / (0.030 * fs));     // potard : pas de saut de fréquence
        const double dCoef = std::exp (-1.0 / (0.00025 * fs));   // lissage du délai (≈ BBD analogique)

        for (int i = 0; i < n; ++i)
        {
            rateR = targetR + (rateR - targetR) * rCoef;
            // petite dérive analogique de l'horloge du LFO
            drift += (rnd() * 2.0 - 1.0) * 0.0004; drift *= 0.9995;
            const double vt = lfo.tick (rateR * (1.0 + drift * 0.02), dt);
            lfoVis = lfo.normalised();

            const double vxL = depthL.tick (vt, st.deep, dt);
            const double vtR = st.stereoSpread ? (AnalogLfo::thrUp() + AnalogLfo::thrDown()) - vt : vt;
            const double vxR = st.stereoSpread ? depthR.tick (vtR, st.deep, dt) : vxL;
            if (! st.stereoSpread) depthR.vx = depthL.vx;
            controlV = vxL;

            const double dL = bbdDelaySeconds (clockFromControl (vxL)) * fs;
            const double dR = bbdDelaySeconds (clockFromControl (vxR)) * fs;
            delaySmooth[0] = dL + (delaySmooth[0] - dL) * dCoef;
            delaySmooth[1] = dR + (delaySmooth[1] - dR) * dCoef;
            currentDelayMs = delaySmooth[0] / fs * 1000.0;

            if (mix < mixTarget) mix = std::min (mixTarget, mix + mixStep);
            else if (mix > mixTarget) mix = std::max (mixTarget, mix - mixStep);

            for (int c = 0; c < nCh && c < 2; ++c)
            {
                auto& ch = chans[(size_t) c];
                const double x = io[c][i];
                const double dry = ch.hpIn.process (x);

                // --- chemin BBD
                double w = ch.pre2.process (ch.pre1.process (dry * 1.05));
                w = bbdSaturate (w);
                if (st.noise) w += (rnd() - 0.5) * 2.0e-4;                       // ≈ -80 dBFS
                ch.buf[(size_t) ch.w] = (float) w;
                const double rd = readFrac (ch, delaySmooth[(size_t) c]);
                ch.w = (ch.w + 1) & mask;
                double wet = ch.post2.process (ch.post1.process (rd));
                wet = ch.hpWet.process (wet) * 0.93;                                // perte du BBD

                const double fx = (dry + wet) * kMixGain;
                io[c][i] = (float) (x + (fx - x) * mix);
            }
        }
    }

    // infos pour l'interface
    double lfoNormalised() const { return lfoVis; }
    double delayMs() const { return currentDelayMs; }
    double controlVoltage() const { return controlV; }
    static double rateHz (double pos) { return 1.0 / AnalogLfo::periodFor (rateResistance (pos)); }

    static constexpr double kMixGain = 0.62;

private:
    struct Channel
    {
        std::vector<float> buf;
        int w = 0;
        Biquad pre1, pre2, post1, post2, hpWet, hpIn;
    };

    static inline double bbdSaturate (double x)
    {
        // linéaire jusqu'à ~ -6 dBFS puis compression douce (le MN3207 sature tôt)
        const double t = 0.62;
        const double a = std::abs (x);
        if (a <= t) return x;
        const double over = (a - t) / (1.0 - t);
        return std::copysign (t + (1.0 - t) * std::tanh (over), x);
    }

    inline double readFrac (const Channel& ch, double d) const
    {
        d = std::clamp (d, 2.0, (double) size - 4.0);
        const double rp = (double) ch.w - d;
        const double fl = std::floor (rp);
        const double f = rp - fl;
        const int i1 = ((int) fl) & mask;
        const int i0 = (i1 - 1) & mask, i2 = (i1 + 1) & mask, i3 = (i1 + 2) & mask;
        const double y0 = ch.buf[(size_t) i0], y1 = ch.buf[(size_t) i1], y2 = ch.buf[(size_t) i2], y3 = ch.buf[(size_t) i3];
        // Hermite (4 points)
        const double c0 = y1, c1 = 0.5 * (y2 - y0);
        const double c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3;
        const double c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + c0;
    }

    inline double rnd() { seed = seed * 1664525u + 1013904223u; return (double) (seed >> 8) / 16777216.0; }

    double fs = 48000.0;
    int size = 1024, mask = 1023;
    Channel chans[2];
    AnalogLfo lfo;
    DepthNetwork depthL, depthR;
    double rateR = 200e3, mix = 1.0, drift = 0;
    double delaySmooth[2] { 200, 200 };
    double lfoVis = 0, currentDelayMs = 0, controlV = 0;
    uint32_t seed = 0x1234567u;
};
} // namespace smalltone

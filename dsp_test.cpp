// Test du moteur Small Tone (compilé et lancé par GitHub Actions sur Linux)
//   g++ -O2 -std=c++17 -I. dsp_test.cpp -o dsp_test && ./dsp_test
#include "SmallToneEngine.h"
#include <cstdio>
#include <cmath>
#include <vector>

using namespace smalltone;
static int fails = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf ("  ok   "); } else { std::printf ("  FAIL "); ++fails; } std::printf (__VA_ARGS__); std::printf ("\n"); } while (0)

// fréquence du LFO mesurée en comptant les basculements du comparateur
static double measureLfoHz (double pos, double fs, double syncHz = 0)
{
    Engine e; e.prepare (fs, 512);
    Engine::Settings st; st.rate = pos; st.syncHz = syncHz;
    std::vector<float> l (512, 0.0f), r (512, 0.0f);
    float* io[2] { l.data(), r.data() };
    const int blocks = (int) (fs * 30.0 / 512);
    double last = e.lfoNormalised(); bool rising = false; int peaks = 0; double t0 = -1, t1 = 0;
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < 512; ++i)   // un échantillon à la fois pour suivre le LFO
        {
            float* one[2] { l.data() + i, r.data() + i };
            e.process (one, 2, 1, st);
            const double v = e.lfoNormalised();
            const double t = (b * 512 + i) / fs;
            if (rising && v < last && t > 2.0) { if (t0 < 0) t0 = t; t1 = t; ++peaks; }
            rising = v > last; last = v;
        }
        (void) io;
    }
    return peaks > 1 ? (peaks - 1) / (t1 - t0) : 0.0;
}

static void delayRange (bool deep, double pos, double& dmin, double& dmax, double fs = 48000)
{
    Engine e; e.prepare (fs, 256);
    Engine::Settings st; st.rate = pos; st.deep = deep;
    std::vector<float> l (256, 0.0f), r (256, 0.0f);
    dmin = 1e9; dmax = 0;
    for (int b = 0; b < (int) (fs * 20 / 256); ++b)
    {
        float* io[2] { l.data(), r.data() };
        for (int i = 0; i < 256; ++i)
        {
            float* one[2] { l.data() + i, r.data() + i };
            e.process (one, 2, 1, st);
            if (b * 256 > fs * 8) { dmin = std::min (dmin, e.delayMs()); dmax = std::max (dmax, e.delayMs()); }
        }
        (void) io;
    }
}

int main()
{
    std::printf ("Small Tone - test du moteur\n\n[LFO]\n");
    const double fMin = measureLfoHz (0.0, 48000), fMid = measureLfoHz (0.5, 48000), fMax = measureLfoHz (1.0, 48000);
    std::printf ("  RATE min %.3f Hz | milieu %.3f Hz | max %.3f Hz  (formule : %.3f / %.3f / %.3f)\n",
                 fMin, fMid, fMax, Engine::rateHz (0), Engine::rateHz (0.5), Engine::rateHz (1));
    CHECK (fMin > 0.3 && fMin < 0.55, "RATE minimum ~0.42 Hz");
    CHECK (fMax > 8.0 && fMax < 11.0, "RATE maximum ~9.4 Hz");
    CHECK (std::abs (fMid - Engine::rateHz (0.5)) / fMid < 0.03, "mesure = formule du circuit");
    const double fs96 = measureLfoHz (0.5, 96000);
    CHECK (std::abs (fs96 - fMid) / fMid < 0.02, "LFO indépendant de la fréquence d'échantillonnage (96 kHz : %.3f Hz)", fs96);
    const double fSync = measureLfoHz (0.5, 48000, 2.0);   // 120 BPM, noire
    CHECK (std::abs (fSync - 2.0) < 0.04, "synchro tempo : 2.000 Hz demandé, %.3f Hz mesuré", fSync);

    std::printf ("\n[Délai du BBD]\n");
    double a, b;
    delayRange (true, 0.15, a, b);  std::printf ("  DEPTH lent  : %.2f … %.2f ms\n", a, b);
    CHECK (a > 2.4 && b < 7.0 && b - a > 2.0, "balayage profond (> 2 ms)");
    const double deepSlow = b - a;
    delayRange (false, 0.15, a, b); std::printf ("  DIN.  lent  : %.2f … %.2f ms\n", a, b);
    CHECK (b - a < deepSlow * 0.4 && b - a > 0.2, "DIN. nettement plus léger que DEPTH");
    delayRange (true, 1.0, a, b);   std::printf ("  DEPTH rapide: %.2f … %.2f ms\n", a, b);
    CHECK (b - a < deepSlow * 0.5, "RATE rapide : balayage arrondi par le 10 uF (comme l'original)");

    std::printf ("\n[Audio]\n");
    {
        const double fs = 48000;
        Engine e; e.prepare (fs, 512);
        Engine::Settings st;
        std::vector<float> l (512), r (512);
        double ph = 0, sumIn = 0, sumOut = 0, peak = 0; bool finite = true;
        for (int blk = 0; blk < 400; ++blk)
        {
            for (int i = 0; i < 512; ++i) { const float s = (float) (0.3 * std::sin (ph)); ph += 2 * kPi * 330.0 / fs; l[(size_t) i] = r[(size_t) i] = s; sumIn += s * s; }
            float* io[2] { l.data(), r.data() };
            e.process (io, 2, 512, st);
            for (int i = 0; i < 512; ++i) { finite &= std::isfinite (l[(size_t) i]); sumOut += l[(size_t) i] * l[(size_t) i]; peak = std::max (peak, (double) std::abs (l[(size_t) i])); }
        }
        const double g = 10 * std::log10 (sumOut / sumIn);
        CHECK (finite, "aucun NaN / inf");
        CHECK (g > -4.0 && g < 1.5, "niveau effet ≈ niveau bypass (%.2f dB)", g);
        CHECK (peak < 0.6, "pas de dépassement (crête %.3f pour 0.3 en entrée)", peak);

        // bypass : sortie = entrée après le fondu
        st.on = false;
        double err = 0;
        for (int blk = 0; blk < 20; ++blk)
        {
            for (int i = 0; i < 512; ++i) { const float s = (float) (0.3 * std::sin (ph)); ph += 2 * kPi * 330.0 / fs; l[(size_t) i] = r[(size_t) i] = s; }
            std::vector<float> ref = l;
            float* io[2] { l.data(), r.data() };
            e.process (io, 2, 512, st);
            if (blk > 2) for (int i = 0; i < 512; ++i) err = std::max (err, (double) std::abs (l[(size_t) i] - ref[(size_t) i]));
        }
        CHECK (err < 1e-6, "bypass transparent (écart %.2e)", err);
    }
    {
        // chaque fréquence d'échantillonnage courante
        for (double fs : { 44100.0, 88200.0, 192000.0 })
        {
            Engine e; e.prepare (fs, 2048); Engine::Settings st; st.stereoSpread = true;
            std::vector<float> l (2048, 0.1f), r (2048, -0.1f); bool ok = true;
            for (int k = 0; k < 50; ++k) { float* io[2] { l.data(), r.data() }; e.process (io, 2, 2048, st); for (float v : l) ok &= std::isfinite (v) && std::abs (v) < 2; }
            CHECK (ok, "stable à %.0f Hz (stéréo élargie)", fs);
        }
    }

    std::printf ("\n%s (%d échec(s))\n", fails == 0 ? "TOUT EST OK" : "ÉCHEC", fails);
    return fails == 0 ? 0 : 1;
}

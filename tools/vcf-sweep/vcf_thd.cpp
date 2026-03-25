// VCF self-oscillation THD (total harmonic distortion) test.
//
// Usage: vcf_thd [res] [samplerate]
//   Defaults: res=1.0, 44100
//
// Self-oscillates the filter at several frequencies and measures
// harmonic distortion via DFT. Reports fundamental amplitude,
// THD percentage, and individual harmonic levels.
//
// Output (stdout): CSV with columns:
//   target_hz, fund_hz, fund_db, h2_db, h3_db, h4_db, h5_db, thd_pct

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <vector>

#include "../../Source/DSP/KR106VCF.h"

static constexpr int kNumHarmonics = 5; // fundamental + 4 harmonics
static constexpr float kSettleSec = 1.0f;
static constexpr float kMeasureSec = 1.0f;

// Goertzel algorithm: compute magnitude of a single DFT bin.
static double goertzelMag(const float* data, int N, double binFreq)
{
    double w = 2.0 * M_PI * binFreq / N;
    double coeff = 2.0 * cos(w);
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;

    for (int i = 0; i < N; i++)
    {
        s0 = static_cast<double>(data[i]) + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }

    double real = s1 - s2 * cos(w);
    double imag = s2 * sin(w);
    return 2.0 * sqrt(real * real + imag * imag) / N;
}

// Find exact frequency by zero-crossing count, then refine with
// parabolic interpolation around the Goertzel peak.
static double findFundamental(const float* data, int N, double sr)
{
    // Zero-crossing estimate
    int crossings = 0;
    for (int i = 1; i < N; i++)
        if (data[i - 1] <= 0.f && data[i] > 0.f)
            crossings++;
    double zcHz = static_cast<double>(crossings) * sr / N;

    // Refine: search around zcHz with Goertzel in 0.1 Hz steps
    double bestHz = zcHz;
    double bestMag = 0.0;
    double binRes = sr / N;
    double lo = std::max(zcHz - 2.0 * binRes, 1.0);
    double hi = zcHz + 2.0 * binRes;
    double step = binRes * 0.1;

    for (double f = lo; f <= hi; f += step)
    {
        double bin = f * N / sr;
        double mag = goertzelMag(data, N, bin);
        if (mag > bestMag)
        {
            bestMag = mag;
            bestHz = f;
        }
    }

    return bestHz;
}

struct THDResult {
    double fundHz;
    double fundAmp;               // linear amplitude of fundamental
    double harmonicAmp[kNumHarmonics]; // [0]=fund, [1]=h2, [2]=h3, ...
    double thdPct;
    bool oscillating;
};

static THDResult measureTHD(float frq, float res, float sr)
{
    kr106::VCF vcf;
    vcf.SetSampleRate(sr);
    vcf.Reset();

    int settle = static_cast<int>(sr * kSettleSec);
    int measure = static_cast<int>(sr * kMeasureSec);

    // Settle
    for (int i = 0; i < settle; i++)
        vcf.Process(0.f, frq, res);

    // Capture
    std::vector<float> buf(measure);
    for (int i = 0; i < measure; i++)
        buf[i] = vcf.Process(0.f, frq, res);

    // Check if oscillating
    float peak = 0.f;
    for (float s : buf)
        peak = std::max(peak, fabsf(s));

    THDResult r = {};
    r.oscillating = (peak >= 0.01f);
    if (!r.oscillating) return r;

    // Apply Hann window to reduce spectral leakage
    std::vector<float> windowed(measure);
    for (int i = 0; i < measure; i++)
    {
        float w = 0.5f * (1.f - cosf(2.f * static_cast<float>(M_PI) * i / (measure - 1)));
        windowed[i] = buf[i] * w;
    }

    // Find fundamental frequency
    r.fundHz = findFundamental(windowed.data(), measure, sr);

    // Measure each harmonic via Goertzel
    double distortionPower = 0.0;
    for (int h = 0; h < kNumHarmonics; h++)
    {
        double hzH = r.fundHz * (h + 1);
        if (hzH > sr * 0.5) // above Nyquist
        {
            r.harmonicAmp[h] = 0.0;
            continue;
        }
        double bin = hzH * measure / sr;
        r.harmonicAmp[h] = goertzelMag(windowed.data(), measure, bin);
        if (h > 0)
            distortionPower += r.harmonicAmp[h] * r.harmonicAmp[h];
    }

    r.fundAmp = r.harmonicAmp[0];
    if (r.fundAmp > 1e-10)
        r.thdPct = 100.0 * sqrt(distortionPower) / r.fundAmp;
    else
        r.thdPct = 0.0;

    return r;
}

static double toDb(double amp)
{
    return amp > 1e-10 ? 20.0 * log10(amp) : -200.0;
}

int main(int argc, char* argv[])
{
    float res = (argc > 1) ? static_cast<float>(atof(argv[1])) : 1.0f;
    float sr  = (argc > 2) ? static_cast<float>(atof(argv[2])) : 44100.f;

    float testHz[] = {110.f, 440.f, 1760.f, 7040.f};
    int nTests = 4;

    fprintf(stderr, "VCF THD test: res=%.2f, sr=%.0f Hz\n", res, sr);
    fprintf(stderr, "Settle %.1fs, measure %.1fs per frequency\n\n", kSettleSec, kMeasureSec);

    printf("target_hz,fund_hz,fund_db,h2_db,h3_db,h4_db,h5_db,thd_pct\n");

    for (int i = 0; i < nTests; i++)
    {
        float frq = testHz[i] / (sr * 0.5f);
        THDResult r = measureTHD(frq, res, sr);

        if (!r.oscillating)
        {
            printf("%.0f,,,,,,,not oscillating\n", testHz[i]);
            fprintf(stderr, "  %6.0f Hz: not oscillating\n", testHz[i]);
            continue;
        }

        printf("%.0f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.3f\n",
               testHz[i], r.fundHz,
               toDb(r.harmonicAmp[0]),
               toDb(r.harmonicAmp[1]),
               toDb(r.harmonicAmp[2]),
               toDb(r.harmonicAmp[3]),
               toDb(r.harmonicAmp[4]),
               r.thdPct);

        fprintf(stderr, "  %6.0f Hz: fund=%.1f Hz, THD=%.2f%%",
                testHz[i], r.fundHz, r.thdPct);
        for (int h = 0; h < kNumHarmonics; h++)
            fprintf(stderr, "  H%d=%.1fdB", h + 1, toDb(r.harmonicAmp[h]));
        fprintf(stderr, "\n");
    }

    return 0;
}

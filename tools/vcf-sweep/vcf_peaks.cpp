// VCF peak comparison: saw through open filter vs self-oscillation.
//
// Usage: vcf_peaks [samplerate]
//   Default: 44100
//
// At each frequency (8 octaves from 55 Hz), measures:
//   1. Peak amplitude of saw wave through wide-open VCF (frq matched, res=0)
//   2. Peak amplitude of self-oscillation (no input, res=1.0)
//
// Output (stdout): CSV with columns:
//   freq_hz, saw_peak, saw_db, osc_peak, osc_db, ratio_db

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <vector>

#include "../../Source/DSP/KR106VCF.h"
#include "../../Source/DSP/KR106OscillatorsWT.h"

static constexpr float kSettleSec = 0.5f;
static constexpr float kMeasureSec = 0.5f;

static float measureSawPeak(float freqHz, float sr, kr106::SawTables& tables)
{
    kr106::VCF vcf;
    vcf.SetSampleRate(sr);
    vcf.Reset();

    kr106::OscillatorsWT osc;
    osc.SetTables(&tables);
    osc.Init(sr);

    float cps = freqHz / sr;
    float frq = freqHz / (sr * 0.5f); // normalized cutoff = note frequency

    int settle = static_cast<int>(sr * kSettleSec);
    int measure = static_cast<int>(sr * kMeasureSec);

    // Settle: run oscillator + filter to reach steady state
    for (int i = 0; i < settle; i++)
    {
        bool sync = false;
        float saw = osc.Process(cps, 0.5f, true, false, false, 0.f, 0.f, sync);
        vcf.Process(saw, frq, 0.f);
    }

    // Measure peak
    float peak = 0.f;
    for (int i = 0; i < measure; i++)
    {
        bool sync = false;
        float saw = osc.Process(cps, 0.5f, true, false, false, 0.f, 0.f, sync);
        float out = vcf.Process(saw, frq, 0.f);
        peak = std::max(peak, fabsf(out));
    }
    return peak;
}

static float measureOscPeak(float freqHz, float sr)
{
    kr106::VCF vcf;
    vcf.SetSampleRate(sr);
    vcf.Reset();

    float frq = freqHz / (sr * 0.5f);

    int settle = static_cast<int>(sr * kSettleSec);
    int measure = static_cast<int>(sr * kMeasureSec);

    for (int i = 0; i < settle; i++)
        vcf.Process(0.f, frq, 1.f);

    float peak = 0.f;
    for (int i = 0; i < measure; i++)
    {
        float out = vcf.Process(0.f, frq, 1.f);
        peak = std::max(peak, fabsf(out));
    }
    return peak;
}

static double toDb(double amp)
{
    return amp > 1e-10 ? 20.0 * log10(amp) : -200.0;
}

int main(int argc, char* argv[])
{
    float sr = (argc > 1) ? static_cast<float>(atof(argv[1])) : 44100.f;

    kr106::SawTables tables;
    tables.Init(sr);

    fprintf(stderr, "VCF peak comparison: saw (res=0) vs self-osc (res=1.0), sr=%.0f\n", sr);
    fprintf(stderr, "Settle %.1fs, measure %.1fs per test\n\n", kSettleSec, kMeasureSec);

    printf("freq_hz,saw_peak,saw_db,osc_peak,osc_db,ratio_db\n");

    float hz = 55.f;
    for (int oct = 0; oct < 8; oct++)
    {
        float sawPeak = measureSawPeak(hz, sr, tables);
        float oscPeak = measureOscPeak(hz, sr);

        double sawDb = toDb(sawPeak);
        double oscDb = toDb(oscPeak);
        double ratioDb = oscDb - sawDb;

        printf("%.0f,%.4f,%.1f,%.4f,%.1f,%+.1f\n",
               hz, sawPeak, sawDb, oscPeak, oscDb, ratioDb);

        fprintf(stderr, "  %6.0f Hz: saw=%.4f (%+.1f dB)  osc=%.4f (%+.1f dB)  ratio=%+.1f dB\n",
                hz, sawPeak, sawDb, oscPeak, oscDb, ratioDb);

        hz *= 2.f;
    }

    return 0;
}

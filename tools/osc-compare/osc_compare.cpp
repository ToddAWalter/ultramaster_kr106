// Standalone oscillator comparison tool
// Renders PolyBLEP and wavetable oscillators side-by-side and reports
// peak/RMS difference plus alias energy above Nyquist/2.
//
// Usage: osc_compare [freq_hz] [samplerate]
//   Defaults: 440 Hz, 44100 Hz
//
// Output: CSV to stdout (blep,wavetable columns), stats to stderr.

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <complex>
#include <algorithm>
#include <numeric>

#include "../../Source/DSP/KR106Oscillators.h"
#include "../../Source/DSP/KR106OscillatorsWT.h"

using Complex = std::complex<double>;

static void fft(std::vector<Complex>& x)
{
    size_t N = x.size();
    for (size_t i = 1, j = 0; i < N; i++) {
        size_t bit = N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }
    for (size_t len = 2; len <= N; len <<= 1) {
        double angle = -2.0 * M_PI / static_cast<double>(len);
        Complex wn(cos(angle), sin(angle));
        for (size_t i = 0; i < N; i += len) {
            Complex w(1.0);
            for (size_t j = 0; j < len / 2; j++) {
                Complex u = x[i + j], v = w * x[i + j + len / 2];
                x[i + j] = u + v;
                x[i + j + len / 2] = u - v;
                w *= wn;
            }
        }
    }
}

// Measure alias energy: sum of bins above targetBin (fundamental's Nyquist/2 region)
static double aliasEnergy(const std::vector<float>& buf, int fundamental_bin)
{
    size_t N = buf.size();
    std::vector<Complex> spectrum(N);
    for (size_t i = 0; i < N; i++)
        spectrum[i] = Complex(buf[i], 0.0);
    fft(spectrum);

    // Signal energy in fundamental + harmonics up to Nyquist/2
    int nyquist2_bin = static_cast<int>(N) / 4;
    double signal = 0.0, alias = 0.0;
    for (int i = 1; i < static_cast<int>(N) / 2; i++) {
        double mag2 = std::norm(spectrum[i]);
        if (i <= nyquist2_bin)
            signal += mag2;
        else
            alias += mag2;
    }
    if (signal < 1e-30) return -999.0;
    return 10.0 * log10(alias / signal);
}

int main(int argc, char* argv[])
{
    float freq = (argc > 1) ? static_cast<float>(atof(argv[1])) : 440.f;
    float sr   = (argc > 2) ? static_cast<float>(atof(argv[2])) : 44100.f;

    float cps = freq / sr;
    int numSamples = 8192;

    // --- PolyBLEP oscillator ---
    kr106::Oscillators blep;
    blep.Init(sr);

    // --- Wavetable oscillator ---
    kr106::SawTables tables;
    tables.Init(sr);
    kr106::OscillatorsWT wt;
    wt.Init(sr);
    wt.SetTables(&tables);

    // Pulse width 50%
    float pw = 0.5f;

    // Render: saw + pulse + sub all on, levels at 1.0
    std::vector<float> blepBuf(numSamples), wtBuf(numSamples);

    fprintf(stderr, "Rendering %d samples at %.0f Hz (sr=%.0f, cps=%.6f)\n",
            numSamples, freq, sr, cps);

    // Pre-taper levels (pass 1.0 for full level, already tapered)
    float subAT = kr106::Oscillators::AudioTaper(1.f);
    float noiseAT = 0.f; // no noise for comparison

    for (int i = 0; i < numSamples; i++) {
        bool sync = false;
        blepBuf[i] = blep.Process(cps, pw, true, false, false, subAT, noiseAT, sync);
        wtBuf[i]   = wt.Process(cps, pw, true, false, false, subAT, noiseAT, sync);
    }

    // --- Stats ---
    float peakDiff = 0.f, rmsDiff = 0.f;
    for (int i = 0; i < numSamples; i++) {
        float d = blepBuf[i] - wtBuf[i];
        peakDiff = std::max(peakDiff, fabsf(d));
        rmsDiff += d * d;
    }
    rmsDiff = sqrtf(rmsDiff / static_cast<float>(numSamples));

    int fund_bin = static_cast<int>(roundf(freq * numSamples / sr));
    double blepAlias = aliasEnergy(blepBuf, fund_bin);
    double wtAlias = aliasEnergy(wtBuf, fund_bin);

    fprintf(stderr, "\n=== Saw-only comparison at %.0f Hz ===\n", freq);
    fprintf(stderr, "Peak difference:  %.6f (%.1f dB)\n", peakDiff,
            peakDiff > 1e-10f ? 20.f * log10f(peakDiff) : -200.f);
    fprintf(stderr, "RMS difference:   %.6f (%.1f dB)\n", rmsDiff,
            rmsDiff > 1e-10f ? 20.f * log10f(rmsDiff) : -200.f);
    fprintf(stderr, "PolyBLEP alias energy (>Nyq/2): %.1f dB rel signal\n", blepAlias);
    fprintf(stderr, "Wavetable alias energy (>Nyq/2): %.1f dB rel signal\n", wtAlias);

    // --- Repeat for pulse ---
    blep.Reset(); blep.Init(sr);
    wt.Reset(); wt.Init(sr); wt.SetTables(&tables);

    std::vector<float> blepPulse(numSamples), wtPulse(numSamples);
    for (int i = 0; i < numSamples; i++) {
        bool sync = false;
        blepPulse[i] = blep.Process(cps, pw, false, true, false, subAT, noiseAT, sync);
        wtPulse[i]   = wt.Process(cps, pw, false, true, false, subAT, noiseAT, sync);
    }

    peakDiff = 0.f; rmsDiff = 0.f;
    for (int i = 0; i < numSamples; i++) {
        float d = blepPulse[i] - wtPulse[i];
        peakDiff = std::max(peakDiff, fabsf(d));
        rmsDiff += d * d;
    }
    rmsDiff = sqrtf(rmsDiff / static_cast<float>(numSamples));

    double blepPulseAlias = aliasEnergy(blepPulse, fund_bin);
    double wtPulseAlias = aliasEnergy(wtPulse, fund_bin);

    fprintf(stderr, "\n=== Pulse (50%%) comparison at %.0f Hz ===\n", freq);
    fprintf(stderr, "Peak difference:  %.6f (%.1f dB)\n", peakDiff,
            peakDiff > 1e-10f ? 20.f * log10f(peakDiff) : -200.f);
    fprintf(stderr, "RMS difference:   %.6f (%.1f dB)\n", rmsDiff,
            rmsDiff > 1e-10f ? 20.f * log10f(rmsDiff) : -200.f);
    fprintf(stderr, "PolyBLEP alias energy (>Nyq/2): %.1f dB rel signal\n", blepPulseAlias);
    fprintf(stderr, "Wavetable alias energy (>Nyq/2): %.1f dB rel signal\n", wtPulseAlias);

    // --- Repeat for sub ---
    blep.Reset(); blep.Init(sr);
    wt.Reset(); wt.Init(sr); wt.SetTables(&tables);

    std::vector<float> blepSub(numSamples), wtSub(numSamples);
    for (int i = 0; i < numSamples; i++) {
        bool sync = false;
        blepSub[i] = blep.Process(cps, pw, false, false, true, subAT, noiseAT, sync);
        wtSub[i]   = wt.Process(cps, pw, false, false, true, subAT, noiseAT, sync);
    }

    peakDiff = 0.f; rmsDiff = 0.f;
    for (int i = 0; i < numSamples; i++) {
        float d = blepSub[i] - wtSub[i];
        peakDiff = std::max(peakDiff, fabsf(d));
        rmsDiff += d * d;
    }
    rmsDiff = sqrtf(rmsDiff / static_cast<float>(numSamples));

    double blepSubAlias = aliasEnergy(blepSub, fund_bin);
    double wtSubAlias = aliasEnergy(wtSub, fund_bin);

    fprintf(stderr, "\n=== Sub comparison at %.0f Hz ===\n", freq);
    fprintf(stderr, "Peak difference:  %.6f (%.1f dB)\n", peakDiff,
            peakDiff > 1e-10f ? 20.f * log10f(peakDiff) : -200.f);
    fprintf(stderr, "RMS difference:   %.6f (%.1f dB)\n", rmsDiff,
            rmsDiff > 1e-10f ? 20.f * log10f(rmsDiff) : -200.f);
    fprintf(stderr, "PolyBLEP alias energy (>Nyq/2): %.1f dB rel signal\n", blepSubAlias);
    fprintf(stderr, "Wavetable alias energy (>Nyq/2): %.1f dB rel signal\n", wtSubAlias);

    // --- CSV output (saw only) ---
    fprintf(stdout, "sample,blep,wavetable\n");
    for (int i = 0; i < numSamples; i++)
        fprintf(stdout, "%d,%.8f,%.8f\n", i, blepBuf[i], wtBuf[i]);

    return 0;
}

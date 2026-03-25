// Standalone VCF sweep — generates a mono 24-bit PCM .wav file.
//
// Usage: vcf_sweep [output.wav] [duration_sec] [samplerate] [mode] [res] [noise]
//   Defaults: sweep.wav, 10s, 44100, j106, 1.0, 0.0
//   mode: "j6" or "j106" (selects slider→Hz conversion)
//   res:  resonance 0.0-1.0
//   noise: white noise input amplitude 0.0-1.0
//
// Sweeps VCF slider 0→1. With noise=0, this is pure self-oscillation.
// With noise>0, white noise is fed through the filter during the sweep.

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <vector>
#include <algorithm>

#include "../../Source/DSP/KR106VCF.h"
#include "../../Source/DSP/KR106VcfFreqJ6.h"
#include "../../Source/DSP/KR106VcfFreqJ106.h"

// --- WAV file writer (mono, 24-bit PCM) ---

static void writeWav24(const char* filename, const float* data, int numSamples, int sampleRate)
{
    FILE* f = fopen(filename, "wb");
    if (!f) { fprintf(stderr, "Error: cannot open %s for writing\n", filename); return; }

    int bytesPerSample = 3; // 24-bit
    int dataSize = numSamples * bytesPerSample;
    int fileSize = 36 + dataSize;

    auto write16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    auto write32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };

    // RIFF header
    fwrite("RIFF", 1, 4, f);
    write32(fileSize);
    fwrite("WAVE", 1, 4, f);

    // fmt chunk
    fwrite("fmt ", 1, 4, f);
    write32(16);              // chunk size
    write16(1);               // PCM format
    write16(1);               // mono
    write32(sampleRate);
    write32(sampleRate * bytesPerSample); // byte rate
    write16(bytesPerSample);  // block align
    write16(24);              // bits per sample

    // data chunk
    fwrite("data", 1, 4, f);
    write32(dataSize);

    for (int i = 0; i < numSamples; i++)
    {
        float s = std::clamp(data[i], -1.f, 1.f);
        int32_t v = static_cast<int32_t>(s * 8388607.f); // 2^23 - 1
        uint8_t bytes[3] = {
            static_cast<uint8_t>(v & 0xFF),
            static_cast<uint8_t>((v >> 8) & 0xFF),
            static_cast<uint8_t>((v >> 16) & 0xFF)
        };
        fwrite(bytes, 1, 3, f);
    }

    fclose(f);
    fprintf(stderr, "Wrote %s: %d samples, %d Hz, 24-bit mono (%.1f sec)\n",
            filename, numSamples, sampleRate, numSamples / (float)sampleRate);
}

int main(int argc, char* argv[])
{
    const char* outFile = (argc > 1) ? argv[1] : "sweep.wav";
    float duration = (argc > 2) ? static_cast<float>(atof(argv[2])) : 10.f;
    float sr = (argc > 3) ? static_cast<float>(atof(argv[3])) : 44100.f;
    bool j6Mode = (argc > 4) && strcmp(argv[4], "j6") == 0;
    float res = (argc > 5) ? static_cast<float>(atof(argv[5])) : 1.f;
    float noiseAmp = (argc > 6) ? static_cast<float>(atof(argv[6])) : 0.f;

    int numSamples = static_cast<int>(duration * sr);
    std::vector<float> buffer(numSamples);

    kr106::VCF vcf;
    vcf.SetSampleRate(sr);
    vcf.Reset();

    uint32_t noiseSeed = 12345;

    fprintf(stderr, "Rendering %d samples (%.1fs at %.0f Hz, mode=%s)\n",
            numSamples, duration, sr, j6Mode ? "j6" : "j106");
    fprintf(stderr, "Sweep: VCF slider 0->1, res=%.2f, noise=%.2f\n", res, noiseAmp);

    for (int i = 0; i < numSamples; i++)
    {
        float slider = static_cast<float>(i) / static_cast<float>(numSamples - 1);

        float hz;
        if (j6Mode)
            hz = kr106::j6_vcf_freq_from_slider(slider);
        else
            hz = kr106::dacToHz(static_cast<uint16_t>(slider * 0x3F80));

        float input = 0.f;
        if (noiseAmp > 0.f)
        {
            noiseSeed = noiseSeed * 196314165u + 907633515u;
            input = (static_cast<float>(noiseSeed) / static_cast<float>(0xFFFFFFFFu) * 2.f - 1.f) * noiseAmp;
        }

        float frq = hz / (sr * 0.5f);
        buffer[i] = vcf.Process(input, frq, res) * 0.3f;
    }

    // Find peak for reporting
    float peak = 0.f;
    for (int i = 0; i < numSamples; i++)
        peak = std::max(peak, std::abs(buffer[i]));
    fprintf(stderr, "Peak level: %.4f (%.1f dBFS)\n", peak,
            peak > 1e-10f ? 20.f * log10f(peak) : -200.f);

    writeWav24(outFile, buffer.data(), numSamples, static_cast<int>(sr));
    return 0;
}

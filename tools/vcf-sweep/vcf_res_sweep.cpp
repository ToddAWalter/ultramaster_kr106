// Standalone VCF resonance sweep -- generates a mono 24-bit PCM .wav file.
//
// Usage: vcf_res_sweep <freq_hz> [output.wav] [duration_sec] [samplerate]
//   Defaults: res_sweep.wav, 10s, 44100
//
// Runs white noise through VCF at a fixed cutoff frequency,
// sweeping resonance 0->1 over the duration.

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <vector>
#include <algorithm>

#include "../../Source/DSP/KR106VCF.h"

// --- WAV writer (mono, 24-bit PCM) ---

static void writeWav24(const char* filename, const float* data, int numSamples, int sampleRate)
{
    FILE* f = fopen(filename, "wb");
    if (!f) { fprintf(stderr, "Error: cannot open %s for writing\n", filename); return; }

    int bytesPerSample = 3;
    int dataSize = numSamples * bytesPerSample;
    int fileSize = 36 + dataSize;

    auto write16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    auto write32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };

    fwrite("RIFF", 1, 4, f);
    write32(fileSize);
    fwrite("WAVE", 1, 4, f);

    fwrite("fmt ", 1, 4, f);
    write32(16);
    write16(1);               // PCM
    write16(1);               // mono
    write32(sampleRate);
    write32(sampleRate * bytesPerSample);
    write16(bytesPerSample);
    write16(24);

    fwrite("data", 1, 4, f);
    write32(dataSize);

    for (int i = 0; i < numSamples; i++)
    {
        float s = std::clamp(data[i], -1.f, 1.f);
        int32_t v = static_cast<int32_t>(s * 8388607.f);
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
    if (argc < 2)
    {
        fprintf(stderr, "Usage: vcf_res_sweep <freq_hz> [output.wav] [duration_sec] [samplerate]\n");
        return 1;
    }

    float freqHz = static_cast<float>(atof(argv[1]));
    const char* outFile = (argc > 2) ? argv[2] : "res_sweep.wav";
    float duration = (argc > 3) ? static_cast<float>(atof(argv[3])) : 10.f;
    float sr = (argc > 4) ? static_cast<float>(atof(argv[4])) : 44100.f;

    int numSamples = static_cast<int>(duration * sr);
    std::vector<float> buffer(numSamples);

    float frq = freqHz / (sr * 0.5f);

    kr106::VCF vcf;
    vcf.SetSampleRate(sr);
    vcf.Reset();

    uint32_t noiseSeed = 12345;

    fprintf(stderr, "Rendering %d samples (%.1fs at %.0f Hz)\n", numSamples, duration, sr);
    fprintf(stderr, "White noise -> VCF at %.1f Hz, res sweep 0->1\n", freqHz);

    for (int i = 0; i < numSamples; i++)
    {
        float res = static_cast<float>(i) / static_cast<float>(numSamples - 1);

        noiseSeed = noiseSeed * 196314165u + 907633515u;
        float noise = static_cast<float>(noiseSeed) / static_cast<float>(0xFFFFFFFFu) * 2.f - 1.f;

        buffer[i] = vcf.Process(noise * 0.5f, frq, res) * 0.3f;
    }

    float peak = 0.f;
    for (int i = 0; i < numSamples; i++)
        peak = std::max(peak, std::abs(buffer[i]));
    fprintf(stderr, "Peak level: %.4f (%.1f dBFS)\n", peak,
            peak > 1e-10f ? 20.f * log10f(peak) : -200.f);

    writeWav24(outFile, buffer.data(), numSamples, static_cast<int>(sr));
    return 0;
}

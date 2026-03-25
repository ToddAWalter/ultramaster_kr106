// Standalone VCF noise test -- generates a mono 24-bit PCM .wav file.
//
// Usage: vcf_noise [output.wav] [samplerate]
//   Defaults: noise_test.wav, 44100
//
// Sequence (matches the in-synth filter test):
//   Step 0:   self-oscillation at 200 Hz, R=1.0 (1.75s tone + 0.25s silence)
//   Steps 1-36: white noise through VCF at 6 frequencies x 6 resonance levels
//     Frequencies: 200, 400, 800, 1600, 3200, 6400 Hz
//     Resonance:   0.0, 0.2, 0.4, 0.6, 0.8, 1.0
//     Each: 1.75s tone + 0.25s silence
//   Total: 37 steps x 2.0s = 74s

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <vector>
#include <algorithm>

#include "../../Source/DSP/KR106VCF.h"

static constexpr int kTotalSteps = 37;
static constexpr float kToneSec = 1.75f;
static constexpr float kSilenceSec = 0.25f;
static constexpr float kNoteSec = kToneSec + kSilenceSec;
static constexpr float kFreqs[6] = {200.f, 400.f, 800.f, 1600.f, 3200.f, 6400.f};

static float stepRes(int step)
{
    if (step == 0) return 1.f;
    return static_cast<float>((step - 1) / 6) * 0.2f;
}

static float stepHz(int step)
{
    if (step == 0) return 200.f;
    return kFreqs[(step - 1) % 6];
}

static bool stepNoise(int step) { return step > 0; }

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
    const char* outFile = (argc > 1) ? argv[1] : "noise_test.wav";
    float sr = (argc > 2) ? static_cast<float>(atof(argv[2])) : 44100.f;

    int samplesPerNote = static_cast<int>(kNoteSec * sr);
    int toneSamples = static_cast<int>(kToneSec * sr);
    int totalSamples = samplesPerNote * kTotalSteps;
    std::vector<float> buffer(totalSamples, 0.f);

    kr106::VCF vcf;
    vcf.SetSampleRate(sr);

    uint32_t noiseSeed = 12345;

    fprintf(stderr, "Rendering %d steps (%.1fs total at %.0f Hz)\n",
            kTotalSteps, totalSamples / sr, sr);

    for (int step = 0; step < kTotalSteps; step++)
    {
        float res = stepRes(step);
        float hz = stepHz(step);
        float frq = hz / (sr * 0.5f);
        bool noise = stepNoise(step);

        vcf.Reset();

        fprintf(stderr, "  Step %2d: %s %.0f Hz, R=%.1f\n",
                step, noise ? "noise" : "self-osc", hz, res);

        int base = step * samplesPerNote;
        for (int s = 0; s < samplesPerNote; s++)
        {
            float out;
            if (s < toneSamples)
            {
                if (noise)
                {
                    noiseSeed = noiseSeed * 196314165u + 907633515u;
                    float n = static_cast<float>(noiseSeed) / static_cast<float>(0xFFFFFFFFu) * 2.f - 1.f;
                    out = vcf.Process(n * 0.5f, frq, res);
                }
                else
                {
                    out = vcf.Process(0.f, frq, res);
                }
            }
            else
            {
                out = vcf.Process(0.f, 0.01f, 0.f);
            }
            buffer[base + s] = out * 0.3f;
        }
    }

    float peak = 0.f;
    for (int i = 0; i < totalSamples; i++)
        peak = std::max(peak, std::abs(buffer[i]));
    fprintf(stderr, "Peak level: %.4f (%.1f dBFS)\n", peak,
            peak > 1e-10f ? 20.f * log10f(peak) : -200.f);

    writeWav24(outFile, buffer.data(), totalSamples, static_cast<int>(sr));
    return 0;
}

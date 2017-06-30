#include "WaveEditor.hpp"
#include <string.h>


Bank currentBank;

const char *effectNames[EFFECTS_LEN] {
	"Pre-Gain",
	"Harmonic Shift",
	"Comb Filter",
	"Ring Modulation",
	"Chebyshev Wavefolding",
	"Sample & Hold",
	"Quantization",
	"Slew Limiter",
	"Lowpass Filter",
	"Highpass Filter",
	"Post-Gain",
};


void bankInit() {
	bankClear();
}

void updatePost(int waveId) {
	Wave *wave = &currentBank.waves[waveId];
	float *effects = wave->effects;
	float out[WAVE_LEN];
	memcpy(out, wave->samples, sizeof(float) * WAVE_LEN);

	// Pre-gain
	if (effects[PRE_GAIN]) {
		float gain = powf(20.0, effects[PRE_GAIN]);
		for (int i = 0; i < WAVE_LEN; i++) {
			out[i] *= gain;
		}
	}

	// Harmonic Shift
	if (effects[HARMONIC_SHIFT] > 0.0) {
		// Shift Fourier phase proportionally
		float tmp[WAVE_LEN];
		RFFT(out, tmp, WAVE_LEN);
		float phase = clampf(effects[HARMONIC_SHIFT], 0.0, 1.0);
		float br = cosf(2 * M_PI * phase);
		float bi = sinf(2 * M_PI * phase);
		for (int i = 0; i < WAVE_LEN / 2; i++) {
			complexMult(&tmp[2 * i], &tmp[2 * i + 1], br, bi);
		}
		IRFFT(tmp, out, WAVE_LEN);
	}

	// Comb filter
	if (effects[COMB] > 0.0) {
		const float base = 0.75;
		const int taps = 40;

		// Build the kernel in Fourier space
		// Place taps at positions `comb * j`, with exponentially decreasing amplitude
		float kernel[WAVE_LEN] = {};
		for (int k = 0; k < WAVE_LEN / 2; k++) {
			for (int j = 0; j < taps; j++) {
				float amplitude = powf(base, j);
				// Normalize by sum of geometric series
				amplitude *= (1.0 - base);
				float phase = -2.0 * M_PI * k * effects[COMB] * j;
				kernel[2 * k] += amplitude * cosf(phase);
				kernel[2 * k + 1] += amplitude * sinf(phase);
			}
		}

		// Convolve FFT of input with kernel
		float fft[WAVE_LEN];
		RFFT(out, fft, WAVE_LEN);
		for (int k = 0; k < WAVE_LEN / 2; k++) {
			complexMult(&fft[2 * k], &fft[2 * k + 1], kernel[2 * k], kernel[2 * k + 1]);
		}
		IRFFT(fft, out, WAVE_LEN);
	}

	// Ring modulation
	if (effects[RING] > 0.0) {
		float ring = ceilf(powf(effects[RING], 2) * (WAVE_LEN / 2 - 1));
		for (int i = 0; i < WAVE_LEN; i++) {
			float phase = (float)i / WAVE_LEN * ring;
			out[i] *= sinf(2 * M_PI * phase);
		}
	}

	// Chebyshev waveshaping
	if (effects[CHEBYSHEV] > 0.0) {
		float n = powf(50.0, effects[CHEBYSHEV]);
		for (int i = 0; i < WAVE_LEN; i++) {
			// Apply a distant variant of the Chebyshev polynomial of the first kind
			if (-1.0 <= out[i] && out[i] <= 1.0)
				out[i] = sinf(n * asinf(out[i]));
			else
				out[i] = sinf(n * asinf(1.0 / out[i]));
		}
	}

	// Sample & Hold
	if (effects[SAMPLE_AND_HOLD] > 0.0) {
		float frameskip = powf(WAVE_LEN / 2.0, clampf(effects[SAMPLE_AND_HOLD], 0.0, 1.0));
		float tmp[WAVE_LEN + 1];
		memcpy(tmp, out, sizeof(float) * WAVE_LEN);
		tmp[WAVE_LEN] = tmp[0];

		// Dumb linear interpolation S&H
		for (int i = 0; i < WAVE_LEN; i++) {
			float index = roundf(i / frameskip) * frameskip;
			out[i] = linterpf(tmp, clampf(index, 0.0, WAVE_LEN - 1));
		}
	}

	// Quantization
	if (effects[QUANTIZATION] > 1e-3) {
		float levels = powf(clampf(effects[QUANTIZATION], 0.0, 1.0), -1.5);
		for (int i = 0; i < WAVE_LEN; i++) {
			out[i] = roundf(out[i] * levels) / levels;
		}
	}

	// Slew Limiter
	if (effects[SLEW] > 0.0) {
		float slew = powf(0.001, effects[SLEW]);

		float y = out[0];
		for (int i = 1; i < WAVE_LEN; i++) {
			float dxdt = out[i] - y;
			float dydt = clampf(dxdt, -slew, slew);
			y += dydt;
			out[i] = y;
		}
	}

	// Brick-wall lowpass / highpass filter
	// TODO Maybe change this into a more musical filter
	if (effects[LOWPASS] > 0.0 || effects[HIGHPASS]) {
		float fft[WAVE_LEN];
		RFFT(out, fft, WAVE_LEN);
		float lowpass = 1.0 - effects[LOWPASS];
		float highpass = effects[HIGHPASS];
		for (int i = 1; i < WAVE_LEN / 2; i++) {
			float v = clampf(WAVE_LEN / 2 * lowpass - i, 0.0, 1.0) * clampf(-WAVE_LEN / 2 * highpass + i, 0.0, 1.0);
			fft[2 * i] *= v;
			fft[2 * i + 1] *= v;
		}
		IRFFT(fft, out, WAVE_LEN);
	}

	// TODO Consider removing because Normalize does this for you
	// Post gain
	if (effects[POST_GAIN]) {
		float gain = powf(20.0, effects[POST_GAIN]);
		for (int i = 0; i < WAVE_LEN; i++) {
			out[i] *= gain;
		}
	}

	// Cycle
	if (wave->cycle) {
		float start = out[0];
		float end = out[WAVE_LEN - 1] / (WAVE_LEN - 1) * WAVE_LEN;

		for (int i = 0; i < WAVE_LEN; i++) {
			out[i] -= (end - start) * (i - WAVE_LEN / 2) / WAVE_LEN;
		}
	}

	// Normalize
	if (wave->normalize) {
		float max = -INFINITY;
		float min = INFINITY;
		for (int i = 0; i < WAVE_LEN; i++) {
			if (out[i] > max) max = out[i];
			if (out[i] < min) min = out[i];
		}

		if (max - min >= 1e-6) {
			for (int i = 0; i < WAVE_LEN; i++) {
				out[i] = rescalef(out[i], min, max, -1.0, 1.0);
			}
		}
		else {
			memset(out, 0, sizeof(float) * WAVE_LEN);
		}
	}

	// Hard clip :(
	for (int i = 0; i < WAVE_LEN; i++) {
		out[i] = clampf(out[i], -1.0, 1.0);
	}

	// TODO Fix possible race condition with audio thread here
	memcpy(wave->postSamples, out, sizeof(float)*WAVE_LEN);

	// Convert wave to spectrum
	RFFT(wave->postSamples, wave->postSpectrum, WAVE_LEN);
	// Convert spectrum to harmonics
	for (int i = 0; i < WAVE_LEN / 2; i++) {
		wave->postHarmonics[i] = hypotf(wave->postSpectrum[2 * i], wave->postSpectrum[2 * i + 1]) * 2.0;
	}
}

/** Called when wave has been updated */
void commitWave(int waveId) {
	Wave *wave = &currentBank.waves[waveId];

	// Convert wave to spectrum
	RFFT(wave->samples, wave->spectrum, WAVE_LEN);
	// Convert spectrum to harmonics
	for (int i = 0; i < WAVE_LEN / 2; i++) {
		wave->harmonics[i] = hypotf(wave->spectrum[2 * i], wave->spectrum[2 * i + 1]) * 2.0;
	}
	updatePost(waveId);
}

void commitHarmonics(int waveId) {
	Wave *wave = &currentBank.waves[waveId];

	// Rescale spectrum by the new norm
	for (int i = 0; i < WAVE_LEN / 2; i++) {
		float oldHarmonic = hypotf(wave->spectrum[2 * i], wave->spectrum[2 * i + 1]);
		float newHarmonic = wave->harmonics[i] / 2.0;
		if (oldHarmonic > 1.0e-6) {
			// Preserve old phase but apply new magnitude
			float ratio = newHarmonic / oldHarmonic;
			if (i == 0) {
				wave->spectrum[2 * i] *= ratio;
				wave->spectrum[2 * i + 1] = 0.0;
			}
			else {
				wave->spectrum[2 * i] *= ratio;
				wave->spectrum[2 * i + 1] *= ratio;
			}
		}
		else {
			// If there is no old phase (magnitude is 0), set to 90 degrees
			if (i == 0) {
				wave->spectrum[2 * i] = newHarmonic;
				wave->spectrum[2 * i + 1] = 0.0;
			}
			else {
				wave->spectrum[2 * i] = 0.0;
				wave->spectrum[2 * i + 1] = -newHarmonic;
			}
		}
	}
	// Convert spectrum to wave
	IRFFT(wave->spectrum, wave->samples, WAVE_LEN);
	updatePost(waveId);
}

void clearEffect(int waveId) {
	memset(currentBank.waves[waveId].effects, 0, sizeof(float) * EFFECTS_LEN);
	commitWave(waveId);
}

/** Bakes effect into the wavetable and resets the effect parameters */
void bakeEffect(int waveId) {
	Wave *wave = &currentBank.waves[waveId];

	memcpy(wave->samples, wave->postSamples, sizeof(float)*WAVE_LEN);
	clearEffect(waveId);
}

void randomizeEffect(int waveId) {
	Wave *wave = &currentBank.waves[waveId];
	float *effects = wave->effects;

	effects[PRE_GAIN]        = powf(clampf(randf() * 2 - 1, 0.0, 1.0), 2);
	effects[HARMONIC_SHIFT]  = powf(clampf(randf() * 2 - 1, 0.0, 1.0), 2);
	effects[COMB]            = powf(clampf(randf() * 2 - 1, 0.0, 1.0), 2);
	effects[RING]            = powf(clampf(randf() * 2 - 1, 0.0, 1.0), 2);
	effects[CHEBYSHEV]       = powf(clampf(randf() * 2 - 1, 0.0, 1.0), 2);
	effects[SAMPLE_AND_HOLD] = powf(clampf(randf() * 2 - 1, 0.0, 1.0), 2);
	effects[QUANTIZATION]    = powf(clampf(randf() * 2 - 1, 0.0, 1.0), 2);
	effects[SLEW]            = powf(clampf(randf() * 2 - 1, 0.0, 1.0), 2);
	effects[LOWPASS]         = powf(clampf(randf() * 2 - 1, 0.0, 1.0), 2);
	effects[HIGHPASS]        = powf(clampf(randf() * 2 - 1, 0.0, 1.0), 2);
	commitWave(waveId);
}

void bankClear() {
	// The lazy way
	memset(&currentBank, 0, sizeof(Bank));

	for (int i = 0; i < BANK_LEN; i++) {
		commitWave(i);
	}
}

void saveBank(const char *fileName) {
	// TODO Use a separate library for this (even if I have to write it)

	int16_t format = 1; // PCM
	int16_t channels = 1;
	uint32_t sampleRate = 44100;
	int16_t bitsPerSample = 16;
	uint32_t bytesPerSecond = sampleRate * bitsPerSample * channels / 8;
	int16_t bytesPerFrame = bitsPerSample * channels / 8;
	uint32_t dataSize = BANK_LEN * WAVE_LEN * bytesPerFrame;
	uint32_t fileSize = 4 + (8 + 16) + (8 + dataSize);

	FILE *f = fopen(fileName, "wb");
	if (!f)
		return;

	// RIFF header
	fwrite("RIFF", 1, 4, f);
	fwrite(&fileSize, 4, 1, f);
	fwrite("WAVE", 1, 4, f);

	// SubChunk1
	fwrite("fmt ", 1, 4, f);
	uint32_t formatLength = 16;
	fwrite(&formatLength, 4, 1, f);
	fwrite(&format, 2, 1, f);
	fwrite(&channels, 2, 1, f);
	fwrite(&sampleRate, 4, 1, f);
	fwrite(&bytesPerSecond, 4, 1, f);
	fwrite(&bytesPerFrame, 2, 1, f);
	fwrite(&bitsPerSample, 2, 1, f);

	// SubChunk2
	fwrite("data", 1, 4, f);
	fwrite(&dataSize, 4, 1, f);

	int16_t data[BANK_LEN * WAVE_LEN];
	for (int b = 0; b < BANK_LEN; b++)
		for (int i = 0; i < WAVE_LEN; i++) {
			data[b * WAVE_LEN + i] = (int16_t)(clampf(currentBank.waves[b].postSamples[i], -1.0, 1.0) * 32767.0);
		}
	fwrite(&data, 2, BANK_LEN * WAVE_LEN, f);

	fclose(f);
}

void loadBank(const char *fileName) {
	bankClear();

	// TODO Rewrite this properly
	FILE *f = fopen(fileName, "rb");
	if (!f)
		return;
	fseek(f, 44, SEEK_SET);
	int16_t data[BANK_LEN * WAVE_LEN];
	fread(data, sizeof(int16_t), BANK_LEN * WAVE_LEN, f);

	for (int b = 0; b < BANK_LEN; b++) {
		for (int i = 0; i < WAVE_LEN; i++) {
			currentBank.waves[b].samples[i] = data[b * WAVE_LEN + i] / 32767.0;
		}
		commitWave(b);
	}

	fclose(f);
}

void loadWave(const char *fileName, float *wave) {
	// TODO Rewrite this properly
	FILE *f = fopen(fileName, "rb");
	if (!f)
		return;
	fseek(f, 44, SEEK_SET);
	int16_t data[WAVE_LEN];
	fread(data, sizeof(int16_t), BANK_LEN * WAVE_LEN, f);

	for (int j = 0; j < WAVE_LEN; j++) {
		wave[j] = data[j] / 32767.0;
	}

	fclose(f);
}

/** If NULL, clears wave */
void setWave(int waveId, const float *wave) {
	if (wave)
		memcpy(currentBank.waves[waveId].samples, wave, sizeof(float) * WAVE_LEN);
	else
		memset(currentBank.waves[waveId].samples, 0, sizeof(float) * WAVE_LEN);
	commitWave(waveId);
}

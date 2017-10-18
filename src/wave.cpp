#include "WaveEdit.hpp"
#include <string.h>
#include <sndfile.h>


static Wave clipboardWave = {};
bool clipboardActive = false;


const char *effectNames[EFFECTS_LEN] {
	"Pre-Gain",
	"Phase Shift",
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


void Wave::clear() {
	memset(this, 0, sizeof(Wave));
}

void Wave::updatePost() {
	float out[WAVE_LEN];
	memcpy(out, samples, sizeof(float) * WAVE_LEN);

	// Pre-gain
	if (effects[PRE_GAIN]) {
		float gain = powf(20.0, effects[PRE_GAIN]);
		for (int i = 0; i < WAVE_LEN; i++) {
			out[i] *= gain;
		}
	}

	// Temporal and Harmonic Shift
	if (effects[PHASE_SHIFT] > 0.0 || effects[HARMONIC_SHIFT] > 0.0) {
		// Shift Fourier phase proportionally
		float tmp[WAVE_LEN];
		RFFT(out, tmp, WAVE_LEN);
		for (int k = 0; k < WAVE_LEN / 2; k++) {
			float phase = clampf(effects[HARMONIC_SHIFT], 0.0, 1.0) + clampf(effects[PHASE_SHIFT], 0.0, 1.0) * k;
			float br = cosf(2 * M_PI * phase);
			float bi = -sinf(2 * M_PI * phase);
			cmultf(&tmp[2 * k], &tmp[2 * k + 1], tmp[2 * k], tmp[2 * k + 1], br, bi);
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
			cmultf(&fft[2 * k], &fft[2 * k + 1], fft[2 * k], fft[2 * k + 1], kernel[2 * k], kernel[2 * k + 1]);
		}
		IRFFT(fft, out, WAVE_LEN);
	}

	// Ring modulation
	if (effects[RING] > 0.0) {
		float ring = ceilf(powf(effects[RING], 2) * (WAVE_LEN / 2 - 2));
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
	if (cycle) {
		float start = out[0];
		float end = out[WAVE_LEN - 1] / (WAVE_LEN - 1) * WAVE_LEN;

		for (int i = 0; i < WAVE_LEN; i++) {
			out[i] -= (end - start) * (i - WAVE_LEN / 2) / WAVE_LEN;
		}
	}

	// Normalize
	if (normalize) {
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
	// Or not, because the race condition would only just replace samples as they are being read, which just gives a click sound.
	memcpy(postSamples, out, sizeof(float)*WAVE_LEN);

	// Convert wave to spectrum
	RFFT(postSamples, postSpectrum, WAVE_LEN);
	// Convert spectrum to harmonics
	for (int i = 0; i < WAVE_LEN / 2; i++) {
		postHarmonics[i] = hypotf(postSpectrum[2 * i], postSpectrum[2 * i + 1]) * 2.0;
	}
}

void Wave::commitSamples() {
	// Convert wave to spectrum
	RFFT(samples, spectrum, WAVE_LEN);
	// Convert spectrum to harmonics
	for (int i = 0; i < WAVE_LEN / 2; i++) {
		harmonics[i] = hypotf(spectrum[2 * i], spectrum[2 * i + 1]) * 2.0;
	}
	updatePost();
}

void Wave::commitHarmonics() {
	// Rescale spectrum by the new norm
	for (int i = 0; i < WAVE_LEN / 2; i++) {
		float oldHarmonic = hypotf(spectrum[2 * i], spectrum[2 * i + 1]);
		float newHarmonic = harmonics[i] / 2.0;
		if (oldHarmonic > 1.0e-6) {
			// Preserve old phase but apply new magnitude
			float ratio = newHarmonic / oldHarmonic;
			if (i == 0) {
				spectrum[2 * i] *= ratio;
				spectrum[2 * i + 1] = 0.0;
			}
			else {
				spectrum[2 * i] *= ratio;
				spectrum[2 * i + 1] *= ratio;
			}
		}
		else {
			// If there is no old phase (magnitude is 0), set to 90 degrees
			if (i == 0) {
				spectrum[2 * i] = newHarmonic;
				spectrum[2 * i + 1] = 0.0;
			}
			else {
				spectrum[2 * i] = 0.0;
				spectrum[2 * i + 1] = -newHarmonic;
			}
		}
	}
	// Convert spectrum to wave
	IRFFT(spectrum, samples, WAVE_LEN);
	updatePost();
}

void Wave::clearEffects() {
	memset(effects, 0, sizeof(float) * EFFECTS_LEN);
	cycle = false;
	normalize = false;
	updatePost();
}

void Wave::bakeEffects() {
	memcpy(samples, postSamples, sizeof(float)*WAVE_LEN);
	clearEffects();
}

void Wave::randomizeEffects() {
	for (int i = 0; i < EFFECTS_LEN; i++) {
		effects[i] = randf() > 0.5 ? powf(randf(), 2) : 0.0;
	}
	updatePost();
}

void Wave::saveWAV(const char *filename) {
	SF_INFO info;
	info.samplerate = 44100;
	info.channels = 1;
	info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16 | SF_ENDIAN_LITTLE;
	SNDFILE *sf = sf_open(filename, SFM_WRITE, &info);
	if (!sf)
		return;

	sf_write_float(sf, postSamples, WAVE_LEN);

	sf_close(sf);
}

void Wave::loadWAV(const char *filename) {
	clear();

	SF_INFO info;
	SNDFILE *sf = sf_open(filename, SFM_READ, &info);
	if (!sf)
		return;

	sf_read_float(sf, samples, WAVE_LEN);
	commitSamples();

	sf_close(sf);
}

void Wave::clipboardCopy() {
	memcpy(&clipboardWave, this, sizeof(*this));
	clipboardActive = true;
}

void Wave::clipboardPaste() {
	if (clipboardActive) {
		memcpy(this, &clipboardWave, sizeof(*this));
	}
}

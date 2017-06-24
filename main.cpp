#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <string>
#include <thread>
#include <vector>

#include <dirent.h>

#include <SDL.h>
#include <SDL_opengl.h>

#include <samplerate.h>
#include "pffft/pffft.h"

#include "imgui.h"
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui_internal.h"
#include "imgui/examples/sdl_opengl2_example/imgui_impl_sdl.h"

#define NOC_FILE_DIALOG_IMPLEMENTATION
extern "C" {
#include "noc/noc_file_dialog.h"
}

#include "lodepng/lodepng.h"


////////////////////
// Math
////////////////////

// Integers

inline int eucmodi(int a, int base) {
	int mod = a % base;
	return mod < 0 ? mod + base : mod;
}

inline int mini(int a, int b) {
	return a < b ? a : b;
}

inline int maxi(int a, int b) {
	return a > b ? a : b;
}

/** Limits a value between a minimum and maximum */
inline int clampi(int x, int min, int max) {
	return x > max ? max : x < min ? min : x;
}

// Floats

inline float sgnf(float x) {
	return copysignf(1.0, x);
}

/** Limits a value between a minimum and maximum */
inline float clampf(float x, float min, float max) {
	return x > max ? max : x < min ? min : x;
}

/** If the magnitude of x if less than eps, return 0 */
inline float chopf(float x, float eps) {
	return (-eps < x && x < eps) ? 0.0 : x;
}

inline float rescalef(float x, float xMin, float xMax, float yMin, float yMax) {
	return yMin + (x - xMin) / (xMax - xMin) * (yMax - yMin);
}

inline float crossf(float a, float b, float frac) {
	return (1.0 - frac) * a + frac * b;
}

/** Linearly interpolate an array `p` with index `x`
Assumes that the array at `p` is of length at least floor(x)+1.
*/
inline float linterpf(const float *p, float x) {
	int xi = x;
	float xf = x - xi;
	return crossf(p[xi], p[xi + 1], xf);
}

/** Returns a random number on [0, 1) */
float randf() {
	return (float)rand() / RAND_MAX;
}

void RFFT(const float *in, float *out, int len) {
	PFFFT_Setup *pffft = pffft_new_setup(len, PFFFT_REAL);
	pffft_transform_ordered(pffft, in, out, NULL, PFFFT_FORWARD);
	pffft_destroy_setup(pffft);
	float a = 1.0 / len;
	for (int i = 0; i < len; i++) {
		out[i] *= a;
	}
}

void IRFFT(const float *in, float *out, int len) {
	PFFFT_Setup *pffft = pffft_new_setup(len, PFFFT_REAL);
	pffft_transform_ordered(pffft, in, out, NULL, PFFFT_BACKWARD);
	pffft_destroy_setup(pffft);
}

inline void complexMult(float *ar, float *ai, float br, float bi) {
	float oldAr = *ar;
	*ar = oldAr * br - *ai * bi;
	*ai = oldAr * bi + *ai * br;
}


////////////////////
// Utilities
////////////////////

void openBrowser(const char *url) {
	// shell injection is possible if the URL is not trusted
#if defined(__linux__)
	char command[1024];
	snprintf(command, sizeof(command), "xdg-open %s", url);
	system(command);
#endif
#if defined(__APPLE__)
	char command[1024];
	snprintf(command, sizeof(command), "open %s", url);
	system(command);
#endif
#if defined(_WIN32)
	ShellExecute(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
#endif
}

////////////////////
// Waves
////////////////////

#define NUM_WAVES 64
#define NUM_SAMPLES 256
#define OVERSAMPLE 4

float waves[NUM_WAVES][NUM_SAMPLES] = {};
/** FFT of wave */
float spectrums[NUM_WAVES][NUM_SAMPLES] = {};
/** Norm of spectrum divided by harmonic number */
float harmonics[NUM_WAVES][NUM_SAMPLES / 2] = {};
/** Wave after effects have been applied */
float postWaves[NUM_WAVES][NUM_SAMPLES] = {};
float postSpectrums[NUM_WAVES][NUM_SAMPLES] = {};
float postHarmonics[NUM_WAVES][NUM_SAMPLES / 2] = {};
int selectedWave = 0;

struct Effect {
	float preGain;
	float harmonicShift;
	float comb;
	float ring;
	float chebyshev;
	float quantization;
	float posterization;
	float slew;
	float lowpass;
	float highpass;
	float postGain;
	bool cycle;
	bool normalize;
};

Effect effects[NUM_WAVES] = {};

void updatePost(int waveId) {
	float out[NUM_SAMPLES];
	memcpy(out, waves[waveId], sizeof(float) * NUM_SAMPLES);

	// Pre-gain
	if (effects[waveId].preGain) {
		float gain = powf(20.0, effects[waveId].preGain);
		for (int i = 0; i < NUM_SAMPLES; i++) {
			out[i] *= gain;
		}
	}

	// Harmonic Shift
	if (effects[waveId].harmonicShift > 0.0) {
		// Shift Fourier phase proportionally
		float tmp[NUM_SAMPLES];
		RFFT(out, tmp, NUM_SAMPLES);
		float phase = clampf(effects[waveId].harmonicShift, 0.0, 1.0);
		float br = cosf(2 * M_PI * phase);
		float bi = sinf(2 * M_PI * phase);
		for (int i = 0; i < NUM_SAMPLES / 2; i++) {
			complexMult(&tmp[2 * i], &tmp[2 * i + 1], br, bi);
		}
		IRFFT(tmp, out, NUM_SAMPLES);
	}

	// Comb filter
	if (effects[waveId].comb > 0.0) {
		const float base = 0.75;
		const int taps = 40;
		float comb = effects[waveId].comb;

		// Build the kernel in Fourier space
		// Place taps at positions `comb * j`, with exponentially decreasing amplitude
		float kernel[NUM_SAMPLES] = {};
		for (int k = 0; k < NUM_SAMPLES / 2; k++) {
			for (int j = 0; j < taps; j++) {
				float amplitude = powf(base, j);
				// Normalize by sum of geometric series
				amplitude *= (1.0 - base);
				float phase = -2.0 * M_PI * k * comb * j;
				kernel[2 * k] += amplitude * cosf(phase);
				kernel[2 * k + 1] += amplitude * sinf(phase);
			}
		}

		// Convolve FFT of input with kernel
		float fft[NUM_SAMPLES];
		RFFT(out, fft, NUM_SAMPLES);
		for (int k = 0; k < NUM_SAMPLES / 2; k++) {
			complexMult(&fft[2 * k], &fft[2 * k + 1], kernel[2 * k], kernel[2 * k + 1]);
		}
		IRFFT(fft, out, NUM_SAMPLES);
	}

	// Ring modulation
	if (effects[waveId].ring > 0.0) {
		float ring = ceilf(powf(effects[waveId].ring, 3) * (NUM_SAMPLES/2 - 1));
		for (int i = 1; i < NUM_SAMPLES; i++) {
			float phase = (float)i / NUM_SAMPLES * ring;
			out[i] *= sinf(2*M_PI * phase);
		}
	}

	// Chebyshev waveshaping
	if (effects[waveId].chebyshev > 0.0) {
		float n = powf(50.0, effects[waveId].chebyshev);
		for (int i = 0; i < NUM_SAMPLES; i++) {
			// Apply a distant variant of the Chebyshev polynomial of the first kind
			if (-1.0 <= out[i] && out[i] <= 1.0)
				out[i] = sinf(n * asinf(out[i]));
			else
				out[i] = sinf(n * asinf(1.0 / out[i]));
		}
	}

	// Quantize
	if (effects[waveId].quantization > 0.0) {
		float frameskip = powf(NUM_SAMPLES / 2.0, clampf(effects[waveId].quantization, 0.0, 1.0));
		float tmp[NUM_SAMPLES + 1];
		memcpy(tmp, out, sizeof(float) * NUM_SAMPLES);
		tmp[NUM_SAMPLES] = tmp[0];

		// Dumb linear interpolation S&H
		for (int i = 0; i < NUM_SAMPLES; i++) {
			float index = roundf(i / frameskip) * frameskip;
			out[i] = linterpf(tmp, clampf(index, 0.0, NUM_SAMPLES - 1));
		}
	}

	// Posterize
	if (effects[waveId].posterization > 1e-3) {
		float levels = powf(clampf(effects[waveId].posterization, 0.0, 1.0), -1.5);
		for (int i = 0; i < NUM_SAMPLES; i++) {
			out[i] = roundf(out[i] * levels) / levels;
		}
	}

	// Slew Limiter
	if (effects[waveId].slew > 0.0) {
		float slew = powf(0.001, effects[waveId].slew);

		float y = out[0];
		for (int i = 1; i < NUM_SAMPLES; i++) {
			float dxdt = out[i] - y;
			float dydt = clampf(dxdt, -slew, slew);
			y += dydt;
			out[i] = y;
		}
	}

	// Brick-wall lowpass / highpass filter
	// TODO Maybe change this into a more musical filter
	if (effects[waveId].lowpass > 0.0 || effects[waveId].highpass) {
		float fft[NUM_SAMPLES];
		RFFT(out, fft, NUM_SAMPLES);
		float lowpass = 1.0 - effects[waveId].lowpass;
		float highpass = effects[waveId].highpass;
		for (int i = 1; i < NUM_SAMPLES / 2; i++) {
			float v = clampf(NUM_SAMPLES / 2 * lowpass - i, 0.0, 1.0) * clampf(-NUM_SAMPLES / 2 * highpass + i, 0.0, 1.0);
			fft[2 * i] *= v;
			fft[2 * i + 1] *= v;
		}
		IRFFT(fft, out, NUM_SAMPLES);
	}

	// TODO Consider removing because Normalize does this for you
	// Post gain
	if (effects[waveId].postGain) {
		float gain = powf(20.0, effects[waveId].postGain);
		for (int i = 0; i < NUM_SAMPLES; i++) {
			out[i] *= gain;
		}
	}

	// Cycle
	if (effects[waveId].cycle) {
		float start = out[0];
		float end = out[NUM_SAMPLES-1] / (NUM_SAMPLES-1) * NUM_SAMPLES;

		for (int i = 0; i < NUM_SAMPLES; i++) {
			out[i] -= (end - start) * (i - NUM_SAMPLES/2) / NUM_SAMPLES;
		}
	}

	// Normalize
	if (effects[waveId].normalize) {
		float max = -INFINITY;
		float min = INFINITY;
		for (int i = 0; i < NUM_SAMPLES; i++) {
			if (out[i] > max) max = out[i];
			if (out[i] < min) min = out[i];
		}

		if (max - min >= 1e-6) {
			for (int i = 0; i < NUM_SAMPLES; i++) {
				out[i] = rescalef(out[i], min, max, -1.0, 1.0);
			}
		} else {
			memset(out, 0, sizeof(float) * NUM_SAMPLES);
		}
	}

	// TODO Fix race condition with audio thread here
	memcpy(postWaves[waveId], out, sizeof(float)*NUM_SAMPLES);

	// Convert wave to spectrum
	RFFT(postWaves[waveId], postSpectrums[waveId], NUM_SAMPLES);
	// Convert spectrum to harmonics
	for (int i = 0; i < NUM_SAMPLES / 2; i++) {
		postHarmonics[waveId][i] = hypotf(postSpectrums[waveId][2 * i], postSpectrums[waveId][2 * i + 1]) * 2.0;
	}

}

/** Called when wave has been updated */
void commitWave(int waveId) {
	// Convert wave to spectrum
	RFFT(waves[waveId], spectrums[waveId], NUM_SAMPLES);
	// Convert spectrum to harmonics
	for (int i = 0; i < NUM_SAMPLES / 2; i++) {
		harmonics[waveId][i] = hypotf(spectrums[waveId][2 * i], spectrums[waveId][2 * i + 1]) * 2.0;
	}
	updatePost(waveId);
}

void commitHarmonics(int waveId) {
	// Rescale spectrum by the new norm
	for (int i = 0; i < NUM_SAMPLES / 2; i++) {
		float oldHarmonic = hypotf(spectrums[waveId][2 * i], spectrums[waveId][2 * i + 1]);
		float newHarmonic = harmonics[waveId][i] / 2.0;
		if (oldHarmonic > 1.0e-6) {
			// Preserve old phase but apply new magnitude
			float ratio = newHarmonic / oldHarmonic;
			if (i == 0) {
				spectrums[waveId][2 * i] *= ratio;
				spectrums[waveId][2 * i + 1] = 0.0;
			} else {
				spectrums[waveId][2 * i] *= ratio;
				spectrums[waveId][2 * i + 1] *= ratio;
			}
		} else {
			// If there is no old phase (magnitude is 0), set to 90 degrees
			if (i == 0) {
				spectrums[waveId][2 * i] = newHarmonic;
				spectrums[waveId][2 * i + 1] = 0.0;
			} else {
				spectrums[waveId][2 * i] = 0.0;
				spectrums[waveId][2 * i + 1] = -newHarmonic;
			}
		}
	}
	// Convert spectrum to wave
	IRFFT(spectrums[waveId], waves[waveId], NUM_SAMPLES);
	updatePost(waveId);
}

void initWaves() {
	for (int i = 0; i < NUM_WAVES; i++) {
		for (int j = 0; j < NUM_SAMPLES; j++) {
			waves[i][j] = 0.0;
		}
		commitWave(i);
	}
}

void clearEffect(int waveId) {
	memset(&effects[waveId], 0, sizeof(Effect));
	commitWave(waveId);
}

/** Bakes effect into wavetable and resets the effect parameters */
void bakeEffect(int waveId) {
	memcpy(waves[waveId], postWaves[waveId], sizeof(float)*NUM_SAMPLES);
	clearEffect(waveId);
}

void randomizeEffect(int waveId) {
	effects[waveId].preGain       = powf(clampf(randf()*2-1, 0.0, 1.0), 2);
	effects[waveId].harmonicShift = powf(clampf(randf()*2-1, 0.0, 1.0), 2);
	effects[waveId].comb          = powf(clampf(randf()*2-1, 0.0, 1.0), 2);
	effects[waveId].ring          = powf(clampf(randf()*2-1, 0.0, 1.0), 2);
	effects[waveId].chebyshev     = powf(clampf(randf()*2-1, 0.0, 1.0), 2);
	effects[waveId].quantization  = powf(clampf(randf()*2-1, 0.0, 1.0), 2);
	effects[waveId].posterization = powf(clampf(randf()*2-1, 0.0, 1.0), 2);
	effects[waveId].slew          = powf(clampf(randf()*2-1, 0.0, 1.0), 2);
	effects[waveId].lowpass       = powf(clampf(randf()*2-1, 0.0, 1.0), 2);
	effects[waveId].highpass      = powf(clampf(randf()*2-1, 0.0, 1.0), 2);
	commitWave(waveId);
}

void saveBank(const char *fileName) {
	int16_t format = 1; // PCM
	int16_t channels = 1;
	uint32_t sampleRate = 44100;
	int16_t bitsPerSample = 16;
	uint32_t bytesPerSecond = sampleRate * bitsPerSample * channels / 8;
	int16_t bytesPerFrame = bitsPerSample * channels / 8;
	uint32_t dataSize = NUM_WAVES * NUM_SAMPLES * bytesPerFrame;
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

	int16_t data[NUM_WAVES * NUM_SAMPLES];
	for (int b = 0; b < NUM_WAVES; b++)
		for (int i = 0; i < NUM_SAMPLES; i++) {
			data[b * NUM_SAMPLES + i] = (int16_t)(clampf(postWaves[b][i], -1.0, 1.0) * 32767.0);
		}
	fwrite(&data, 2, NUM_WAVES * NUM_SAMPLES, f);

	fclose(f);
}

void loadBank(const char *fileName) {
	// TODO Rewrite this properly
	FILE *f = fopen(fileName, "rb");
	if (!f)
		return;
	fseek(f, 44, SEEK_SET);
	int16_t data[NUM_WAVES * NUM_SAMPLES];
	fread(data, sizeof(int16_t), NUM_WAVES * NUM_SAMPLES, f);

	for (int i = 0; i < NUM_WAVES; i++) {
		for (int j = 0; j < NUM_SAMPLES; j++) {
			waves[i][j] = data[i * NUM_SAMPLES + j] / 32767.0;
		}
		commitWave(i);
	}

	fclose(f);
}

void loadWave(const char *fileName, float *wave) {
	// TODO Rewrite this properly
	FILE *f = fopen(fileName, "rb");
	if (!f)
		return;
	fseek(f, 44, SEEK_SET);
	int16_t data[NUM_SAMPLES];
	fread(data, sizeof(int16_t), NUM_WAVES * NUM_SAMPLES, f);

	for (int j = 0; j < NUM_SAMPLES; j++) {
		wave[j] = data[j] / 32767.0;
	}

	fclose(f);
}

/** If NULL, clears wave */
void setWave(int waveId, const float *wave) {
	if (wave)
		memcpy(waves[waveId], wave, sizeof(float) * NUM_SAMPLES);
	else
		memset(waves[waveId], 0, sizeof(float) * NUM_SAMPLES);
	commitWave(waveId);
}

////////////////////
// Wave Files
////////////////////

struct WaveFile {
	float samples[NUM_SAMPLES];
	std::string name;
};

struct WaveDirectory {
	std::vector<WaveFile> waveFiles;
	std::string name;
};

std::vector<WaveDirectory> waveDirectories;


int filterCallback(const struct dirent *dir) {
	if (dir->d_name[0] == '.')
		return 0;
	return 1;
}


void wavetableInit() {
	struct dirent **directories;
	const char *wavesDir = "waves";
	int directoriesLen = scandir(wavesDir, &directories, filterCallback, alphasort);

	for (int i = 0; i < directoriesLen; i++) {
		// Skip digits at beginning of filename
		// e.g. "00Digital" -> "Digital"
		const char *name = directories[i]->d_name;
		while (isdigit(*name))
			name++;

		WaveDirectory waveDirectory;
		waveDirectory.name = name;

		struct dirent **files;
		char directoryPath[PATH_MAX];
		snprintf(directoryPath, sizeof(directoryPath), "%s/%s", wavesDir, directories[i]->d_name);
		int filesLen = scandir(directoryPath, &files, filterCallback, alphasort);
		for (int i = 0; i < filesLen; i++) {
			const char *name = files[i]->d_name;
			while (isdigit(*name))
				name++;

			// Find last period
			const char *period = name;
			while (*period != '\0' && *period != '.')
				period++;

			WaveFile waveFile;
			waveFile.name = std::string(name, period - name);
			char wavePath[PATH_MAX];
			snprintf(wavePath, sizeof(wavePath), "%s/%s", directoryPath, files[i]->d_name);

			loadWave(wavePath, waveFile.samples);
			waveDirectory.waveFiles.push_back(waveFile);
			free(files[i]);
		}
		free(files);

		waveDirectories.push_back(waveDirectory);
		free(directories[i]);
	}
	free(directories);
}

////////////////////
// Audio
////////////////////

static float playVolume = -12.0;
static float playFrequency = 50.0;
static float playFrequencySmooth = playFrequency;
static bool playModeXY = false;
static bool playEnabled = false;
static SDL_AudioDeviceID audioDevice = 0;
static SDL_AudioSpec audioSpec;
static SRC_STATE *audioSrc;
static int playIndex = 0;

void computeOversample(const float *in, float *out, int len, int oversample) {
	float inCycle[len * 3];
	float outCycle[len * oversample * 3];
	memcpy(inCycle, in, sizeof(float) * len);
	memcpy(inCycle + len, in, sizeof(float) * len);
	memcpy(inCycle + 2 * len, in, sizeof(float) * len);
	SRC_DATA data;
	data.data_in = inCycle;
	data.data_out = outCycle;
	data.input_frames = len * 3;
	data.output_frames = len * oversample * 3;
	data.end_of_input = false;
	data.src_ratio = (double)oversample;
	src_simple(&data, SRC_SINC_MEDIUM_QUALITY, 1);
	memcpy(out, outCycle + len * oversample, sizeof(float) * len * oversample);
}

long audioSrcCallback(void *cb_data, float **data) {
	// TODO Fix this race condition for accessing postWaves
	// memcpy(*data, postWaves, sizeof(float) * NUM_SAMPLES);
	*data = postWaves[selectedWave];
	return NUM_SAMPLES;
}

void audioCallback(void *userdata, Uint8 *stream, int len) {
	float *samples = (float *) stream;
	int samplesLen = len / sizeof(float);

	if (playEnabled) {
		float gain = powf(10.0, playVolume / 20.0);
		const float lambda = 0.5;
		// Apply exponential smoothing
		playFrequencySmooth = powf(playFrequencySmooth, 1.0 - lambda) * powf(playFrequency, lambda);
		double ratio = (double)audioSpec.freq / NUM_SAMPLES / playFrequencySmooth;
		long samplesRead = src_callback_read(audioSrc, ratio, samplesLen, samples);
		assert(samplesRead == samplesLen);

		for (int i = 0; i < samplesLen; i++) {
			samples[i] *= gain;
		}
	} else {
		for (int i = 0; i < samplesLen; i++) {
			samples[i] = 0.0;
		}
	}
}

int audioDeviceCount() {
	return SDL_GetNumAudioDevices(0);
}

const char *audioDeviceName(int deviceId) {
	return SDL_GetAudioDeviceName(deviceId, 0);
}

void audioClose() {
	if (audioDevice > 0) {
		SDL_CloseAudioDevice(audioDevice);
	}
}

/** if deviceName is -1, the default audio device is chosen */
void audioOpen(int deviceId) {
	audioClose();

	SDL_AudioSpec spec;
	memset(&spec, 0, sizeof(spec));
	spec.freq = 44100;
	spec.format = AUDIO_F32;
	spec.channels = 1;
	spec.samples = 1024;
	spec.callback = audioCallback;

	const char *deviceName = deviceId >= 0 ? SDL_GetAudioDeviceName(deviceId, 0) : NULL;
	// TODO Be more tolerant of devices which can't use floats, 1 channel
	audioDevice = SDL_OpenAudioDevice(deviceName, 0, &spec, &audioSpec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
	if (audioDevice <= 0)
		return;
	SDL_PauseAudioDevice(audioDevice, 0);
}

void audioInit() {
	audioSrc = src_callback_new(audioSrcCallback, SRC_SINC_MEDIUM_QUALITY, 1, NULL, NULL);
	assert(audioSrc);
	audioOpen(-1);
}

void audioDestroy() {
	audioClose();
	src_delete(audioSrc);
}

////////////////////
// Renderers
////////////////////

static bool showTestWindow = false;
static bool exportPopup = false;
static ImTextureID logoTexture;

static enum {
	WAVEFORM_EDITOR,
	EFFECT_EDITOR,
	GRID_VIEW,
} currentPage = WAVEFORM_EDITOR;


ImTextureID loadImage(const char *filename) {
	GLuint textureId;
	glGenTextures(1, &textureId);
	glBindTexture(GL_TEXTURE_2D, textureId);
	unsigned char *pixels;
	unsigned int width, height;
	unsigned err = lodepng_decode_file(&pixels, &width, &height, filename, LCT_RGBA, 8);
	assert(!err);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	glBindTexture(GL_TEXTURE_2D, 0);
	return (void*)(intptr_t) textureId;
}

void getImageSize(ImTextureID id, int *width, int *height) {
	GLuint textureId = (GLuint)(intptr_t) id;
	glBindTexture(GL_TEXTURE_2D, textureId);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, width);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, height);
	glBindTexture(GL_TEXTURE_2D, 0);
}

enum Tool {
	PENCIL_TOOL,
	BRUSH_TOOL,
	GRAB_TOOL,
	LINE_TOOL,
	ERASER_TOOL,
	SMOOTH_TOOL,
};

void waveLine(float *points, int pointsLen, float startIndex, float endIndex, float startValue, float endValue) {
	// Switch indices if out of order
	if (startIndex > endIndex) {
		float tmpIndex = startIndex;
		startIndex = endIndex;
		endIndex = tmpIndex;
		float tmpValue = startValue;
		startValue = endValue;
		endValue = tmpValue;
	}

	int startI = maxi(0, roundf(startIndex));
	int endI = mini(pointsLen - 1, roundf(endIndex));
	for (int i = startI; i <= endI; i++) {
		float frac = (startIndex < endIndex) ? rescalef(i, startIndex, endIndex, 0.0, 1.0) : 0.0;
		points[i] = crossf(startValue, endValue, frac);
	}
}

void waveSmooth(float *points, int pointsLen, float index) {
	// TODO
	for (int i = 0; i <= pointsLen - 1; i++) {
		const float a = 0.05;
		float w = expf(-a * powf(i - index, 2.0));
		points[i] = clampf(points[i] + 0.01 * w, -1.0, 1.0);
	}
}

void waveBrush(float *points, int pointsLen, float startIndex, float endIndex, float startValue, float endValue) {
	const float sigma = 10.0;
	for (int i = 0; i < pointsLen; i++) {
		float x = i - startIndex;
		// float a;
		float a = expf(-x*x / (2.0*sigma));
		points[i] = crossf(points[i], startValue, a);
	}
}

bool editorBehavior(ImGuiID id, const ImRect& box, const ImRect& inner, float *points, int pointsLen, float minIndex, float maxIndex, float minValue, float maxValue, enum Tool tool) {
	ImGuiContext &g = *GImGui;
	ImGuiWindow *window = ImGui::GetCurrentWindow();

	bool hovered = ImGui::IsHovered(box, id);
	if (hovered) {
		ImGui::SetHoveredID(id);
		if (g.IO.MouseClicked[0]) {
			ImGui::SetActiveID(id, window);
			ImGui::FocusWindow(window);
			g.ActiveIdClickOffset = g.IO.MousePos - box.Min;
		}
	}

	// Unhover
	if (g.ActiveId == id) {
		if (!g.IO.MouseDown[0]) {
			ImGui::ClearActiveID();
		}
	}

	// Tool behavior
	if (g.ActiveId == id) {
		if (g.IO.MouseDown[0]) {
			ImVec2 pos = g.IO.MousePos;
			ImVec2 lastPos = g.IO.MousePos - g.IO.MouseDelta;
			ImVec2 originalPos = g.ActiveIdClickOffset + box.Min;
			float originalIndex = rescalef(originalPos.x, inner.Min.x, inner.Max.x, minIndex, maxIndex);
			float originalValue = rescalef(originalPos.y, inner.Min.y, inner.Max.y, minValue, maxValue);
			float lastIndex = rescalef(lastPos.x, inner.Min.x, inner.Max.x, minIndex, maxIndex);
			float lastValue = rescalef(lastPos.y, inner.Min.y, inner.Max.y, minValue, maxValue);
			float index = rescalef(pos.x, inner.Min.x, inner.Max.x, minIndex, maxIndex);
			float value = rescalef(pos.y, inner.Min.y, inner.Max.y, minValue, maxValue);

			// Pencil tool
			if (tool == PENCIL_TOOL) {
				waveLine(points, pointsLen, lastIndex, index, lastValue, value);
			}
			// Grab tool
			else if (tool == GRAB_TOOL) {
				waveLine(points, pointsLen, originalIndex, originalIndex, value, value);
			}
			// Brush tool
			else if (tool == BRUSH_TOOL) {
				waveBrush(points, pointsLen, lastIndex, index, lastValue, value);
			}
			// Line tool
			else if (tool == LINE_TOOL) {
				// TODO Restore points from when it was originally clicked, using undo history
				waveLine(points, pointsLen, originalIndex, index, originalValue, value);
			}
			// Eraser tool
			else if (tool == ERASER_TOOL) {
				waveLine(points, pointsLen, lastIndex, index, 0.0, 0.0);
			}
			// Smooth tool
			else if (tool == SMOOTH_TOOL) {
				waveSmooth(points, pointsLen, lastIndex);
			}

			for (int i = 0; i < pointsLen; i++) {
				points[i] = clampf(points[i], fminf(minValue, maxValue), fmaxf(minValue, maxValue));
			}

			return true;
		}
	}

	return false;
}

bool renderWaveEditor(const char *name, float height, float *points, int pointsLen, const float *lines, int linesLen, enum Tool tool) {
	if (!lines) {
		lines = points;
		linesLen = pointsLen;
	}

	ImGuiContext &g = *GImGui;
	ImGuiWindow *window = ImGui::GetCurrentWindow();
	const ImGuiStyle &style = g.Style;
	const ImGuiID id = window->GetID(name);

	// Compute positions
	ImVec2 pos = window->DC.CursorPos;
	ImVec2 size = ImVec2(ImGui::CalcItemWidth(), height);
	ImRect box = ImRect(pos, pos + size);
	ImRect inner = ImRect(box.Min + style.FramePadding, box.Max - style.FramePadding);
	ImGui::ItemSize(box, style.FramePadding.y);
	if (!ImGui::ItemAdd(box, NULL))
		return false;

	// Draw frame
	ImGui::RenderFrame(box.Min, box.Max, ImGui::GetColorU32(ImGuiCol_FrameBg), true, style.FrameRounding);

	// // Tooltip
	// if (ImGui::IsHovered(box, 0)) {
	// 	ImVec2 mousePos = ImGui::GetMousePos();
	// 	float x = rescalef(mousePos.x, inner.Min.x, inner.Max.x, 0, pointsLen-1);
	// 	int xi = (int)clampf(x, 0, pointsLen-2);
	// 	float xf = x - xi;
	// 	float y = crossf(values[xi], values[xi+1], xf);
	// 	ImGui::SetTooltip("%f, %f\n", x, y);
	// }

	// const bool hovered = ImGui::IsHovered(box, id);
	// if (hovered) {
	// 	ImGui::SetHoveredID(id);
	// 	ImGui::SetActiveID(id, window);
	// }

	bool edited = editorBehavior(id, box, inner, points, pointsLen, 0.0, pointsLen - 1, 1.0, -1.0, tool);

	ImGui::PushClipRect(box.Min, box.Max, false);
	ImVec2 pos0, pos1;
	// Draw lines
	for (int i = 0; i < linesLen; i++) {
		pos1 = ImVec2(rescalef(i, 0, linesLen - 1, inner.Min.x, inner.Max.x), rescalef(lines[i], 1.0, -1.0, inner.Min.y, inner.Max.y));
		if (i > 0)
			window->DrawList->AddLine(pos0, pos1, ImGui::GetColorU32(ImGuiCol_PlotLines));
		pos0 = pos1;
	}
	// Draw points
	for (int i = 0; i < pointsLen; i++) {
		pos1 = ImVec2(rescalef(i, 0, pointsLen - 1, inner.Min.x, inner.Max.x), rescalef(points[i], 1.0, -1.0, inner.Min.y, inner.Max.y));
		window->DrawList->AddCircleFilled(pos1 + ImVec2(0.5, 0.5), 2.0, ImGui::GetColorU32(ImGuiCol_PlotLines), 12);
	}
	ImGui::PopClipRect();

	return edited;
}

bool renderHistogramEditor(const char *name, float height, float *points, int pointsLen, const float *lines, int linesLen, enum Tool tool) {
	ImGuiContext &g = *GImGui;
	ImGuiWindow *window = ImGui::GetCurrentWindow();
	const ImGuiStyle &style = g.Style;
	const ImGuiID id = window->GetID(name);

	// Compute positions
	ImVec2 pos = window->DC.CursorPos;
	ImVec2 size = ImVec2(ImGui::CalcItemWidth(), height);
	ImRect box = ImRect(pos, pos + size);
	ImRect inner = ImRect(box.Min + style.FramePadding, box.Max - style.FramePadding);
	ImGui::ItemSize(box, style.FramePadding.y);
	if (!ImGui::ItemAdd(box, NULL))
		return false;

	bool edited = editorBehavior(id, box, inner, points, pointsLen, -0.5, pointsLen - 0.5, 1.0, 0.0, tool);

	// Draw frame
	ImGui::RenderFrame(box.Min, box.Max, ImGui::GetColorU32(ImGuiCol_FrameBg), true, style.FrameRounding);

	// Draw bars
	ImGui::PushClipRect(box.Min, box.Max, false);
	const float rounding = 0.0;
	for (int i = 0; i < pointsLen; i++) {
		float value = points[i];
		ImVec2 pos0 = ImVec2(rescalef(i, 0, pointsLen, inner.Min.x, inner.Max.x), rescalef(value, 0.0, 1.0, inner.Max.y, inner.Min.y));
		ImVec2 pos1 = ImVec2(rescalef(i + 1, 0, pointsLen, inner.Min.x, inner.Max.x) - 1, inner.Max.y);
		window->DrawList->AddRectFilled(pos0, pos1, ImGui::GetColorU32(ImVec4(1.0, 0.8, 0.2, 1.0)), rounding);
	}
	for (int i = 0; i < linesLen; i++) {
		float value = lines[i];
		ImVec2 pos0 = ImVec2(rescalef(i, 0, linesLen, inner.Min.x, inner.Max.x), rescalef(value, 0.0, 1.0, inner.Max.y, inner.Min.y));
		ImVec2 pos1 = ImVec2(rescalef(i + 1, 0, linesLen, inner.Min.x, inner.Max.x) - 1, inner.Max.y);
		window->DrawList->AddRectFilled(pos0, pos1, ImGui::GetColorU32(ImVec4(0.9, 0.7, 0.1, 0.75)), rounding);
	}
	ImGui::PopClipRect();

	return edited;
}

void renderMenuBar() {
	// HACK
	// Display a window on top of the menu with the logo, since I'm too lazy to make my own custom MenuImageItem widget
	{
		int width, height;
		getImageSize(logoTexture, &width, &height);
		ImVec2 padding = ImVec2(8, 3);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0, 0));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
		ImGui::SetNextWindowPos(ImVec2(0, 0));
		ImGui::SetNextWindowSize(ImVec2(width + 2*padding.x, height + 2*padding.y));
		if (ImGui::Begin("Logo", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoInputs)) {
			ImGui::Image(logoTexture, ImVec2(width, height));
			ImGui::End();
		}
		ImGui::PopStyleVar();
		ImGui::PopStyleVar();
	}



	if (ImGui::BeginMenuBar()) {
		// This will be hidden by the window with the logo
		if (ImGui::BeginMenu("                          WaveEditor V0.0", false)) {
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("File")) {
			if (ImGui::MenuItem("New Bank")) {}
			if (ImGui::MenuItem("Open...")) {
				const char *filename = noc_file_dialog_open(NOC_FILE_DIALOG_OPEN, "WAV\0*.wav\0", NULL, NULL);
				printf("%s\n", filename);
			}
			if (ImGui::MenuItem("Save As...")) {
				const char *filename = noc_file_dialog_open(NOC_FILE_DIALOG_SAVE, "WAV\0*.wav\0", NULL, NULL);
				printf("%s\n", filename);
			}
			if (ImGui::MenuItem("Export...")) exportPopup = true;

			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Upload")) {
			ImGui::MenuItem("(Not supported)", NULL, false, false);
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Download")) {
			ImGui::MenuItem("(Not supported)", NULL, false, false);
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Audio Output")) {
			int deviceCount = audioDeviceCount();
			for (int deviceId = 0; deviceId < deviceCount; deviceId++) {
				const char *deviceName = audioDeviceName(deviceId);
				if (ImGui::MenuItem(deviceName, NULL, false)) audioOpen(deviceId);
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Help")) {
			if (ImGui::MenuItem("Online manual", NULL, false))
				openBrowser("https://github.com/AndrewBelt/WaveEditor/wiki");
			if (ImGui::MenuItem("imgui Demo", NULL, showTestWindow)) showTestWindow = !showTestWindow;
			ImGui::EndMenu();
		}
		ImGui::EndMenuBar();
	}
}

void renderPreview() {
	ImGui::Checkbox("Preview", &playEnabled);
	ImGui::SameLine();
	ImGui::PushItemWidth(300.0);
	ImGui::SliderFloat("##playVolume", &playVolume, -60.0f, 0.0f, "Volume: %.2f dB");
	ImGui::PushItemWidth(-1.0);
	ImGui::SameLine();
	ImGui::SliderFloat("##playFrequency", &playFrequency, 1.0f, 10000.0f, "Frequency: %.2f Hz", 3.0f);

	if (ImGui::RadioButton("Morph XY", playModeXY)) playModeXY = true;
	ImGui::SameLine();
	if (ImGui::RadioButton("Morph Z", !playModeXY)) playModeXY = false;
	if (playModeXY) {
		// TODO
		static float morphX = 0.0;
		static float morphY = 0.0;
		ImGui::SameLine();
		ImGui::PushItemWidth(300.0);
		ImGui::SliderFloat("##Morph X", &morphX, 0.0, 7.0, "Morph X: %.3f");
		ImGui::SameLine();
		ImGui::PushItemWidth(-1.0);
		ImGui::SliderFloat("##Morph Y", &morphY, 0.0, 7.0, "Morph Y: %.3f");
	}
	else {
		static float morphZ = 0.0;
		ImGui::SameLine();
		ImGui::SliderFloat("##Morph Z", &morphZ, 0.0, 63.0, "Morph Z: %.3f");
	}
}

void renderEditor() {
	ImGui::BeginChild("Sub1", ImVec2(200, 0), true);
	{
		// User state
		static const char *items[NUM_WAVES] = {
			"Item One", "Item Two", "Item Three", "Item Four", "Item Four", "Item Four", "Item Four", "Item Four", "Item Four", "Item Four", "Item Four", "Item Four", "Item Four", "Item Four", "Item Four", "Item Four", "Item Four", "Item Four", "Item Four", "Item Four", "Item Four", "Item Four", "Item Four", "Item Four", "Item Four", "Item Four", "Item Four", "Item Four", "Item Four", "Item Four", "Item Four", "Item Four"
		};

		// Render + dragging
		ImGui::PushItemWidth(-20);
		for (int n = 0; n < NUM_WAVES; n++) {
			// ImGui::Selectable(items[n]);
			char text[10];
			snprintf(text, sizeof(text), "%d", n);
			// renderWaveEditor(waves[n], NUM_SAMPLES);
			ImGui::PlotLines(text, postWaves[n], 256, 0, NULL, -1.0f, 1.0f, ImVec2(0, 32));

			// if (ImGui::IsItemActive() && !ImGui::IsItemHovered()) {
			// 	float drag_dy = ImGui::GetMouseDragDelta(0).y;
			// 	if (drag_dy < 0.0f && n > 0) {
			// 		// Swap
			// 		const char *tmp = items[n];
			// 		items[n] = items[n - 1];
			// 		items[n - 1] = tmp;
			// 		ImGui::ResetMouseDragDelta();
			// 	} else if (drag_dy > 0.0f && n < NUM_WAVES - 1) {
			// 		const char *tmp = items[n];
			// 		items[n] = items[n + 1];
			// 		items[n + 1] = tmp;
			// 		ImGui::ResetMouseDragDelta();
			// 	}
			// }
		}
		ImGui::PopItemWidth();
	}
	ImGui::EndChild();

	ImGui::SameLine();
	ImGui::BeginChild("Sub2", ImVec2(0, 0), true);
	{
		ImGui::PushItemWidth(-1);
		ImGui::Text("Waveform editor");

		if (ImGui::Button("Clear"))
			setWave(selectedWave, NULL);

		for (WaveDirectory &waveDirectory : waveDirectories) {
			ImGui::SameLine();
			if (ImGui::Button(waveDirectory.name.c_str())) ImGui::OpenPopup(waveDirectory.name.c_str());
			if (ImGui::BeginPopup(waveDirectory.name.c_str())) {
				for (WaveFile &waveFile : waveDirectory.waveFiles) {
					if (ImGui::Selectable(waveFile.name.c_str())) {
						setWave(selectedWave, waveFile.samples);
					}
				}
				ImGui::EndPopup();
			}
		}

		static enum Tool tool = PENCIL_TOOL;
		ImGui::SameLine();
		if (ImGui::RadioButton("Pencil", tool == PENCIL_TOOL)) tool = PENCIL_TOOL;
		ImGui::SameLine();
		if (ImGui::RadioButton("Brush", tool == BRUSH_TOOL)) tool = BRUSH_TOOL;
		ImGui::SameLine();
		if (ImGui::RadioButton("Grab", tool == GRAB_TOOL)) tool = GRAB_TOOL;
		ImGui::SameLine();
		if (ImGui::RadioButton("Line", tool == LINE_TOOL)) tool = LINE_TOOL;
		ImGui::SameLine();
		if (ImGui::RadioButton("Eraser", tool == ERASER_TOOL)) tool = ERASER_TOOL;
		// ImGui::SameLine();
		// if (ImGui::RadioButton("Smooth", tool == SMOOTH_TOOL)) tool = SMOOTH_TOOL;

		float waveOversample[NUM_SAMPLES * OVERSAMPLE];
		computeOversample(postWaves[selectedWave], waveOversample, NUM_SAMPLES, OVERSAMPLE);
		if (renderWaveEditor("we1", 200.0, waves[selectedWave], NUM_SAMPLES, waveOversample, NUM_SAMPLES * OVERSAMPLE, tool)) {
			commitWave(selectedWave);
		}

		if (renderHistogramEditor("he1", 200.0, harmonics[selectedWave], NUM_SAMPLES / 2, postHarmonics[selectedWave], NUM_SAMPLES / 2, tool)) {
			commitHarmonics(selectedWave);
		}

		// ImGui::PlotHistogram("##hidelabel", waves[selectedWave], 64, 0, NULL, -1.0f, 1.0f, ImVec2(0, 100));
		ImGui::Text("Waveshaper");
		if (ImGui::SliderFloat("##Pre Gain", &effects[selectedWave].preGain, 0.0f, 1.0f, "Pre Gain: %.3f")) updatePost(selectedWave);
		if (ImGui::SliderFloat("##Harmonic Shift", &effects[selectedWave].harmonicShift, 0.0f, 1.0f, "Harmonic Shift: %.3f")) updatePost(selectedWave);
		if (ImGui::SliderFloat("##Comb", &effects[selectedWave].comb, 0.0f, 1.0f, "Comb: %.3f")) updatePost(selectedWave);
		if (ImGui::SliderFloat("##Ring Modulation", &effects[selectedWave].ring, 0.0f, 1.0f, "Ring Modulation: %.3f")) updatePost(selectedWave);
		if (ImGui::SliderFloat("##Chebyshev", &effects[selectedWave].chebyshev, 0.0f, 1.0f, "Chebyshev: %.3f")) updatePost(selectedWave);
		if (ImGui::SliderFloat("##Quantization", &effects[selectedWave].quantization, 0.0f, 1.0f, "Quantization: %.3f")) updatePost(selectedWave);
		if (ImGui::SliderFloat("##Posterization", &effects[selectedWave].posterization, 0.0f, 1.0f, "Posterization: %.3f")) updatePost(selectedWave);
		if (ImGui::SliderFloat("##Slew Limiter", &effects[selectedWave].slew, 0.0f, 1.0f, "Slew Limiter: %.3f")) updatePost(selectedWave);
		if (ImGui::SliderFloat("##Brickwall Lowpass", &effects[selectedWave].lowpass, 0.0f, 1.0f, "Brickwall Lowpass: %.3f")) updatePost(selectedWave);
		if (ImGui::SliderFloat("##Brickwall Highpass", &effects[selectedWave].highpass, 0.0f, 1.0f, "Brickwall Highpass: %.3f")) updatePost(selectedWave);
		if (ImGui::SliderFloat("##Post Gain", &effects[selectedWave].postGain, 0.0f, 1.0f, "Post Gain: %.3f")) updatePost(selectedWave);
		if (ImGui::Checkbox("Cycle", &effects[selectedWave].cycle)) updatePost(selectedWave);
		ImGui::SameLine();
		if (ImGui::Checkbox("Normalize", &effects[selectedWave].normalize)) updatePost(selectedWave);

		if (ImGui::Button("Apply")) bakeEffect(selectedWave);
		ImGui::SameLine();
		if (ImGui::Button("Randomize")) randomizeEffect(selectedWave);
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) clearEffect(selectedWave);
		if (ImGui::Button("Dump to WAV")) saveBank("out.wav");

		ImGui::PopItemWidth();
	}
	ImGui::EndChild();
}

void renderExport() {
	if (exportPopup) {
		ImGui::OpenPopup("Export");
		exportPopup = false;
	}
	if (ImGui::BeginPopupModal("Export", NULL, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
		ImGui::Text("All those beautiful files will be deleted.\nThis operation cannot be undone!\n\n");
		if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
}

void renderMain() {
	ImGui::SetNextWindowPos(ImVec2(0, 0));
	ImGui::SetNextWindowSize(ImVec2((int)ImGui::GetIO().DisplaySize.x, (int)ImGui::GetIO().DisplaySize.y));

	ImGui::Begin("", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_MenuBar);
	{
		// Menu bar
		renderMenuBar();
		renderPreview();
		ImGui::Spacing();
		ImGui::Spacing();
		// Tab bar
		{
			ImGui::Columns(6, "Tabs", false);
			if (ImGui::Selectable("Waveform Editor", currentPage == WAVEFORM_EDITOR)) currentPage = WAVEFORM_EDITOR;
			ImGui::NextColumn();
			if (ImGui::Selectable("Effect Editor", currentPage == EFFECT_EDITOR)) currentPage = EFFECT_EDITOR;
			ImGui::NextColumn();
			if (ImGui::Selectable("Grid View", currentPage == GRID_VIEW)) currentPage = GRID_VIEW;
			ImGui::NextColumn();
			ImGui::Selectable("???");
			ImGui::NextColumn();
			ImGui::Selectable("???");
			ImGui::NextColumn();
			ImGui::Selectable("???");
			ImGui::NextColumn();
			ImGui::Columns(1);
		}
		ImGui::Spacing();
		ImGui::Spacing();
		// Page
		if (currentPage == WAVEFORM_EDITOR)
			renderEditor();

		// Modals
		renderExport();
	}
	ImGui::End();

	if (showTestWindow) {
		ImGui::ShowTestWindow(&showTestWindow);
	}
}

////////////////////
// Main
////////////////////

/** Sets the style colors and font */
void initStyle() {
	ImGuiStyle& style = ImGui::GetStyle();

	style.WindowRounding = 2.f;
	style.GrabRounding = 2.f;
	style.ChildWindowRounding = 2.f;
	style.ScrollbarRounding = 2.f;
	style.FrameRounding = 2.f;
	style.FramePadding = ImVec2(6.0f, 4.0f);

	style.Colors[ImGuiCol_Text]                  = ImVec4(0.73f, 0.73f, 0.73f, 1.00f);
	style.Colors[ImGuiCol_TextDisabled]          = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
	style.Colors[ImGuiCol_WindowBg]              = ImVec4(0.26f, 0.26f, 0.26f, 0.95f);
	style.Colors[ImGuiCol_ChildWindowBg]         = ImVec4(0.26f, 0.26f, 0.26f, 1.00f);
	style.Colors[ImGuiCol_PopupBg]               = ImVec4(0.26f, 0.26f, 0.26f, 1.00f);
	style.Colors[ImGuiCol_Border]                = ImVec4(0.26f, 0.26f, 0.26f, 1.00f);
	style.Colors[ImGuiCol_BorderShadow]          = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
	style.Colors[ImGuiCol_FrameBg]               = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
	style.Colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
	style.Colors[ImGuiCol_FrameBgActive]         = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
	style.Colors[ImGuiCol_TitleBg]               = ImVec4(0.36f, 0.36f, 0.36f, 1.00f);
	style.Colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.36f, 0.36f, 0.36f, 1.00f);
	style.Colors[ImGuiCol_TitleBgActive]         = ImVec4(0.36f, 0.36f, 0.36f, 1.00f);
	style.Colors[ImGuiCol_MenuBarBg]             = ImVec4(0.26f, 0.26f, 0.26f, 1.00f);
	style.Colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.21f, 0.21f, 0.21f, 1.00f);
	style.Colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.36f, 0.36f, 0.36f, 1.00f);
	style.Colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.36f, 0.36f, 0.36f, 1.00f);
	style.Colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.36f, 0.36f, 0.36f, 1.00f);
	style.Colors[ImGuiCol_ComboBg]               = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
	style.Colors[ImGuiCol_CheckMark]             = ImVec4(0.78f, 0.78f, 0.78f, 1.00f);
	style.Colors[ImGuiCol_SliderGrab]            = ImVec4(0.74f, 0.74f, 0.74f, 1.00f);
	style.Colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.74f, 0.74f, 0.74f, 1.00f);
	style.Colors[ImGuiCol_Button]                = ImVec4(0.36f, 0.36f, 0.36f, 1.00f);
	style.Colors[ImGuiCol_ButtonHovered]         = ImVec4(0.43f, 0.43f, 0.43f, 1.00f);
	style.Colors[ImGuiCol_ButtonActive]          = ImVec4(0.11f, 0.11f, 0.11f, 1.00f);
	style.Colors[ImGuiCol_Header]                = ImVec4(0.36f, 0.36f, 0.36f, 1.00f);
	style.Colors[ImGuiCol_HeaderHovered]         = ImVec4(0.36f, 0.36f, 0.36f, 1.00f);
	style.Colors[ImGuiCol_HeaderActive]          = ImVec4(0.36f, 0.36f, 0.36f, 1.00f);
	style.Colors[ImGuiCol_Column]                = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);
	style.Colors[ImGuiCol_ColumnHovered]         = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	style.Colors[ImGuiCol_ColumnActive]          = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	style.Colors[ImGuiCol_ResizeGrip]            = ImVec4(0.36f, 0.36f, 0.36f, 1.00f);
	style.Colors[ImGuiCol_ResizeGripHovered]     = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	style.Colors[ImGuiCol_ResizeGripActive]      = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	style.Colors[ImGuiCol_CloseButton]           = ImVec4(0.59f, 0.59f, 0.59f, 1.00f);
	style.Colors[ImGuiCol_CloseButtonHovered]    = ImVec4(0.98f, 0.39f, 0.36f, 1.00f);
	style.Colors[ImGuiCol_CloseButtonActive]     = ImVec4(0.98f, 0.39f, 0.36f, 1.00f);
	style.Colors[ImGuiCol_PlotLines]             = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);
	style.Colors[ImGuiCol_PlotLinesHovered]      = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
	style.Colors[ImGuiCol_PlotHistogram]         = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
	style.Colors[ImGuiCol_PlotHistogramHovered]  = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
	style.Colors[ImGuiCol_TextSelectedBg]        = ImVec4(0.32f, 0.52f, 0.65f, 1.00f);
	style.Colors[ImGuiCol_ModalWindowDarkening]  = ImVec4(0.20f, 0.20f, 0.20f, 0.50f);

	// Load fonts
	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->AddFontFromFileTTF("lekton/Lekton-Regular.ttf", 15.0);
}

int main() {
	ImGui::GetIO().IniFilename = NULL;

	// Set up SDL
	int err = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
	assert(!err);

	// Set up window
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
	SDL_DisplayMode current;
	SDL_GetCurrentDisplayMode(0, &current);
	SDL_Window *window = SDL_CreateWindow("Synthesis Technology WaveEditor", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1024, 768, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_SHOWN);
	assert(window);
	SDL_SetWindowMinimumSize(window, 800, 600);
	SDL_GLContext glContext = SDL_GL_CreateContext(window);
	SDL_GL_SetSwapInterval(1);

	initStyle();
	logoTexture = loadImage("logo-white.png");

	// Set up Imgui binding
	ImGui_ImplSdl_Init(window);

	initWaves();
	wavetableInit();
	loadBank("banks/ROM A.wav");
	audioInit();

	// Main loop
	bool running = true;
	while (running) {
		// Scan events
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			ImGui_ImplSdl_ProcessEvent(&event);
			if (event.type == SDL_QUIT) {
				running = false;
			}
		}

		// Build render buffer
		ImGui_ImplSdl_NewFrame(window);
		renderMain();

		// Render frame
		glViewport(0, 0, (int)ImGui::GetIO().DisplaySize.x, (int)ImGui::GetIO().DisplaySize.y);

		glClearColor(0.0, 0.0, 0.0, 1.0);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui::Render();
		SDL_GL_SwapWindow(window);
	}

	// Cleanup
	ImGui_ImplSdl_Shutdown();
	SDL_GL_DeleteContext(glContext);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}
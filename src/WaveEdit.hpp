#pragma once

#include <stdio.h>
#include <math.h>
#include <assert.h>
#include <stdint.h>
#include <string>
#include <thread>
#include <vector>
#include <complex>


#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)


////////////////////
// math.cpp
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
Assumes that the array at `p` is of length at least ceil(x).
*/
inline float linterpf(const float *p, float x) {
	int xi = x;
	float xf = x - xi;
	if (xf < 1e-6)
		return p[xi];
	else
		return crossf(p[xi], p[xi + 1], xf);
}

/** Returns a random number on [0, 1) */
inline float randf() {
	return (float)rand() / RAND_MAX;
}

/** Complex multiply c = a * b
It is of course acceptable to reuse arguments
i.e. cmultf(&ar, &ai, ar, ai, br, bi)
*/
inline void cmultf(float *cr, float *ci, float ar, float ai, float br, float bi) {
	*cr = ar * br - ai * bi;
	*ci = ar * bi + ai * br;
}

void RFFT(const float *in, float *out, int len);
void IRFFT(const float *in, float *out, int len);

int resample(const float *in, int inLen, float *out, int outLen, double ratio);
void cyclicOversample(const float *in, float *out, int len, int oversample);


////////////////////
// util.cpp
////////////////////

void openBrowser(const char *url);
/** Caller must free(). Returns NULL if unsuccessful */
float *loadAudio(const char *filename, int *length);


////////////////////
// wave.cpp
////////////////////

#define WAVE_LEN 256

enum EffectID {
	PRE_GAIN,
	HARMONIC_SHIFT,
	COMB,
	RING,
	CHEBYSHEV,
	SAMPLE_AND_HOLD,
	QUANTIZATION,
	SLEW,
	LOWPASS,
	HIGHPASS,
	POST_GAIN,
	EFFECTS_LEN
};

extern const char *effectNames[EFFECTS_LEN];

struct Wave {
	float samples[WAVE_LEN];
	/** FFT of wave, interleaved complex numbers */
	float spectrum[WAVE_LEN];
	/** Norm of spectrum */
	float harmonics[WAVE_LEN / 2];
	/** Wave after effects have been applied */
	float postSamples[WAVE_LEN];
	float postSpectrum[WAVE_LEN];
	float postHarmonics[WAVE_LEN / 2];

	float effects[EFFECTS_LEN];
	bool cycle;
	bool normalize;

	void clear();
	/** Generates post*** arrays from the sample array, by applying effects */
	void updatePost();
	void commitSamples();
	void commitHarmonics();
	void clearEffects();
	/** Applies effects to the sample array and resets the effect parameters */
	void bakeEffects();
	void randomizeEffects();
	void save(const char *filename);
};


////////////////////
// bank.cpp
////////////////////

#define BANK_LEN 64
#define BANK_GRID_WIDTH 8
#define BANK_GRID_HEIGHT 8

enum ImportMode {
	OVERWRITE_IMPORT,
	CLEAR_IMPORT,
	ADD_IMPORT,
	MULTIPLY_IMPORT,
	LOOP_IMPORT,
};

struct Bank {
	Wave waves[BANK_LEN];

	void clear();
	void setSamples(const float *in);
	void getSamples(float *out);
	void importSamples(const float *in, int inLen, float offset, ImportMode mode);
	void save(const char *filename);
	void load(const char *filename);
	/** Saves each wave to its own file in a directory */
	void saveWaves(const char *dirname);
};


////////////////////
// history.cpp
////////////////////

void historyPush();
void historyPop();
void historyRedo();

extern Bank currentBank;


////////////////////
// waves.cpp
////////////////////

struct WaveFile {
	float samples[WAVE_LEN];
	std::string name;
};

struct WaveDirectory {
	std::vector<WaveFile> waveFiles;
	std::string name;
};

extern std::vector<WaveDirectory> waveDirectories;

void wavesInit();


////////////////////
// audio.cpp
////////////////////

// TODO Some of these should not be exposed in the header
extern float playVolume;
extern float playFrequency;
extern float playFrequencySmooth;
extern bool playEnabled;
extern bool playModeXY;
extern float morphX;
extern float morphY;
extern float morphZ;
extern int playIndex;
extern const char *audioDeviceName;

int audioGetDeviceCount();
const char *audioGetDeviceName(int deviceId);
void audioClose();
void audioOpen(int deviceId);
void audioInit();
void audioDestroy();


////////////////////
// ui.cpp
////////////////////

void uiInit();
void uiRender();

#pragma once

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#define _USE_MATH_DEFINES
#include <math.h>

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
void i16_to_f32(const int16_t *in, float *out, int length);
void f32_to_i16(const float *in, int16_t *out, int length);


////////////////////
// util.cpp
////////////////////

/** Opens a URL, also happens to work with PDFs */
void openBrowser(const char *url);
/** Caller must free(). Returns NULL if unsuccessful */
float *loadAudio(const char *filename, int *length);
/** Converts a printf format to a std::string */
std::string stringf(const char *format, ...);
/** Truncates a string if needed, inserting ellipses (...), to be no greater than `maxLen` characters */
void ellipsize(char *str, int maxLen);
unsigned char *base64_encode(const unsigned char *src, size_t len, size_t *out_len);
unsigned char *base64_decode(const unsigned char *src, size_t len, size_t *out_len);


////////////////////
// wave.cpp
////////////////////

#define WAVE_LEN 256

enum EffectID {
	PRE_GAIN,
	PHASE_SHIFT,
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
	/** Generates post arrays from the sample array, by applying effects */
	void updatePost();
	void commitSamples();
	void commitHarmonics();
	void clearEffects();
	/** Applies effects to the sample array and resets the effect parameters */
	void bakeEffects();
	void randomizeEffects();
	void saveWAV(const char *filename);
	void loadWAV(const char *filename);
	/** Writes to a global state */
	void clipboardCopy();
	void clipboardPaste();
};

extern bool clipboardActive;


////////////////////
// bank.cpp
////////////////////

#define BANK_LEN 64
#define BANK_GRID_WIDTH 8
#define BANK_GRID_HEIGHT 8

struct Bank {
	Wave waves[BANK_LEN];

	void clear();
	void swap(int i, int j);
	void shuffle();
	/** `in` must be length BANK_LEN * WAVE_LEN */
	void setSamples(const float *in);
	void getPostSamples(float *out);
	void duplicateToAll(int waveId);
	/** Binary dump of the bank struct */
	void save(const char *filename);
	void load(const char *filename);
	/** WAV file with BANK_LEN * WAVE_LEN samples */
	void saveWAV(const char *filename);
	void loadWAV(const char *filename);
	/** Saves each wave to its own file in a directory */
	void saveWaves(const char *dirname);
};


////////////////////
// history.cpp
////////////////////

/** Call as much as you like. History will only be pushed if a time delay between the last call has occurred. */
void historyPush();
void historyUndo();
void historyRedo();
void historyClear();

extern Bank currentBank;


////////////////////
// catalog.cpp
////////////////////

struct CatalogFile {
	float samples[WAVE_LEN];
	std::string name;
};

struct CatalogCategory {
	std::vector<CatalogFile> files;
	std::string name;
};

extern std::vector<CatalogCategory> catalogCategories;

void catalogInit();


////////////////////
// audio.cpp
////////////////////

// TODO Some of these should not be exposed in the header
extern float playVolume;
extern float playFrequency;
extern float playFrequencySmooth;
extern bool playEnabled;
extern bool playModeXY;
extern bool morphInterpolate;
extern float morphX;
extern float morphY;
extern float morphZ;
extern float morphZSpeed;
extern int playIndex;
extern const char *audioDeviceName;
extern Bank *playingBank;

int audioGetDeviceCount();
const char *audioGetDeviceName(int deviceId);
void audioClose();
void audioOpen(int deviceId);
void audioInit();
void audioDestroy();


////////////////////
// widgets.cpp
////////////////////

enum Tool {
	NO_TOOL,
	PENCIL_TOOL,
	BRUSH_TOOL,
	GRAB_TOOL,
	LINE_TOOL,
	ERASER_TOOL,
	SMOOTH_TOOL,
	NUM_TOOLS
};


bool renderWave(const char *name, float height, float *points, int pointsLen, const float *lines, int linesLen, enum Tool tool = NO_TOOL);
bool renderHistogram(const char *name, float height, float *bars, int barsLen, const float *ghost, int ghostLen, enum Tool tool);
void renderBankGrid(const char *name, float height, int gridWidth, float *gridX, float *gridY);
void renderWaterfall(const char *name, float height, float amplitude, float angle, float *activeZ);
/** A widget like renderWave() except without editing, and bank lines are overlaid
Returns the relative amount dragged
*/
float renderBankWave(const char *name, float height, const float *lines, int linesLen, float bankStart, float bankEnd, int bankLen);

////////////////////
// ui.cpp
////////////////////

void renderWaveMenu();
void uiInit();
void uiDestroy();
void uiRender();

// Selections span the range between these indices
extern int selectedId;
extern int lastSelectedId;
extern char lastFilename[1024];


////////////////////
// db.cpp
////////////////////

void dbInit();
void dbPage();


////////////////////
// import.cpp
////////////////////

void importPage();

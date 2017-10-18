#include "WaveEdit.hpp"
#include <SDL.h>
#include <samplerate.h>


float playVolume = -12.0;
float playFrequency = 220.0;
float playFrequencySmooth = playFrequency;
bool playModeXY = false;
bool playEnabled = false;
bool morphInterpolate = true;
float morphX = 0.0;
float morphY = 0.0;
float morphZ = 0.0;
float morphZSpeed = 0.0;
int playIndex = 0;
Bank *playingBank;

static float morphXSmooth = morphX;
static float morphYSmooth = morphY;
static float morphZSmooth = morphZ;
static SDL_AudioDeviceID audioDevice = 0;
static SDL_AudioSpec audioSpec;
static SRC_STATE *audioSrc = NULL;

long srcCallback(void *cb_data, float **data) {
	float gain = powf(10.0, playVolume / 20.0);
	// Generate next samples
	const int inLen = 64;
	static float in[inLen];
	for (int i = 0; i < inLen; i++) {
		if (morphInterpolate) {
			const float lambdaMorph = fminf(0.1 / playFrequency, 0.5);
			morphXSmooth = crossf(morphXSmooth, clampf(morphX, 0.0, BANK_GRID_WIDTH - 1), lambdaMorph);
			morphYSmooth = crossf(morphYSmooth, clampf(morphY, 0.0, BANK_GRID_HEIGHT - 1), lambdaMorph);
			morphZSmooth = crossf(morphZSmooth, clampf(morphZ, 0.0, BANK_LEN - 1), lambdaMorph);
		}
		else {
			// Snap X, Y, Z
			morphXSmooth = roundf(morphX);
			morphYSmooth = roundf(morphY);
			morphZSmooth = roundf(morphZ);
		}

		int index = (playIndex + i) % WAVE_LEN;
		if (playModeXY) {
			// Morph XY
			int xi = morphXSmooth;
			float xf = morphXSmooth - xi;
			int yi = morphYSmooth;
			float yf = morphYSmooth - yi;
			// 2D linear interpolate
			float v0 = crossf(
				playingBank->waves[yi * BANK_GRID_WIDTH + xi].postSamples[index],
				playingBank->waves[yi * BANK_GRID_WIDTH + eucmodi(xi + 1, BANK_GRID_WIDTH)].postSamples[index],
				xf);
			float v1 = crossf(
				playingBank->waves[eucmodi(yi + 1, BANK_GRID_HEIGHT) * BANK_GRID_WIDTH + xi].postSamples[index],
				playingBank->waves[eucmodi(yi + 1, BANK_GRID_HEIGHT) * BANK_GRID_WIDTH + eucmodi(xi + 1, BANK_GRID_WIDTH)].postSamples[index],
				xf);
			in[i] = crossf(v0, v1, yf);
		}
		else {
			// Morph Z
			int zi = morphZSmooth;
			float zf = morphZSmooth - zi;
			in[i] = crossf(
				playingBank->waves[zi].postSamples[index],
				playingBank->waves[eucmodi(zi + 1, BANK_LEN)].postSamples[index],
				zf);
		}
		in[i] = clampf(in[i] * gain, -1.0, 1.0);
	}

	playIndex += inLen;
	playIndex %= WAVE_LEN;

	*data = in;
	return inLen;
}


void audioCallback(void *userdata, Uint8 *stream, int len) {
	float *out = (float *) stream;
	int outLen = len / sizeof(float);

	if (playEnabled) {
		// Apply exponential smoothing to frequency
		const float lambdaFrequency = 0.5;
		playFrequency = clampf(playFrequency, 1.0, 10000.0);
		playFrequencySmooth = powf(playFrequencySmooth, 1.0 - lambdaFrequency) * powf(playFrequency, lambdaFrequency);
		double ratio = (double)audioSpec.freq / WAVE_LEN / playFrequencySmooth;

		src_callback_read(audioSrc, ratio, outLen, out);

		// Modulate Z
		if (!playModeXY && morphZSpeed > 0.f) {
			float deltaZ = morphZSpeed * outLen / audioSpec.freq;
			deltaZ = clampf(deltaZ, 0.f, 1.f);
			morphZ += (BANK_LEN-1) * deltaZ;
			if (morphZ >= (BANK_LEN-1)) {
				morphZ = fmodf(morphZ, (BANK_LEN-1));
				morphZSmooth = morphZ;
			}
		}
	}
	else {
		for (int i = 0; i < outLen; i++) {
			out[i] = 0.0;
		}
	}
}

int audioGetDeviceCount() {
	return SDL_GetNumAudioDevices(0);
}

const char *audioGetDeviceName(int deviceId) {
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
	// TODO Be more tolerant of devices which can't use floats or 1 channel
	audioDevice = SDL_OpenAudioDevice(deviceName, 0, &spec, &audioSpec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
	if (audioDevice <= 0)
		return;
	SDL_PauseAudioDevice(audioDevice, 0);
}

void audioInit() {
	assert(!audioSrc);
	int err;
	audioSrc = src_callback_new(srcCallback, SRC_SINC_FASTEST, 1, &err, NULL);
	assert(audioSrc);
	audioOpen(-1);
}

void audioDestroy() {
	audioClose();
	src_delete(audioSrc);
}

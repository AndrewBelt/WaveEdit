#include "WaveEditor.hpp"

#include <SDL.h>
#include <samplerate.h>


float playVolume = -12.0;
float playFrequency = 220.0;
float playFrequencySmooth = playFrequency;
bool playModeXY = false;
bool playEnabled = false;
float morphX = 0.0;
float morphY = 0.0;
float morphZ = 0.0;
int playIndex = 0;
static float morphZSmooth = morphZ;
static SDL_AudioDeviceID audioDevice = 0;
static SDL_AudioSpec audioSpec;
static SRC_STATE *audioSrc;


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

void audioCallback(void *userdata, Uint8 *stream, int len) {
	float *out = (float *) stream;
	int outLen = len / sizeof(float);

	if (playEnabled) {
		// Apply exponential smoothing to frequency
		const float lambdaFrequency = 0.5;
		playFrequency = clampf(playFrequency, 1.0, 10000.0);
		playFrequencySmooth = powf(playFrequencySmooth, 1.0 - lambdaFrequency) * powf(playFrequency, lambdaFrequency);
		float gain = powf(10.0, playVolume / 20.0);
		double ratio = (double)audioSpec.freq / WAVE_LEN / playFrequencySmooth;

		int outPos = 0;
		while (outPos < outLen) {
			// Generate next samples
			// NOTE: We may not need them, because of the weird way libsamplerate requests audio, so don't advance playIndex yet
			int inLen = 64;
			float in[inLen];
			for (int i = 0; i < inLen; i++) {
				int index = (playIndex + i) % WAVE_LEN;
				if (playModeXY) {
					// TODO
					in[i] = 0.0;
				}
				else {
					const float lambdaMorphZ = 0.002;
					morphZSmooth = crossf(morphZSmooth, clampf(morphZ, 0.0, BANK_LEN - 1), lambdaMorphZ);
					int zi = morphZSmooth;
					float zf = morphZSmooth - zi;
					in[i] = currentBank.waves[zi].postSamples[index];
					if (zf >= 1e-6)
						in[i] = crossf(in[i], currentBank.waves[zi + 1].postSamples[index], zf);
				}
				in[i] = clampf(in[i] * gain, -1.0, 1.0);
			}

			// Pull output samples
			SRC_DATA srcData;
			srcData.data_in = in;
			srcData.input_frames = inLen;
			srcData.data_out = &out[outPos];
			srcData.output_frames = outLen - outPos;
			srcData.src_ratio = ratio;
			srcData.end_of_input = false;
			int err = src_process(audioSrc, &srcData);
			assert(!err);

			playIndex = (playIndex + srcData.input_frames_used) % WAVE_LEN;
			outPos += srcData.output_frames_gen;
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
	// TODO Be more tolerant of devices which can't use floats, 1 channel
	audioDevice = SDL_OpenAudioDevice(deviceName, 0, &spec, &audioSpec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
	if (audioDevice <= 0)
		return;
	SDL_PauseAudioDevice(audioDevice, 0);
}

void audioInit() {
	audioSrc = src_new(SRC_SINC_FASTEST, 1, NULL);
	assert(audioSrc);
	audioOpen(-1);
}

void audioDestroy() {
	audioClose();
	src_delete(audioSrc);
}

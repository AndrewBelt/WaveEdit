#include "WaveEdit.hpp"
#include <string.h>
#include <sndfile.h>


void Bank::clear() {
	// The lazy way
	memset(this, 0, sizeof(Bank));

	for (int i = 0; i < BANK_LEN; i++) {
		waves[i].commitSamples();
	}
}


void Bank::setSamples(const float *in) {
	for (int j = 0; j < BANK_LEN; j++) {
		memcpy(waves[j].samples, &in[j * WAVE_LEN], sizeof(float) * WAVE_LEN);
		waves[j].commitSamples();
	}
}


void Bank::getPostSamples(float *out) {
	for (int j = 0; j < BANK_LEN; j++) {
		memcpy(&out[j * WAVE_LEN], waves[j].postSamples, sizeof(float) * WAVE_LEN);
	}
}


void Bank::duplicateToAll(int waveId) {
	for (int j = 0; j < BANK_LEN; j++) {
		if (j != waveId)
			waves[j] = waves[waveId];
		// No need to commit the wave because we're copying everything
	}
}


void Bank::importSamples(const float *in, int inLen, float gain, float offset, float zoom, ImportMode mode) {
	zoom = clampf(zoom, -8.0, 8.0);
	const int outLen = BANK_LEN * WAVE_LEN;
	// A bunch of weird constants to align the resampler correctly
	float X = inLen;
	float X0 = X / 2.0;
	float W = powf(2.0, -zoom) * inLen;
	float W0 = X0 - offset * (W + X) / 2.0;
	float Wl = W0 - W / 2.0;
	float Wr = W0 + W / 2.0;
	float Xl = clampf(Wl, 0, X);
	float Xr = clampf(Wr, 0, X);
	float Y = outLen;
	float Yl = rescalef(Xl, Wl, Wr, 0, Y);
	float Yr = rescalef(Xr, Wl, Wr, 0, Y);
	Yl = clampf(Yl, 0, Y);
	Yr = clampf(Yr, 0, Y);
	Xl = rescalef(Yl, 0, Y, Wl, Wr);
	Xr = rescalef(Yr, 0, Y, Wl, Wr);
	int Xli = roundf(Xl);
	int Xri = roundf(Xr);
	int Yli = roundf(Yl);
	int Yri = roundf(Yr);
	float out[outLen] = {};
	float ratio = clampf(Y / W, 1/300.0, 300.0);
	resample(in + Xli, Xri - Xli, out + Yli, Yri - Yli, ratio);

	// Import to each wave
	for (int j = 0; j < BANK_LEN; j++) {
		for (int i = 0; i < WAVE_LEN; i++) {
			int index = j * WAVE_LEN + i;
			switch (mode) {
				case CLEAR_IMPORT:
					waves[j].samples[i] = gain * out[index];
					break;
				case OVERWRITE_IMPORT:
					if (Yl <= index && index <= Yr)
						waves[j].samples[i] = gain * out[index];
					break;
				case ADD_IMPORT:
					waves[j].samples[i] += gain * out[index];
					break;
				case MULTIPLY_IMPORT:
					waves[j].samples[i] *= gain * out[index];
					break;
			}

		}
		waves[j].commitSamples();
	}
}


void Bank::save(const char *filename) {
	FILE *f = fopen(filename, "wb");
	if (!f)
		return;
	fwrite(this, sizeof(*this), 1, f);
	fclose(f);
}


void Bank::load(const char *filename) {
	clear();

	FILE *f = fopen(filename, "rb");
	if (!f)
		return;
	fread(this, sizeof(*this), 1, f);
	fclose(f);

	for (int j = 0; j < BANK_LEN; j++) {
		waves[j].commitSamples();
	}
}


void Bank::saveWAV(const char *filename) {
	SF_INFO info;
	info.samplerate = 44100;
	info.channels = 1;
	info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16 | SF_ENDIAN_LITTLE;
	SNDFILE *sf = sf_open(filename, SFM_WRITE, &info);
	if (!sf)
		return;

	for (int j = 0; j < BANK_LEN; j++) {
		sf_write_float(sf, waves[j].postSamples, WAVE_LEN);
	}

	sf_close(sf);
}


void Bank::loadWAV(const char *filename) {
	clear();

	SF_INFO info;
	SNDFILE *sf = sf_open(filename, SFM_READ, &info);
	if (!sf)
		return;

	for (int i = 0; i < BANK_LEN; i++) {
		sf_read_float(sf, waves[i].samples, WAVE_LEN);
		waves[i].commitSamples();
	}

	sf_close(sf);
}


void Bank::saveWaves(const char *dirname) {
	for (int b = 0; b < BANK_LEN; b++) {
		char filename[1024];
		snprintf(filename, sizeof(filename), "%s/%02d.wav", dirname, b);

		waves[b].save(filename);
	}
}

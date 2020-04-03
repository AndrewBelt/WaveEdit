#include "WaveEdit.hpp"
#include <string.h>
#include <sndfile.h>

#if defined(_WIN32)
#define strcasecmp _stricmp
#endif

void Bank::clear() {
	// The lazy way
	memset(this, 0, sizeof(Bank));

	for (int i = 0; i < BANK_LEN; i++) {
		waves[i].commitSamples();
	}
}


void Bank::swap(int i, int j) {
	Wave tmp = waves[i];
	waves[i] = waves[j];
	waves[j] = tmp;
}


void Bank::shuffle() {
	for (int j = BANK_LEN - 1; j >= 3; j--) {
		int i = rand() % j;
		swap(i, j);
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

		waves[b].saveWAV(filename);
	}
}


void Bank::saveWT(const char *filename)
{
	FILE *f = fopen(filename, "wb");
	if (!f)
		return;

	fputs("vawt", f);
	fwriteLE32(WAVE_LEN, f);
	fwriteLE16(BANK_LEN, f);
	fwriteLE16(0, f);

	for (int i = 0; i < BANK_LEN; i++) {
		for (int j = 0; j < WAVE_LEN; j++) {
			union { uint32_t i; float f; } u;
			u.f = waves[i].postSamples[j];
			fwriteLE32(u.i, f);
		}
	}

	fclose(f);
}


void Bank::loadWT(const char *filename)
{
	FILE *f = fopen(filename, "rb");
	if (!f)
		return;

	char magic[4];
	memset(magic, 0, 4);
	fread(magic, 4, 1, f);

	if (memcmp(magic, "vawt", 4) != 0) {
		fclose(f);
		return;
	}

	uint32_t waveLen = 0;
	uint16_t fileBankLen = 0;
	uint16_t flags = 0;

	freadLE32(&waveLen, f);
	freadLE16(&fileBankLen, f);
	freadLE16(&flags, f);

	if (waveLen > 1024) {
		fclose(f);
		return;
	}

	uint32_t bankLen = fileBankLen;
	if (bankLen > BANK_LEN)
		bankLen = BANK_LEN;

	clear();

	std::vector<float> rawSamples(waveLen * bankLen);

	if (flags & 4) {
		for (uint32_t i = 0; i < waveLen * bankLen; ++i) {
			int16_t sample = 0;
			freadLE16((uint16_t *)&sample, f);
			rawSamples[i] = sample / 32768.0f;
		}
	}
	else {
		for (uint32_t i = 0; i < waveLen * bankLen; ++i) {
			union { uint32_t i; float f; } u;
			u.i = 0;
			freadLE32(&u.i, f);
			rawSamples[i] = u.f;
		}
	}

	for (uint32_t i = 0; i < bankLen; i++) {
		const float *src = &rawSamples[waveLen * i];
		float *dst = waves[i].samples;
		if (waveLen == WAVE_LEN)
			memcpy(dst, src, WAVE_LEN * sizeof(float));
		else
			resample(src, waveLen, dst, WAVE_LEN, (double)WAVE_LEN/waveLen);
		waves[i].commitSamples();
	}

	fclose(f);
}


enum BankFileFormat {
	BANK_FORMAT_WAV,
	BANK_FORMAT_WT,
};


static int getFormatByFilename(const char *filename)
{
	size_t len = strlen(filename);
	if (len > 4 && strcasecmp(filename + len - 4, ".wav") == 0)
		return BANK_FORMAT_WAV;
	else if (len > 3 && strcasecmp(filename + len - 3, ".wt") == 0)
		return BANK_FORMAT_WT;
	else
		return -1;
}


void Bank::saveAuto(const char *filename)
{
	switch (getFormatByFilename(filename)) {
		default:
		case BANK_FORMAT_WAV:
			saveWAV(filename);
			break;
		case BANK_FORMAT_WT:
			saveWT(filename);
			break;
	}
}


void Bank::loadAuto(const char *filename)
{
	switch (getFormatByFilename(filename)) {
		default:
		case BANK_FORMAT_WAV:
			loadWAV(filename);
			break;
		case BANK_FORMAT_WT:
			loadWT(filename);
			break;
	}
}

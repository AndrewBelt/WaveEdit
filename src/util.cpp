#include "WaveEdit.hpp"
#include <string.h>
#include <sndfile.h>
#include <stdarg.h>

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#endif

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


float *loadAudio(const char *filename, int *length) {
	SF_INFO info;
	SNDFILE *sf = sf_open(filename, SFM_READ, &info);
	if (!sf)
		return NULL;

	// Get length of audio
	int len = sf_seek(sf, 0, SEEK_END);
	if (len <= 0)
		return NULL;
	sf_seek(sf, 0, SEEK_SET);
	float *samples = new float[len];

	int pos = 0;
	while (pos < len) {
		const int bufferLen = 1<<12;
		float buffer[bufferLen * info.channels];
		int frames = sf_readf_float(sf, buffer, bufferLen);
		for (int i = 0; i < frames; i++) {
			float sample = 0.0;
			for (int c = 0; c < info.channels; c++) {
				sample += buffer[i * info.channels + c];
			}
			samples[pos] = sample / info.channels;
			pos++;
		}
	}

	sf_close(sf);
	if (length)
		*length = len;
	return samples;
}


std::string stringf(const char *format, ...) {
	va_list args;
	va_start(args, format);
	int size = vsnprintf(NULL, 0, format, args);
	va_end(args);
	if (size < 0)
		return "";
	std::string s;
	s.resize(size);
	va_start(args, format);
	vsnprintf(&s[0], size+1, format, args);
	va_end(args);
	return s;
}

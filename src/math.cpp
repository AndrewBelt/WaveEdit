#include "WaveEdit.hpp"
#include "tools/kiss_fftr.h"


void RFFT(const float *in, float *out, int len) {
	kiss_fftr_cfg cfg = kiss_fftr_alloc(len, false, NULL, NULL);
	kiss_fftr(cfg, in, (kiss_fft_cpx*) out);
	kiss_fftr_free(cfg);
	for (int i = 0; i < len; i++) {
		out[i] /= len;
	}
}

void IRFFT(const float *in, float *out, int len) {
	kiss_fftr_cfg cfg = kiss_fftr_alloc(len, true, NULL, NULL);
	kiss_fftri(cfg, (kiss_fft_cpx*) in, out);
	kiss_fftr_free(cfg);
}

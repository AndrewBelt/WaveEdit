#include "WaveEditor.hpp"
#include "pffft/pffft.h"


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

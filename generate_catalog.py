import os
import numpy as np
import scipy.signal
# import matplotlib.pyplot as plt
import scipy.io.wavfile

CATALOG_DIR = "catalog"
SAMPLE_RATE = 44100
NUM_SAMPLES = 256
t = np.linspace(0, 1, NUM_SAMPLES, False)

def saveWAV16(filename, x):
	xi16 = np.int16(np.clip(x, -1, 1) * 32767)
	path = "%s/%s" % (CATALOG_DIR, filename)
	os.makedirs(os.path.dirname(path), exist_ok=True)
	print("Saving", path)
	scipy.io.wavfile.write(path, SAMPLE_RATE, xi16)

def normalize(x):
	min = np.amin(x)
	max = np.amax(x)
	return (x - min) / (max - min) * 2 - 1

# Digital

def Digital():
	i = 0
	saveWAV16("00Digital/%02dSine.wav" % i, np.sin(2*np.pi * t)); i += 1
	saveWAV16("00Digital/%02dSawtooth.wav" % i, scipy.signal.sawtooth(2*np.pi * t, width=0)); i += 1
	saveWAV16("00Digital/%02dRamp.wav" % i, scipy.signal.sawtooth(2*np.pi * t)); i += 1
	saveWAV16("00Digital/%02dTriangle.wav" % i, scipy.signal.sawtooth(2*np.pi * t, width=0.5)); i += 1
	saveWAV16("00Digital/%02dSquare.wav" % i, scipy.signal.square(2*np.pi * t)); i += 1
	saveWAV16("00Digital/%02dRectangle.wav" % i, scipy.signal.square(2*np.pi * t, duty=1/4)); i += 1
	saveWAV16("00Digital/%02dTrigger.wav" % i, (scipy.signal.square(2*np.pi * t, duty=1/128)+1) / 2); i += 1
	saveWAV16("00Digital/%02dRectified Sine.wav" % i, np.abs(np.sin(2*np.pi * t/2))*2 - 1); i += 1
	saveWAV16("00Digital/%02dChirp 4.wav" % i, scipy.signal.chirp(t, 1, 1, 4, 'logarithmic')); i += 1
	saveWAV16("00Digital/%02dChirp 16.wav" % i, scipy.signal.chirp(t, 1, 1, 16, 'logarithmic')); i += 1
	saveWAV16("00Digital/%02dChirp 64.wav" % i, scipy.signal.chirp(t, 1, 1, 64, 'logarithmic')); i += 1
	saveWAV16("00Digital/%02dMortlet 4.wav" % i, scipy.signal.gausspulse(t*2-1, 4/2, 1/2, -6, -60)); i += 1
	saveWAV16("00Digital/%02dMortlet 16.wav" % i, scipy.signal.gausspulse(t*2-1, 16/2, 1/2, -6, -60)); i += 1
	saveWAV16("00Digital/%02dMortlet 64.wav" % i, scipy.signal.gausspulse(t*2-1, 64/2, 1/2, -6, -60)); i += 1


# Analog



# FM

def FM():
	i = 0
	OVERSAMPLE = 64
	to = np.linspace(0, 1, NUM_SAMPLES*OVERSAMPLE, False)

	def fm(t, freq):
		phase = np.cumsum(freq) / to.shape[0]
		return np.sin(2*np.pi * phase)

	saveWAV16("02FM/%02dSine FM 1.wav" % i, fm(to, 1 + np.sin(2*np.pi * 1*to))[::OVERSAMPLE]); i += 1
	saveWAV16("02FM/%02dSine FM 2.wav" % i, fm(to, 1 + np.sin(2*np.pi * 2*to))[::OVERSAMPLE]); i += 1
	saveWAV16("02FM/%02dSine FM 3.wav" % i, fm(to, 1 + np.sin(2*np.pi * 3*to))[::OVERSAMPLE]); i += 1
	saveWAV16("02FM/%02dSine FM 5.wav" % i, fm(to, 1 + np.sin(2*np.pi * 5*to))[::OVERSAMPLE]); i += 1
	saveWAV16("02FM/%02dSine FM 7.wav" % i, fm(to, 1 + np.sin(2*np.pi * 7*to))[::OVERSAMPLE]); i += 1
	saveWAV16("02FM/%02dSine FM 11.wav" % i, fm(to, 1 + np.sin(2*np.pi * 11*to))[::OVERSAMPLE]); i += 1
	saveWAV16("02FM/%02dSine TZFM 1.wav" % i, fm(to, 1 + 3 * np.sin(2*np.pi * 1*to))[::OVERSAMPLE]); i += 1
	saveWAV16("02FM/%02dSine TZFM 2.wav" % i, fm(to, 1 + 3 * np.sin(2*np.pi * 2*to))[::OVERSAMPLE]); i += 1
	saveWAV16("02FM/%02dSine TZFM 3.wav" % i, fm(to, 1 + 3 * np.sin(2*np.pi * 3*to))[::OVERSAMPLE]); i += 1
	saveWAV16("02FM/%02dSine TZFM 5.wav" % i, fm(to, 1 + 3 * np.sin(2*np.pi * 5*to))[::OVERSAMPLE]); i += 1
	saveWAV16("02FM/%02dSine TZFM 7.wav" % i, fm(to, 1 + 3 * np.sin(2*np.pi * 7*to))[::OVERSAMPLE]); i += 1
	saveWAV16("02FM/%02dSine TZFM 11.wav" % i, fm(to, 1 + 3 * np.sin(2*np.pi * 11*to))[::OVERSAMPLE]); i += 1
	saveWAV16("02FM/%02dSawtooth FM 1.wav" % i, fm(to, 1 + scipy.signal.sawtooth(2*np.pi * 1*to))[::OVERSAMPLE]); i += 1
	saveWAV16("02FM/%02dSawtooth FM 2.wav" % i, fm(to, 1 + scipy.signal.sawtooth(2*np.pi * 2*to))[::OVERSAMPLE]); i += 1
	saveWAV16("02FM/%02dSawtooth FM 3.wav" % i, fm(to, 1 + scipy.signal.sawtooth(2*np.pi * 3*to))[::OVERSAMPLE]); i += 1
	saveWAV16("02FM/%02dSawtooth FM 5.wav" % i, fm(to, 1 + scipy.signal.sawtooth(2*np.pi * 5*to))[::OVERSAMPLE]); i += 1
	saveWAV16("02FM/%02dSawtooth FM 7.wav" % i, fm(to, 1 + scipy.signal.sawtooth(2*np.pi * 7*to))[::OVERSAMPLE]); i += 1
	saveWAV16("02FM/%02dSawtooth FM 11.wav" % i, fm(to, 1 + scipy.signal.sawtooth(2*np.pi * 11*to))[::OVERSAMPLE]); i += 1

# Glitch

def Glitch():
	i = 0
	def swap_endianness(x):
		return (x * 32767).astype(np.int16).byteswap().astype(np.float32) / 32767
	saveWAV16("03Glitch/%02dBig-Endian Sine.wav" % i, swap_endianness(np.sin(2*np.pi * t))); i += 1
	sine = np.sin(2*np.pi * t)
	sine_unsigned = np.where(sine >= 0, 0, 1) + sine
	sine_unsigned = sine_unsigned * 2 - 1
	saveWAV16("03Glitch/%02dUnsigned Sine.wav" % i, sine_unsigned); i += 1
	saveWAV16("03Glitch/%02dIncomplete Sine.wav" % i, np.sin(2*np.pi * t * 3/4)); i += 1

# Noise

def Noise():
	np.random.seed(42)

	i = 0
	white = np.random.normal(size=t.shape[0])
	saveWAV16("04Noise/%02dWhite.wav" % i, normalize(white)); i += 1
	pink = np.diff(np.concatenate(([0.0], white)))
	saveWAV16("04Noise/%02dPink.wav" % i, normalize(pink)); i += 1
	brown = np.cumsum(white)
	saveWAV16("04Noise/%02dBrown.wav" % i, normalize(brown)); i += 1



shepard = np.zeros(NUM_SAMPLES * 64)
for i in range(128):
	
saveWAV16("Shepard.wav", )
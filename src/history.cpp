#include "WaveEdit.hpp"
#include <SDL.h>


Bank currentBank;

static std::vector<Bank> history;
static int currentIndex = -1;
static double previousTime = -INFINITY;
static const double delayTime = 0.2;


void historyPush() {
	double time = SDL_GetTicks() / 1000.0;
	if (time - previousTime >= delayTime) {
		currentIndex++;
	}

	// Delete redo history
	history.resize(currentIndex + 1);

	history[currentIndex] = currentBank;
	previousTime = time;
}

void historyUndo() {
	if (currentIndex >= 1) {
		currentIndex--;
		currentBank = history[currentIndex];
		previousTime = -INFINITY;
	}
}

void historyRedo() {
	if ((int) history.size() > currentIndex + 1) {
		currentIndex++;
		currentBank = history[currentIndex];
		previousTime = -INFINITY;
	}
}

void historyClear() {
	history.clear();
	currentIndex = -1;
	previousTime = -INFINITY;
}

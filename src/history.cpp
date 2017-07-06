#include "WaveEdit.hpp"


Bank currentBank;

static int currentBankIndex = 0;
static std::vector<Bank> history;


void historyPush() {
	// TODO
	// Remove redo "history"

	history.push_back(currentBank);
}

void historyPop() {
	if (history.size() > 0) {
		currentBank = history[history.size() - 1];
		history.pop_back();
	}
}

void historyRedo() {
	// TODO
}


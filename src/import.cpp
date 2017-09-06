#include "WaveEdit.hpp"

#include "imgui.h"
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui_internal.h"

#include "osdialog/osdialog.h"


static bool showImportPopup = false;

void importPage() {
	static float gain;
	static float offset;
	static float zoom;
	static float left;
	static float right;
	static ImportMode mode;
	static float *audio = NULL;
	static int audioLen;
	static Bank menuNewBank;

	const int audioLenMin = 32;
	const int audioLenMax = 44100 * 20;

	static char importError[1024] = "";

	if (showImportPopup) {
		showImportPopup = false;
		char *path = osdialog_file(OSDIALOG_OPEN, ".", NULL, NULL);
		if (path) {
			offset = 0.0;
			zoom = 0.0;
			gain = 0.0;
			left = 0.0;
			right = 1.0;
			mode = CLEAR_IMPORT;
			audio = loadAudio(path, &audioLen);
			if (!audio) {
				ImGui::OpenPopup("Import Error");
				snprintf(importError, sizeof(importError), "Could not load audio file");
				printf("%s\n", importError);
			}
			else if (audioLen < audioLenMin) {
				ImGui::OpenPopup("Import Error");
				snprintf(importError, sizeof(importError), "Audio file contains %d samples, needs at least %d", audioLen, audioLenMin);
			}
			else if (audioLen > audioLenMax) {
				ImGui::OpenPopup("Import Error");
				snprintf(importError, sizeof(importError), "Audio file contains %d samples, needs at most %d", audioLen, audioLenMax);
			}
			free(path);
		}
	}

	ImGui::BeginChild("Import", ImVec2(0, 0), true);
	{
		ImGui::PushItemWidth(-1.0);

		ImGui::Button("Clear");
		ImGui::SameLine();
		ImGui::Button("Browse...");
		ImGui::SameLine();
		ImGui::Text("%s", "asdf");

		// Import samples
		// menuNewBank = currentBank;
		// menuNewBank.importSamples(audio, audioLen, powf(10.0, gain / 20.0), offset, zoom, left, right, mode);
		// Wave view
		// float samples[BANK_LEN * WAVE_LEN];
		// menuNewBank.getPostSamples(samples);
		// renderWave("##importSamples", 100, NULL, 0, samples, BANK_LEN * WAVE_LEN, NO_TOOL);

		// Parameters
		ImGui::SliderFloat("##left", &left, 0.0, 1.0, "Left Trim: %.2f");

		ImGui::SliderFloat("##right", &right, 0.0, 1.0, "Right Trim: %.2f");

		if (ImGui::Button("Reset Offset")) offset = 0.0;
		ImGui::SameLine();
		ImGui::SliderFloat("##offset", &offset, -1.0, 1.0, "Offset: %.4f");

		if (ImGui::Button("Reset Zoom")) zoom = 0.0;
		ImGui::SameLine();
		ImGui::SliderFloat("##zoom", &zoom, -7.0, 7.0, "Zoom: %.2f");

		if (ImGui::Button("Reset Gain")) gain = 0.0;
		ImGui::SameLine();
		ImGui::SliderFloat("##gain", &gain, -40.0, 40.0, "Gain: %.2fdB");

		// Modes
		if (ImGui::RadioButton("Clear", mode == CLEAR_IMPORT)) mode = CLEAR_IMPORT;
		ImGui::SameLine();
		if (ImGui::RadioButton("Overwrite", mode == OVERWRITE_IMPORT)) mode = OVERWRITE_IMPORT;
		ImGui::SameLine();
		if (ImGui::RadioButton("Mix", mode == ADD_IMPORT)) mode = ADD_IMPORT;
		ImGui::SameLine();
		if (ImGui::RadioButton("Ring Modulate", mode == MULTIPLY_IMPORT)) mode = MULTIPLY_IMPORT;

		// Buttons
		bool cleanup = false;

		if (ImGui::Button("Import")) {
			// currentBank = menuNewBank;
			historyPush();
			// ImGui::CloseCurrentPopup();
		}
	}
	ImGui::EndChild();
}

#include "WaveEdit.hpp"

#include "imgui.h"
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui_internal.h"

#include <libgen.h>
#include "osdialog/osdialog.h"


/** A widget like renderWave() except without editing, and bank lines are overlaid */
void renderBankWave(const char *name, float height, const float *lines, int linesLen, float bankStart, float bankEnd, int bankLen) {
	ImGuiContext &g = *GImGui;
	ImGuiWindow *window = ImGui::GetCurrentWindow();
	const ImGuiStyle &style = g.Style;
	const ImGuiID id = window->GetID(name);

	// Compute positions
	ImVec2 size = ImVec2(ImGui::CalcItemWidth(), height);
	ImRect box = ImRect(window->DC.CursorPos, window->DC.CursorPos + size);
	ImRect inner = ImRect(box.Min + style.FramePadding, box.Max - style.FramePadding);
	ImGui::ItemSize(box, style.FramePadding.y);
	if (!ImGui::ItemAdd(box, NULL))
		return;

	// Draw frame
	ImGui::RenderFrame(box.Min, box.Max, ImGui::GetColorU32(ImGuiCol_FrameBg), true, style.FrameRounding);

	ImGui::PushClipRect(box.Min, box.Max, true);
	// Draw bank grid
	float lastTextX = -INFINITY;
	for (int i = 0; i <= bankLen; i++) {
		float gridX = rescalef(i, 0, bankLen, bankStart, bankEnd);
		gridX = rescalef(gridX, 0, linesLen, inner.Min.x, inner.Max.x);
		// Grid line
		float thickness;
		if (i % 64 == 0)
			thickness = 4.0;
		else if (i % 8 == 0)
			thickness = 3.0;
		else
			thickness = 2.0;
		window->DrawList->AddLine(ImVec2(gridX, inner.Min.y), ImVec2(gridX, inner.Max.y), ImGui::GetColorU32(ImGuiCol_WindowBg), thickness);
		// Text
		if (fabsf(lastTextX - gridX) >= 18.0 && i < bankLen) {
			lastTextX = gridX;
			char label[64];
			snprintf(label, sizeof(label), "%d", i);
			ImVec2 labelPos = ImVec2(gridX, inner.Max.y) + ImVec2(5, -13);
			window->DrawList->AddText(labelPos, ImGui::GetColorU32(ImGuiCol_PlotLines), label);
		}
	}
	// Draw lines
	if (lines) {
		ImVec2 lastPos;
		for (int i = 0; i < linesLen; i++) {
			ImVec2 pos = ImVec2(rescalef(i, 0, linesLen, inner.Min.x, inner.Max.x), rescalef(lines[i], 1.0, -1.0, inner.Min.y, inner.Max.y));
			if (i > 0)
				window->DrawList->AddLine(lastPos, pos, ImGui::GetColorU32(ImGuiCol_PlotLines));
			lastPos = pos;
		}
	}
	ImGui::PopClipRect();
}



enum ImportMode {
	CLEAR_IMPORT,
	OVERWRITE_IMPORT,
	ADD_IMPORT,
	MULTIPLY_IMPORT,
};

static float gain;
static float offset;
static float zoom;
static float leftTrim;
static float rightTrim;
static ImportMode mode;
static float *audio = NULL;
static int audioLen;
static float *audioPreview = NULL;
static char status[1024] = "";
static Bank importBank;

const int audioLenMin = 32;
const int audioLenMax = BANK_LEN * WAVE_LEN * 100;


static void zoomFit() {
	zoom = clampf((float)audioLen / (BANK_LEN * WAVE_LEN), 0.01, 100.0);
}

static void clearImport() {
	gain = 0.0;
	offset = 0.0;
	zoom = 1.0;
	leftTrim = 0.0;
	rightTrim = BANK_LEN;
	mode = CLEAR_IMPORT;
	if (audio)
		delete[] audio;
	audio = NULL;
	audioLen = 0;
	if (audioPreview)
		delete[] audioPreview;
	audioPreview = NULL;

	status[0] = '\0';
	importBank.clear();
}

static void loadImport(const char *path) {
	clearImport();
	audio = loadAudio(path, &audioLen);
	if (!audio) {
		snprintf(status, sizeof(status), "Cannot load audio file. Only WAV files are supported for now.");
		return;
	}

	if (audioLen > audioLenMax) {
		snprintf(status, sizeof(status), "Audio file contains %d samples, may have up to %d", audioLen, audioLenMax);
		delete[] audio;
		audio = NULL;
		return;
	}

	if (audioLen < audioLenMin) {
		snprintf(status, sizeof(status), "Audio file contains %d samples, must have at least %d", audioLen, audioLenMin);
		delete[] audio;
		audio = NULL;
		return;
	}

	zoomFit();

	// Generate status line
	char *pathCpy = strdup(path);
	char *filename = basename(pathCpy);
	ellipsize(filename, 80);
	snprintf(status, sizeof(status), "%s: %d samples", filename, audioLen);
	free(pathCpy);

	// Render audio preview by resampling to constant size
	audioPreview = new float[BANK_LEN * WAVE_LEN]();
	double previewRatio = BANK_LEN * WAVE_LEN / (double)audioLen;
	resample(audio, audioLen, audioPreview, BANK_LEN * WAVE_LEN, previewRatio);
}

static float getAudioAmplitude() {
	float max = 0.0;
	for (int i = 0; i < audioLen; i++) {
		float amplitude = fabsf(audio[i]);
		if (amplitude > max)
			max = amplitude;
	}
	return max;
}

static void computeImport(float *samples) {
	float importSamples[BANK_LEN * WAVE_LEN] = {};

	// A bunch of weird constants to align the resampler correctly
	// Basically x's and w's are indices for the audio array, y's are for the bank array
	float wl = offset * audioLen;
	float wr = wl + BANK_LEN * WAVE_LEN * zoom;
	float xl = clampf(wl, 0, audioLen);
	float xr = clampf(wr, 0, audioLen);
	float yl = rescalef(xl, wl, wr, 0, BANK_LEN * WAVE_LEN);
	float yr = rescalef(xr, wl, wr, 0, BANK_LEN * WAVE_LEN);
	yl = clampf(yl, 0, BANK_LEN * WAVE_LEN);
	yr = clampf(yr, 0, BANK_LEN * WAVE_LEN);
	yl = clampf(yl, leftTrim * WAVE_LEN, rightTrim * WAVE_LEN);
	yr = clampf(yr, leftTrim * WAVE_LEN, rightTrim * WAVE_LEN);
	xl = rescalef(yl, 0, BANK_LEN * WAVE_LEN, wl, wr);
	xr = rescalef(yr, 0, BANK_LEN * WAVE_LEN, wl, wr);
	int xli = roundf(xl);
	int xri = roundf(xr);
	int yli = roundf(yl);
	int yri = roundf(yr);
	float ratio = clampf(1.0 / zoom, 1/300.0, 300.0);

	resample(audio + xli, xri - xli, importSamples + yli, yri - yli, ratio);

	// Apply mode mixing and gain
	switch (mode) {
		case CLEAR_IMPORT:
			break;
		case OVERWRITE_IMPORT:
		case ADD_IMPORT:
		case MULTIPLY_IMPORT:
			currentBank.getPostSamples(samples);
			break;
	}

	float amp = powf(10.0, gain / 20.0);
	for (int i = 0; i < BANK_LEN * WAVE_LEN; i++) {
		importSamples[i] *= amp;

		switch (mode) {
			case CLEAR_IMPORT:
				samples[i] = importSamples[i];
				break;
			case OVERWRITE_IMPORT:
				if (yli <= i && i <= yri)
					samples[i] = importSamples[i];
				break;
			case ADD_IMPORT:
				samples[i] += importSamples[i];
				break;
			case MULTIPLY_IMPORT:
				samples[i] *= importSamples[i];
				break;
		}
	}
}


void importPage() {
	ImGui::BeginChild("Import", ImVec2(0, 0), true);
	{
		ImGui::PushItemWidth(-1.0);

		if (ImGui::Button("Clear")) {
			clearImport();
		}
		ImGui::SameLine();
		if (ImGui::Button("Browse...")) {
			char *path = osdialog_file(OSDIALOG_OPEN, NULL, NULL, NULL);
			if (path) {
				loadImport(path);
				free(path);
			}
		}
		ImGui::SameLine();
		ImGui::Text("%s", status);

		if (audio) {
			playingBank = &importBank;
			float amp = powf(10.0, gain / 20.0);

			// Audio preview
			float audioPreviewGain[BANK_LEN * WAVE_LEN];
			for (int i = 0; i < BANK_LEN * WAVE_LEN; i++) {
				audioPreviewGain[i] = amp * audioPreview[i];
			}
			float previewStart = offset * BANK_LEN * WAVE_LEN;
			float previewRatio = BANK_LEN * WAVE_LEN / (float)audioLen;
			float previewEnd = previewStart + BANK_LEN * WAVE_LEN * previewRatio * zoom;
			renderBankWave("audio preview", 200.0, audioPreviewGain,
				BANK_LEN * WAVE_LEN,
				previewStart,
				previewEnd,
				BANK_LEN);

			// Bank preview
			// Initialize from previous bank
			float bankSamples[BANK_LEN * WAVE_LEN];
			computeImport(bankSamples);
			renderBankWave("bank preview", 200.0, bankSamples,
				BANK_LEN * WAVE_LEN,
				0,
				BANK_LEN * WAVE_LEN,
				BANK_LEN);

			// Gain
			if (ImGui::Button("Reset Gain")) gain = 0.0;
			ImGui::SameLine();
			if (ImGui::Button("Normalize")) {
				gain = clampf(-20.0 * log10f(getAudioAmplitude()), -40.0, 40.0);
			}
			ImGui::SameLine();
			ImGui::SliderFloat("##gain", &gain, -40.0, 40.0, "Gain: %.2fdB");

			// Offset
			ImGui::SliderFloat("##offset", &offset, 0.0, 1.0, "Offset: %.4f");

			// Zoom
			if (ImGui::Button("Zoom 1:1")) zoom = 1.0;
			ImGui::SameLine();
			if (ImGui::Button("Zoom Fit")) {
				zoomFit();
			}
			static bool snapZoom = false;
			ImGui::SameLine();
			ImGui::Checkbox("Snap to Power of 2", &snapZoom);
			ImGui::SameLine();
			ImGui::SliderFloat("##zoom", &zoom, 0.01, 100.0, "Zoom: %.4f", 0.0);
			if (snapZoom) {
				zoom = powf(2.0, roundf(log2f(zoom)));
			}

			// Trim
			if (ImGui::Button("Reset Trim")) {
				leftTrim = 0;
				rightTrim = BANK_LEN;
			}
			ImGui::SameLine();
			static bool snapTrim = true;
			ImGui::Checkbox("Snap Trim", &snapTrim);
			if (snapTrim) {
				leftTrim = roundf(leftTrim);
				rightTrim = roundf(rightTrim);
			}
			ImGui::SameLine();
			ImGui::PushItemWidth(-1.0);
			float width = ImGui::CalcItemWidth() / 2.0 - ImGui::GetStyle().FramePadding.y;
			ImGui::PushItemWidth(width);
			ImGui::SliderFloat("##leftTrim", &leftTrim, 0.0, BANK_LEN, snapTrim ? "Left Trim: %.0f" : "Left Trim: %.2f");
			ImGui::SameLine();
			ImGui::SliderFloat("##rightTrim", &rightTrim, 0.0, BANK_LEN, snapTrim ? "Right Trim: %.0f" : "Right Trim: %.2f");
			ImGui::PopItemWidth();
			ImGui::PopItemWidth();

			// Modes
			if (ImGui::RadioButton("Clear###import", mode == CLEAR_IMPORT)) mode = CLEAR_IMPORT;
			ImGui::SameLine();
			if (ImGui::RadioButton("Overwrite", mode == OVERWRITE_IMPORT)) mode = OVERWRITE_IMPORT;
			ImGui::SameLine();
			if (ImGui::RadioButton("Mix", mode == ADD_IMPORT)) mode = ADD_IMPORT;
			ImGui::SameLine();
			if (ImGui::RadioButton("Ring Modulate", mode == MULTIPLY_IMPORT)) mode = MULTIPLY_IMPORT;

			// Apply
			importBank.setSamples(bankSamples);
			if (ImGui::Button("Apply")) {
				currentBank = importBank;
				clearImport();
			}
		}
	}
	ImGui::EndChild();
}

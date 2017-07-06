#include "WaveEdit.hpp"

#include <SDL.h>
#include <SDL_opengl.h>

#include "imgui.h"
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui_internal.h"

extern "C" {
#include "noc/noc_file_dialog.h"
}

#include "lodepng/lodepng.h"

#include "tablabels.hpp"


static bool showTestWindow = false;
static bool showImportPopup = false;
static ImTextureID logoTexture;
static int selectedWave = 0;
static char lastFilename[1024] = "\0";

static enum {
	WAVEFORM_EDITOR = 0,
	EFFECT_EDITOR,
	GRID_VIEW,
	_3D_VIEW,
} currentPage = WAVEFORM_EDITOR;


static ImTextureID loadImage(const char *filename) {
	GLuint textureId;
	glGenTextures(1, &textureId);
	glBindTexture(GL_TEXTURE_2D, textureId);
	unsigned char *pixels;
	unsigned int width, height;
	unsigned err = lodepng_decode_file(&pixels, &width, &height, filename, LCT_RGBA, 8);
	assert(!err);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	glBindTexture(GL_TEXTURE_2D, 0);
	return (void*)(intptr_t) textureId;
}


static void getImageSize(ImTextureID id, int *width, int *height) {
	GLuint textureId = (GLuint)(intptr_t) id;
	glBindTexture(GL_TEXTURE_2D, textureId);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, width);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, height);
	glBindTexture(GL_TEXTURE_2D, 0);
}


void selectWave(int waveId) {
	selectedWave = waveId;
	morphX = (float)(selectedWave % BANK_GRID_WIDTH);
	morphY = (float)(selectedWave / BANK_GRID_WIDTH);
	morphZ = (float)selectedWave;
}


enum Tool {
	NO_TOOL,
	PENCIL_TOOL,
	BRUSH_TOOL,
	GRAB_TOOL,
	LINE_TOOL,
	ERASER_TOOL,
	SMOOTH_TOOL,
};


void waveLine(float *points, int pointsLen, float startIndex, float endIndex, float startValue, float endValue) {
	// Switch indices if out of order
	if (startIndex > endIndex) {
		float tmpIndex = startIndex;
		startIndex = endIndex;
		endIndex = tmpIndex;
		float tmpValue = startValue;
		startValue = endValue;
		endValue = tmpValue;
	}

	int startI = maxi(0, roundf(startIndex));
	int endI = mini(pointsLen - 1, roundf(endIndex));
	for (int i = startI; i <= endI; i++) {
		float frac = (startIndex < endIndex) ? rescalef(i, startIndex, endIndex, 0.0, 1.0) : 0.0;
		points[i] = crossf(startValue, endValue, frac);
	}
}


void waveSmooth(float *points, int pointsLen, float index) {
	// TODO
	for (int i = 0; i <= pointsLen - 1; i++) {
		const float a = 0.05;
		float w = expf(-a * powf(i - index, 2.0));
		points[i] = clampf(points[i] + 0.01 * w, -1.0, 1.0);
	}
}


void waveBrush(float *points, int pointsLen, float startIndex, float endIndex, float startValue, float endValue) {
	const float sigma = 10.0;
	for (int i = 0; i < pointsLen; i++) {
		float x = i - startIndex;
		// float a;
		float a = expf(-x * x / (2.0 * sigma));
		points[i] = crossf(points[i], startValue, a);
	}
}


bool editorBehavior(ImGuiID id, const ImRect& box, const ImRect& inner, float *points, int pointsLen, float minIndex, float maxIndex, float minValue, float maxValue, enum Tool tool) {
	ImGuiContext &g = *GImGui;
	ImGuiWindow *window = ImGui::GetCurrentWindow();

	bool hovered = ImGui::IsHovered(box, id);
	if (hovered) {
		ImGui::SetHoveredID(id);
		if (g.IO.MouseClicked[0]) {
			ImGui::SetActiveID(id, window);
			ImGui::FocusWindow(window);
			g.ActiveIdClickOffset = g.IO.MousePos - box.Min;
		}
	}

	// Unhover
	if (g.ActiveId == id) {
		if (!g.IO.MouseDown[0]) {
			ImGui::ClearActiveID();
		}
	}

	// Tool behavior
	if (g.ActiveId == id) {
		if (g.IO.MouseDown[0]) {
			ImVec2 pos = g.IO.MousePos;
			ImVec2 lastPos = g.IO.MousePos - g.IO.MouseDelta;
			ImVec2 originalPos = g.ActiveIdClickOffset + box.Min;
			float originalIndex = rescalef(originalPos.x, inner.Min.x, inner.Max.x, minIndex, maxIndex);
			float originalValue = rescalef(originalPos.y, inner.Min.y, inner.Max.y, minValue, maxValue);
			float lastIndex = rescalef(lastPos.x, inner.Min.x, inner.Max.x, minIndex, maxIndex);
			float lastValue = rescalef(lastPos.y, inner.Min.y, inner.Max.y, minValue, maxValue);
			float index = rescalef(pos.x, inner.Min.x, inner.Max.x, minIndex, maxIndex);
			float value = rescalef(pos.y, inner.Min.y, inner.Max.y, minValue, maxValue);

			if (tool == NO_TOOL) {}
			// Pencil tool
			else if (tool == PENCIL_TOOL) {
				waveLine(points, pointsLen, lastIndex, index, lastValue, value);
			}
			// Grab tool
			else if (tool == GRAB_TOOL) {
				waveLine(points, pointsLen, originalIndex, originalIndex, value, value);
			}
			// Brush tool
			else if (tool == BRUSH_TOOL) {
				waveBrush(points, pointsLen, lastIndex, index, lastValue, value);
			}
			// Line tool
			else if (tool == LINE_TOOL) {
				// TODO Restore points from when it was originally clicked, using undo history
				waveLine(points, pointsLen, originalIndex, index, originalValue, value);
			}
			// Eraser tool
			else if (tool == ERASER_TOOL) {
				waveLine(points, pointsLen, lastIndex, index, 0.0, 0.0);
			}
			// Smooth tool
			else if (tool == SMOOTH_TOOL) {
				waveSmooth(points, pointsLen, lastIndex);
			}

			for (int i = 0; i < pointsLen; i++) {
				points[i] = clampf(points[i], fminf(minValue, maxValue), fmaxf(minValue, maxValue));
			}

			return true;
		}
	}

	return false;
}


bool renderWave(const char *name, float height, float *points, int pointsLen, const float *lines, int linesLen, enum Tool tool = NO_TOOL) {
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
		return false;

	// Draw frame
	ImGui::RenderFrame(box.Min, box.Max, ImGui::GetColorU32(ImGuiCol_FrameBg), true, style.FrameRounding);

	// // Tooltip
	// if (ImGui::IsHovered(box, 0)) {
	// 	ImVec2 mousePos = ImGui::GetMousePos();
	// 	float x = rescalef(mousePos.x, inner.Min.x, inner.Max.x, 0, pointsLen-1);
	// 	int xi = (int)clampf(x, 0, pointsLen-2);
	// 	float xf = x - xi;
	// 	float y = crossf(values[xi], values[xi+1], xf);
	// 	ImGui::SetTooltip("%f, %f\n", x, y);
	// }

	// const bool hovered = ImGui::IsHovered(box, id);
	// if (hovered) {
	// 	ImGui::SetHoveredID(id);
	// 	ImGui::SetActiveID(id, window);
	// }

	bool edited = editorBehavior(id, box, inner, points, pointsLen, 0.0, pointsLen - 1, 1.0, -1.0, tool);

	ImGui::PushClipRect(box.Min, box.Max, true);
	ImVec2 lastPos;
	// Draw lines
	if (lines) {
		for (int i = 0; i < linesLen; i++) {
			ImVec2 pos = ImVec2(rescalef(i, 0, linesLen - 1, inner.Min.x, inner.Max.x), rescalef(lines[i], 1.0, -1.0, inner.Min.y, inner.Max.y));
			if (i > 0)
				window->DrawList->AddLine(lastPos, pos, ImGui::GetColorU32(ImGuiCol_PlotLines));
			lastPos = pos;
		}
	}
	// Draw points
	if (points) {
		for (int i = 0; i < pointsLen; i++) {
			ImVec2 pos = ImVec2(rescalef(i, 0, pointsLen - 1, inner.Min.x, inner.Max.x), rescalef(points[i], 1.0, -1.0, inner.Min.y, inner.Max.y));
			window->DrawList->AddCircleFilled(pos + ImVec2(0.5, 0.5), 2.0, ImGui::GetColorU32(ImGuiCol_PlotLines), 12);
		}
	}
	ImGui::PopClipRect();

	return edited;
}


bool renderHistogram(const char *name, float height, float *points, int pointsLen, const float *lines, int linesLen, enum Tool tool) {
	ImGuiContext &g = *GImGui;
	ImGuiWindow *window = ImGui::GetCurrentWindow();
	const ImGuiStyle &style = g.Style;
	const ImGuiID id = window->GetID(name);

	// Compute positions
	ImVec2 pos = window->DC.CursorPos;
	ImVec2 size = ImVec2(ImGui::CalcItemWidth(), height);
	ImRect box = ImRect(pos, pos + size);
	ImRect inner = ImRect(box.Min + style.FramePadding, box.Max - style.FramePadding);
	ImGui::ItemSize(box, style.FramePadding.y);
	if (!ImGui::ItemAdd(box, NULL))
		return false;

	bool edited = editorBehavior(id, box, inner, points, pointsLen, -0.5, pointsLen - 0.5, 1.0, 0.0, tool);

	// Draw frame
	ImGui::RenderFrame(box.Min, box.Max, ImGui::GetColorU32(ImGuiCol_FrameBg), true, style.FrameRounding);

	// Draw bars
	ImGui::PushClipRect(box.Min, box.Max, true);
	const float rounding = 0.0;
	for (int i = 0; i < pointsLen; i++) {
		float value = points[i];
		ImVec2 pos0 = ImVec2(rescalef(i, 0, pointsLen, inner.Min.x, inner.Max.x), rescalef(value, 0.0, 1.0, inner.Max.y, inner.Min.y));
		ImVec2 pos1 = ImVec2(rescalef(i + 1, 0, pointsLen, inner.Min.x, inner.Max.x) - 1, inner.Max.y);
		window->DrawList->AddRectFilled(pos0, pos1, ImGui::GetColorU32(ImVec4(1.0, 0.8, 0.2, 1.0)), rounding);
	}
	for (int i = 0; i < linesLen; i++) {
		float value = lines[i];
		ImVec2 pos0 = ImVec2(rescalef(i, 0, linesLen, inner.Min.x, inner.Max.x), rescalef(value, 0.0, 1.0, inner.Max.y, inner.Min.y));
		ImVec2 pos1 = ImVec2(rescalef(i + 1, 0, linesLen, inner.Min.x, inner.Max.x) - 1, inner.Max.y);
		window->DrawList->AddRectFilled(pos0, pos1, ImGui::GetColorU32(ImVec4(0.9, 0.7, 0.1, 0.75)), rounding);
	}
	ImGui::PopClipRect();

	return edited;
}


void renderWave3D(float height, const float *const *waves, int bankLen, int waveLen) {
	ImGuiContext &g = *GImGui;
	ImGuiWindow *window = ImGui::GetCurrentWindow();
	const ImGuiStyle &style = g.Style;

	ImVec2 size = ImVec2(ImGui::CalcItemWidth(), height);
	ImRect box = ImRect(window->DC.CursorPos, window->DC.CursorPos + size);
	ImRect inner = ImRect(box.Min + style.FramePadding, box.Max - style.FramePadding);

	ImGui::PushClipRect(box.Min, box.Max, true);

	const float waveHeight = 10.0;
	ImVec2 waveOffset = ImVec2(5, -5);

	for (int b = 0; b < bankLen; b++) {
		ImVec2 points[waveLen];
		for (int i = 0; i < waveLen; i++) {
			float value = waves[b][i];
			points[i] = ImVec2(rescalef(i, 0, waveLen - 1, inner.Min.x, inner.Max.x), rescalef(value, -1.0, 1.0, inner.Max.y - 100 + waveHeight, inner.Max.y - 100 - waveHeight) + 0.5*i) + waveOffset*b;
		}
		window->DrawList->AddPolyline(points, waveLen, ImGui::GetColorU32(ImVec4(1.0, 0.8, 0.2, 1.0)), false, 0.1, true);
	}

	ImGui::PopClipRect();
}


void renderMenuBar() {
	// HACK
	// Display a window on top of the menu with the logo, since I'm too lazy to make my own custom MenuImageItem widget
	{
		int width, height;
		getImageSize(logoTexture, &width, &height);
		ImVec2 padding = ImVec2(8, 4);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0, 0));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
		ImGui::SetNextWindowPos(ImVec2(0, 0));
		ImGui::SetNextWindowSize(ImVec2(width + 2 * padding.x, height + 2 * padding.y));
		if (ImGui::Begin("Logo", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoInputs)) {
			ImGui::Image(logoTexture, ImVec2(width, height));
			ImGui::End();
		}
		ImGui::PopStyleVar();
		ImGui::PopStyleVar();
	}

	// Draw main menu
	if (ImGui::BeginMenuBar()) {
		// This will be hidden by the window with the logo
		if (ImGui::BeginMenu("                        " TOSTRING(VERSION), false)) {
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("File")) {
			if (ImGui::MenuItem("New Bank")) {
				historyPush();
				currentBank.clear();
			}
			if (ImGui::MenuItem("Open Bank...")) {
				const char *filename = noc_file_dialog_open(NOC_FILE_DIALOG_OPEN, "WAV\0*.wav\0", NULL, NULL);
				if (filename) {
					historyPush();
					currentBank.load(filename);
					snprintf(lastFilename, sizeof(lastFilename), "%s", filename);
				}
			}
			if (ImGui::MenuItem("Save Bank", NULL, false, lastFilename[0] != '\0')) {
				currentBank.save(lastFilename);
			}
			if (ImGui::MenuItem("Save Bank As...")) {
				const char *filename = noc_file_dialog_open(NOC_FILE_DIALOG_SAVE, "WAV\0*.wav\0", NULL, NULL);
				if (filename) {
					currentBank.save(filename);
					snprintf(lastFilename, sizeof(lastFilename), "%s", filename);
				}
			}
			if (ImGui::MenuItem("Save Waves To Folder...", NULL, false, true)) {
				const char *dirname = noc_file_dialog_open(NOC_FILE_DIALOG_OPEN | NOC_FILE_DIALOG_DIR, NULL, NULL, NULL);
				if (dirname)
					currentBank.saveWaves(dirname);
			}
			if (ImGui::MenuItem("Import Audio...", NULL, false, true))
				showImportPopup = true;

			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Audio Output")) {
			int deviceCount = audioGetDeviceCount();
			for (int deviceId = 0; deviceId < deviceCount; deviceId++) {
				const char *deviceName = audioGetDeviceName(deviceId);
				if (ImGui::MenuItem(deviceName, NULL, false)) audioOpen(deviceId);
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Help")) {
			if (ImGui::MenuItem("Online Manual", NULL, false))
				openBrowser("http://example.com");
			// if (ImGui::MenuItem("imgui Demo", NULL, showTestWindow)) showTestWindow = !showTestWindow;
			ImGui::EndMenu();
		}
		ImGui::EndMenuBar();
	}
}


void renderPreview() {
	ImGui::Checkbox("Play", &playEnabled);
	ImGui::SameLine();
	ImGui::PushItemWidth(300.0);
	ImGui::SliderFloat("##playVolume", &playVolume, -60.0f, 0.0f, "Volume: %.2f dB");
	ImGui::PushItemWidth(-1.0);
	ImGui::SameLine();
	ImGui::SliderFloat("##playFrequency", &playFrequency, 1.0f, 10000.0f, "Frequency: %.2f Hz", 3.0f);

	if (ImGui::RadioButton("Morph XY", playModeXY)) playModeXY = true;
	ImGui::SameLine();
	if (ImGui::RadioButton("Morph Z", !playModeXY)) playModeXY = false;
	if (playModeXY) {
		ImGui::SameLine();
		ImGui::PushItemWidth(-1.0);
		float width = ImGui::CalcItemWidth() / 2.0 - ImGui::GetStyle().FramePadding.y;
		ImGui::PushItemWidth(width);
		ImGui::SliderFloat("##Morph X", &morphX, 0.0, BANK_GRID_WIDTH - 1, "Morph X: %.3f");
		ImGui::SameLine();
		ImGui::SliderFloat("##Morph Y", &morphY, 0.0, BANK_GRID_HEIGHT - 1, "Morph Y: %.3f");
	}
	else {
		ImGui::SameLine();
		ImGui::PushItemWidth(-1.0);
		ImGui::SliderFloat("##Morph Z", &morphZ, 0.0, BANK_LEN - 1, "Morph Z: %.3f");
	}
}


void renderToolSelector(Tool *tool) {
	if (ImGui::RadioButton("Pencil", *tool == PENCIL_TOOL)) *tool = PENCIL_TOOL;
	ImGui::SameLine();
	if (ImGui::RadioButton("Brush", *tool == BRUSH_TOOL)) *tool = BRUSH_TOOL;
	ImGui::SameLine();
	if (ImGui::RadioButton("Grab", *tool == GRAB_TOOL)) *tool = GRAB_TOOL;
	ImGui::SameLine();
	if (ImGui::RadioButton("Line", *tool == LINE_TOOL)) *tool = LINE_TOOL;
	ImGui::SameLine();
	if (ImGui::RadioButton("Eraser", *tool == ERASER_TOOL)) *tool = ERASER_TOOL;
}


void effectSlider(EffectID effect) {
	char id[64];
	snprintf(id, sizeof(id), "##%s", effectNames[effect]);
	char text[64];
	snprintf(text, sizeof(text), "%s: %%.3f", effectNames[effect]);
	if (ImGui::SliderFloat(id, &currentBank.waves[selectedWave].effects[effect], 0.0f, 1.0f, text))
		currentBank.waves[selectedWave].updatePost();
}


void editorPage() {
	ImGui::BeginChild("Sidebar", ImVec2(200, 0), true);
	{
		// Render + dragging
		ImGui::PushItemWidth(-1);
		for (int n = 0; n < BANK_LEN; n++) {
			char text[10];
			snprintf(text, sizeof(text), "%d", n);
			if (renderWave(text, 30, NULL, 0, currentBank.waves[n].postSamples, WAVE_LEN))
				selectWave(n);

			// if (ImGui::IsItemActive() && !ImGui::IsItemHovered()) {
			// 	float drag_dy = ImGui::GetMouseDragDelta(0).y;
			// 	if (drag_dy < 0.0f && n > 0) {
			// 		// Swap
			// 		const char *tmp = items[n];
			// 		items[n] = items[n - 1];
			// 		items[n - 1] = tmp;
			// 		ImGui::ResetMouseDragDelta();
			// 	} else if (drag_dy > 0.0f && n < BANK_LEN - 1) {
			// 		const char *tmp = items[n];
			// 		items[n] = items[n + 1];
			// 		items[n + 1] = tmp;
			// 		ImGui::ResetMouseDragDelta();
			// 	}
			// }
		}
		ImGui::PopItemWidth();
	}
	ImGui::EndChild();

	ImGui::SameLine();
	ImGui::BeginChild("Editor", ImVec2(0, 0), true);
	{
		Wave *wave = &currentBank.waves[selectedWave];
		float *effects = wave->effects;

		ImGui::PushItemWidth(-1);

		static enum Tool tool = PENCIL_TOOL;
		renderToolSelector(&tool);

		ImGui::SameLine();
		if (ImGui::Button("Clear"))
			currentBank.waves[selectedWave].clear();

		for (WaveDirectory &waveDirectory : waveDirectories) {
			ImGui::SameLine();
			if (ImGui::Button(waveDirectory.name.c_str())) ImGui::OpenPopup(waveDirectory.name.c_str());
			if (ImGui::BeginPopup(waveDirectory.name.c_str())) {
				for (WaveFile &waveFile : waveDirectory.waveFiles) {
					if (ImGui::Selectable(waveFile.name.c_str())) {
						// TODO
					}
				}
				ImGui::EndPopup();
			}
		}

		// ImGui::SameLine();
		// if (ImGui::RadioButton("Smooth", tool == SMOOTH_TOOL)) tool = SMOOTH_TOOL;

		const int oversample = 4;
		float waveOversample[WAVE_LEN * oversample];
		cyclicOversample(wave->postSamples, waveOversample, WAVE_LEN, oversample);
		if (renderWave("we1", 200.0, wave->samples, WAVE_LEN, waveOversample, WAVE_LEN * oversample, tool)) {
			currentBank.waves[selectedWave].commitSamples();
		}

		if (renderHistogram("he1", 200.0, wave->harmonics, WAVE_LEN / 2, wave->postHarmonics, WAVE_LEN / 2, tool)) {
			currentBank.waves[selectedWave].commitHarmonics();
		}

		for (int i = 0; i < EFFECTS_LEN; i++) {
			effectSlider((EffectID) i);
		}

		if (ImGui::Checkbox("Cycle", &currentBank.waves[selectedWave].cycle))
			currentBank.waves[selectedWave].updatePost();
		ImGui::SameLine();
		if (ImGui::Checkbox("Normalize", &currentBank.waves[selectedWave].normalize))
			currentBank.waves[selectedWave].updatePost();

		ImGui::SameLine();
		if (ImGui::Button("Apply"))
			currentBank.waves[selectedWave].bakeEffects();
		ImGui::SameLine();
		if (ImGui::Button("Randomize"))
			currentBank.waves[selectedWave].randomizeEffects();
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
			currentBank.waves[selectedWave].clearEffects();
		// if (ImGui::Button("Dump to WAV")) saveBank("out.wav");

		ImGui::PopItemWidth();
	}
	ImGui::EndChild();
}


void effectHistogram(EffectID effect, Tool tool) {
	float value[BANK_LEN];
	for (int i = 0; i < BANK_LEN; i++) {
		value[i] = currentBank.waves[i].effects[effect];
	}
	ImGui::Text(effectNames[effect]);
	if (renderHistogram(effectNames[effect], 80, value, BANK_LEN, NULL, 0, tool)) {
		for (int i = 0; i < BANK_LEN; i++) {
			if (currentBank.waves[i].effects[effect] != value[i]) {
				currentBank.waves[selectedWave].updatePost();
				selectWave(i);
				currentBank.waves[i].effects[effect] = value[i];
			}
		}
	}
}


void effectPage() {
	ImGui::BeginChild("Effect Editor", ImVec2(0, 0), true); {
		static Tool tool = PENCIL_TOOL;
		renderToolSelector(&tool);

		ImGui::PushItemWidth(-1);
		for (int i = 0; i < EFFECTS_LEN; i++) {
			effectHistogram((EffectID) i, tool);
		}
		ImGui::PopItemWidth();

		if (ImGui::Button("Normalize All")) {
			for (int i = 0; i < BANK_LEN; i++) {
				currentBank.waves[i].normalize = true;
				currentBank.waves[i].updatePost();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Normalize None")) {
			for (int i = 0; i < BANK_LEN; i++) {
				currentBank.waves[i].normalize = false;
				currentBank.waves[i].updatePost();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Cycle All")) {
			for (int i = 0; i < BANK_LEN; i++) {
				currentBank.waves[i].cycle = true;
				currentBank.waves[i].updatePost();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Cycle None")) {
			for (int i = 0; i < BANK_LEN; i++) {
				currentBank.waves[i].cycle = false;
				currentBank.waves[i].updatePost();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Randomize")) {
			for (int i = 0; i < BANK_LEN; i++) {
				currentBank.waves[i].randomizeEffects();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Clear")) {
			for (int i = 0; i < BANK_LEN; i++) {
				currentBank.waves[i].clearEffects();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Apply")) {
			for (int i = 0; i < BANK_LEN; i++) {
				currentBank.waves[i].bakeEffects();
			}
		}
	}
	ImGui::EndChild();
}


void gridPage() {}


void _3DViewPage() {
	ImGui::BeginChild("3D View", ImVec2(0, 0), true);
	{
		float *waves[BANK_LEN];
		for (int b = 0; b < BANK_LEN; b++) {
			waves[b] = currentBank.waves[b].postSamples;
		}
		renderWave3D(600, waves, BANK_LEN, WAVE_LEN);
	}
	ImGui::EndChild();
}


void importPopup() {
	static float offsetPercent;
	static float zoom;
	static ImportMode mode;
	static float *audio = NULL;
	static int audioLen;
	static Bank newBank;

	const int audioLengthMin = 32;
	const int audioLengthMax = 1<<19;

	// Open popup and reset state
	if (showImportPopup) {
		showImportPopup = false;
		const char *filename = noc_file_dialog_open(NOC_FILE_DIALOG_OPEN, "WAV\0*.wav\0", NULL, NULL);
		if (filename) {
			offsetPercent = 0.0;
			zoom = 1.0;
			mode = OVERWRITE_IMPORT;
			audio = loadAudio(filename, &audioLen);
			if (audioLengthMin <= audioLen && audioLen <= audioLengthMax)
				ImGui::OpenPopup("Import");
			else
				ImGui::OpenPopup("Import Error");
		}
	}
	ImGui::SetNextWindowContentWidth(800.0);
	if (ImGui::BeginPopupModal("Import", NULL, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
		ImGui::PushItemWidth(-1.0);
		// Import samples
		newBank = currentBank;
		newBank.importSamples(audio, audioLen, offsetPercent / 100.0, mode);
		// Wave view
		float samples[BANK_LEN * WAVE_LEN];
		newBank.getSamples(samples);
		renderWave("##importSamples", 100, NULL, 0, samples, BANK_LEN * WAVE_LEN, NO_TOOL);

		// Parameters
		ImGui::SliderFloat("##offsetPercent", &offsetPercent, 0.0, 100.0, "Offset: %.2f%%");
		ImGui::SliderFloat("##zoom", &zoom, 1.0, 100.0, "Zoom: %.2f", 2.0);

		// Modes
		if (ImGui::RadioButton("Overwrite", mode == OVERWRITE_IMPORT)) mode = OVERWRITE_IMPORT;
		ImGui::SameLine();
		if (ImGui::RadioButton("Loop", mode == LOOP_IMPORT)) mode = LOOP_IMPORT;
		ImGui::SameLine();
		if (ImGui::RadioButton("Clear", mode == CLEAR_IMPORT)) mode = CLEAR_IMPORT;
		ImGui::SameLine();
		if (ImGui::RadioButton("Mix", mode == ADD_IMPORT)) mode = ADD_IMPORT;
		ImGui::SameLine();
		if (ImGui::RadioButton("Ring Modulate", mode == MULTIPLY_IMPORT)) mode = MULTIPLY_IMPORT;

		// Buttons
		bool cleanup = false;

		if (ImGui::Button("Cancel")) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Import")) {
			historyPush();
			currentBank = newBank;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
	else {
		// Cleanup if window is closed
		if (audio) {
			delete[] audio;
			audio = NULL;
		}
	}

	if (ImGui::BeginPopupModal("Import Error", NULL, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
		ImGui::Text("Could not import audio, or file is too long or short");
		if (ImGui::Button("OK")) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}


void renderMain() {
	ImGui::SetNextWindowPos(ImVec2(0, 0));
	ImGui::SetNextWindowSize(ImVec2((int)ImGui::GetIO().DisplaySize.x, (int)ImGui::GetIO().DisplaySize.y));

	ImGui::Begin("", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_MenuBar);
	{
		// Menu bar
		renderMenuBar();
		renderPreview();
		// Tab bar
		{
			static const char *tabLabels[4] = {
				"Waveform Editor",
				"Effect Editor",
				"Grid XY View",
				"3D Z View"
			};
			static int hoveredTab = 0;
			ImGui::TabLabels(4, tabLabels, (int*)&currentPage, NULL, false, &hoveredTab);
		}
		// Page
		switch (currentPage) {
			case WAVEFORM_EDITOR: editorPage(); break;
			case EFFECT_EDITOR: effectPage(); break;
			case GRID_VIEW: gridPage(); break;
			case _3D_VIEW: _3DViewPage(); break;
		}

		// Modals
		importPopup();
	}
	ImGui::End();

	if (showTestWindow) {
		ImGui::ShowTestWindow(&showTestWindow);
	}
}


void initStyle() {
	ImGuiStyle& style = ImGui::GetStyle();

	style.WindowRounding = 2.f;
	style.GrabRounding = 2.f;
	style.ChildWindowRounding = 2.f;
	style.ScrollbarRounding = 2.f;
	style.FrameRounding = 2.f;
	style.FramePadding = ImVec2(6.0f, 4.0f);

	style.Colors[ImGuiCol_Text]                  = ImVec4(0.73f, 0.73f, 0.73f, 1.00f);
	style.Colors[ImGuiCol_TextDisabled]          = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
	style.Colors[ImGuiCol_WindowBg]              = ImVec4(0.26f, 0.26f, 0.26f, 0.95f);
	style.Colors[ImGuiCol_ChildWindowBg]         = ImVec4(0.26f, 0.26f, 0.26f, 1.00f);
	style.Colors[ImGuiCol_PopupBg]               = ImVec4(0.26f, 0.26f, 0.26f, 1.00f);
	style.Colors[ImGuiCol_Border]                = ImVec4(0.26f, 0.26f, 0.26f, 1.00f);
	style.Colors[ImGuiCol_BorderShadow]          = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
	style.Colors[ImGuiCol_FrameBg]               = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
	style.Colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
	style.Colors[ImGuiCol_FrameBgActive]         = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
	style.Colors[ImGuiCol_TitleBg]               = ImVec4(0.36f, 0.36f, 0.36f, 1.00f);
	style.Colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.36f, 0.36f, 0.36f, 1.00f);
	style.Colors[ImGuiCol_TitleBgActive]         = ImVec4(0.36f, 0.36f, 0.36f, 1.00f);
	style.Colors[ImGuiCol_MenuBarBg]             = ImVec4(0.26f, 0.26f, 0.26f, 1.00f);
	style.Colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.21f, 0.21f, 0.21f, 1.00f);
	style.Colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.36f, 0.36f, 0.36f, 1.00f);
	style.Colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.36f, 0.36f, 0.36f, 1.00f);
	style.Colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.36f, 0.36f, 0.36f, 1.00f);
	style.Colors[ImGuiCol_ComboBg]               = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
	style.Colors[ImGuiCol_CheckMark]             = ImVec4(0.78f, 0.78f, 0.78f, 1.00f);
	style.Colors[ImGuiCol_SliderGrab]            = ImVec4(0.74f, 0.74f, 0.74f, 1.00f);
	style.Colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.74f, 0.74f, 0.74f, 1.00f);
	style.Colors[ImGuiCol_Button]                = ImVec4(0.36f, 0.36f, 0.36f, 1.00f);
	style.Colors[ImGuiCol_ButtonHovered]         = ImVec4(0.43f, 0.43f, 0.43f, 1.00f);
	style.Colors[ImGuiCol_ButtonActive]          = ImVec4(0.11f, 0.11f, 0.11f, 1.00f);
	style.Colors[ImGuiCol_Header]                = ImVec4(0.36f, 0.36f, 0.36f, 1.00f);
	style.Colors[ImGuiCol_HeaderHovered]         = ImVec4(0.36f, 0.36f, 0.36f, 1.00f);
	style.Colors[ImGuiCol_HeaderActive]          = ImVec4(0.36f, 0.36f, 0.36f, 1.00f);
	style.Colors[ImGuiCol_Column]                = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);
	style.Colors[ImGuiCol_ColumnHovered]         = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	style.Colors[ImGuiCol_ColumnActive]          = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	style.Colors[ImGuiCol_ResizeGrip]            = ImVec4(0.36f, 0.36f, 0.36f, 1.00f);
	style.Colors[ImGuiCol_ResizeGripHovered]     = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	style.Colors[ImGuiCol_ResizeGripActive]      = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	style.Colors[ImGuiCol_CloseButton]           = ImVec4(0.59f, 0.59f, 0.59f, 1.00f);
	style.Colors[ImGuiCol_CloseButtonHovered]    = ImVec4(0.98f, 0.39f, 0.36f, 1.00f);
	style.Colors[ImGuiCol_CloseButtonActive]     = ImVec4(0.98f, 0.39f, 0.36f, 1.00f);
	style.Colors[ImGuiCol_PlotLines]             = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);
	style.Colors[ImGuiCol_PlotLinesHovered]      = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
	style.Colors[ImGuiCol_PlotHistogram]         = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
	style.Colors[ImGuiCol_PlotHistogramHovered]  = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
	style.Colors[ImGuiCol_TextSelectedBg]        = ImVec4(0.32f, 0.52f, 0.65f, 1.00f);
	style.Colors[ImGuiCol_ModalWindowDarkening]  = ImVec4(0.20f, 0.20f, 0.20f, 0.50f);

	// Load fonts
	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->AddFontFromFileTTF("fonts/Lekton-Regular.ttf", 15.0);
}


void uiInit() {
	ImGui::GetIO().IniFilename = NULL;

	initStyle();
	logoTexture = loadImage("logo-white.png");
}


void uiRender() {
	renderMain();
}

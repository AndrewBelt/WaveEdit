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
static ImTextureID logoTextureLight;
static ImTextureID logoTextureDark;
static ImTextureID logoTexture;
static int selectedWave = 0;
static char lastFilename[1024] = "\0";
static int styleId = 0;


static void refreshStyle();


static enum {
	EDITOR_PAGE = 0,
	EFFECT_PAGE,
	GRID_PAGE,
	// WATERFALL_PAGE,
	DB_PAGE,
	NUM_PAGES
} currentPage = EDITOR_PAGE;


static ImVec4 lighten(ImVec4 col, float p) {
	col.x = crossf(col.x, 1.0, p);
	col.y = crossf(col.y, 1.0, p);
	col.z = crossf(col.z, 1.0, p);
	col.w = crossf(col.w, 1.0, p);
	return col;
}

static ImVec4 darken(ImVec4 col, float p) {
	col.x = crossf(col.x, 0.0, p);
	col.y = crossf(col.y, 0.0, p);
	col.z = crossf(col.z, 0.0, p);
	col.w = crossf(col.w, 0.0, p);
	return col;
}

static ImVec4 alpha(ImVec4 col, float a) {
	col.w *= a;
	return col;
}


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



void refreshMorphSnap() {
	if (morphSnap) {
		morphX = roundf(morphX);
		morphY = roundf(morphY);
		morphZ = roundf(morphZ);
	}
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
		// File
		if (ImGui::BeginMenu("File")) {
			if (ImGui::MenuItem("New Bank")) {
				currentBank.clear();
				lastFilename[0] = '\0';
				historyClear();
			}
			if (ImGui::MenuItem("Open Bank...")) {
				const char *filename = noc_file_dialog_open(NOC_FILE_DIALOG_OPEN, "WAV Bank\0*.wav\0", NULL, NULL);
				if (filename) {
					currentBank.loadWAV(filename);
					snprintf(lastFilename, sizeof(lastFilename), "%s", filename);
					historyClear();
				}
			}
			if (ImGui::MenuItem("Save Bank", NULL, false, lastFilename[0] != '\0')) {
				currentBank.saveWAV(lastFilename);
			}
			if (ImGui::MenuItem("Save Bank As...")) {
				const char *filename = noc_file_dialog_open(NOC_FILE_DIALOG_SAVE, "WAV Bank\0*.wav\0", NULL, NULL);
				if (filename) {
					currentBank.saveWAV(filename);
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
		// Edit
		if (ImGui::BeginMenu("Edit")) {
			if (ImGui::MenuItem("Undo", "Ctrl+Z", false))
				historyUndo();
			if (ImGui::MenuItem("Redo", "Ctrl+Shift+Z", false))
				historyRedo();
			ImGui::EndMenu();
		}
		// Audio Output
		if (ImGui::BeginMenu("Audio Output")) {
			int deviceCount = audioGetDeviceCount();
			for (int deviceId = 0; deviceId < deviceCount; deviceId++) {
				const char *deviceName = audioGetDeviceName(deviceId);
				if (ImGui::MenuItem(deviceName, NULL, false)) audioOpen(deviceId);
			}
			ImGui::EndMenu();
		}
		// Colors
		if (ImGui::BeginMenu("Colors")) {
			if (ImGui::MenuItem("Sol", NULL, styleId == 0)) {
				styleId = 0;
				refreshStyle();
			}
			if (ImGui::MenuItem("Mars", NULL, styleId == 1)) {
				styleId = 1;
				refreshStyle();
			}
			if (ImGui::MenuItem("Mercury", NULL, styleId == 2)) {
				styleId = 2;
				refreshStyle();
			}
			if (ImGui::MenuItem("Titan", NULL, styleId == 3)) {
				styleId = 3;
				refreshStyle();
			}
			ImGui::EndMenu();
		}
		// Help
		if (ImGui::BeginMenu("Help")) {
			if (ImGui::MenuItem("Online Manual", NULL, false))
				openBrowser("http://synthtech.com/WaveEdit");
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

	ImGui::Checkbox("Morph Snap", &morphSnap);
	ImGui::SameLine();
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

	refreshMorphSnap();
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
	if (ImGui::SliderFloat(id, &currentBank.waves[selectedWave].effects[effect], 0.0f, 1.0f, text)) {
		currentBank.waves[selectedWave].updatePost();
		historyPush();
	}
}


void editorPage() {
	ImGui::BeginChild("Sidebar", ImVec2(200, 0), true);
	{
		float dummyZ = 0.0;
		ImGui::PushItemWidth(-1);
		if (renderBankGrid("SidebarGrid", BANK_LEN * 35.0, 1, &currentBank, &dummyZ, &morphZ, &selectedWave)) {
			historyPush();
		}
		refreshMorphSnap();
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
		if (ImGui::Button("Clear")) {
			currentBank.waves[selectedWave].clear();
			historyPush();
		}


		for (const CatalogCategory &catalogCategory : catalogCategories) {
			ImGui::SameLine();
			if (ImGui::Button(catalogCategory.name.c_str())) ImGui::OpenPopup(catalogCategory.name.c_str());
			if (ImGui::BeginPopup(catalogCategory.name.c_str())) {
				for (const CatalogFile &catalogFile : catalogCategory.files) {
					if (ImGui::Selectable(catalogFile.name.c_str())) {
						memcpy(currentBank.waves[selectedWave].samples, catalogFile.samples, sizeof(float) * WAVE_LEN);
						currentBank.waves[selectedWave].commitSamples();
						historyPush();
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
		if (renderWave("WaveEditor", 200.0, wave->samples, WAVE_LEN, waveOversample, WAVE_LEN * oversample, tool)) {
			currentBank.waves[selectedWave].commitSamples();
			historyPush();
		}

		if (renderHistogram("HarmonicEditor", 200.0, wave->harmonics, WAVE_LEN / 2, wave->postHarmonics, WAVE_LEN / 2, tool)) {
			currentBank.waves[selectedWave].commitHarmonics();
			historyPush();
		}

		for (int i = 0; i < EFFECTS_LEN; i++) {
			effectSlider((EffectID) i);
		}

		if (ImGui::Checkbox("Cycle", &currentBank.waves[selectedWave].cycle)) {
			currentBank.waves[selectedWave].updatePost();
			historyPush();
		}
		ImGui::SameLine();
		if (ImGui::Checkbox("Normalize", &currentBank.waves[selectedWave].normalize)) {
			currentBank.waves[selectedWave].updatePost();
			historyPush();
		}
		ImGui::SameLine();
		if (ImGui::Button("Apply")) {
			currentBank.waves[selectedWave].bakeEffects();
			historyPush();
		}
		ImGui::SameLine();
		if (ImGui::Button("Randomize")) {
			currentBank.waves[selectedWave].randomizeEffects();
			historyPush();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) {
			currentBank.waves[selectedWave].clearEffects();
			historyPush();
		}
		// if (ImGui::Button("Dump to WAV")) saveBank("out.wav");

		ImGui::PopItemWidth();
	}
	ImGui::EndChild();
}


void effectHistogram(EffectID effect, Tool tool) {
	float value[BANK_LEN];
	float average = 0.0;
	for (int i = 0; i < BANK_LEN; i++) {
		value[i] = currentBank.waves[i].effects[effect];
		average += value[i];
	}
	average /= BANK_LEN;
	float oldAverage = average;

	ImGui::Text("%s", effectNames[effect]);

	char id[64];
	snprintf(id, sizeof(id), "##%sAverage", effectNames[effect]);
	char text[64];
	snprintf(text, sizeof(text), "Average %s: %%.3f", effectNames[effect]);
	if (ImGui::SliderFloat(id, &average, 0.0f, 1.0f, text)) {
		// Change the average effect level to the new average
		float deltaAverage = average - oldAverage;
		for (int i = 0; i < BANK_LEN; i++) {
			if (0.0 < average && average < 1.0) {
				currentBank.waves[i].effects[effect] = clampf(currentBank.waves[i].effects[effect] + deltaAverage, 0.0, 1.0);
			}
			else {
				currentBank.waves[i].effects[effect] = average;
			}
			currentBank.waves[i].updatePost();
			historyPush();
		}
	}

	if (renderHistogram(effectNames[effect], 120, value, BANK_LEN, NULL, 0, tool)) {
		for (int i = 0; i < BANK_LEN; i++) {
			if (currentBank.waves[i].effects[effect] != value[i]) {
				// TODO This always selects the highest index. Select the index the mouse is hovering (requires renderHistogram() to return an int)
				selectWave(i);
				currentBank.waves[i].effects[effect] = value[i];
				currentBank.waves[i].updatePost();
				historyPush();
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
				historyPush();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Normalize None")) {
			for (int i = 0; i < BANK_LEN; i++) {
				currentBank.waves[i].normalize = false;
				currentBank.waves[i].updatePost();
				historyPush();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Cycle All")) {
			for (int i = 0; i < BANK_LEN; i++) {
				currentBank.waves[i].cycle = true;
				currentBank.waves[i].updatePost();
				historyPush();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Cycle None")) {
			for (int i = 0; i < BANK_LEN; i++) {
				currentBank.waves[i].cycle = false;
				currentBank.waves[i].updatePost();
				historyPush();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Randomize")) {
			for (int i = 0; i < BANK_LEN; i++) {
				currentBank.waves[i].randomizeEffects();
				historyPush();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Clear")) {
			for (int i = 0; i < BANK_LEN; i++) {
				currentBank.waves[i].clearEffects();
				historyPush();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Apply")) {
			for (int i = 0; i < BANK_LEN; i++) {
				currentBank.waves[i].bakeEffects();
				historyPush();
			}
		}
	}
	ImGui::EndChild();
}


void gridPage() {
	ImGui::BeginChild("Grid Page", ImVec2(0, 0), true);
	{
		ImGui::PushItemWidth(-1.0);
		if (renderBankGrid("WaveGrid", -1.f, BANK_GRID_WIDTH, &currentBank, &morphX, &morphY, &selectedWave)) {
			historyPush();
		}
		refreshMorphSnap();
	}
	ImGui::EndChild();
}


void waterfallPage() {
	ImGui::BeginChild("3D View", ImVec2(0, 0), true);
	{
		ImGui::PushItemWidth(-1.0);
		float *waves[BANK_LEN];
		for (int b = 0; b < BANK_LEN; b++) {
			waves[b] = currentBank.waves[b].postSamples;
		}
		renderWave3D(600, waves, BANK_LEN, WAVE_LEN);
	}
	ImGui::EndChild();
}


void importPopup() {
	static float gain;
	static float offset;
	static float zoom;
	static float left;
	static float right;
	static ImportMode mode;
	static float *audio = NULL;
	static int audioLen;
	static Bank newBank;

	const int audioLengthMin = 32;
	const int audioLengthMax = 44100 * 20;

	// Open popup and reset state
	if (showImportPopup) {
		showImportPopup = false;
		const char *filename = noc_file_dialog_open(NOC_FILE_DIALOG_OPEN, NULL, NULL, NULL);
		if (filename) {
			offset = 0.0;
			zoom = 0.0;
			gain = 0.0;
			left = 0.0;
			right = 1.0;
			mode = CLEAR_IMPORT;
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
		newBank.importSamples(audio, audioLen, powf(10.0, gain / 20.0), offset, zoom, left, right, mode);
		// Wave view
		float samples[BANK_LEN * WAVE_LEN];
		newBank.getPostSamples(samples);
		renderWave("##importSamples", 100, NULL, 0, samples, BANK_LEN * WAVE_LEN, NO_TOOL);

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
			currentBank = newBank;
			historyPush();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) {
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
			static const char *tabLabels[NUM_PAGES] = {
				"Waveform Editor",
				"Effect Editor",
				"Grid XY View",
				// "3D Z View",
				"WaveEdit Online"
			};
			static int hoveredTab = 0;
			ImGui::TabLabels(NUM_PAGES, tabLabels, (int*)&currentPage, NULL, false, &hoveredTab);
		}
		// Page
		switch (currentPage) {
		case EDITOR_PAGE: editorPage(); break;
		case EFFECT_PAGE: effectPage(); break;
		case GRID_PAGE: gridPage(); break;
		// case WATERFALL_PAGE: waterfallPage(); break;
		case DB_PAGE: dbPage(); break;
		default: break;
		}

		// Modals
		importPopup();
	}
	ImGui::End();

	if (showTestWindow) {
		ImGui::ShowTestWindow(&showTestWindow);
	}
}


static void refreshStyle() {
	const ImVec4 transparent = ImVec4(0.0, 0.0, 0.0, 0.0);

	ImGuiStyle& style = ImGui::GetStyle();

	style.Alpha = 1.f;
	style.WindowRounding = 2.f;
	style.GrabRounding = 2.f;
	style.ChildWindowRounding = 2.f;
	style.ScrollbarRounding = 2.f;
	style.FrameRounding = 2.f;
	style.FramePadding = ImVec2(6.0f, 4.0f);

	if (styleId == 0) {
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
		style.Colors[ImGuiCol_PlotHistogram]         = ImVec4(1.0, 0.8, 0.2, 1.0);
		style.Colors[ImGuiCol_PlotHistogramHovered]  = ImVec4(0.7, 0.5, 0.1, 0.5);
		style.Colors[ImGuiCol_TextSelectedBg]        = ImVec4(0.32f, 0.52f, 0.65f, 1.00f);
		style.Colors[ImGuiCol_ModalWindowDarkening]  = ImVec4(0.20f, 0.20f, 0.20f, 0.50f);
		logoTexture = logoTextureLight;
	}
	else if (styleId == 1) {
		// base16-atelier-dune
		ImVec4 base[16] = {
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0x20, 0x20, 0x1d, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0x29, 0x28, 0x24, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0x6e, 0x6b, 0x5e, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0x7d, 0x7a, 0x68, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0x99, 0x95, 0x80, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0xa6, 0xa2, 0x8c, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0xe8, 0xe4, 0xcf, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0xfe, 0xfb, 0xec, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0xd7, 0x37, 0x37, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0xb6, 0x56, 0x11, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0xae, 0x95, 0x13, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0x60, 0xac, 0x39, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0x1f, 0xad, 0x83, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0x66, 0x84, 0xe1, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0xb8, 0x54, 0xd4, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0xd4, 0x35, 0x52, 0xff)),
		};

		style.Colors[ImGuiCol_Text]                 = base[0x6];
		style.Colors[ImGuiCol_TextDisabled]         = base[0x4];
		style.Colors[ImGuiCol_WindowBg]             = base[0x2];
		style.Colors[ImGuiCol_ChildWindowBg]        = base[0x2];
		style.Colors[ImGuiCol_PopupBg]              = base[0x2];
		style.Colors[ImGuiCol_Border]               = transparent;
		style.Colors[ImGuiCol_BorderShadow]         = transparent;
		style.Colors[ImGuiCol_FrameBg]              = base[0x1];
		style.Colors[ImGuiCol_FrameBgHovered]       = base[0x1];
		style.Colors[ImGuiCol_FrameBgActive]        = base[0x1];
		style.Colors[ImGuiCol_TitleBg]              = base[0x3];
		style.Colors[ImGuiCol_TitleBgCollapsed]     = base[0x3];
		style.Colors[ImGuiCol_TitleBgActive]        = base[0x3];
		style.Colors[ImGuiCol_MenuBarBg]            = base[0x2];
		style.Colors[ImGuiCol_ScrollbarBg]          = base[0x3];
		style.Colors[ImGuiCol_ScrollbarGrab]        = base[0x4];
		style.Colors[ImGuiCol_ScrollbarGrabHovered] = base[0x4];
		style.Colors[ImGuiCol_ScrollbarGrabActive]  = base[0x4];
		style.Colors[ImGuiCol_ComboBg]              = base[0x4];
		style.Colors[ImGuiCol_CheckMark]            = base[0x4];
		style.Colors[ImGuiCol_SliderGrab]           = base[0x4];
		style.Colors[ImGuiCol_SliderGrabActive]     = base[0x4];
		style.Colors[ImGuiCol_Button]               = base[0x3];
		style.Colors[ImGuiCol_ButtonHovered]        = base[0x4];
		style.Colors[ImGuiCol_ButtonActive]         = base[0x4];
		style.Colors[ImGuiCol_Header]               = base[0x3];
		style.Colors[ImGuiCol_HeaderHovered]        = base[0x3];
		style.Colors[ImGuiCol_HeaderActive]         = base[0x3];
		style.Colors[ImGuiCol_Column]               = base[0x2];
		style.Colors[ImGuiCol_ColumnHovered]        = base[0x2];
		style.Colors[ImGuiCol_ColumnActive]         = base[0x2];
		style.Colors[ImGuiCol_ResizeGrip]           = base[0x2];
		style.Colors[ImGuiCol_ResizeGripHovered]    = base[0x2];
		style.Colors[ImGuiCol_ResizeGripActive]     = base[0x2];
		style.Colors[ImGuiCol_CloseButton]          = base[0x2];
		style.Colors[ImGuiCol_CloseButtonHovered]   = base[0x2];
		style.Colors[ImGuiCol_CloseButtonActive]    = base[0x2];
		style.Colors[ImGuiCol_PlotLines]            = base[0x4];
		style.Colors[ImGuiCol_PlotLinesHovered]     = base[0x4];
		style.Colors[ImGuiCol_PlotHistogram]        = darken(base[0x8], 0.2);
		style.Colors[ImGuiCol_PlotHistogramHovered] = alpha(base[0x7], 0.2);
		style.Colors[ImGuiCol_TextSelectedBg]       = base[0x3];
		style.Colors[ImGuiCol_ModalWindowDarkening] = alpha(base[0x1], 0.5);
		logoTexture = logoTextureLight;
	}
	else if (styleId == 2) {
		// base16-atelier-dune
		ImVec4 base[16] = {
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0x20, 0x20, 0x1d, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0x29, 0x28, 0x24, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0x6e, 0x6b, 0x5e, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0x7d, 0x7a, 0x68, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0x99, 0x95, 0x80, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0xa6, 0xa2, 0x8c, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0xe8, 0xe4, 0xcf, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0xfe, 0xfb, 0xec, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0xd7, 0x37, 0x37, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0xb6, 0x56, 0x11, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0xae, 0x95, 0x13, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0x60, 0xac, 0x39, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0x1f, 0xad, 0x83, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0x66, 0x84, 0xe1, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0xb8, 0x54, 0xd4, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0xd4, 0x35, 0x52, 0xff)),
		};

		style.Colors[ImGuiCol_Text]                 = base[0x1];
		style.Colors[ImGuiCol_TextDisabled]         = base[0x2];
		style.Colors[ImGuiCol_WindowBg]             = base[0x7];
		style.Colors[ImGuiCol_ChildWindowBg]        = base[0x7];
		style.Colors[ImGuiCol_PopupBg]              = base[0x7];
		style.Colors[ImGuiCol_Border]               = transparent;
		style.Colors[ImGuiCol_BorderShadow]         = transparent;
		style.Colors[ImGuiCol_FrameBg]              = base[0x6];
		style.Colors[ImGuiCol_FrameBgHovered]       = base[0x6];
		style.Colors[ImGuiCol_FrameBgActive]        = base[0x6];
		style.Colors[ImGuiCol_TitleBg]              = base[0x4];
		style.Colors[ImGuiCol_TitleBgCollapsed]     = base[0x4];
		style.Colors[ImGuiCol_TitleBgActive]        = base[0x4];
		style.Colors[ImGuiCol_MenuBarBg]            = base[0x7];
		style.Colors[ImGuiCol_ScrollbarBg]          = base[0x6];
		style.Colors[ImGuiCol_ScrollbarGrab]        = base[0x5];
		style.Colors[ImGuiCol_ScrollbarGrabHovered] = base[0x5];
		style.Colors[ImGuiCol_ScrollbarGrabActive]  = base[0x5];
		style.Colors[ImGuiCol_ComboBg]              = base[0x6];
		style.Colors[ImGuiCol_CheckMark]            = base[0x5];
		style.Colors[ImGuiCol_SliderGrab]           = base[0x5];
		style.Colors[ImGuiCol_SliderGrabActive]     = base[0x5];
		style.Colors[ImGuiCol_Button]               = base[0x5];
		style.Colors[ImGuiCol_ButtonHovered]        = base[0x6];
		style.Colors[ImGuiCol_ButtonActive]         = base[0x6];
		style.Colors[ImGuiCol_Header]               = base[0x6];
		style.Colors[ImGuiCol_HeaderHovered]        = base[0x5];
		style.Colors[ImGuiCol_HeaderActive]         = base[0x5];
		style.Colors[ImGuiCol_Column]               = base[0x2];
		style.Colors[ImGuiCol_ColumnHovered]        = base[0x2];
		style.Colors[ImGuiCol_ColumnActive]         = base[0x2];
		style.Colors[ImGuiCol_ResizeGrip]           = base[0x2];
		style.Colors[ImGuiCol_ResizeGripHovered]    = base[0x2];
		style.Colors[ImGuiCol_ResizeGripActive]     = base[0x2];
		style.Colors[ImGuiCol_CloseButton]          = base[0x2];
		style.Colors[ImGuiCol_CloseButtonHovered]   = base[0x2];
		style.Colors[ImGuiCol_CloseButtonActive]    = base[0x2];
		style.Colors[ImGuiCol_PlotLines]            = base[0x4];
		style.Colors[ImGuiCol_PlotLinesHovered]     = base[0x4];
		style.Colors[ImGuiCol_PlotHistogram]        = base[0xc];
		style.Colors[ImGuiCol_PlotHistogramHovered] = alpha(base[0x5], 0.8);
		style.Colors[ImGuiCol_TextSelectedBg]       = base[0x3];
		style.Colors[ImGuiCol_ModalWindowDarkening] = alpha(base[0x2], 0.5);
		logoTexture = logoTextureDark;
	}
	else if (styleId == 3) {
		// base16-ashes
		ImVec4 base[16] = {
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0x0c, 0x0d, 0x0e, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0x2e, 0x2f, 0x30, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0x51, 0x52, 0x53, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0x73, 0x74, 0x75, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0x95, 0x96, 0x97, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0xb7, 0xb8, 0xb9, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0xda, 0xdb, 0xdc, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0xfc, 0xfd, 0xfe, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0xe3, 0x1a, 0x1c, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0xe6, 0x55, 0x0d, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0xdc, 0xa0, 0x60, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0x31, 0xa3, 0x54, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0x80, 0xb1, 0xd3, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0x31, 0x82, 0xbd, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0x75, 0x6b, 0xb1, 0xff)),
			ImGui::ColorConvertU32ToFloat4(IM_COL32(0xb1, 0x59, 0x28, 0xff)),
		};

		style.Colors[ImGuiCol_Text]                 = base[0x1];
		style.Colors[ImGuiCol_TextDisabled]         = base[0x2];
		style.Colors[ImGuiCol_WindowBg]             = base[0x7];
		style.Colors[ImGuiCol_ChildWindowBg]        = base[0x7];
		style.Colors[ImGuiCol_PopupBg]              = base[0x7];
		style.Colors[ImGuiCol_Border]               = transparent;
		style.Colors[ImGuiCol_BorderShadow]         = transparent;
		style.Colors[ImGuiCol_FrameBg]              = base[0x6];
		style.Colors[ImGuiCol_FrameBgHovered]       = base[0x6];
		style.Colors[ImGuiCol_FrameBgActive]        = base[0x6];
		style.Colors[ImGuiCol_TitleBg]              = base[0x4];
		style.Colors[ImGuiCol_TitleBgCollapsed]     = base[0x4];
		style.Colors[ImGuiCol_TitleBgActive]        = base[0x4];
		style.Colors[ImGuiCol_MenuBarBg]            = base[0x7];
		style.Colors[ImGuiCol_ScrollbarBg]          = base[0x6];
		style.Colors[ImGuiCol_ScrollbarGrab]        = base[0x5];
		style.Colors[ImGuiCol_ScrollbarGrabHovered] = base[0x5];
		style.Colors[ImGuiCol_ScrollbarGrabActive]  = base[0x5];
		style.Colors[ImGuiCol_ComboBg]              = base[0x6];
		style.Colors[ImGuiCol_CheckMark]            = base[0x5];
		style.Colors[ImGuiCol_SliderGrab]           = base[0x5];
		style.Colors[ImGuiCol_SliderGrabActive]     = base[0x5];
		style.Colors[ImGuiCol_Button]               = base[0x5];
		style.Colors[ImGuiCol_ButtonHovered]        = base[0x6];
		style.Colors[ImGuiCol_ButtonActive]         = base[0x6];
		style.Colors[ImGuiCol_Header]               = base[0x6];
		style.Colors[ImGuiCol_HeaderHovered]        = base[0x5];
		style.Colors[ImGuiCol_HeaderActive]         = base[0x5];
		style.Colors[ImGuiCol_Column]               = base[0x2];
		style.Colors[ImGuiCol_ColumnHovered]        = base[0x2];
		style.Colors[ImGuiCol_ColumnActive]         = base[0x2];
		style.Colors[ImGuiCol_ResizeGrip]           = base[0x2];
		style.Colors[ImGuiCol_ResizeGripHovered]    = base[0x2];
		style.Colors[ImGuiCol_ResizeGripActive]     = base[0x2];
		style.Colors[ImGuiCol_CloseButton]          = base[0x2];
		style.Colors[ImGuiCol_CloseButtonHovered]   = base[0x2];
		style.Colors[ImGuiCol_CloseButtonActive]    = base[0x2];
		style.Colors[ImGuiCol_PlotLines]            = base[0x4];
		style.Colors[ImGuiCol_PlotLinesHovered]     = base[0x4];
		style.Colors[ImGuiCol_PlotHistogram]        = base[0xd];
		style.Colors[ImGuiCol_PlotHistogramHovered] = alpha(base[0xc], 0.8);
		style.Colors[ImGuiCol_TextSelectedBg]       = base[0x3];
		style.Colors[ImGuiCol_ModalWindowDarkening] = alpha(base[0x2], 0.5);
		logoTexture = logoTextureDark;
	}
}


void uiInit() {
	ImGui::GetIO().IniFilename = NULL;

	// Load fonts
	ImGui::GetIO().Fonts->AddFontFromFileTTF("fonts/Lekton-Regular.ttf", 15.0);
	logoTextureLight = loadImage("logo-light.png");
	logoTextureDark = loadImage("logo-dark.png");

	// Load UI settings
	// If this gets any more complicated, it should be JSON.
	{
		FILE *f = fopen("ui.dat", "rb");
		if (f) {
			fread(&styleId, sizeof(styleId), 1, f);
			fclose(f);
		}
	}

	refreshStyle();
}


void uiDestroy() {
	// Save UI settings
	{
		FILE *f = fopen("ui.dat", "wb");
		if (f) {
			fwrite(&styleId, sizeof(styleId), 1, f);
			fclose(f);
		}
	}
}


void uiRender() {
	// Key commands
	if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(SDLK_z)) {
		// History
		if (ImGui::GetIO().KeyShift)
			historyRedo();
		else
			historyUndo();
	}

	renderMain();
}

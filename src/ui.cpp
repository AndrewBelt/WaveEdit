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
	EDITOR_PAGE = 0,
	EFFECT_PAGE,
	GRID_PAGE,
	WATERFALL_PAGE,
	DB_PAGE,
	NUM_PAGES
} currentPage = EDITOR_PAGE;


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
				lastFilename[0] = '\0';
			}
			if (ImGui::MenuItem("Open Bank...")) {
				const char *filename = noc_file_dialog_open(NOC_FILE_DIALOG_OPEN, "WAV Bank\0*.wav\0", NULL, NULL);
				if (filename) {
					historyPush();
					currentBank.loadWAV(filename);
					snprintf(lastFilename, sizeof(lastFilename), "%s", filename);
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
			if (ImGui::MenuItem("imgui Demo", NULL, showTestWindow)) showTestWindow = !showTestWindow;
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
		float *samples[BANK_LEN];
		for (int j = 0; j < BANK_LEN; j++) {
			samples[j] = currentBank.waves[j].samples;
		}
		ImVec2 gridPos = ImVec2(0, morphZ);
		float dummyZ = 0.0;
		if (renderWaveGrid("", 1, BANK_LEN, samples, WAVE_LEN, &dummyZ, &morphZ)) {
			for (int j = 0; j < BANK_LEN; j++) {
				currentBank.waves[j].commitSamples();
			}
		}
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

		for (const CatalogCategory &catalogCategory : catalogCategories) {
			ImGui::SameLine();
			if (ImGui::Button(catalogCategory.name.c_str())) ImGui::OpenPopup(catalogCategory.name.c_str());
			if (ImGui::BeginPopup(catalogCategory.name.c_str())) {
				for (const CatalogFile &catalogFile : catalogCategory.files) {
					if (ImGui::Selectable(catalogFile.name.c_str())) {
						memcpy(currentBank.waves[selectedWave].samples, catalogFile.samples, sizeof(float) * WAVE_LEN);
						currentBank.waves[selectedWave].commitSamples();
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
		}
	}

	if (renderHistogram(effectNames[effect], 120, value, BANK_LEN, NULL, 0, tool)) {
		for (int i = 0; i < BANK_LEN; i++) {
			if (currentBank.waves[i].effects[effect] != value[i]) {
				selectWave(i);
				currentBank.waves[i].effects[effect] = value[i];
				currentBank.waves[i].updatePost();
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


void gridPage() {
	ImGui::BeginChild("Grid Page", ImVec2(0, 0), true);
	{
		ImGui::PushItemWidth(-1.0);
		float *samples[BANK_LEN];
		for (int j = 0; j < BANK_LEN; j++) {
			samples[j] = currentBank.waves[j].samples;
		}
		if (renderWaveGrid("", BANK_GRID_WIDTH, BANK_GRID_HEIGHT, samples, WAVE_LEN, &morphX, &morphY)) {
			for (int j = 0; j < BANK_LEN; j++) {
				currentBank.waves[j].commitSamples();
			}
		}
	}
	ImGui::EndChild();
}


void _3DViewPage() {
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
		newBank.importSamples(audio, audioLen, powf(10.0, gain / 20.0), offset, zoom, mode);
		// Wave view
		float samples[BANK_LEN * WAVE_LEN];
		newBank.getSamples(samples);
		renderWave("##importSamples", 100, NULL, 0, samples, BANK_LEN * WAVE_LEN, NO_TOOL);

		// Parameters
		if (ImGui::Button("Reset Gain")) gain = 0.0;
		ImGui::SameLine();
		ImGui::SliderFloat("##gain", &gain, -40.0, 40.0, "Gain: %.2fdB");

		if (ImGui::Button("Reset Offset")) offset = 0.0;
		ImGui::SameLine();
		ImGui::SliderFloat("##offset", &offset, -1.0, 1.0, "Offset: %.4f");

		if (ImGui::Button("Reset Zoom")) zoom = 0.0;
		ImGui::SameLine();
		ImGui::SliderFloat("##zoom", &zoom, -7.0, 7.0, "Zoom: %.2f");

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
			historyPush();
			currentBank = newBank;
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
				"3D Z View",
				"WaveEdit DB"
			};
			static int hoveredTab = 0;
			ImGui::TabLabels(NUM_PAGES, tabLabels, (int*)&currentPage, NULL, false, &hoveredTab);
		}
		// Page
		switch (currentPage) {
		case EDITOR_PAGE: editorPage(); break;
		case EFFECT_PAGE: effectPage(); break;
		case GRID_PAGE: gridPage(); break;
		case WATERFALL_PAGE: _3DViewPage(); break;
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


void initStyle(int styleID) {
	ImGuiStyle& style = ImGui::GetStyle();

	style.WindowRounding = 2.f;
	style.GrabRounding = 2.f;
	style.ChildWindowRounding = 2.f;
	style.ScrollbarRounding = 2.f;
	style.FrameRounding = 2.f;
	style.FramePadding = ImVec2(6.0f, 4.0f);

	if (styleID == 0) {
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
	}
	else if (styleID == 1) {
		style.Colors[ImGuiCol_Text]                  = ImVec4(0.40f, 0.39f, 0.38f, 1.00f);
		style.Colors[ImGuiCol_TextDisabled]          = ImVec4(0.40f, 0.39f, 0.38f, 0.77f);
		style.Colors[ImGuiCol_WindowBg]              = ImVec4(0.92f, 0.91f, 0.88f, 0.70f);
		style.Colors[ImGuiCol_ChildWindowBg]         = ImVec4(1.00f, 0.98f, 0.95f, 0.58f);
		style.Colors[ImGuiCol_PopupBg]               = ImVec4(0.92f, 0.91f, 0.88f, 0.92f);
		style.Colors[ImGuiCol_Border]                = ImVec4(0.84f, 0.83f, 0.80f, 0.65f);
		style.Colors[ImGuiCol_BorderShadow]          = ImVec4(0.92f, 0.91f, 0.88f, 0.00f);
		style.Colors[ImGuiCol_FrameBg]               = ImVec4(1.00f, 0.98f, 0.95f, 1.00f);
		style.Colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.99f, 1.00f, 0.40f, 0.78f);
		style.Colors[ImGuiCol_FrameBgActive]         = ImVec4(0.26f, 1.00f, 0.00f, 1.00f);
		style.Colors[ImGuiCol_TitleBg]               = ImVec4(1.00f, 0.98f, 0.95f, 1.00f);
		style.Colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(1.00f, 0.98f, 0.95f, 0.75f);
		style.Colors[ImGuiCol_TitleBgActive]         = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
		style.Colors[ImGuiCol_MenuBarBg]             = ImVec4(1.00f, 0.98f, 0.95f, 0.47f);
		style.Colors[ImGuiCol_ScrollbarBg]           = ImVec4(1.00f, 0.98f, 0.95f, 1.00f);
		style.Colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.00f, 0.00f, 0.00f, 0.21f);
		style.Colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.90f, 0.91f, 0.00f, 0.78f);
		style.Colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
		style.Colors[ImGuiCol_ComboBg]               = ImVec4(1.00f, 0.98f, 0.95f, 1.00f);
		style.Colors[ImGuiCol_CheckMark]             = ImVec4(0.25f, 1.00f, 0.00f, 0.80f);
		style.Colors[ImGuiCol_SliderGrab]            = ImVec4(0.00f, 0.00f, 0.00f, 0.14f);
		style.Colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
		style.Colors[ImGuiCol_Button]                = ImVec4(0.00f, 0.00f, 0.00f, 0.14f);
		style.Colors[ImGuiCol_ButtonHovered]         = ImVec4(0.99f, 1.00f, 0.22f, 0.86f);
		style.Colors[ImGuiCol_ButtonActive]          = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
		style.Colors[ImGuiCol_Header]                = ImVec4(0.25f, 1.00f, 0.00f, 0.76f);
		style.Colors[ImGuiCol_HeaderHovered]         = ImVec4(0.25f, 1.00f, 0.00f, 0.86f);
		style.Colors[ImGuiCol_HeaderActive]          = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
		style.Colors[ImGuiCol_Column]                = ImVec4(0.00f, 0.00f, 0.00f, 0.32f);
		style.Colors[ImGuiCol_ColumnHovered]         = ImVec4(0.25f, 1.00f, 0.00f, 0.78f);
		style.Colors[ImGuiCol_ColumnActive]          = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
		style.Colors[ImGuiCol_ResizeGrip]            = ImVec4(0.00f, 0.00f, 0.00f, 0.04f);
		style.Colors[ImGuiCol_ResizeGripHovered]     = ImVec4(0.25f, 1.00f, 0.00f, 0.78f);
		style.Colors[ImGuiCol_ResizeGripActive]      = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
		style.Colors[ImGuiCol_CloseButton]           = ImVec4(0.40f, 0.39f, 0.38f, 0.16f);
		style.Colors[ImGuiCol_CloseButtonHovered]    = ImVec4(0.40f, 0.39f, 0.38f, 0.39f);
		style.Colors[ImGuiCol_CloseButtonActive]     = ImVec4(0.40f, 0.39f, 0.38f, 1.00f);
		style.Colors[ImGuiCol_PlotLines]             = ImVec4(0.40f, 0.39f, 0.38f, 0.63f);
		style.Colors[ImGuiCol_PlotLinesHovered]      = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
		style.Colors[ImGuiCol_PlotHistogram]         = ImVec4(0.40f, 0.39f, 0.38f, 0.63f);
		style.Colors[ImGuiCol_PlotHistogramHovered]  = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
		style.Colors[ImGuiCol_TextSelectedBg]        = ImVec4(0.25f, 1.00f, 0.00f, 0.43f);
		style.Colors[ImGuiCol_ModalWindowDarkening] = ImVec4(1.00f, 0.98f, 0.95f, 0.73f);
	}
	else if (styleID == 2) {
		style.Colors[ImGuiCol_WindowBg] = ImVec4(0.9, 0.9, 0.9, 0.8);
		style.Colors[ImGuiCol_FrameBg] = ImVec4(0.0, 0.0, 0.0, 0.2);
		style.Colors[ImGuiCol_Text] = ImVec4(0.0, 0.0, 0.0, 0.9);
		style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.0, 0.0, 0.0, 0.5);
		style.Colors[ImGuiCol_TitleBg] = ImVec4(0.8, 0.8, 0.8, 0.9);
		style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.7, 0.7, 0.7, 1.0);
		style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.0, 0.0, 0.0, 0.03);
		style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.0, 0.0, 0.0, 0.2);
		style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.0, 0.0, 1.0, 0.6);
		style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.0, 0.0, 1.0, 1.0);
		style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.0, 0.0, 0.0, 0.1);
		style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.0, 0.0, 1.0, 0.6);
		style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.0, 0.0, 1.0, 1.0);
		style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.0, 0.0, 1.0, 0.6);
		style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.0, 0.0, 1.0, 1.0);
		style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.0, 0.0, 0.0, 0.6);
		style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.0, 0.0, 1.0, 1.0);
	}
}


void uiInit() {
	ImGui::GetIO().IniFilename = NULL;

	initStyle(0);

	// Load fonts
	ImGui::GetIO().Fonts->AddFontFromFileTTF("fonts/Lekton-Regular.ttf", 15.0);
	logoTexture = loadImage("logo-white.png");
}


void uiRender() {
	renderMain();
}

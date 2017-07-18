#include "WaveEdit.hpp"

#include <thread>
#include <curl/curl.h>
#include <jansson.h>
#include "imgui.h"


struct BankEntry {
	std::string uuid;
	std::string title;
	std::string attribution;
	std::string notes;
	std::string url;
	double datestamp;
};

std::vector<BankEntry> bankEntries;


static bool showUploadPopup = false;
static std::thread requestThread;
static bool requestRunning = false;


static size_t write_string_callback(void *data, size_t size, size_t nmemb, void *p) {
	std::string &text = *((std::string*)p);
	char *dataStr = (char*) data;
	size_t len = size * nmemb;
	text.append(dataStr, len);
	return len;
}


static void search() {
	requestRunning = true;
	CURL *curl = curl_easy_init();

	std::string resText;

	curl_easy_setopt(curl, CURLOPT_USERAGENT, "WaveEdit/" TOSTRING(VERSION));
	curl_easy_setopt(curl, CURLOPT_URL, "http://192.237.162.24/files/library.json");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_string_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resText);
	CURLcode res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);

	// Clear existing bank entries
	bankEntries.clear();

	if (res == CURLE_OK) {
		// Parse JSON response
		json_error_t error;
		json_t *root = json_loads(resText.c_str(), 0, &error);
		if (root) {
			json_t *entriesJ = json_object_get(root, "entries");
			if (entriesJ) {
				size_t entryId;
				json_t *entryJ;
				json_array_foreach(entriesJ, entryId, entryJ) {
					BankEntry bankEntry;

					// Fill bank entry with metadata
					json_t *uuidJ = json_object_get(entryJ, "uuid");
					if (!uuidJ) continue;
					const char *uuid = json_string_value(uuidJ);
					bankEntry.uuid = uuid;

					json_t *titleJ = json_object_get(entryJ, "title");
					if (!titleJ) continue;
					const char *title = json_string_value(titleJ);
					bankEntry.title = title;

					json_t *attributionJ = json_object_get(entryJ, "attribution");
					if (!attributionJ) continue;
					const char *attribution = json_string_value(attributionJ);
					bankEntry.attribution = attribution;

					json_t *notesJ = json_object_get(entryJ, "notes");
					if (!notesJ) continue;
					const char *notes = json_string_value(notesJ);
					bankEntry.notes = notes;

					json_t *urlJ = json_object_get(entryJ, "url");
					if (!urlJ) continue;
					const char *url = json_string_value(urlJ);
					bankEntry.url = url;

					// Add bank entry to list
					bankEntries.push_back(bankEntry);
				}
			}
			json_decref(root);
		}
	}
	requestRunning = false;
}


void uploadPopup() {
	static char title[128];
	static char author[128];
	static char notes[1024];

	if (showUploadPopup) {
		showUploadPopup = false;
		ImGui::OpenPopup("Upload");
	}

	ImGui::SetNextWindowContentWidth(600.0);
	if (ImGui::BeginPopupModal("Upload", NULL, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
		ImGui::PushItemWidth(-100.0);
		ImGui::InputText("Title", title, sizeof(title));
		ImGui::InputText("Author", author, sizeof(author));
		ImGui::InputTextMultiline("Notes", notes, sizeof(notes));

		ImGui::TextWrapped("%s", "By submitting the currently loaded wavetable bank to the WaveEdit DB, you agree to release this work under the CC0 public domain license.");
		if (ImGui::SmallButton("https://creativecommons.org/publicdomain/zero/1.0/")) openBrowser("https://creativecommons.org/publicdomain/zero/1.0/");

		if (ImGui::Button("Upload")) {
			strcpy(title, "");
			strcpy(author, "");
			strcpy(notes, "");
			// TODO
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();

		ImGui::EndPopup();
	}
}

void dbPage() {
	ImGui::BeginChild("DB Page", ImVec2(0, 0), true);
	{
		static char searchText[128] = "";
		static int page = 1;
		static const char *sortItems[2] = {
			"Downloads",
			"Date Uploaded",
		};
		static int sort = 0;

		// Search box
		ImGui::PushItemWidth(300.0);
		ImGui::InputText("##search", searchText, sizeof(searchText), ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::PopItemWidth();
		ImGui::SameLine();
		if (ImGui::Button("Search")) {
			if (!requestRunning) {
				requestThread = std::thread(search);
				requestThread.detach();
			}
		}
		ImGui::SameLine();
		ImGui::PushItemWidth(140.0);
		ImGui::Combo("##sortby", &sort, sortItems, 2);
		ImGui::PopItemWidth();
		ImGui::SameLine();
		if (ImGui::Button("Upload Current Bank")) showUploadPopup = true;

		ImGui::Dummy(ImVec2(10, 20));

		// Waves
		float dummy[WAVE_LEN];
		for (int i = 0; i < WAVE_LEN; i++) {
			dummy[i] = 2*randf() - 1;
		}

		for (const BankEntry &bankEntry : bankEntries) {
			renderWave("", 140.0, NULL, 0, dummy, WAVE_LEN, NO_TOOL);
			ImGui::SameLine();
			ImGui::BeginGroup();
			{
				ImGui::TextWrapped("%s", bankEntry.title.c_str());
				ImGui::Spacing();
				ImGui::TextWrapped("%s", bankEntry.attribution.c_str());
				ImGui::Spacing();
				ImGui::TextWrapped("%s", bankEntry.notes.c_str());
			}
			if (ImGui::Button("Load Bank")) {
				// TODO
			}
			ImGui::EndGroup();
			ImGui::Spacing();
		}

		// Page selector
		int oldPage = page;
		ImGui::Dummy(ImVec2(10, 20));
		if (ImGui::Button("<<")) page = 1;
		ImGui::SameLine();
		ImGui::PushItemWidth(120.0);
		ImGui::InputInt("##page", &page, 1, 10);
		ImGui::SameLine();
		if (ImGui::Button(">>")) page = 1;
		ImGui::PopItemWidth();

		if (oldPage != page) {
			ImGui::SetScrollX(0);
			ImGui::SetScrollY(0);
		}
	}
	ImGui::EndChild();

	uploadPopup();
}
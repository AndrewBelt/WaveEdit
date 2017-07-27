#include "WaveEdit.hpp"

#include <thread>
#include <curl/curl.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <jansson.h>
#include "imgui.h"


#define API_HOST "https://waveeditonline.com"


struct BankEntry {
	std::string uuid;
	std::string title;
	std::string attribution;
	std::string notes;
	double datestamp;
	float samples[BANK_LEN * WAVE_LEN];
};

std::vector<BankEntry> bankEntries;


static bool showUploadPopup = false;
static std::thread requestThread;
static bool requestRunning = false;

static char searchText[128] = "";
static const int quantity = 10;
static int page = 0; // zero-indexed
// static const char *sortItems[2] = {
// 	"Downloads",
// 	"Date Uploaded",
// };
// static int sort = 0;


/** base64 must be null terminated. `length` is the length of the output buffer. Returns the actual read length */
static int decodeBase64(const char *base64, void *buffer, int length) {
	BIO *bio = BIO_new_mem_buf(base64, -1);
	BIO *b64 = BIO_new(BIO_f_base64());
	bio = BIO_push(b64, bio);
	BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
	int actual = BIO_read(bio, buffer, length);
	BIO_free_all(bio);
	return actual;
}


static size_t write_string_callback(void *data, size_t size, size_t nmemb, void *p) {
	std::string &text = *((std::string*)p);
	char *dataStr = (char*) data;
	size_t len = size * nmemb;
	text.append(dataStr, len);
	return len;
}


static void refresh() {
	if (requestRunning)
		return;

	requestRunning = true;

	// Clear existing bank entries
	bankEntries.clear();

	// Make API request
	CURL *curl = curl_easy_init();

	char *searchTextEscaped = curl_easy_escape(curl, searchText, strlen(searchText));
	std::string url = stringf("%s/x/fetch?search=%s&quantity=%d&page=%d", API_HOST, searchTextEscaped, quantity, page);
	curl_free(searchTextEscaped);

	std::string resText;
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "WaveEdit/" TOSTRING(VERSION));
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_string_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resText);
	CURLcode res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);

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
					bankEntry.uuid = json_string_value(uuidJ);

					json_t *titleJ = json_object_get(entryJ, "title");
					if (!titleJ) continue;
					bankEntry.title = json_string_value(titleJ);

					json_t *attributionJ = json_object_get(entryJ, "attribution");
					if (!attributionJ) continue;
					bankEntry.attribution = json_string_value(attributionJ);

					json_t *notesJ = json_object_get(entryJ, "notes");
					if (!notesJ) continue;
					bankEntry.notes = json_string_value(notesJ);

					json_t *datestampJ = json_object_get(entryJ, "datestamp");
					if (!datestampJ) continue;
					bankEntry.datestamp = json_number_value(datestampJ);

					json_t *wavfilebase64J = json_object_get(entryJ, "wavfilebase64");
					if (!wavfilebase64J) continue;
					const char *wavefilebase64 = json_string_value(wavfilebase64J);
					int16_t samples[BANK_LEN * WAVE_LEN] = {};
					decodeBase64(wavefilebase64, samples, sizeof(samples));
					i16_to_f32(samples, bankEntry.samples, BANK_LEN * WAVE_LEN);

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

		ImGui::TextWrapped("%s", "By sharing the currently loaded wavetable bank to WaveEdit Online, you agree to release this work under the CC0 public domain license.");
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
	static bool dirty = true;
	// Refresh if needed
	if (dirty) {
		requestThread = std::thread(refresh);
		requestThread.detach();
		dirty = false;
	}

	ImGui::BeginChild("DB Page", ImVec2(0, 0), true);
	{
		// Search box
		ImGui::PushItemWidth(300.0);
		if (ImGui::InputText("##search", searchText, sizeof(searchText), ImGuiInputTextFlags_EnterReturnsTrue)) {
			page = 0;
			dirty = true;
		}
		ImGui::PopItemWidth();
		ImGui::SameLine();
		if (ImGui::Button("Search")) {
			page = 0;
			dirty = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Reset Search")) {
			page = 0;
			strcpy(searchText, "");
			dirty = true;
		}

		// ImGui::SameLine();
		// ImGui::PushItemWidth(140.0);
		// ImGui::Combo("##sortby", &sort, sortItems, 2);
		// ImGui::PopItemWidth();
		ImGui::SameLine();
		if (ImGui::Button("Upload Current Bank")) showUploadPopup = true;

		ImGui::Dummy(ImVec2(10, 20));

		// Waves
		if (requestRunning) {
			ImGui::Text("Loading...");
		}
		else {
			for (const BankEntry &bankEntry : bankEntries) {
				renderWave("", 140.0, NULL, 0, bankEntry.samples, BANK_LEN * WAVE_LEN, NO_TOOL);
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
					currentBank.setSamples(bankEntry.samples);
				}
				ImGui::EndGroup();
				ImGui::Spacing();
			}
			if (bankEntries.empty()) {
				ImGui::Text("Nothing!");
			}
		}

		// Page selector
		int oldPage = page;
		ImGui::Dummy(ImVec2(10, 20));
		if (ImGui::Button("<<")) page = 0;
		ImGui::SameLine();
		ImGui::PushItemWidth(120.0);
		int pageOne = page + 1;
		ImGui::InputInt("##page", &pageOne, 1, 10);
		page = pageOne - 1;
		ImGui::PopItemWidth();

		if (oldPage != page) {
			ImGui::SetScrollX(0);
			ImGui::SetScrollY(0);
			dirty = true;
		}
	}
	ImGui::EndChild();

	uploadPopup();
}
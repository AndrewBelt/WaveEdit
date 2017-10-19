#include "WaveEdit.hpp"

#include <thread>
#include <curl/curl.h>
#include <jansson.h>
#include "imgui.h"


static const char *api_host = "http://waveeditonline.com";


struct BankEntry {
	std::string uuid;
	std::string title;
	std::string attribution;
	std::string notes;
	double datestamp;
	float samples[BANK_LEN * WAVE_LEN];
	bool loaded;
};

std::vector<BankEntry> bankEntries;


// Probably should be atomic integer
static int activeRequests = 0;
static bool showUploadPopup = false;
static bool showUploadedPopup = false;
static char sekret[128] = "";
static bool quarantine = false;

static char searchText[128] = "";
static const int quantity = 10;
static int page = 0; // zero-indexed
// static const char *sortItems[2] = {
// 	"Downloads",
// 	"Date Uploaded",
// };
// static int sort = 0;



static size_t write_string_callback(void *data, size_t size, size_t nmemb, void *p) {
	std::string &text = *((std::string*)p);
	char *dataStr = (char*) data;
	size_t len = size * nmemb;
	text.append(dataStr, len);
	return len;
}


static CURL *db_curl_easy_init() {
	CURL *curl = curl_easy_init();

	static struct curl_slist *headers = NULL;
	// Lazy create static headers
	if (!headers) {
		headers = curl_slist_append(headers, "Accept: application/json");
		headers = curl_slist_append(headers, "Content-Type: application/json");
		headers = curl_slist_append(headers, "Charsets: utf-8");
	}

	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "WaveEdit/" TOSTRING(VERSION));
	return curl;
}


static void refresh() {
	activeRequests++;

	// Make API request
	CURL *curl = db_curl_easy_init();

	char *searchTextEscaped = curl_easy_escape(curl, searchText, strlen(searchText));
	const char *path = quarantine ? "/x/fetchquarantine" : "/x/fetch";
	std::string url = stringf("%s%s?search=%s&quantity=%d&page=%d", api_host, path, searchTextEscaped, quantity, page);
	curl_free(searchTextEscaped);
	printf("Requesting %s\n", url.c_str());

	std::string resText;
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_string_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resText);
	CURLcode res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);

	if (res == CURLE_OK) {
		// Clear existing bank entries
		bankEntries.clear();

		// Parse JSON response
		json_error_t error;
		json_t *root = json_loads(resText.c_str(), 0, &error);
		if (root) {
			json_t *entriesJ = json_object_get(root, quarantine ? "quarantine" : "entries");
			if (entriesJ) {
				size_t entryId;
				json_t *entryJ;
				json_array_foreach(entriesJ, entryId, entryJ) {
					BankEntry bankEntry;
					bankEntry.loaded = false;

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
					const char *wavfilebase64 = json_string_value(wavfilebase64J);
					size_t samplesSize;
					int16_t *samples = (int16_t*) base64_decode((unsigned char*) wavfilebase64, strlen(wavfilebase64), &samplesSize);
					if (samplesSize != sizeof(int16_t) * BANK_LEN * WAVE_LEN) {
						free(samples);
						continue;
					}
					i16_to_f32(samples, bankEntry.samples, BANK_LEN * WAVE_LEN);
					free(samples);

					// Add bank entry to list
					bankEntries.push_back(bankEntry);
				}
			}
			json_decref(root);
		}
		else {
			printf("Could not parse JSON\n");
		}
	}
	else {
		printf("Network error: %s\n", curl_easy_strerror(res));
	}

	activeRequests--;
}


enum ValidateId {
	VALIDATE,
	INVALIDATE,
	PURGE
};

/** command 0 is validate, 1 is invalidate, 2 is purge */
static void validate(ValidateId validateId, std::string uuid) {
	// Build JSON request
	json_t *rootJ = json_object();

	json_t *uuidJ = json_string(uuid.c_str());
	json_object_set_new(rootJ, "uuid", uuidJ);

	json_t *sekretJ = json_string(sekret);
	json_object_set_new(rootJ, "sekret", sekretJ);

	char *reqJson = json_dumps(rootJ, JSON_COMPACT);
	json_decref(rootJ);
	if (!reqJson)
		return;

	// Make API request
	CURL *curl = db_curl_easy_init();

	const char *path = "";
	switch (validateId) {
		case VALIDATE: path = "/x/validate"; break;
		case INVALIDATE: path = "/x/invalidate"; break;
		case PURGE: path = "/x/purge"; break;
		default: assert(false); break;
	}
	std::string url = stringf("%s%s", api_host, path);
	printf("Requesting %s\n", url.c_str());

	std::string resText;
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_POST, true);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_string_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resText);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, reqJson);
	CURLcode res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);

	printf("%s\n", resText.c_str());

	free(reqJson);
}


static void uploadedPopup() {
	if (showUploadedPopup) {
		showUploadedPopup = false;
		ImGui::OpenPopup("Successfully Uploaded");
	}

	ImGui::SetNextWindowContentWidth(300.0);

	if (ImGui::BeginPopupModal("Successfully Uploaded", NULL, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
		ImGui::TextWrapped("%s", "Bank successfully uploaded! It will be added to the library after moderator approval.");

		if (ImGui::Button("OK"))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
}


static void upload(std::string title, std::string attribution, std::string notes) {
	// Build JSON request
	json_t *rootJ = json_object();

	json_t *titleJ = json_string(title.c_str());
	json_object_set_new(rootJ, "title", titleJ);

	json_t *attributionJ = json_string(attribution.c_str());
	json_object_set_new(rootJ, "attribution", attributionJ);

	json_t *notesJ = json_string(notes.c_str());
	json_object_set_new(rootJ, "notes", notesJ);

	// Get i16 samples
	float *samples = new float[BANK_LEN * WAVE_LEN];
	currentBank.getPostSamples(samples);
	int16_t *samples_i16 = new int16_t[BANK_LEN * WAVE_LEN];
	f32_to_i16(samples, samples_i16, BANK_LEN * WAVE_LEN);
	delete[] samples;

	// Get base64 samples
	int samplesSize = sizeof(int16_t) * BANK_LEN * WAVE_LEN;
	size_t wavfilebase64Size;
	char *wavfilebase64 = (char*) base64_encode((unsigned char*) samples_i16, samplesSize, &wavfilebase64Size);
	delete[] samples_i16;

	json_t *wavfilebase64J = json_string(wavfilebase64);
	json_object_set_new(rootJ, "wavfilebase64", wavfilebase64J);
	free(wavfilebase64);

	char *reqJson = json_dumps(rootJ, JSON_COMPACT);
	json_decref(rootJ);
	if (!reqJson)
		return;

	// Make API request
	CURL *curl = db_curl_easy_init();

	std::string url = stringf("%s/x/upload", api_host);
	std::string resText;
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_POST, true);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_string_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resText);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, reqJson);
	CURLcode res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);

	free(reqJson);

	// Process response
	json_t *resJ = json_loads(resText.c_str(), 0, NULL);
	if (!resJ)
		return;

	json_t *successJ = json_object_get(resJ, "success");
	bool success = json_is_true(successJ);
	if (success) {
		showUploadedPopup = true;
	}
	else {
		printf("%s\n", resText.c_str());
	}

	json_decref(resJ);
}


static void uploadPopup() {
	static char title[128];
	static char attribution[128];
	static char notes[1024];

	if (showUploadPopup) {
		showUploadPopup = false;
		ImGui::OpenPopup("Upload");
	}

	ImGui::SetNextWindowContentWidth(600.0);
	if (ImGui::BeginPopupModal("Upload", NULL, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
		ImGui::PushItemWidth(-140.0);
		ImGui::InputText("Title (required)", title, sizeof(title));
		ImGui::InputText("Author (required)", attribution, sizeof(attribution));
		ImGui::InputTextMultiline("Notes", notes, sizeof(notes));

		ImGui::TextWrapped("%s", "By sharing the currently loaded wavetable bank to WaveEdit Online, you agree to release this work under the CC0 public domain license.");
		if (ImGui::SmallButton("https://creativecommons.org/publicdomain/zero/1.0/"))
			openBrowser("https://creativecommons.org/publicdomain/zero/1.0/");

		if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();

		ImGui::SameLine();
		bool uploadable = (strlen(title) > 0 && strlen(attribution) > 0);
		if (!uploadable)
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5);
		if (ImGui::Button("Upload")) {
			if (uploadable) {
				std::thread uploadThread(upload, std::string(title), std::string(attribution), std::string(notes));
				uploadThread.detach();
				ImGui::CloseCurrentPopup();
			}
		}
		if (!uploadable)
			ImGui::PopStyleVar();

		ImGui::EndPopup();
	}
}

void dbPage() {
	static bool dirty = true;
	// Refresh if needed
	if (dirty) {
		std::thread requestThread(refresh);
		requestThread.detach();
		dirty = false;
		bankEntries.clear();
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
		if (sekret[0]) {
			ImGui::SameLine();
			if (ImGui::Checkbox("Quarantine", &quarantine)) {
				dirty = true;
			}
		}

		// ImGui::SameLine();
		// ImGui::PushItemWidth(140.0);
		// ImGui::Combo("##sortby", &sort, sortItems, 2);
		// ImGui::PopItemWidth();
		ImGui::SameLine();
		if (ImGui::Button("Upload Current Bank")) showUploadPopup = true;

		ImGui::Dummy(ImVec2(10, 20));

		// Waves
		if (activeRequests > 0) {
			ImGui::Text("Loading...");
		}
		else {
			for (BankEntry &bankEntry : bankEntries) {
				ImGui::PushID(bankEntry.uuid.c_str());
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
				if (ImGui::Button(bankEntry.loaded ? "Loaded bank!" : "Load Bank")) {
					// Make all bankEntries unloaded
					for (BankEntry &bankEntryOther : bankEntries) {
						bankEntryOther.loaded = false;
					}
					bankEntry.loaded = true;
					currentBank.clear();
					currentBank.setSamples(bankEntry.samples);
					historyPush();
				}
				if (sekret[0]) {
					if (quarantine) {
						ImGui::SameLine();
						if (ImGui::Button("Validate")) {
							validate(VALIDATE, bankEntry.uuid);
							dirty = true;
						}
						ImGui::SameLine();
						if (ImGui::Button("Purge")) {
							validate(PURGE, bankEntry.uuid);
							dirty = true;
						}
					}
					else {
						ImGui::SameLine();
						if (ImGui::Button("Invalidate")) {
							validate(INVALIDATE, bankEntry.uuid);
							dirty = true;
						}
					}
				}
				ImGui::EndGroup();
				ImGui::Spacing();
				ImGui::PopID();
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
		page = maxi(0, page);
		ImGui::PopItemWidth();

		if (oldPage != page) {
			ImGui::SetScrollX(0);
			ImGui::SetScrollY(0);
			dirty = true;
		}
	}
	ImGui::EndChild();

	uploadPopup();
	uploadedPopup();
}


void dbInit() {
	// Load sekret string
	FILE *sekretFile = fopen("sekret.txt", "r");
	if (sekretFile) {
		fscanf(sekretFile, "%127s", sekret);
		fclose(sekretFile);
	}
}

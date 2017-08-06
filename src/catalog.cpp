#include "WaveEdit.hpp"

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>


std::vector<CatalogCategory> catalogCategories;


int alphaEntryComp(const void *a, const void *b) {
	const struct dirent *ad = (const struct dirent *) a;
	const struct dirent *bd = (const struct dirent *) b;
	return strcmp(ad->d_name, bd->d_name);
}

/** Writes entries of directory to `entries` array of length `len`
Sorts alphabetically, omits entries beginning with "."
Returns number of entries read
*/
static int dirEntries(DIR *dir, struct dirent *entries, int len) {
	if (!dir)
		return 0;

	int i = 0;
	while (i < len) {
		struct dirent *entry = readdir(dir);
		if (!entry)
			break;
		// Omit entries beginning with "."
		if (entry->d_name[0] == '.')
			continue;
		entries[i] = *entry;
		i++;
	}

	// Sort entries
	qsort(entries, i, sizeof(struct dirent), alphaEntryComp);
	return i;
}


void catalogInit() {
	static const char *rootPath = "catalog";
	DIR *rootDir = opendir(rootPath);
	struct dirent categoryEntries[128];
	int categoriesLength = dirEntries(rootDir, categoryEntries, 128);

	for (int i = 0; i < categoriesLength; i++) {
		char categoryPath[PATH_MAX];
		snprintf(categoryPath, sizeof(categoryPath), "%s/%s", rootPath, categoryEntries[i].d_name);

		// Directories only
		struct stat categoryStat;
		stat(categoryPath, &categoryStat);
		if (!S_ISDIR(categoryStat.st_mode))
			continue;

		// Skip digits at beginning of filename
		// e.g. "00Digital" -> "Digital"
		const char *name = categoryEntries[i].d_name;
		while (isdigit(*name))
			name++;

		CatalogCategory catalogCategory;
		catalogCategory.name = name;

		DIR *categoryDir = opendir(categoryPath);
		struct dirent fileEntries[128];
		int filesLength = dirEntries(categoryDir, fileEntries, 128);

		for (int j = 0; j < filesLength; j++) {
			char filePath[PATH_MAX];
			snprintf(filePath, sizeof(filePath), "%s/%s", categoryPath, fileEntries[j].d_name);

			// Regular files only
			// Directories only
			struct stat fileStat;
			stat(filePath, &fileStat);
			if (!S_ISREG(fileStat.st_mode))
				continue;

			// Get the name without digits at the beginning
			const char *name = fileEntries[j].d_name;
			while (isdigit(*name))
				name++;

			// Find first period
			const char *period = name;
			while (*period != '\0' && *period != '.')
				period++;

			CatalogFile catalogFile;
			catalogFile.name = std::string(name, period - name);

			int length;
			float *samples = loadAudio(filePath, &length);
			if (samples) {
				if (length == WAVE_LEN) {
					memcpy(catalogFile.samples, samples, sizeof(float) * WAVE_LEN);
					catalogCategory.files.push_back(catalogFile);
				}
				else {
					printf("%s has length %d but needs %d\n", filePath, length, WAVE_LEN);
				}
				delete[] samples;
			}
		}

		catalogCategories.push_back(catalogCategory);
		closedir(categoryDir);
	}

	closedir(rootDir);
}

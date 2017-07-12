#include "WaveEdit.hpp"

#include <string.h>
#include <dirent.h>


std::vector<CatalogDirectory> catalogDirectories;


#ifndef ARCH_WIN
static int filterCallback(const struct dirent *dir) {
	// Filter files beginning with "."
	if (dir->d_name[0] == '.')
		return 0;
	return 1;
}
#endif

void catalogInit() {
#ifndef ARCH_WIN
	struct dirent **directories;
	const char *catalogDir = "waves";
	int directoriesLen = scandir(catalogDir, &directories, filterCallback, NULL);

	for (int i = 0; i < directoriesLen; i++) {
		// Skip digits at beginning of filename
		// e.g. "00Digital" -> "Digital"
		const char *name = directories[i]->d_name;
		while (isdigit(*name))
			name++;

		CatalogDirectory catalogDirectory;
		catalogDirectory.name = name;

		struct dirent **files;
		char directoryPath[PATH_MAX];
		snprintf(directoryPath, sizeof(directoryPath), "%s/%s", catalogDir, directories[i]->d_name);
		int filesLen = scandir(directoryPath, &files, filterCallback, NULL);
		for (int i = 0; i < filesLen; i++) {
			// Get the name without digits at the beginning
			const char *name = files[i]->d_name;
			while (isdigit(*name))
				name++;

			// Find last period
			const char *period = name;
			while (*period != '\0' && *period != '.')
				period++;

			CatalogFile catalogFile;
			catalogFile.name = std::string(name, period - name);
			char wavePath[PATH_MAX];
			snprintf(wavePath, sizeof(wavePath), "%s/%s", directoryPath, files[i]->d_name);

			int length;
			float *samples = loadAudio(wavePath, &length);
			if (samples) {
				if (length == WAVE_LEN) {
					memcpy(catalogFile.samples, samples, sizeof(float) * WAVE_LEN);
					catalogDirectory.catalogFiles.push_back(catalogFile);
				}
				delete[] samples;
			}
			free(files[i]);
		}
		free(files);

		catalogDirectories.push_back(catalogDirectory);
		free(directories[i]);
	}
	free(directories);
#endif
}

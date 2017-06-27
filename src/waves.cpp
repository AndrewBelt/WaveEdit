#include "WaveEditor.hpp"

#include <dirent.h>


std::vector<WaveDirectory> waveDirectories;


static int filterCallback(const struct dirent *dir) {
	if (dir->d_name[0] == '.')
		return 0;
	return 1;
}

void wavesInit() {
	/*
	struct dirent **directories;
	const char *wavesDir = "waves";
	int directoriesLen = scandir(wavesDir, &directories, filterCallback, alphasort);

	for (int i = 0; i < directoriesLen; i++) {
		// Skip digits at beginning of filename
		// e.g. "00Digital" -> "Digital"
		const char *name = directories[i]->d_name;
		while (isdigit(*name))
			name++;

		WaveDirectory waveDirectory;
		waveDirectory.name = name;

		struct dirent **files;
		char directoryPath[PATH_MAX];
		snprintf(directoryPath, sizeof(directoryPath), "%s/%s", wavesDir, directories[i]->d_name);
		int filesLen = scandir(directoryPath, &files, filterCallback, alphasort);
		for (int i = 0; i < filesLen; i++) {
			const char *name = files[i]->d_name;
			while (isdigit(*name))
				name++;

			// Find last period
			const char *period = name;
			while (*period != '\0' && *period != '.')
				period++;

			WaveFile waveFile;
			waveFile.name = std::string(name, period - name);
			char wavePath[PATH_MAX];
			snprintf(wavePath, sizeof(wavePath), "%s/%s", directoryPath, files[i]->d_name);

			loadWave(wavePath, waveFile.samples);
			waveDirectory.waveFiles.push_back(waveFile);
			free(files[i]);
		}
		free(files);

		waveDirectories.push_back(waveDirectory);
		free(directories[i]);
	}
	free(directories);
	*/
}

#include "WaveEditor.hpp"


void openBrowser(const char *url) {
	// shell injection is possible if the URL is not trusted
#if defined(__linux__)
	char command[1024];
	snprintf(command, sizeof(command), "xdg-open %s", url);
	system(command);
#endif
#if defined(__APPLE__)
	char command[1024];
	snprintf(command, sizeof(command), "open %s", url);
	system(command);
#endif
#if defined(_WIN32)
	ShellExecute(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
#endif
}

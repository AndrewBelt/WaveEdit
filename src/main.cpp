#include "WaveEdit.hpp"

#include <time.h>

#include <SDL.h>
#include <SDL_opengl.h>

#include "imconfig.h"
#include "imgui.h"
#include "imgui/examples/sdl_opengl2_example/imgui_impl_sdl.h"


#ifdef ARCH_MAC

#include <unistd.h> // for chdir
#include <libgen.h> // for dirname
#include <mach-o/dyld.h> // for _NSGetExecutablePath
#include <limits.h> // for PATH_MAX?

void fixWorkingDirectory() {
	char path[PATH_MAX];
	uint32_t pathLen = sizeof(path);
	int err = _NSGetExecutablePath(path, &pathLen);
	assert(!err);

	// Switch to the directory of the actual binary
	chdir(dirname(path));
	// This will fail silently if there is no ../Resources directory.
	// Super hacky, but it allows the binary to be run from an app bundle or from the command line.
	chdir("../Resources");

	char cwd[PATH_MAX];
	getcwd(cwd, sizeof(cwd));
	printf("Working directory is %s\n", cwd);
}

#endif


#ifdef ARCH_LIN
__asm__(".symver realpath,realpath@GLIBC_2.2.5");
#endif


int main(int argc, char **argv) {
	srand(time(NULL));

#ifdef ARCH_MAC
	fixWorkingDirectory();
#endif

	// Set up SDL
	int err = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
	assert(!err);

	// Set up window
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
	SDL_DisplayMode current;
	SDL_GetCurrentDisplayMode(0, &current);
	SDL_Window *window = SDL_CreateWindow("",
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		1024, 768,
		SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
	assert(window);
	SDL_SetWindowMinimumSize(window, 800, 600);
	SDL_GLContext glContext = SDL_GL_CreateContext(window);
	// Disable V-Sync
	// SDL_GL_SetSwapInterval(0);
	// Enable V-Sync
	SDL_GL_SetSwapInterval(1);

	// Set up Imgui binding
	ImGui_ImplSdlGL2_Init(window);

	// Initialize modules
	uiInit();
	historyClear();
	currentBank.load("autosave.dat");
	historyPush();
	catalogInit();
	audioInit();

	// Main loop
	bool running = true;
	while (running) {
		// Scan events
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			ImGui_ImplSdlGL2_ProcessEvent(&event);
			if (event.type == SDL_QUIT) {
				running = false;
			}
		}

		// Set title
		const char *title = SDL_GetWindowTitle(window);
		char newTitle[1024];
		if (lastFilename[0]) {
			char *lastBasenameP;
#if defined(ARCH_WIN)
			lastBasenameP = lastFilename;
#else
			char lastBasename[1024];
			snprintf(lastBasename, sizeof(lastBasename), "%s", lastFilename);
			lastBasenameP = basename(lastBasename);
#endif
			snprintf(newTitle, sizeof(newTitle), "Synthesis Technology WaveEdit - %s", lastBasenameP);
		}
		else {
			snprintf(newTitle, sizeof(newTitle), "Synthesis Technology WaveEdit");
		}
		if (strcmp(title, newTitle) != 0) {
			SDL_SetWindowTitle(window, newTitle);
		}

		ImGui_ImplSdlGL2_NewFrame(window);
		// Only render if window is visible
		Uint32 flags = SDL_GetWindowFlags(window);
		if ((flags & SDL_WINDOW_SHOWN) && !(flags & SDL_WINDOW_MINIMIZED)) {
			// Build render buffer
			uiRender();
		}

		// Render frame
		glViewport(0, 0, (int)ImGui::GetIO().DisplaySize.x, (int)ImGui::GetIO().DisplaySize.y);

		glClearColor(0.0, 0.0, 0.0, 1.0);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui::Render();
		SDL_GL_SwapWindow(window);
	}

	currentBank.save("autosave.dat");

	// Cleanup
	uiDestroy();
	ImGui_ImplSdlGL2_Shutdown();
	SDL_GL_DeleteContext(glContext);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}

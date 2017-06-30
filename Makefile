VERSION = v0.3.1

CPPFLAGS = -Wall -Wno-unused -O2 \
	-DVERSION=$(VERSION) \
	-I. -Iimgui -Inoc \
	$(shell pkg-config --cflags --static sdl2) \
	$(shell pkg-config --cflags samplerate)
CXXFLAGS = -std=c++11
LDFLAGS =


# OS-specific
MACHINE = $(shell gcc -dumpmachine)
ifneq (,$(findstring linux,$(MACHINE)))
	# Linux
	ARCH = lin
	CPPFLAGS += -DNOC_FILE_DIALOG_GTK
	CPPFLAGS += $(shell pkg-config --cflags gtk+-2.0)
	LDFLAGS += -lGL -lpthread \
		$(shell pkg-config --libs sdl2) \
		$(shell pkg-config --libs samplerate) \
		-lgtk-x11-2.0 -lgobject-2.0
else ifneq (,$(findstring apple,$(MACHINE)))
	# Mac
	ARCH = mac
	CPPFLAGS += -DNOC_FILE_DIALOG_OSX
	SOURCES_M = $(wildcard src/*.m)
	LDFLAGS += -stdlib=libc++ -lpthread -framework Cocoa -framework OpenGL -framework IOKit -framework CoreVideo \
		$(shell pkg-config --libs sdl2) \
		$(shell pkg-config --libs samplerate)
else ifneq (,$(findstring mingw,$(MACHINE)))
	# Windows
	ARCH = win
	CPPFLAGS += -D_USE_MATH_DEFINES -DNOC_FILE_DIALOG_WIN32
	LDFLAGS += \
		$(shell pkg-config --libs samplerate) \
		$(shell pkg-config --libs sdl2) \
		-lopengl32 -mwindows
else
	$(error Could not determine machine type. Try hacking around in the Makefile)
endif


SOURCES_C = \
	lodepng/lodepng.c \
	pffft/pffft.c

SOURCES_CXX = $(wildcard src/*.cpp) \
	imgui/imgui.cpp \
	imgui/imgui_draw.cpp \
	imgui/imgui_demo.cpp \
	imgui/examples/sdl_opengl2_example/imgui_impl_sdl.cpp \

OBJECTS = $(SOURCES_C:.c=.o) $(SOURCES_CXX:.cpp=.o) $(SOURCES_M:.m=.o)


all: WaveEditor

run: all
	./WaveEditor

debug: all
	gdb -ex 'run' ./WaveEditor

WaveEditor: $(OBJECTS)
	$(CXX) -o $@ $^ $(LDFLAGS)

clean:
	rm -frv $(OBJECTS) WaveEditor dist

.PHONY: dist
dist: WaveEditor
	mkdir -p dist/WaveEditor
	cp -R WaveEditor logo* LICENSE* fonts banks waves dist/WaveEditor
ifeq ($(ARCH),lin)
	cp WaveEditor.sh dist/WaveEditor
	cp /usr/lib/libSDL2-2.0.so.0 dist/WaveEditor
	cp /usr/lib/libsamplerate.so.0 dist/WaveEditor
else ifeq ($(ARCH),win)
	cp /mingw64/bin/libgcc_s_seh-1.dll dist/WaveEditor
	cp /mingw64/bin/libsamplerate-0.dll dist/WaveEditor
	cp /mingw64/bin/libstdc++-6.dll dist/WaveEditor
	cp /mingw64/bin/libwinpthread-1.dll dist/WaveEditor
	cp /mingw64/bin/SDL2.dll dist/WaveEditor
endif
	cd dist && zip -9 -r WaveEditor_$(VERSION)_$(ARCH).zip WaveEditor


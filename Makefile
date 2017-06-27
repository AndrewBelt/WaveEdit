FLAGS = -Wall -Wno-unused -O2 \
	-I. -Iimgui \
	$(shell pkg-config --cflags sdl2) \
	$(shell pkg-config --cflags samplerate)
CXXFLAGS = $(FLAGS) -std=c++11
LDFLAGS =


# OS-specific
MACHINE = $(shell gcc -dumpmachine)
ifneq (,$(findstring linux,$(MACHINE)))
	CXXFLAGS += -DNOC_FILE_DIALOG_GTK
	CXXFLAGS += $(shell pkg-config --cflags gtk+-2.0)
	LDFLAGS += -lGL $(shell pkg-config --libs gtk+-2.0) -lpthread \
		$(shell pkg-config --libs sdl2) \
		$(shell pkg-config --libs samplerate)
else ifneq (,$(findstring apple,$(MACHINE)))
else ifneq (,$(findstring mingw,$(MACHINE)))
	CXXFLAGS += -D_USE_MATH_DEFINES -DNOC_FILE_DIALOG_WIN32
	LDFLAGS += \
		$(shell pkg-config --libs samplerate) \
		$(shell pkg-config --libs sdl2) \
		-lopengl32 \
		-mwindows
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

OBJECTS = $(SOURCES_C:.c=.o) $(SOURCES_CXX:.cpp=.o)


all: WaveEditor

.PHONY: dist
dist: WaveEditor
	mkdir -p dist/WaveEditor
	cp *.dll dist/WaveEditor
	cp WaveEditor.exe dist/WaveEditor
	cp logo* dist/WaveEditor
	cp LICENSE* dist/WaveEditor
	cp -R fonts dist/WaveEditor
	#cp -R waves dist/WaveEditor
	cp -R banks dist/WaveEditor
	cd dist && zip -5 -r WaveEditor_v0.3_win64.zip WaveEditor

run: all
	./WaveEditor

debug: all
	gdb -ex 'run' ./WaveEditor

WaveEditor: $(OBJECTS)
	$(CXX) -o $@ $^ $(LDFLAGS)

clean:
	rm -frv $(OBJECTS) WaveEditor dist

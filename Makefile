FLAGS = -Wall -Wno-unused -O2 \
	-I. -Iimgui \
	$(shell pkg-config --cflags sdl2) \
	$(shell pkg-config --cflags samplerate)
CXXFLAGS = $(FLAGS) -std=c++11
LDFLAGS = -lSDL2 -lGL \
	$(shell pkg-config --libs sdl2) \
	$(shell pkg-config --libs samplerate)


# OS-specific
MACHINE = $(shell gcc -dumpmachine)
ifneq (,$(findstring linux,$(MACHINE)))
	CXXFLAGS += -DNOC_FILE_DIALOG_GTK $(shell pkg-config --cflags gtk+-2.0)
	LDFLAGS += $(shell pkg-config --libs gtk+-2.0) -lpthread
else ifneq (,$(findstring apple,$(MACHINE)))
else ifneq (,$(findstring mingw,$(MACHINE)))
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

run: all
	./WaveEditor

debug: all
	gdb -ex 'run' ./WaveEditor

WaveEditor: $(OBJECTS)
	$(CXX) -o $@ $^ $(LDFLAGS)

clean:
	rm -fv $(OBJECTS) WaveEditor

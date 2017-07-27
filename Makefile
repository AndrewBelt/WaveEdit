VERSION = v0.4

FLAGS = -Wall -Wextra -Wno-unused-parameter -g -Wno-unused -O3 -march=core2 -ffast-math \
	-DVERSION=$(VERSION) -DPFFFT_SIMD_DISABLE \
	-I. -Iimgui -Inoc \
	$(shell pkg-config --cflags sdl2) \
	$(shell pkg-config --cflags samplerate) \
	$(shell pkg-config --cflags sndfile) \
	$(shell pkg-config --cflags libcurl) \
	$(shell pkg-config --cflags openssl) \
	$(shell pkg-config --cflags jansson)
CFLAGS =
CXXFLAGS = -std=c++11
LDFLAGS =


SOURCES = \
	pffft/pffft.c \
	lodepng/lodepng.cpp \
	imgui/imgui.cpp \
	imgui/imgui_draw.cpp \
	imgui/imgui_demo.cpp \
	imgui/examples/sdl_opengl2_example/imgui_impl_sdl.cpp \
	$(wildcard src/*.cpp)


# OS-specific
MACHINE = $(shell gcc -dumpmachine)
ifneq (,$(findstring linux,$(MACHINE)))
	# Linux
	ARCH = lin
	FLAGS += -DARCH_LIN $(shell pkg-config --cflags gtk+-2.0)
	LDFLAGS += -lGL -lpthread \
		$(shell pkg-config --libs sdl2) \
		$(shell pkg-config --libs samplerate) \
		$(shell pkg-config --libs sndfile) \
		$(shell pkg-config --libs libcurl) \
		$(shell pkg-config --libs openssl) \
		$(shell pkg-config --libs jansson) \
		-lgtk-x11-2.0 -lgobject-2.0
	SOURCES += src/noc_file_dialog_gtk.c
else ifneq (,$(findstring apple,$(MACHINE)))
	# Mac
	ARCH = mac
	FLAGS += -DARCH_MAC -mmacosx-version-min=10.7
	CXXFLAGS += -stdlib=libc++
	LDFLAGS += -stdlib=libc++ -lpthread -framework Cocoa -framework OpenGL -framework IOKit -framework CoreVideo \
		$(shell pkg-config --libs sdl2) \
		$(shell pkg-config --libs samplerate) \
		$(shell pkg-config --libs sndfile) \
		$(shell pkg-config --libs libcurl) \
		$(shell pkg-config --libs jansson)
	SOURCES += src/noc_file_dialog_osx.m
else ifneq (,$(findstring mingw,$(MACHINE)))
	# Windows
	ARCH = win
	FLAGS += -DARCH_WIN -D_USE_MATH_DEFINES
	LDFLAGS += \
		$(shell pkg-config --libs sdl2) \
		$(shell pkg-config --libs samplerate) \
		$(shell pkg-config --libs sndfile) \
		$(shell pkg-config --libs libcurl) \
		$(shell pkg-config --libs jansson) \
		-lopengl32 -mwindows
	SOURCES += src/noc_file_dialog_win.c
else
	$(error Could not determine machine type. Try hacking around in the Makefile)
endif


OBJECTS = $(SOURCES:%=build/%.o)


all: WaveEdit

run: WaveEdit
	./WaveEdit

debug: WaveEdit
ifeq ($(ARCH),mac)
	lldb ./WaveEdit
else
	gdb -ex 'run' ./WaveEdit
endif

WaveEdit: $(OBJECTS)
	$(CXX) -o $@ $^ $(LDFLAGS)

clean:
	rm -frv $(OBJECTS) WaveEdit dist


DIST_ZIP = WaveEdit_$(VERSION)_$(ARCH).zip

.PHONY: dist
dist: WaveEdit
	mkdir -p dist/WaveEdit
	cp -R banks dist/WaveEdit
	cp LICENSE* dist/WaveEdit
ifeq ($(ARCH),lin)
	cp -R logo* fonts waves dist/WaveEdit
	cp WaveEdit WaveEdit.sh dist/WaveEdit
	cp /usr/lib/libSDL2-2.0.so.0 dist/WaveEdit
	cp /usr/lib/libsamplerate.so.0 dist/WaveEdit
	cp /usr/lib/libsndfile.so.1 dist/WaveEdit
	cp /usr/lib/libFLAC.so.8 dist/WaveEdit
	cp /usr/lib/libogg.so.0 dist/WaveEdit
	cp /usr/lib/libvorbis.so.0 dist/WaveEdit
	cp /usr/lib/libvorbisenc.so.2 dist/WaveEdit
else ifeq ($(ARCH),win)
	cp -R logo* fonts waves dist/WaveEdit
	cp WaveEdit.exe dist/WaveEdit
	cp /mingw64/bin/libgcc_s_seh-1.dll dist/WaveEdit
	cp /mingw64/bin/libsamplerate-0.dll dist/WaveEdit
	cp /mingw64/bin/libsndfile-1.dll dist/WaveEdit
	cp /mingw64/bin/libFLAC-8.dll dist/WaveEdit
	cp /mingw64/bin/libogg-0.dll dist/WaveEdit
	cp /mingw64/bin/libspeex-1.dll dist/WaveEdit
	cp /mingw64/bin/libvorbis-0.dll dist/WaveEdit
	cp /mingw64/bin/libvorbisenc-2.dll dist/WaveEdit
	cp /mingw64/bin/libstdc++-6.dll dist/WaveEdit
	cp /mingw64/bin/libwinpthread-1.dll dist/WaveEdit
	cp /mingw64/bin/SDL2.dll dist/WaveEdit
else ifeq ($(ARCH),mac)
	mkdir -p dist/WaveEdit/WaveEdit.app/Contents/MacOS
	mkdir -p dist/WaveEdit/WaveEdit.app/Contents/Resources
	cp Info.plist dist/WaveEdit/WaveEdit.app/Contents
	cp WaveEdit dist/WaveEdit/WaveEdit.app/Contents/MacOS
	cp -R logo* fonts waves dist/WaveEdit/WaveEdit.app/Contents/Resources
	# Remap dylibs in executable
	otool -L dist/WaveEdit/WaveEdit.app/Contents/MacOS/WaveEdit
	cp /usr/local/opt/sdl2/lib/libSDL2-2.0.0.dylib dist/WaveEdit/WaveEdit.app/Contents/MacOS
	install_name_tool -change /usr/local/opt/sdl2/lib/libSDL2-2.0.0.dylib @executable_path/libSDL2-2.0.0.dylib dist/WaveEdit/WaveEdit.app/Contents/MacOS/WaveEdit
	cp /usr/local/opt/libsamplerate/lib/libsamplerate.0.dylib dist/WaveEdit/WaveEdit.app/Contents/MacOS
	install_name_tool -change /usr/local/opt/libsamplerate/lib/libsamplerate.0.dylib @executable_path/libsamplerate.0.dylib dist/WaveEdit/WaveEdit.app/Contents/MacOS/WaveEdit
	otool -L dist/WaveEdit/WaveEdit.app/Contents/MacOS/WaveEdit
endif
	cd dist && zip -9 -r $(DIST_ZIP) WaveEdit


# SUFFIXES:

build/%.c.o: %.c
	@mkdir -p $(@D)
	$(CC) $(FLAGS) $(CFLAGS) -c -o $@ $<

build/%.cpp.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(FLAGS) $(CXXFLAGS) -c -o $@ $<

build/%.m.o: %.m
	@mkdir -p $(@D)
	$(CC) $(FLAGS) $(CFLAGS) -c -o $@ $<

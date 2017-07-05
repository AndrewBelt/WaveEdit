# Synthesis Technology WaveEdit

The wavetable and bank editor for the Synthesis Technology [E370](http://synthtech.com/eurorack/E370/) and [E352](http://synthtech.com/eurorack/E352/) Eurorack synthesizer modules.

### Building

Install the following dependencies.

- [SDL2](https://www.libsdl.org/)
- [libsamplerate](http://www.mega-nerd.com/SRC/)
- [libsndfile](http://www.mega-nerd.com/libsndfile/)
- pkg-config (build requirement)
- [MSYS2](http://www.msys2.org/) (if using Windows)

Clone the in-source dependencies.

	git submodule update --init

Compile the program. The Makefile will automatically detect your operating system.

	make

Launch the program.

	./WaveEdit

You can even try your luck with building the polished distributable. Although this method is unsupported, it may work with some tweaks to the Makefile.

	make dist

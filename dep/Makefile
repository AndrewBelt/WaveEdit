
LOCAL = $(PWD)

include ../Makefile-arch.inc
ifeq ($(ARCH),mac)
	export CFLAGS = \
		-mmacosx-version-min=10.7
	export CXXFLAGS = $(CFLAGS)
	export LDFLAGS = $(CFLAGS)
endif

sdl2 = SDL2-2.0.7
jansson = jansson-2.10
libsndfile = libsndfile-1.0.28
libsamplerate = libsamplerate-0.1.9
libcurl = curl-7.54.1


# This instance of make should be serialized, but -j flags are passed down to each recursive Makefile
.NOTPARALLEL:

all: $(sdl2) $(jansson) $(libsndfile) $(libsamplerate) $(libcurl)

$(sdl2):
	wget -nc https://www.libsdl.org/release/$@.tar.gz
	tar xf $@.tar.gz
	cd $@ && ./configure --prefix="$(LOCAL)" --enable-input-tslib=no
	$(MAKE) -C $@
	$(MAKE) -C $@ install

$(jansson):
	wget -nc http://www.digip.org/jansson/releases/$@.tar.gz
	tar xf $@.tar.gz
	cd $@ && ./configure --prefix="$(LOCAL)"
	$(MAKE) -C $@
	$(MAKE) -C $@ install

$(libsndfile):
	wget -nc http://www.mega-nerd.com/libsndfile/files/$@.tar.gz
	tar xf $@.tar.gz
	cd $@ && ./configure --prefix="$(LOCAL)"
	$(MAKE) -C $@
	$(MAKE) -C $@ install

$(libsamplerate):
	wget -nc http://www.mega-nerd.com/SRC/$@.tar.gz
	tar xf $@.tar.gz
	cd $@ && ./configure --prefix="$(LOCAL)"
	$(MAKE) -C $@
	$(MAKE) -C $@ install

$(libcurl):
	wget https://curl.haxx.se/download/$@.tar.gz
	tar xf $@.tar.gz
	cd $@ && ./configure --prefix="$(LOCAL)" \
		--disable-ftp --disable-file --disable-ldap --disable-ldaps --disable-rtsp --disable-proxy --disable-dict --disable-telnet --disable-tftp --disable-pop3 --disable-imap --disable-smb --disable-smtp --disable-gopher --disable-manual \
		--without-zlib --without-ssl --without-ca-bundle --without-ca-path --without-ca-fallback --without-libpsl --without-libmetalink --without-libssh2 --without-librtmp --without-winidn --without-libidn2 --without-nghttp2
	$(MAKE) -C $@
	$(MAKE) -C $@ install

clean:
	git clean -fdx
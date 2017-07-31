
LOCAL=$PWD/local
echo $LOCAL


openssl() {
	if [ ! -d openssl-1.0.2l ]; then
		wget https://www.openssl.org/source/openssl-1.0.2l.tar.gz
		tar xf openssl-1.0.2l.tar.gz
	fi
	cd openssl-1.0.2l
	./config shared --prefix="$LOCAL" --openssldir="$LOCAL"
	make -j$(nproc)
	make install
	cd ..
}


libcurl() {
	if [ ! -d curl-7.54.1 ]; then
		wget https://curl.haxx.se/download/curl-7.54.1.tar.gz
		tar xf curl-7.54.1.tar.gz
	fi
	cd curl-7.54.1
	./configure --prefix="$LOCAL" \
		--disable-ftp --disable-file --disable-ldap --disable-ldaps --disable-rtsp --disable-proxy --disable-dict --disable-telnet --disable-tftp --disable-pop3 --disable-imap --disable-smb --disable-smtp --disable-gopher --disable-manual \
		--without-zlib --without-ssl --without-ca-bundle --without-ca-path --without-ca-fallback --without-libpsl --without-libmetalink --without-libssh2 --without-librtmp --without-winidn --without-libidn2 --without-nghttp2
	make -j$(nproc)
	make install
	cd ..
}


# openssl
libcurl

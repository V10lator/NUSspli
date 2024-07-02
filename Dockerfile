FROM devkitpro/devkitppc:20240702
COPY --from=ghcr.io/wiiu-env/libmocha:20240603 /artifacts $DEVKITPRO
COPY --from=ghcr.io/wiiu-env/librpxloader:20240425 /artifacts $DEVKITPRO

ENV DEBIAN_FRONTEND=noninteractive \
 PATH=$DEVKITPPC/bin:$DEVKITPRO/portlibs/wiiu/bin/:$PATH \
 WUT_ROOT=$DEVKITPRO/wut \
 CC=$DEVKITPPC/bin/powerpc-eabi-gcc \
 CXX=$DEVKITPPC/bin/powerpc-eabi-g++ \
 AR=$DEVKITPPC/bin/powerpc-eabi-ar \
 RANLIB=$DEVKITPPC/bin/powerpc-eabi-ranlib \
 PKG_CONFIG=$DEVKITPRO/portlibs/wiiu/bin/powerpc-eabi-pkg-config \
 CFLAGS="-mcpu=750 -meabi -mhard-float -Ofast -ffunction-sections -fdata-sections" \
 CXXFLAGS="-mcpu=750 -meabi -mhard-float -Ofast -ffunction-sections -fdata-sections" \
 CPPFLAGS="-D__WIIU__ -D__WUT__ -I$DEVKITPRO/wut/include -L$DEVKITPRO/wut/lib" \
 LDFLAGS="-L$DEVKITPRO/wut/lib" \
 LIBS="-lwut -lm" \
 BROTLI_VER=1.1.0 \
 CURL_VER=8.8.0 \
 NGHTTP2_VER=1.62.0

WORKDIR /

# Upgrade the systen
RUN mkdir -p /usr/share/man/man1 /usr/share/man/man2 && \
 apt-get -y --no-install-recommends update && \
 apt-get -y --no-install-recommends upgrade

# Install the requirements to package the homebrew
RUN apt-get -y install --no-install-recommends autoconf automake libtool openjdk-11-jre-headless python3-pycurl && \
 apt-get clean

# Install nghttp2 for HTTP/2 support (WUT don't include this)
RUN curl -LO https://github.com/nghttp2/nghttp2/releases/download/v$NGHTTP2_VER/nghttp2-$NGHTTP2_VER.tar.xz && \
  mkdir nghttp2 && \
  tar xf nghttp2-$NGHTTP2_VER.tar.xz -C nghttp2/ --strip-components=1 && \
  cd nghttp2 && \
  autoreconf -fi && \
  automake && \
  autoconf && \
  ./configure  \
--enable-lib-only \
--prefix=$DEVKITPRO/portlibs/wiiu/ \
--enable-static \
--disable-threads \
--host=powerpc-eabi && \
  make -j$(nproc) install && \
  cd .. && \
  rm -rf nghttp2 nghttp2-$NGHTTP2_VER.tar.xz

# Install Brotli
RUN git clone --depth 1 --single-branch https://github.com/google/brotli.git && \
 cd brotli && \
 mkdir out && cd out && \
 cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$DEVKITPRO/portlibs/wiiu/ -DBUILD_SHARED_LIBS=OFF -DBROTLI_BUILD_TOOLS=OFF .. && \
 cmake --build . --config Release --target install -j$(nproc) && \
 cd ../.. && \
 rm -rf brotli

# Install libCURL since WUT doesn't ship with the latest version
RUN curl -LO https://curl.se/download/curl-$CURL_VER.tar.xz && \
 curl -LO https://github.com/curl/curl/commit/0c4b4c1e93c8e869af230090f32346fdfd548f21.patch && \
 mkdir /curl && \
 tar xJf curl-$CURL_VER.tar.xz -C /curl --strip-components=1 && \
 cd curl && \
 patch -p1 < ../0c4b4c1e93c8e869af230090f32346fdfd548f21.patch && \
 autoreconf -fi && ./configure \
--prefix=$DEVKITPRO/portlibs/wiiu/ \
--host=powerpc-eabi \
--enable-static \
--disable-threaded-resolver \
--disable-pthreads \
--with-mbedtls=$DEVKITPRO/portlibs/wiiu/ \
--disable-ipv6 \
--disable-unix-sockets \
--disable-socketpair \
--disable-ntlm-wb \
--with-nghttp2=$DEVKITPRO/portlibs/wiiu/ \
--with-brotli=$DEVKITPRO/portlibs/wiiu/ \
--without-libpsl \
--disable-cookies \
--disable-doh \
--disable-form-api \
--disable-http-auth \
--disable-netrc \
--disable-progress-meter \
--disable-ftp \
--disable-file \
--disable-ldap \
--disable-ldaps \
--disable-rtsp \
--disable-dict \
--disable-telnet \
--disable-tftp \
--disable-pop3 \
--disable-imap \
--disable-smb \
--disable-smtp \
--disable-gopher \
--disable-mqtt \
--disable-manual \
--disable-docs && \
 cd lib && \
 make -j$(nproc) install && \
 cd ../include && \
 make -j$(nproc) install && \
 cd ../.. && \
 rm -rf curl curl-$CURL_VER.tar.xz 0c4b4c1e93c8e869af230090f32346fdfd548f21.patch

# Install libSDL since upstream is bugged
RUN curl -LO https://libsdl.org/release/SDL2-2.26.5.tar.gz && \
  curl -LO https://github.com/V10lator/NUSspli/raw/master/sdl.patch && \
  mkdir -p sdl/build && \
  tar xzf SDL2-2.26.5.tar.gz -C /sdl --strip-components=1 && \
  cd sdl && \
  patch -p1 < /sdl.patch && \
  cd build && \
  cmake -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/wut/share/wut.toolchain.cmake -WIIU=1 -DWUT=1 \
  -DCMAKE_BUILD_TYPE="Release" -DCMAKE_INSTALL_PREFIX=$DEVKITPRO/portlibs/wiiu .. && \
  cmake --build . --config Release --target install -j$(nproc) && \
  cd ../.. && \
  rm -rf sdl SDL2-2.26.5.tar.gz sdl.patch

RUN git config --global --add safe.directory /project && \
  git config --global --add safe.directory /project/SDL_FontCache && \
  git config --global --add safe.directory /project/zlib

WORKDIR /project

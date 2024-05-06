FROM ghcr.io/wiiu-env/devkitppc:20240505
COPY --from=ghcr.io/wiiu-env/libmocha:20230621 /artifacts $DEVKITPRO
COPY --from=ghcr.io/wiiu-env/librpxloader:20230621 /artifacts $DEVKITPRO

ENV DEBIAN_FRONTEND=noninteractive \
 PATH=$DEVKITPPC/bin:$PATH \
 WUT_ROOT=$DEVKITPRO/wut \
 BROTLI_VER=1.1.0 \
 CURL_VER=8.6.0 \
 NGHTTP2_VER=1.60.0

WORKDIR /

# Install apt-fast
RUN mkdir -p /usr/share/man/man1 /usr/share/man/man2 && \
 /bin/bash -c "$(curl -sL https://raw.githubusercontent.com/V10lator/NUSspli/master/apt-fast/install.sh)"

# Upgrade the systen
RUN apt-fast -y --no-install-recommends update && \
 apt-fast -y --no-install-recommends upgrade

# Install the requirements to package the homebrew
RUN apt-fast -y install --no-install-recommends autoconf automake libtool openjdk-11-jre-headless python3-pycurl && \
 apt-fast clean

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
--host=powerpc-eabi \
CFLAGS="-mcpu=750 -meabi -mhard-float -Ofast -ffunction-sections -fdata-sections" \
CXXFLAGS="-mcpu=750 -meabi -mhard-float -Ofast -ffunction-sections -fdata-sections" \
CPPFLAGS="-D__WIIU__ -D__WUT__ -I$DEVKITPRO/wut/include" \
LDFLAGS="-L$DEVKITPRO/wut/lib" \
LIBS="-lwut -lm" \
CC=$DEVKITPPC/bin/powerpc-eabi-gcc \
AR=$DEVKITPPC/bin/powerpc-eabi-ar \
RANLIB=$DEVKITPPC/bin/powerpc-eabi-ranlib \
PKG_CONFIG=$DEVKITPRO/portlibs/wiiu/bin/powerpc-eabi-pkg-config && \
  make -j$(nproc) install && \
  cd .. && \
  rm -rf nghttp2 nghttp2-$NGHTTP2_VER.tar.xz

# Install Brotli
RUN git clone --depth 1 --single-branch https://github.com/google/brotli.git && \
 cd brotli && \
 mkdir out && cd out && \
 CC=$DEVKITPPC/bin/powerpc-eabi-gcc \
 AR=$DEVKITPPC/bin/powerpc-eabi-ar \
 RANLIB=$DEVKITPPC/bin/powerpc-eabi-ranlib \
 PKG_CONFIG=$DEVKITPRO/portlibs/wiiu/bin/powerpc-eabi-pkg-config && \
 cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$DEVKITPRO/portlibs/wiiu/ -DBUILD_SHARED_LIBS=OFF -DBROTLI_BUILD_TOOLS=OFF \
 -DCMAKE_C_FLAGS="-mcpu=750 -meabi -mhard-float -Ofast -ffunction-sections -fdata-sections" \
 -DCMAKE_CXX_FLAGS="-mcpu=750 -meabi -mhard-float -Ofast -ffunction-sections -fdata-sections" \
 -DCMAKE_CPP_FLAGS="-L$DEVKITPRO/wut/lib" \
 -DCMAKE_LIBS="-lwut -lm" \
 -DCMAKE_C_COMPILER=$DEVKITPPC/bin/powerpc-eabi-gcc \
 -DCMAKE_CXX_COMPILER=$DEVKITPPC/bin/powerpc-eabi-g++ \
 .. && \
 cmake --build . --config Release --target install && \
 cd ../.. && \
 rm -rf brotli

# Install libCURL since WUT doesn't ship with the latest version
RUN curl -LO https://curl.se/download/curl-$CURL_VER.tar.xz && \
 mkdir /curl && \
 tar xJf curl-$CURL_VER.tar.xz -C /curl --strip-components=1 && \
 cd curl && \
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
--disable-docs \
CFLAGS="-mcpu=750 -meabi -mhard-float -Ofast -ffunction-sections -fdata-sections" \
CXXFLAGS="-mcpu=750 -meabi -mhard-float -Ofast -ffunction-sections -fdata-sections" \
CPPFLAGS="-D__WIIU__ -D__WUT__ -I$DEVKITPRO/wut/include" \
LDFLAGS="-L$DEVKITPRO/wut/lib" \
LIBS="-lwut -lm" \
CC=$DEVKITPPC/bin/powerpc-eabi-gcc \
AR=$DEVKITPPC/bin/powerpc-eabi-ar \
RANLIB=$DEVKITPPC/bin/powerpc-eabi-ranlib \
PKG_CONFIG=$DEVKITPRO/portlibs/wiiu/bin/powerpc-eabi-pkg-config && \
 cd lib && \
 make -j$(nproc) install && \
 cd ../include && \
 make -j$(nproc) install && \
 cd ../.. && \
 rm -rf curl curl-$CURL_VER.tar.xz

RUN git config --global --add safe.directory /project && \
  git config --global --add safe.directory /project/SDL_FontCache && \
  git config --global --add safe.directory /project/zlib

WORKDIR /project

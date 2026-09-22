FROM devkitpro/devkitppc:20260503
COPY --from=ghcr.io/wiiu-env/libmocha:20260331 /artifacts $DEVKITPRO
COPY --from=ghcr.io/wiiu-env/librpxloader:20260329 /artifacts $DEVKITPRO

ENV DEBIAN_FRONTEND=noninteractive \
 PATH=$DEVKITPPC/bin:$DEVKITPRO/portlibs/wiiu/bin/:$PATH \
 WUT_ROOT=$DEVKITPRO/wut \
 CC=$DEVKITPPC/bin/powerpc-eabi-gcc \
 CXX=$DEVKITPPC/bin/powerpc-eabi-g++ \
 AR=$DEVKITPPC/bin/powerpc-eabi-ar \
 RANLIB=$DEVKITPPC/bin/powerpc-eabi-ranlib \
 PKG_CONFIG=$DEVKITPRO/portlibs/wiiu/bin/powerpc-eabi-pkg-config \
 CFLAGS="-mcpu=750 -meabi -mhard-float -O3 -ffast-math -pipe -fipa-pta -ffunction-sections -fdata-sections -D__WIIU__ -D__WUT__ -DIOAPI_NO_64 -D__unix__" \
 CXXFLAGS="-mcpu=750 -meabi -mhard-float -O3 -ffast-math -pipe -fipa-pta -ffunction-sections -fdata-sections -D__WIIU__ -D__WUT__ -DIOAPI_NO_64 -D__unix__" \
 CPPFLAGS="-D__WIIU__ -D__WUT__ -I$DEVKITPRO/wut/include -L$DEVKITPRO/wut/lib" \
 LDFLAGS="-L$DEVKITPRO/wut/lib" \
 LIBS="-lwut -lm" \
 MBEDTLS_VER=3.6.7 \
 BROTLI_VER=1.2.0 \
 CURL_VER=8.22.0 \
 NGHTTP2_VER=1.70.0

WORKDIR /

# Upgrade the systen
RUN mkdir -p /usr/share/man/man1 /usr/share/man/man2 && \
 apt-get -y --no-install-recommends update && \
 apt-get -y --no-install-recommends upgrade

# Install the requirements to package the homebrew
RUN apt-get -y install --no-install-recommends autoconf automake libtool openjdk-17-jre-headless python3-pycurl && \
 apt-get clean

# Install mbedTLS since WUT ships an outdated version. This also makes it much faster:
# The WUT patch reseeds libc's global rand() state via srand() on every single byte — if mbedtls_hardware_poll gets called in a tight loop, OSGetSystemTick() can return the same value across several iterations, meaning several consecutive output bytes come from the same freshly-reseeded rand() stream — correlated, weak output. It also stomps on libc's global rand() state, which is a shared resource any other code could be relying on.
# NUSrng has none of these problems — one accumulating entropy pool, already exercised by the rest of the app, no interference with libc's RNG.
COPY mbedtls.patch /mbedtls.patch
RUN curl -LO https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-$MBEDTLS_VER/mbedtls-$MBEDTLS_VER.tar.bz2 && \
 mkdir mbedtls && \
 tar xjf mbedtls-$MBEDTLS_VER.tar.bz2 -C mbedtls --strip-components=1 && \
 cd mbedtls && \
 patch -p1 < /mbedtls.patch && \
 python3 scripts/config.py unset MBEDTLS_HAVE_ASM && \
 python3 scripts/config.py set MBEDTLS_ENTROPY_HARDWARE_ALT && \
 python3 scripts/config.py set MBEDTLS_NO_PLATFORM_ENTROPY && \
 python3 scripts/config.py unset MBEDTLS_SELF_TEST && \
 python3 scripts/config.py unset MBEDTLS_NET_C && \
 mkdir out && cd out && \
 cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$DEVKITPRO/portlibs/wiiu/ \
    -DUSE_STATIC_MBEDTLS_LIBRARY=ON -DUSE_SHARED_MBEDTLS_LIBRARY=OFF \
    -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=OFF -DMBEDTLS_FATAL_WARNINGS=OFF .. && \
 cmake --build . --config Release --target install -j$(nproc) && \
 cd ../.. && \
 rm -rf mbedtls mbedtls-$MBEDTLS_VER.tar.bz2 /mbedtls.patch

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
--disable-shared \
--without-pic \
--disable-threads \
--host=powerpc-eabi && \
  make -j$(nproc) install && \
  cd .. && \
  rm -rf nghttp2 nghttp2-$NGHTTP2_VER.tar.xz

# Install Brotli
RUN git clone --depth 1 --branch v$BROTLI_VER --single-branch --recurse-submodules -j$(nproc) https://github.com/google/brotli.git && \
 cd brotli && \
 sed -i 's/POSITION_INDEPENDENT_CODE TRUE/POSITION_INDEPENDENT_CODE FALSE/' CMakeLists.txt && \
 mkdir out && cd out && \
 cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$DEVKITPRO/portlibs/wiiu/ -DBUILD_SHARED_LIBS=OFF -DBROTLI_BUILD_TOOLS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=OFF .. && \
 cmake --build . --config Release --target install -j$(nproc) && \
 cd ../.. && \
 rm -rf brotli

# Install libCURL since WUT doesn't ship with the latest version
COPY curl.patch /curl.patch
RUN curl -kLO https://curl.se/download/curl-$CURL_VER.tar.xz && \
 mkdir /curl && \
 tar xJf curl-$CURL_VER.tar.xz -C /curl --strip-components=1 && \
 cd curl && \
 patch -p1 < /curl.patch && \
 autoreconf -fi && \
 curl_cv_mbedtls_version_ok=yes ac_cv_lib_mbedtls_mbedtls_ssl_init=yes ./configure \
--prefix=$DEVKITPRO/portlibs/wiiu/ \
--host=powerpc-eabi \
--enable-static \
--disable-shared \
--without-pic \
--disable-threaded-resolver \
--disable-pthreads \
--with-mbedtls=$DEVKITPRO/portlibs/wiiu/ \
--disable-ipv6 \
--disable-unix-sockets \
--disable-socketpair \
--disable-ntlm-wb \
--with-nghttp2=$DEVKITPRO/portlibs/wiiu/ \
--with-brotli=$DEVKITPRO/portlibs/wiiu/ \
--with-zstd=$DEVKITPRO/portlibs/wiiu/ \
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
 rm -rf curl curl-$CURL_VER.tar.xz /curl.patch

RUN git config --global --add safe.directory /project && \
  git config --global --add safe.directory /project/SDL_FontCache && \
  git config --global --add safe.directory /project/zlib

WORKDIR /project

FROM ghcr.io/wiiu-env/devkitppc:20230326

ENV DEBIAN_FRONTEND=noninteractive \
 PATH=$DEVKITPPC/bin:$PATH \
 WUT_ROOT=$DEVKITPRO/wut \
 CURL_VER=8.0.1

WORKDIR /

# Install apt-fast
RUN mkdir -p /usr/share/man/man1 /usr/share/man/man2 && \
 /bin/bash -c "$(curl -sL https://raw.githubusercontent.com/V10lator/NUSspli/master/apt-fast/install.sh)"

# Upgrade the systen
RUN apt-fast -y --no-install-recommends update && \
 apt-fast -y --no-install-recommends upgrade

# Install the requirements to package the homebrew
RUN apt-fast -y install --no-install-recommends autoconf automake libtool openjdk-11-jre-headless && \
 apt-fast clean

# Install the requirements to build the homebrew
RUN git clone --recursive https://github.com/yawut/libromfs-wiiu --single-branch && \
 cd libromfs-wiiu && \
 make -j$(nproc) && \
 make install

# Install libCURL since WUT doesn't ship with the latest version
RUN wget https://curl.se/download/curl-$CURL_VER.tar.gz && \
 mkdir /curl && \
 tar xf curl-$CURL_VER.tar.gz -C /curl --strip-components=1 && \
 rm -f curl-$CURL_VER.tar.gz && \
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
 rm -rf curl

COPY --from=ghcr.io/wiiu-env/libmocha:20220919 /artifacts $DEVKITPRO
COPY --from=ghcr.io/wiiu-env/librpxloader:20230316 /artifacts $DEVKITPRO

RUN git config --global --add safe.directory /project

WORKDIR /project

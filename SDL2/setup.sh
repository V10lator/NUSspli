#!/bin/sh

# Create folders (shouldn't be missing but just to be sure)
mkdir -p /opt/devkitpro/portlibs/wiiu/lib/cmake
mkdir -p /opt/devkitpro/portlibs/wiiu/lib/pkgconfig
mkdir -p /opt/devkitpro/portlibs/wiiu/share/aclocal

# Remove files
rm -rf \
    /opt/devkitpro/portlibs/wiiu/bin/sdl2-config \
    /opt/devkitpro/portlibs/wiiu/include/SDL2 \
    /opt/devkitpro/portlibs/wiiu/lib/libSDL2* \
    /opt/devkitpro/portlibs/wiiu/lib/cmake/SDL2 \
    /opt/devkitpro/portlibs/wiiu/lib/pkgconfig/sdl2.pc \
    /opt/devkitpro/portlibs/wiiu/lib/pkgconfig/SDL2* \
    /opt/devkitpro/portlibs/wiiu/licenses/wiiu-sdl2* \
    /opt/devkitpro/portlibs/wiiu/share/aclocal/sdl2.m4 \

# Add new files
cp -ar SDL2/sdl2-config /opt/devkitpro/portlibs/wiiu/bin/sdl2-config
cp -ar SDL2/include /opt/devkitpro/portlibs/wiiu/include/SDL2
cp -ar SDL2/libSDL2* /opt/devkitpro/portlibs/wiiu/lib/
cp -ar SDL2/cmake /opt/devkitpro/portlibs/wiiu/lib/cmake/SDL2
cp -ar SDL2/*.pc /opt/devkitpro/portlibs/wiiu/lib/pkgconfig/
cp -ar SDL2/licenses/* /opt/devkitpro/portlibs/wiiu/licenses/
cp -ar SDL2/sdl2.m4 /opt/devkitpro/portlibs/wiiu/share/aclocal/sdl2.m4

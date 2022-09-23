# build wut
FROM wiiuenv/devkitppc:20220907 AS final

ENV DEBIAN_FRONTEND=noninteractive \
 PATH=$DEVKITPPC/bin:$PATH \
 WUT_ROOT=$DEVKITPRO/wut
WORKDIR /
COPY --from=wiiuenv/libmocha:2022090322084275f31e /artifacts $DEVKITPRO
COPY --from=wiiuenv/librpxloader:20220903141341abbf92 /artifacts $DEVKITPRO

RUN mkdir -p /usr/share/man/man1 /usr/share/man/man2 && \
 /bin/bash -c "$(curl -sL https://raw.githubusercontent.com/V10lator/NUSspli/master/apt-fast/install.sh)" && \
 apt-fast -y --no-install-recommends upgrade && \
 apt-fast -y install --no-install-recommends autoconf automake libtool && \
 apt-fast clean && \
 git clone --recursive https://github.com/yawut/libromfs-wiiu --single-branch && \
 cd libromfs-wiiu && \
 make -j$(nproc) && \
 make install && \
 cd .. && \
 rm -rf libromfs-wiiu && \
 mkdir /CNUSPacker && \
 cd /CNUSPacker && \
 wget https://github.com/Xpl0itU/CNUS_Packer/releases/download/v1.2/CNUSPacker.run && \
 chmod +x CNUSPacker.run

WORKDIR /project

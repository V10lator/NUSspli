#!/bin/env python

import os
import pycurl
import shutil
import subprocess
import xml.etree.cElementTree as ET

nuspacker = "../nuspacker/NUSPacker.jar"    # Set path to NUSPacker.jar here. will be downloaded if empty or not found
wuhbtool = ""                               # Set path to wuhbtool. Will use the one from PATH if empty
ForceRelease = False                        # set to True to force release builds even if we're building ALPHA/BETA

# Don't edit below this line

def checkAndDeleteFile(file):
    if os.path.exists(file):
        print(f"Deleting {file}")
        os.remove(file)

def checkAndDeleteDir(dir):
    if os.path.exists(dir):
        print(f"Deleting {dir}")
        shutil.rmtree(dir)

def cDownload(url, file):
    with open(file, 'wb') as f:
        c = pycurl.Curl()
        c.setopt(c.URL, url)
        c.setopt(c.WRITEDATA, f)
        c.setopt(c.FOLLOWLOCATION, True)
        c.setopt(c.USERAGENT, "NUSspliBuilder/2.2")
        c.setopt(c.ACCEPT_ENCODING, "")
        c.perform()
        c.close()

vf = open("include/utils.h", "r")
lines = vf.readlines();
vf.close();

for line in lines:
    if line.find("#define NUSSPLI_VERSION") != -1:
        break

version = line.split()[2][1:-1]
github = open("version.txt", "w")
github.write(f"version={version}\n")
github.close()

if len(nuspacker) == 0 or not os.path.exists(nuspacker):
    cDownload("https://github.com/Maschell/nuspacker/raw/master/NUSPacker.jar", "nuspacker.jar")
    nuspacker = "nuspacker.jar"

isBeta = False
if ForceRelease or version.find("BETA") != -1 or version.find("ALPHA") != -1:
    isBeta = True

if len(wuhbtool) == 0:
    wuhbtool = "wuhbtool"

checkAndDeleteFile("src/gtitles.c")
cDownload("https://napi.v10lator.de/db?t=c", "src/gtitles.c")

checkAndDeleteFile("data/ca-certs.pem");
cDownload("https://ccadb.my.salesforce-sites.com/mozilla/IncludedRootsPEMTxt?TrustBitsInclude=Websites", "data/ca-certs.pem");

checkAndDeleteDir("NUStmp")
checkAndDeleteDir("out")

os.system(f"SDL2/setup.sh")

editionList = ["-DEBUG", ""]
extList = [".rpx", ".zip", ".wuhb"]
pkgList = ["Aroma", "Channel"]
for edition in editionList:
    for ext in extList:
        checkAndDeleteFile(f"NUSspli-{version}{edition}{ext}")
for edition in editionList:
    for ext in extList:
        for pkg in pkgList:
            checkAndDeleteFile(f"zips/NUSspli-{version}-{pkg}{edition}{ext}")

tmpArray = ["out/Aroma-DEBUG", "out/Channel-DEBUG", "NUStmp/code"]
for path in tmpArray:
    os.makedirs(path)
os.makedirs("zips", exist_ok=True)
os.system(f"make clean && make -j$(nproc) debug && {wuhbtool} NUSspli.rpx out/Aroma-DEBUG/NUSspli.wuhb --name=NUSspli --short-name=NUSspli --author=V10lator --icon=meta/menu/iconTex.tga --tv-image=meta/menu/bootTvTex.tga --drc-image=meta/menu/bootDrcTex.tga --content=data")
shutil.make_archive(f"zips/NUSspli-{version}-Aroma-DEBUG", "zip", "out/Aroma-DEBUG", ".")
shutil.copytree("meta/menu", "NUStmp/meta")
for root, dirs, files in os.walk("NUStmp/meta"):
    for file in files:
        if file.endswith(".xcf"):
            os.remove(os.path.join(root, file))

tmpArray = ["NUSspli.rpx", "NUStmp/meta/app.xml",  "NUStmp/meta/cos.xml"]
for file in tmpArray:
    shutil.move(file, "NUStmp/code")
shutil.copytree("data", "NUStmp/content")
os.system(f"java -jar {nuspacker} -in NUStmp -out out/Channel-DEBUG/NUSspli")
shutil.make_archive(f"zips/NUSspli-{version}-Channel-DEBUG", "zip", "out/Channel-DEBUG", ".")

if not isBeta:
    os.makedirs("out/Aroma")
    os.system(f"make clean && make -j$(nproc) release && {wuhbtool} NUSspli.rpx out/Aroma/NUSspli.wuhb --name=NUSspli --short-name=NUSspli --author=V10lator --icon=meta/menu/iconTex.tga --tv-image=meta/menu/bootTvTex.tga --drc-image=meta/menu/bootDrcTex.tga --content=data")
    shutil.make_archive(f"zips/NUSspli-{version}-Aroma", "zip", "out/Aroma", ".")
    os.remove("NUStmp/code/NUSspli.rpx")
    shutil.move("NUSspli.rpx", "NUStmp/code")
    os.makedirs("out/Channel")
    os.system(f"java -jar {nuspacker} -in NUStmp -out out/Channel/NUSspli")
    shutil.make_archive(f"zips/NUSspli-{version}-Channel", "zip", "out/Channel", ".")

shutil.rmtree("NUStmp")

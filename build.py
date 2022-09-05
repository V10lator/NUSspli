#!/bin/env python

import xml.etree.cElementTree as ET
import os
import shutil

cnuspacker = "../CNUSPacker/CNUSPacker.run" # Set path to NUSPacker.jar here. will be downloaded if empty
wuhbtool = ""                               # Set path to wuhbtool. Will use the one from PATH if empty
ForceRelease = False                        # set to True to force release builds even if we'e building ALPHA/BETA

# Don't edit below this line

def checkAndDeleteFile(file):
    if os.path.exists(file):
        print(f"Deleting {file}")
        os.remove(file)

def checkAndDeleteDir(dir):
    if os.path.exists(dir):
        print(f"Deleting {dir}")
        shutil.rmtree(dir)

version = ET.ElementTree(file="meta/hbl/meta.xml").getroot().findtext("version")

isBeta = False
if ForceRelease or version.find("BETA") != -1 or version.find("ALPHA") != -1:
    isBeta = True

if len(wuhbtool) == 0:
    wuhbtool = "wuhbtool"

checkAndDeleteFile("src/gtitles.c")
os.system(f"wget -U NUSspliBuilder/2.0 -O src/gtitles.c https://napi.nbg01.v10lator.de/db")

checkAndDeleteDir("NUStmp")
checkAndDeleteDir("out")

editionList = ["-DEBUG", ""]
extList = [".rpx", ".zip", ".wuhb"]
pkgList = ["Aroma", "HBL", "Channel"]
for edition in editionList:
    for ext in extList:  
        checkAndDeleteFile(f"NUSspli-{version}{edition}{ext}")
for edition in editionList:
    for ext in extList:
        for pkg in pkgList:
            checkAndDeleteFile(f"zips/NUSspli-{version}-{pkg}{edition}{ext}")

tmpArray = ["out/Aroma-DEBUG", "out/Channel-DEBUG", "out/HBL-DEBUG/NUSspli", "NUStmp/code"]
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
os.system(f"{cnuspacker} --target cnuspacker_tmp -- -in ../NUStmp -out ../out/Channel-DEBUG/NUSspli")
shutil.make_archive(f"zips/NUSspli-{version}-Channel-DEBUG", "zip", "out/Channel-DEBUG", ".")

if not isBeta:
    os.makedirs("out/Aroma")
    os.system(f"make clean && make -j$(nproc) release && {wuhbtool} NUSspli.rpx out/Aroma/NUSspli.wuhb --name=NUSspli --short-name=NUSspli --author=V10lator --icon=meta/menu/iconTex.tga --tv-image=meta/menu/bootTvTex.tga --drc-image=meta/menu/bootDrcTex.tga --content=data")
    shutil.make_archive(f"zips/NUSspli-{version}-Aroma", "zip", "out/Aroma", ".")
    os.remove("NUStmp/code/NUSspli.rpx")
    shutil.move("NUSspli.rpx", "NUStmp/code")
    os.makedirs("out/Channel")
    os.system(f"{cnuspacker} --target cnuspacker_tmp -- -in ../NUStmp -out ../out/Channel/NUSspli")
    shutil.make_archive(f"zips/NUSspli-{version}-Channel", "zip", "out/Channel", ".")

shutil.rmtree("cnuspacker_tmp")
shutil.rmtree("NUStmp")
os.system("make clean && make HBL=1 -j$(nproc) debug")
tmpArray = ["NUSspli.rpx", "meta/hbl/meta.xml", "meta/hbl/icon.png"]
for file in tmpArray:
    shutil.copy(file, "out/HBL-DEBUG/NUSspli")
shutil.make_archive(f"zips/NUSspli-{version}-HBL-DEBUG", "zip", "out/HBL-DEBUG", ".")

if not isBeta:
    os.system("make clean && make HBL=1 -j$(nproc) release")
    os.makedirs("out/HBL/NUSspli")
    for file in tmpArray:
        shutil.copy(file, "out/HBL/NUSspli")
    shutil.make_archive(f"zips/NUSspli-{version}-HBL", "zip", "out/HBL", ".")

os.chmod("out" , 0o777)
for root, dirs, files in os.walk("out"):
    for d in dirs:
        os.chmod(os.path.join(root, d) , 0o777)
    for f in files:
        os.chmod(os.path.join(root, f) , 0o777)
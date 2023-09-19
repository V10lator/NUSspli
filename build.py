#!/bin/env python

import os
import shutil
import urllib.request
import xml.etree.cElementTree as ET
import argparse

nuspacker = "../nuspacker/NUSPacker.jar"    # Set path to NUSPacker.jar here. will be downloaded if empty or not found
wuhbtool = ""                               # Set path to wuhbtool. Will use the one from PATH if empty
force_release = False                        # set to True to force release builds even if we're building ALPHA/BETA

# Don't edit below this line

parser = argparse.ArgumentParser(description="Build NUSspli with release and/or debug options")
parser.add_argument("--release", action="store_true", help="Build release versions")
parser.add_argument("--debug", action="store_true", help="Build debug versions")
parser.add_argument("--version", type=str, help="Specify the version", default=None)

args = parser.parse_args()

def check_and_delete_file(file):
    if os.path.exists(file):
        print(f"Deleting {file}")
        os.remove(file)

def check_and_delete_dir(dir):
    if os.path.exists(dir):
        print(f"Deleting {dir}")
        shutil.rmtree(dir)

opener = urllib.request.build_opener()
opener.addheaders = [("User-agent", "NUSspliBuilder/2.1")]
urllib.request.install_opener(opener)

tree = ET.parse("meta/hbl/meta.xml")
version = tree.getroot()[2].text
if args.version:
    tree.getroot()[2].text = args.version
    version = args.version

tree.write("meta/hbl/meta.xml")

if len(nuspacker) == 0 or not os.path.exists(nuspacker):
    urllib.request.urlretrieve("https://github.com/Maschell/nuspacker/raw/master/NUSPacker.jar", "nuspacker.jar")
    nuspacker = "nuspacker.jar"

is_beta = False
if force_release or version.find("BETA") != -1 or version.find("ALPHA") != -1:
    is_beta = True

if len(wuhbtool) == 0:
    wuhbtool = "wuhbtool"

check_and_delete_file("src/gtitles.c")
urllib.request.urlretrieve("https://napi.nbg01.v10lator.de/db", "src/gtitles.c")

check_and_delete_dir("NUStmp")
check_and_delete_dir("out")

edition_list = []
if args.release:
    edition_list.append("")
if args.debug:
    edition_list.append("-DEBUG")

ext_list = [".rpx", ".zip", ".wuhb"]
pkg_list = ["Aroma", "HBL", "Channel", "Lite"]

for edition in edition_list:
    for ext in ext_list:
        check_and_delete_file(f"NUSspli-{version}{edition}{ext}")
for edition in edition_list:
    for ext in ext_list:
        for pkg in pkg_list:
            check_and_delete_file(f"zips/NUSspli-{version}-{pkg}{edition}{ext}")

tmp_array = ["out/Aroma-DEBUG", "out/Lite-DEBUG", "out/Channel-DEBUG", "out/HBL-DEBUG/NUSspli", "NUStmp/code"]
for path in tmp_array:
    os.makedirs(path)
os.makedirs("zips", exist_ok=True)
os.system(f"make clean && make -j$(nproc) {'release' if args.release else 'debug'} && {wuhbtool} NUSspli.rpx out/Aroma-DEBUG/NUSspli.wuhb --name=NUSspli --short-name=NUSspli --author=V10lator --icon=meta/menu/iconTex.tga --tv-image=meta/menu/bootTvTex.tga --drc-image=meta/menu/bootDrcTex.tga --content=data")
shutil.make_archive(f"zips/NUSspli-{version}-Aroma-{'RELEASE' if args.release else 'DEBUG'}", "zip", "out/Aroma-DEBUG", ".")
shutil.copytree("meta/menu", "NUStmp/meta")
for root, dirs, files in os.walk("NUStmp/meta"):
    for file in files:
        if file.endswith(".xcf"):
            os.remove(os.path.join(root, file))
        if file.__contains__("-Lite"):
            os.remove(os.path.join(root, file))

tmp_array = ["NUSspli.rpx", "NUStmp/meta/app.xml",  "NUStmp/meta/cos.xml"]
for file in tmp_array:
    shutil.move(file, "NUStmp/code")
shutil.copytree("data", "NUStmp/content")
os.system(f"java -jar {nuspacker} -in NUStmp -out out/Channel-DEBUG/NUSspli")
shutil.make_archive(f"zips/NUSspli-{version}-Channel-{'RELEASE' if args.release else 'DEBUG'}", "zip", "out/Channel-DEBUG", ".")

os.system(f"make clean && make -j$(nproc) LITE=1 {'release' if args.release else 'debug'} && {wuhbtool} NUSspli.rpx out/Lite-DEBUG/NUSspli-Lite.wuhb --name=\"NUSspli Lite\" --short-name=\"NUSspli Lite\" --author=V10lator --icon=meta/menu/iconTex-lite.tga --tv-image=meta/menu/bootTvTex-lite.tga --drc-image=meta/menu/bootDrcTex.tga --content=data")
shutil.make_archive(f"zips/NUSspli-{version}-Lite-{'RELEASE' if args.release else 'DEBUG'}", "zip", "out/Lite-DEBUG", ".")

if not is_beta:
    os.makedirs("out/Aroma")
    os.system(f"make clean && make -j$(nproc) release && {wuhbtool} NUSspli.rpx out/Aroma/NUSspli.wuhb --name=NUSspli --short-name=NUSspli --author=V10lator --icon=meta/menu/iconTex.tga --tv-image=meta/menu/bootTvTex.tga --drc-image=meta/menu/bootDrcTex.tga --content=data")
    shutil.make_archive(f"zips/NUSspli-{version}-Aroma-RELEASE", "zip", "out/Aroma", ".")
    os.remove("NUStmp/code/NUSspli.rpx")
    shutil.move("NUSspli.rpx", "NUStmp/code")
    os.makedirs("out/Channel")
    os.system(f"java -jar {nuspacker} -in NUStmp -out out/Channel/NUSspli")
    shutil.make_archive(f"zips/NUSspli-{version}-Channel-RELEASE", "zip", "out/Channel", ".")
    os.makedirs("out/Lite")
    os.system(f"make clean && make -j$(nproc) LITE=1 release && {wuhbtool} NUSspli.rpx out/Lite/NUSspli-Lite.wuhb --name=\"NUSspli Lite\" --short-name=\"NUSspli Lite\" --author=V10lator --icon=meta/menu/iconTex-lite.tga --tv-image=meta/menu/bootTvTex-lite.tga --drc-image=meta/menu/bootDrcTex.tga --content=data")
    shutil.make_archive(f"zips/NUSspli-{version}-Lite-RELEASE", "zip", "out/Lite", ".")

shutil.rmtree("NUStmp")
os.system(f"make clean && make HBL=1 -j$(nproc) {'release' if args.release else 'debug'}")
tmp_array = ["NUSspli.rpx", "meta/hbl/meta.xml", "meta/hbl/icon.png"]
for file in tmp_array:
    shutil.copy(file, f"out/HBL-{'RELEASE' if args.release else 'DEBUG'}/NUSspli")
shutil.make_archive(f"zips/NUSspli-{version}-HBL-{'RELEASE' if args.release else 'DEBUG'}", "zip", f"out/HBL-{'RELEASE' if args.release else 'DEBUG'}", ".")

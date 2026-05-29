#!/bin/sh

# Downloads + extracts the prebuilt V8 libraries (gitignored, too big for git).
# Version is read from the committed header so the archive name matches the one
# produced by Releases/UnrealJs/make-v8-libs.js and uploaded to a GitHub release.

VersionHeader=ThirdParty/v8/include/v8-version.h
Major=$(cat $VersionHeader | grep MAJOR | awk '{ print int($3) }')
Minor=$(cat $VersionHeader | grep MINOR | awk '{ print int($3) }')
Build=$(cat $VersionHeader | grep BUILD_NUMBER | awk '{ print int($3) }')
Version=$Major.$Minor.$Build
ZipFile=v8-$Version-libs.7z

# GitHub release that hosts the v8-<Version>-libs.7z asset. Tag tracks the
# plugin version (bump per release); adjust the base URL if you fork/host elsewhere.
# Resolves to: $BaseUrl/$Tag/$ZipFile
Tag=v2.0.0
BaseUrl=https://github.com/getnamo/UnrealJs/releases/download

# Locate 7-Zip (the libs archive is .7z for a smaller download).
SevenZip=$(command -v 7z 2>/dev/null || command -v 7za 2>/dev/null)
if [ -z "$SevenZip" ]; then
    if [ -f "/c/Program Files/7-Zip/7z.exe" ]; then
        SevenZip="/c/Program Files/7-Zip/7z.exe"
    elif [ -f "/c/Program Files (x86)/7-Zip/7z.exe" ]; then
        SevenZip="/c/Program Files (x86)/7-Zip/7z.exe"
    fi
fi

if [ -d "ThirdParty/v8/lib" ]; then
    echo "Unreal.js is ready to build"
else
    if [ ! -f "ThirdParty/v8/$ZipFile" ]; then
        echo "Download prebuilt V8 $Version libraries for Unreal.js"
        (cd ThirdParty/v8; curl -O -L $BaseUrl/$Tag/$ZipFile)
    fi

    if [ -z "$SevenZip" ]; then
        echo "ERROR: 7-Zip not found. Install it (https://www.7-zip.org) to extract $ZipFile"
        exit 1
    fi

    (cd ThirdParty/v8; "$SevenZip" x -y "$ZipFile")
fi

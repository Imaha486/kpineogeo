#!/bin/sh
# Prepare the KbMedia Player plugin SDK headers in third_party/kpisdk/.
#
# The SDK (c) Kobarin is not redistributed in this repository.
#
#   1) From your KbMedia Player package (recommended):
#        scripts/fetch-kpisdk.sh /path/to/KbMediaPlayer/SDK/kpisdk.7z
#      -> extracts kpi.h / kpi_decoder.h / kpi_impl.h / kmp_pi.h and converts CP932 -> UTF-8
#
#   2) Without arguments: downloads the UTF-8 copy kept in auPiECE.kpi
#      (https://github.com/autch/auPiECE.kpi, kpisdk/ = SDK 2017/03/06)
set -e
cd "$(dirname "$0")/.."
DST=third_party/kpisdk
FILES="kpi.h kpi_decoder.h kpi_impl.h kmp_pi.h"
mkdir -p "$DST"

if [ -n "$1" ]; then
    TMP=$(mktemp -d)
    7z x -y -o"$TMP" "$1" >/dev/null
    for f in $FILES; do
        src=$(find "$TMP" -name "$f" | head -1)
        [ -n "$src" ] || { echo "missing $f in $1" >&2; exit 1; }
        iconv -f CP932 -t UTF-8 "$src" > "$DST/$f"
    done
    rm -rf "$TMP"
else
    REV=fa711813fc14d6fcfcf13a750a1232a5a323411a
    for f in $FILES; do
        curl -fsSL "https://raw.githubusercontent.com/autch/auPiECE.kpi/$REV/kpisdk/$f" -o "$DST/$f"
    done
fi
echo "KbMedia Player SDK headers are ready in $DST"

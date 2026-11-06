#!/bin/sh
# Build the 8086/8088 DOS executable with Linux-hosted Open Watcom.
# Usage: WATCOM=/path/to/watcom tools/build-dos.sh [output-directory]
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
: "${WATCOM:?Set WATCOM to the Open Watcom installation directory}"
WATCOM=$(CDPATH= cd -- "$WATCOM" && pwd)
if [ -x "$WATCOM/binl64/wpp" ]; then
    watcom_bin=$WATCOM/binl64
elif [ -x "$WATCOM/binl/wpp" ]; then
    watcom_bin=$WATCOM/binl
else
    echo "No Linux-hosted wpp found in $WATCOM/binl64 or binl" >&2
    exit 1
fi
export WATCOM
PATH=$watcom_bin:$PATH
INCLUDE=$WATCOM/h
LIBDOS=$WATCOM/lib286/dos:$WATCOM/lib286
export PATH INCLUDE LIBDOS

build=${1:-"$root/build/dos"}
mkdir -p "$build"
build=$(CDPATH= cd -- "$build" && pwd)
cd "$build"

# The bundled mTCP files have DOS uppercase names but lowercase includes.
# Keep those upstream files intact; provide aliases on case-sensitive hosts.
mkdir -p include
for header in "$root"/lib/mTCP/TCPINC/*; do
    name=$(basename "$header" | tr '[:upper:]' '[:lower:]')
    ln -sf "$header" "include/$name"
    title=$(printf '%s' "$name" | sed 's/^./\U&/')
    ln -sf "$header" "include/$title"
done
ln -sf "$root/lib/mTCP/TCPINC/GLOBAL.CFG" include/Global.Cfg
ln -sf "$root/lib/mTCP/TCPINC/TCPSOCKM.H" include/tcpSockM.h

# Match the project's size-optimized large-model DOS build. -0 is the
# 8086/8088 instruction set, for both application and networking code.
for source in "$root"/src/*.cpp "$root"/src/DOS/*.cpp \
              "$root"/src/Draw/*.cpp "$root"/src/Image/*.cpp \
              "$root"/src/Memory/*.cpp "$root"/src/Nodes/*.cpp; do
    name=$(basename "$source" .cpp)
    # The historical linker script uses this spelling.
    [ "$name" != Microweb ] || name=MicroWeb
    echo "Compiling $name"
    wpp -zq -0 -os -oh -ok -ol+ -oi+ -ob -s -bt=DOS -w2 -ml \
        "-fi=$root/src/Defines.h" "-i=$build/include" \
        "-i=$root/project/DOS" '-DCFG_H="TCP.CFG"' \
        "-fo=$name.obj" "$source"
done

for name in packet arp eth ip tcp tcpsockm udp utils dns timer trace; do
    source=$(printf '%s' "$name" | tr '[:lower:]' '[:upper:]')
    echo "Compiling mTCP $name"
    wpp -zq -0 -ml -bt=DOS '-DCFG_H="TCP.CFG"' \
        -oh -ok -ot -s -oa -ei -zp2 -zpw -we -ob -ol+ -oi+ \
        "-i=$build/include" "-i=$root/project/DOS" \
        "-fo=$name.obj" "$root/lib/mTCP/TCPLIB/$source.CPP"
done
wasm -zq -0 -ml -fo=ipasm.obj "$root/lib/mTCP/TCPLIB/IPASM.ASM"

wasm -zq -0 -fo=VRESERVE.obj "$root/src/DOS/VRESERVE.ASM"
wlink format dos com name VRESERVE.COM file VRESERVE.obj option quiet
cp "$root/TANDY.BAT" TANDY.BAT

# Retain the DOS project's overlays rather than linking a flat executable.
wlink name MicroWeb.exe "@$root/project/DOS/MicroWeb.lnk"

# The upstream build patches Watcom's startup heap/IBM-detection routines.
# Run that same patcher natively, keeping its short DOS filenames intact.
"${CXX:-c++}" -Dstricmp=strcasecmp -include strings.h \
    "$root/lib/mTCP/UTILS/PATCH.CPP" -o mtcp-patch
./mtcp-patch MicroWeb.exe map.map -ml

printf '\nBuilt %s/MicroWeb.exe (8086/8088, large model)\n' "$build"

#!/bin/sh
# Cross-build the loader (loader/) for goodluckOS in the gtavc-builder Docker
# image (docker/Dockerfile.builder), into build/gtavc. package.sh then
# assembles out/ports/gtavc/.
set -eu
cd "$(dirname "$0")"
docker image inspect gtavc-builder:bookworm >/dev/null 2>&1 ||
	docker buildx build -q -f docker/Dockerfile.builder -t gtavc-builder:bookworm --load docker
mkdir -p build
[ -f build/ffmpeg/lib/libavcodec.a ] || ./tools/build-ffmpeg.sh
docker run --rm -v "$PWD":/w -w /w gtavc-builder:bookworm sh -c '
set -e
arm-linux-gnueabihf-gcc -O2 -g -mcpu=cortex-a7 -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard \
	-Wall -Wno-unused-function -Ibuild/ffmpeg/include $(arm-linux-gnueabihf-pkg-config --cflags sdl2 2>/dev/null || pkg-config --cflags sdl2) \
	loader/*.c -o build/gtavc \
	-Wl,--export-dynamic-symbol=ioctl -Wl,--disable-new-dtags,-rpath,\$ORIGIN/lib \
	build/ffmpeg/lib/libavformat.a build/ffmpeg/lib/libavcodec.a build/ffmpeg/lib/libavutil.a \
	-lSDL2 -lopenal -lz -ldl -lpthread -lm
'
ls -la build/gtavc

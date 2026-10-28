#!/bin/bash
# Se ejecuta dentro de un contenedor Debian bullseye armhf (ver .github/workflows/build.yml)
set -e
OUT=${OUT:-out}
rm -rf "$OUT"; mkdir -p "$OUT/zenonia3/stubs" "$OUT/zenonia3/libs.armhf"
gcc -O2 -g -std=gnu11 -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable \
    -o "$OUT/zenonia3/zenonia3.armhf" src/main.c src/jni.c src/natives.c src/gles.c src/compat.c src/hook.c \
    $(sdl2-config --cflags) $(sdl2-config --libs) -ldl -lm -lpthread
# libs "falsas" con los nombres que pide la .so de Android
gcc -shared -fPIC -nostdlib -Wl,-soname,libc.so   -o "$OUT/zenonia3/stubs/libc.so"   stubs/libc_stub.c
gcc -shared -fPIC -nostdlib -Wl,-soname,libGLESv1_CM.so -o "$OUT/zenonia3/stubs/libGLESv1_CM.so" stubs/gles_stub.c
for n in libm libdl liblog libstdc++; do
  echo "" > /tmp/empty.c; gcc -shared -fPIC -nostdlib -Wl,-soname,$n.so -o "$OUT/zenonia3/stubs/$n.so" /tmp/empty.c
done
# SDL2 propio por si la consola no trae SDL2 de 32 bits
cp -L "$(ldconfig -p | grep -m1 'libSDL2-2.0.so.0' | awk '{print $NF}')" "$OUT/zenonia3/libs.armhf/" || true
cp port/zenonia3/* "$OUT/zenonia3/"
cp "port/Zenonia 3.sh" "$OUT/"
chmod +x "$OUT/zenonia3/zenonia3.armhf" "$OUT/Zenonia 3.sh"
file "$OUT/zenonia3/zenonia3.armhf" || true
echo BUILD_OK

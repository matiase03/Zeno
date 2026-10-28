# Zenonia 3 para R36S (dArkOS RE) - loader nativo, v1 (banco de pruebas)

Carga la `libgameDSO.so` de Android (Gamevil "nexus2", ARM 32-bit, OpenGL ES 1.1) directamente
en Linux, emulando el lado Java (JNI), OpenGL ES 1.1 (sobre ES 2.0) y las diferencias de bionic/glibc.

**Estado: SIN PROBAR en hardware.** Esta version esta pensada para que el `log.txt` diga exactamente
que falta. Todo lo no implementado se loguea con nombre y firma.

## Como compilar (GitHub Actions)
1. Crea un repo **PRIVADO** y subi el contenido de esta carpeta (no incluye assets del juego).
2. Pestana *Actions* -> *Build Zenonia3 R36S* -> *Run workflow* (o se dispara solo con cada push).
3. Al terminar, baja el artifact `zenonia3-r36s-overlay` (es un zip).

## Como instalar
Sobre la carpeta que ya tenes en la SD (`/roms/ports/zenonia3`, con `assets/`, `res/`, `lib/`):
1. **Borra** los viejos: `zenonia3.bin`, `loader.elf`, `zenonia3.armhf`, `zenonia3.aarch64`, `zenonia3.apk` (opcional), `run_game.sh`.
2. Descomprime el overlay sobre `/roms/ports/` (reemplaza `Zenonia 3.sh` y agrega `zenonia3/zenonia3.armhf`,
   `stubs/`, `libs.armhf/`, `zenonia3.cfg`, `zenonia3.gptk`).
3. Ejecuta el port y mandame `/roms/ports/zenonia3/log.txt` (aunque falle: el log dice por que).

## Que esperar en la primera prueba
- Si carga bien vas a ver `[HOOK] N relocaciones redirigidas`, `[INIT] ...` y `primer frame renderizado OK`.
- Si crashea, el log muestra `pc_off`: es el offset dentro de `libgameDSO.so` donde se cayo.
- **Todavia no hay**: texto (clase NexusFont, solo se loguea), audio (OnSoundPlay solo se loguea), pantalla tactil.
- El menu de titulo de Zenonia es Java en el original; los botones estan *supuestos* en `zenonia3.cfg`.

## Archivos
- `src/main.c` SDL2 + contexto GLES2, carga de la .so, bucle principal, controles.
- `src/jni.c` JNIEnv/JavaVM falsos. `src/natives.c` la parte Java (archivos, timers, stubs).
- `src/gles.c` OpenGL ES 1.1 -> ES 2.0. `src/compat.c` + `src/hook.c` parches bionic->glibc (mutex de 4 bytes,
  `stat`, `ceil` softfp, etc.) aplicados sobre la GOT.

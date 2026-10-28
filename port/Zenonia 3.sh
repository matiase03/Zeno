#!/bin/bash
# Zenonia 3 (Gamevil) - port para R36S / dArkOS RE
XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then controlfolder="$XDG_DATA_HOME/PortMaster"
elif [ -d "/roms/ports/PortMaster/" ]; then controlfolder="/roms/ports/PortMaster"
elif [ -d "/roms2/ports/PortMaster/" ]; then controlfolder="/roms2/ports/PortMaster"
else controlfolder="/roms/ports/PortMaster"; fi

if [ -f "${controlfolder}/control.txt" ]; then
  source "${controlfolder}/control.txt"
  [ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
  get_controls
else
  GPTOKEYB="gptokeyb"; ESUDO="sudo"
fi

if [ -d "/roms2/ports/zenonia3" ]; then GAMEDIR="/roms2/ports/zenonia3"
elif [ -d "/roms/ports/zenonia3" ]; then GAMEDIR="/roms/ports/zenonia3"
else GAMEDIR="$(cd "$(dirname "$0")" && pwd)/zenonia3"; fi
export GAMEDIR

cd "$GAMEDIR"
> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1
echo "=== Zenonia 3 (loader real, v1) ==="
echo "GAMEDIR=$GAMEDIR  fecha: $(date)"
uname -a

[ -n "$ESUDO" ] && $ESUDO chmod 666 /dev/uinput 2>/dev/null
chmod +x "$GAMEDIR/zenonia3.armhf"

# Las .so "falsas" (nombres que pide la libreria Android) van primero.
export LD_LIBRARY_PATH="$GAMEDIR/stubs:$LD_LIBRARY_PATH"
# SDL2 de 32 bits: usa el del sistema si existe, si no el incluido.
if ! ls /usr/lib/arm-linux-gnueabihf/libSDL2-2.0.so.0 /usr/lib32/libSDL2-2.0.so.0 >/dev/null 2>&1; then
  echo "No hay SDL2 armhf en el sistema -> uso libs.armhf"
  export LD_LIBRARY_PATH="$GAMEDIR/libs.armhf:$LD_LIBRARY_PATH"
fi
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"

$GPTOKEYB "zenonia3.armhf" -c "$GAMEDIR/zenonia3.gptk" &
GPTK_PID=$!

ldd "$GAMEDIR/zenonia3.armhf" 2>&1 | head -20
"$GAMEDIR/zenonia3.armhf"
echo "=== el juego termino con codigo $? ==="

kill -9 $GPTK_PID 2>/dev/null
systemctl restart ogs-input-daemon 2>/dev/null
printf "\033c" > /dev/tty1

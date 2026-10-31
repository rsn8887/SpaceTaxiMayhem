Space Taxi Mayhem
=================

A multiplayer Space Taxi game with 1-5 local players, inspired by the Amiga PD classic Space Taxi 3.

Pick up passengers, land gently on the pad they call out, and don't crash into walls, hazards, other taxis or the passengers themselves. There are 20 levels with switches, moving platforms, lasers, wind and teleporters. Fly with the D-pad and remember that you can't thrust sideways while the landing gear (A/B) is down.

![Title screen](screenshots/title.png)
![In-game](screenshots/ingame.png)

How to build
---------------

- Switch:
```
docker run -it -v "$(pwd)":/app -w /app devkitpro/devkita64
cmake -S ./spacetaxi -B build-switch -DPLATFORM=SWITCH
cmake --build build-switch -j
```

- PS Vita (needs custom firmware, e.g. HENkaku/Ensō; VITASDK must be set):
```
docker run --rm -it -v "$PWD:/workspace" vitasdk/vitasdk:2026.08
cd /workspace
cmake -S ./spacetaxi -B build-vita -DPLATFORM=VITA
cmake --build build-vita -j
```
This creates `build-vita/SpaceTaxiMayhem.vpk`, which you install with VitaShell. If SDL2 or SDL2_ttf are missing from VitaSDK, run `vdpm sdl2 sdl2_ttf`.

- Linux x64 (Docker, also works on an Apple Silicon Mac):
```
docker build --platform linux/amd64 -t spacetaxi-linux-x64 ./spacetaxi/docker/linux-x64
docker run --rm --platform linux/amd64 -v "$(pwd)":/app -w /app spacetaxi-linux-x64 \
  sh -c "cmake -S ./spacetaxi -B build-linux && cmake --build build-linux -j"
```
This creates `build-linux/spacetaxi` with its `assets` folder next to it. To run it, the target system needs SDL2 and SDL2_ttf installed (e.g. `sudo apt install libsdl2-2.0-0 libsdl2-ttf-2.0-0`).

- Linux ARM64 (e.g. Raspberry Pi 4/5 with 64-bit Raspberry Pi OS; Docker, runs natively on an Apple Silicon Mac):
```
docker build --platform linux/arm64 -t spacetaxi-linux-arm64 ./spacetaxi/docker/linux-arm64
docker run --rm --platform linux/arm64 -v "$(pwd)":/app -w /app spacetaxi-linux-arm64 \
  sh -c "cmake -S ./spacetaxi -B build-linux-arm64 && cmake --build build-linux-arm64 -j"
```
This creates `build-linux-arm64/spacetaxi` with its `assets` folder next to it. On the Pi, install SDL2 and SDL2_ttf with `sudo apt install libsdl2-2.0-0 libsdl2-ttf-2.0-0`.

- MacOS:
```
brew install cmake sdl2 sdl2_ttf
cmake -S ./spacetaxi -B build
cmake --build build -j
```
This builds `Space Taxi Mayhem.app`. To make a self-contained app that can be copied to the Applications folder on any Mac (SDL libraries included), or a .dmg to share:
```
cmake --install build --prefix dist     # -> dist/Space Taxi Mayhem.app
cd build && cpack                        # -> SpaceTaxiMayhem-1.1.0-macOS.dmg
```
The app is ad-hoc signed but not notarized, so on another Mac open it the first time with right-click → Open (or run `xattr -dr com.apple.quarantine "/Applications/Space Taxi Mayhem.app"`).

- Windows:
Install MSYS2 MinGW, then open a msys2 ucrt shell
```
pacman -S mingw-w64-ucrt-x86_64-gcc
pacman -S mingw-w64-ucrt-x86_64-cmake
pacman -S mingw-w64-ucrt-x86_64-SDL2
pacman -S mingw-w64-ucrt-x86_64-SDL2_ttf
cmake -S ./spacetaxi -B build && cmake --build build -j
curl https://raw.githubusercontent.com/rollerozxa/msys2-bundledlls/master/bundledlls > ./bundledlls
bundledlls ./spacetaxi.exe ./
```

Credits
-------

- Font: "Press Start 2P" by CodeMan38, licensed under the SIL Open Font License (see `assets/OFL-PressStart2P.txt`).
- Passenger voices: generated with [espeak-ng](https://github.com/espeak-ng/espeak-ng) (`tools/make_voices.py`).
- Libraries: [SDL2](https://www.libsdl.org) and SDL2_ttf (zlib license), FreeType.
- Inspired by Space Taxi (John F. Kutcher, Muse Software, 1984) and the Amiga PD game Space Taxi 3. This is an unaffiliated fan homage.

assisted by Claude

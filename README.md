# meowtty-xvfb

Xvfb with a small DRI3, for the desktop of [MeowTTY](https://github.com/spizganed) on Android phones with an Adreno GPU.

The Adreno Mesa of [lfdevs/mesa-for-android-container](https://github.com/lfdevs/mesa-for-android-container) needs DRI3 in the X server. Xvfb has none. `vfb-dri3.c` adds it: PixmapFromBuffers takes one linear buffer fd (modifier `LINEAR`, or 1274 for a plain mappable fd), maps it, and uses the memory as the pixmap. Present then copies the frame into the screen. There is no render node: DRI3Open fails, and Mesa goes on without it. The idea comes from the DRI3 of Termux:X11; the code is new.

Measured on an Adreno 810 under proot (glmark2-es2, 800x600, six scenes): 291 with this Xvfb, 180 through Zink on the stock Xvfb, 59 on the CPU.

## Build

`build.sh` builds Xvfb from the xorg-server source of the distro (Debian or Ubuntu) and installs it as `/usr/local/bin/Xvfb-meowtty` (set `OUT` for another path). It needs about 600 MB for the build tools and 10 to 20 minutes on a phone.

The workflow builds it for Ubuntu 26.04 and Debian 13 (arm64) at each `v*` tag and puts the binaries and `SHA256SUMS` on a release.

## Licence

MIT, the same as the X server. See `LICENSE`.

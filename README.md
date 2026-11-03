# meowtty-xvfb

Xvfb with a small DRI3, for the desktop of [MeowTTY](https://github.com/spizganed) on Android phones with an Adreno GPU.

The Adreno Mesa of [lfdevs/mesa-for-android-container](https://github.com/lfdevs/mesa-for-android-container) needs DRI3 in the X server. Xvfb has none. `vfb-dri3.c` adds it: PixmapFromBuffers takes one linear buffer fd (modifier `LINEAR`, or 1274 for a plain mappable fd), maps it, and uses the memory as the pixmap. Present then copies the frame into the screen. There is no render node: DRI3Open fails, and Mesa goes on without it. The idea comes from the DRI3 of Termux:X11; the code is new.

Measured on an Adreno 810 under proot (glmark2-es2, 800x600, six scenes): 291 with this Xvfb, 180 through Zink on the stock Xvfb, 59 on the CPU.

## Build

`build.sh` builds Xvfb from the xorg-server source of the distro (Debian or Ubuntu) and installs it as `/usr/local/bin/Xvfb-meowtty` (set `OUT` for another path). It needs about 600 MB for the build tools and 10 to 20 minutes on a phone.

The workflow builds it for Ubuntu 26.04 and Debian 13 (arm64) at each `v*` tag and puts the binaries and `SHA256SUMS` on a release.

## Android

`build-android.sh` builds the same Xvfb for Android (arm64, bionic, API 28), with all its libraries from their release tarballs, linked in statically. The MeowTTY app runs it natively, outside proot; the clients in the Linux system connect through its socket. `patch-android.py` makes the changes that an app process needs:

- `-nolock` works for any user (an app is no root, and has no hard links).
- `MEOWTTY_XKBCOMP`: the command that compiles the keymap, for example a script that runs `xkbcomp` in the Linux system. `MEOWTTY_XKM_DIR`: an absolute folder for its output.
- The socket path has no leading slash (`tmp/.X11-unix/X2`): start the server in the root folder of the Linux system.

- `MEOWTTY_XBUF`: the abstract unix socket of the app. The server takes an AHardwareBuffer there (`vfb-android.c`) and uses its memory as the screen; the app shows the same buffer with no copy. `xbuf-jni.c` is the JNI library of the app that sends the buffer (`libmeowtty-xbuf-android-arm64.so`).

The NDK compiler runs on x86_64 only. On an arm64 host, the script builds with the system clang and the sysroot and runtime of the NDK. The workflow builds it with the NDK of the runner.

Measured on the phone (Adreno 810), x11perf from a client under proot: PutImage 500x500 1270/s (Xvfb under proot: 605/s), copy 500x500 window to window 12000/s (8700/s). glmark2-es2 through the DRI3: 331.

## Licence

MIT, the same as the X server. See `LICENSE`.

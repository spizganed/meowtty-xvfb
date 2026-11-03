#!/usr/bin/env python3
"""Changes that let Xvfb run as an Android app process, outside proot. build-android.sh runs it in the top folder of the
xorg-server tree. Argument: the prefix that holds the xtrans source (include/X11/Xtrans).

- The app is no root, and Android allows no hard link in the data folder of an app: -nolock works for any user.
- MEOWTTY_XKBCOMP: the command that compiles the keymap, for example "sh /path/xkbcomp.sh". Without it: xkbcomp from
  the built-in folder.
- MEOWTTY_XKM_DIR: the folder for the compiled keymap. It must be absolute: xkbcomp -R changes the working folder.
- Popen and System call setuid and setgid only when the ids differ: Android kills an app for them (seccomp).
- MEOWTTY_XBUF: the abstract socket of the app. The screen is then the AHardwareBuffer that the app sends there
  (vfb-android.c); the app shows it with no copy.
- GLX with direct rendering only (vfb-glx.c in place of the swrast provider): the clients render with their own Mesa.
- MIT-SHM with fds only: the server makes its segments with memfd_create, and a SysV attach fails with BadAccess
  (Android kills an app for shmat). The clients under proot have no SysV shared memory that the server can see.
- The socket and the lock paths have no leading slash: "tmp/.X11-unix/X2". The app starts the server in the root folder
  of the Linux system, so the socket is in the /tmp of that system, where the clients under proot look for it.
"""
import sys

def patch(path, old, new, count=1):
    s = open(path).read()
    if new in s:
        return
    if old not in s:
        sys.exit(f"patch-android: no match in {path}: {old[:60]!r}")
    open(path, "w").write(s.replace(old, new, count))

patch("os/utils.c",
      '            if (getuid() != 0)\n                ErrorF\n                    ("Warning: the -nolock option can only be used by root\\n");\n            else\n',
      '            /* MeowTTY: an Android app is no root, and has no hard links. */\n')
patch("os/utils.c", '#define LOCK_DIR "/tmp"', '#define LOCK_DIR "tmp"')

patch("xkb/ddxLoad.c",
      "    if (r < 0 || r >= size) {\n        assert(strlen(\"/tmp/\") < size);",
      "    /* MeowTTY: an absolute folder from the app. */\n"
      "    if (getenv(\"MEOWTTY_XKM_DIR\"))\n"
      "        r = snprintf(outdir, size, \"%s/\", getenv(\"MEOWTTY_XKM_DIR\"));\n"
      "    if (r < 0 || r >= size) {\n        assert(strlen(\"/tmp/\") < size);")
patch("xkb/ddxLoad.c",
      '    if (asprintf(&buf,\n                 "\\"%s%sxkbcomp\\" -w %d %s -xkm \\"%s\\" "',
      '    /* MeowTTY: the app gives the whole command, for example a script that runs xkbcomp in the Linux system. */\n'
      '    char *xkbcompcmd = NULL;\n'
      '    if (getenv("MEOWTTY_XKBCOMP"))\n'
      '        xkbcompcmd = strdup(getenv("MEOWTTY_XKBCOMP"));\n'
      '    else if (asprintf(&xkbcompcmd, "\\"%s%sxkbcomp\\"", xkbbindir, xkbbindirsep) == -1)\n'
      '        xkbcompcmd = NULL;\n'
      '    if (!xkbcompcmd || asprintf(&buf,\n                 "%s -w %d %s -xkm \\"%s\\" "')
patch("xkb/ddxLoad.c",
      "                 xkbbindir, xkbbindirsep,\n                 ((xkbDebugFlags",
      "                 xkbcompcmd,\n                 ((xkbDebugFlags")
patch("xkb/ddxLoad.c",
      "        buf = NULL;\n\n    free(xkbbasedirflag);",
      "        buf = NULL;\n\n    free(xkbcompcmd);\n    free(xkbbasedirflag);")

# Android kills an app for setuid and setgid (seccomp, SIGSYS), even to its own ids. Popen and System call them
# before they start a command such as xkbcomp: only call them when the ids differ (a setuid server).
s = open("os/utils.c").read()
if "getegid() != getgid() &&" not in s:
    s = s.replace("if (setgid(getgid()) == -1)", "if (getegid() != getgid() && setgid(getgid()) == -1)")
    s = s.replace("if (setuid(getuid()) == -1)", "if (geteuid() != getuid() && setuid(getuid()) == -1)")
    open("os/utils.c", "w").write(s)

# The screen in the AHardwareBuffer of the app (vfb-android.c), when MEOWTTY_XBUF names its socket. The usual memory
# still holds the XWD header and the colour map; malloc does not touch the screen part of it.
import os, shutil
here = os.path.dirname(os.path.abspath(__file__))
for f in ("vfb-android.c", "vfb-android.h"):
    shutil.copy(os.path.join(here, f), "hw/vfb/")
patch("hw/vfb/meson.build", "    'vfb-dri3.c',\n", "    'vfb-dri3.c',\n    'vfb-android.c',\n")
patch("hw/vfb/meson.build", "    install: true,\n)\n", "    link_args: ['-lnativewindow'],\n    install: true,\n)\n")
patch("hw/vfb/InitOutput.c", '#include "vfb-dri3.h"\n#endif\n', '#include "vfb-dri3.h"\n#endif\n#include "vfb-android.h"\n')
patch("hw/vfb/InitOutput.c",
      "    pbits = vfbAllocateFramebufferMemory(pvfb);\n    if (!pbits)\n        return FALSE;\n",
      "    {\n"
      "        /* MeowTTY: the screen in the buffer of the app. */\n"
      "        int stride = 0;\n"
      "        char *shared = getenv(\"MEOWTTY_XBUF\") ? vfbAndroidBuffer(pvfb->width, pvfb->height, &stride) : NULL;\n"
      "        if (getenv(\"MEOWTTY_XBUF\") && !shared)\n"
      "            FatalError(\"vfb: no buffer from the app (MEOWTTY_XBUF)\\n\");\n"
      "        if (shared) {\n"
      "            pvfb->paddedBytesWidth = stride;\n"
      "            pvfb->paddedWidth = stride / (pvfb->bitsPerPixel / 8);\n"
      "        }\n"
      "        pbits = vfbAllocateFramebufferMemory(pvfb);\n"
      "        if (!pbits)\n"
      "            return FALSE;\n"
      "        if (shared)\n"
      "            pbits = pvfb->pfbMemory = shared;\n"
      "    }\n")

# GLX: our provider (vfb-glx.c) in place of the swrast provider and the DRI helpers, which load a GL driver.
shutil.copy(os.path.join(here, "vfb-glx.c"), "glx/")
patch("glx/meson.build", "    'glxdriswrast.c',\n    'glxdricommon.c',\n", "    'vfb-glx.c',\n")
patch("glx/glxext.c", "static __GLXprovider *__glXProviderStack = &__glXDRISWRastProvider;",
      "extern __GLXprovider __glXMeowProvider;\nstatic __GLXprovider *__glXProviderStack = &__glXMeowProvider;")

# MIT-SHM: no SysV shared memory in an app process (seccomp). The fd paths (AttachFd, CreateSegment) work.
patch("Xext/shm.c",
      "        shmdesc->addr = shmat(stuff->shmid, 0,",
      "#ifdef __ANDROID__\n"
      "        /* MeowTTY: Android kills an app for shmat. The client must send an fd (ShmAttachFd). */\n"
      "        free(shmdesc);\n"
      "        return BadAccess;\n"
      "#endif\n"
      "        shmdesc->addr = shmat(stuff->shmid, 0,")
# The meson check finds no sigaction in the static bionic, but bionic has it. MIT-SHM with fds needs it (busfault.c).
patch("include/meson.build", "conf_data.set('HAVE_SIGACTION', cc.has_function('sigaction') ? '1' : false)",
      "conf_data.set('HAVE_SIGACTION', '1')")
# dix-config.h undefines HAVE_MEMFD_CREATE (bionic before API 30 has no wrapper), and Android has no /tmp: call the
# kernel directly.
patch("Xext/shm.c",
      "#ifdef HAVE_MEMFD_CREATE\n    fd = memfd_create(\"xorg\", MFD_CLOEXEC|MFD_ALLOW_SEALING);",
      "#if defined(HAVE_MEMFD_CREATE) || defined(__ANDROID__)\n"
      "    fd = (int) syscall(__NR_memfd_create, \"xorg\", MFD_CLOEXEC|MFD_ALLOW_SEALING);")
patch("Xext/shm.c", "#include <sys/types.h>\n", "#include <sys/types.h>\n#include <sys/syscall.h>\n#include <linux/memfd.h>\n")

prefix = sys.argv[1]
sock = f"{prefix}/include/X11/Xtrans/Xtranssock.c"
s = open(sock).read()
s = s.replace('"/tmp/.X11-unix/X"', '"tmp/.X11-unix/X"').replace('"/tmp/.X11-unix"', '"tmp/.X11-unix"')
open(sock, "w").write(s)

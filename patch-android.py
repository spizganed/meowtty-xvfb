#!/usr/bin/env python3
"""Changes that let Xvfb run as an Android app process, outside proot. build-android.sh runs it in the top folder of the
xorg-server tree. Argument: the prefix that holds the xtrans source (include/X11/Xtrans).

- The app is no root, and Android allows no hard link in the data folder of an app: -nolock works for any user.
- MEOWTTY_XKBCOMP: the command that compiles the keymap, for example "sh /path/xkbcomp.sh". Without it: xkbcomp from
  the built-in folder.
- MEOWTTY_XKM_DIR: the folder for the compiled keymap. It must be absolute: xkbcomp -R changes the working folder.
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

prefix = sys.argv[1]
sock = f"{prefix}/include/X11/Xtrans/Xtranssock.c"
s = open(sock).read()
s = s.replace('"/tmp/.X11-unix/X"', '"tmp/.X11-unix/X"').replace('"/tmp/.X11-unix"', '"tmp/.X11-unix"')
open(sock, "w").write(s)

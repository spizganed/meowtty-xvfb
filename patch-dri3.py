#!/usr/bin/env python3
"""Adds our DRI3 (vfb-dri3.c) to the Xvfb of an xorg-server source tree. Run it in the top folder of the tree.
build.sh and build-android.sh use it. It changes nothing in a tree that has the patch already."""
import os, shutil

here = os.path.dirname(os.path.abspath(__file__))
for f in ("vfb-dri3.c", "vfb-dri3.h"):
    shutil.copy(os.path.join(here, f), "hw/vfb/")
# Add the file to the Xvfb build, and call vfbDri3Init at the end of the screen setup.
p = "hw/vfb/meson.build"; s = open(p).read()
if "vfb-dri3.c" not in s:
    s = s.replace("    'InitOutput.c',\n", "    'InitOutput.c',\n    'vfb-dri3.c',\n", 1)
    s = s.replace("    dependencies: common_dep,", "    dependencies: [common_dep, libdrm_dep],", 1)
    open(p, "w").write(s)
p = "hw/vfb/InitOutput.c"; s = open(p).read()
if "vfbDri3Init" not in s:
    s = s.replace('#include "micmap.h"', '#include "micmap.h"\n#ifdef DRI3\n#include "vfb-dri3.h"\n#endif', 1)
    i = s.index("/* end vfbScreenInit */")
    j = s.rindex("    return ret;", 0, i)
    s = s[:j] + '#ifdef DRI3\n    if (ret && !vfbDri3Init(pScreen))\n        ErrorF("vfb: DRI3 init failed\\n");\n#endif\n' + s[j:]
    open(p, "w").write(s)

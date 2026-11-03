#!/bin/sh
# Builds Xvfb with our small DRI3 (vfb-dri3.c) from the xorg-server source of the distro. Debian and Ubuntu.
# Output: $OUT (default /usr/local/bin/Xvfb-meowtty). The MeowTTY desktop uses that file when it exists.
# With it, OpenGL runs in the Adreno driver (Freedreno) with no Zink: glmark2-es2 291 against 180.
# Needs about 600 MB for the build tools and the source, and 10 to 20 minutes on the phone.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
W=${1:-/root/xsrc}
export DEBIAN_FRONTEND=noninteractive
# The source packages: the distro lists them with "deb-src".
if [ -f /etc/apt/sources.list.d/ubuntu.sources ]; then
  sed -i 's/^Types: deb$/Types: deb deb-src/' /etc/apt/sources.list.d/ubuntu.sources
elif [ -f /etc/apt/sources.list.d/debian.sources ]; then
  sed -i 's/^Types: deb$/Types: deb deb-src/' /etc/apt/sources.list.d/debian.sources
fi
apt-get update
apt-get -y build-dep xorg-server
apt-get -y install --no-install-recommends meson ninja-build python3 dpkg-dev
mkdir -p "$W" && cd "$W"
[ -n "$(ls -d xorg-server-*/ 2>/dev/null)" ] || apt-get source xorg-server
cd "$(ls -d xorg-server-*/ | tail -1)"
cp "$HERE/vfb-dri3.c" "$HERE/vfb-dri3.h" hw/vfb/
# Add the file to the Xvfb build, and call vfbDri3Init at the end of the screen setup.
python3 - <<'EOF'
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
EOF
[ -d build ] || meson setup build -Dxorg=false -Dxephyr=false -Dxnest=false -Dxwin=false -Dxvfb=true -Ddri3=true \
  -Dglamor=false -Dudev=false -Dudev_kms=false -Dsystemd_logind=false -Dhal=false -Ddocs=false -Ddevel-docs=false -Dsecure-rpc=false \
  -Dxkb_dir=/usr/share/X11/xkb -Dxkb_bin_dir=/usr/bin -Dxkb_output_dir=/var/lib/xkb
ninja -C build hw/vfb/Xvfb
OUT=${OUT:-/usr/local/bin/Xvfb-meowtty}
install -D -m 0755 build/hw/vfb/Xvfb "$OUT"
echo "Installed $OUT"

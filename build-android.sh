#!/bin/sh
# Builds Xvfb with our DRI3 for Android (arm64, bionic, API 28): an X server that the MeowTTY app runs natively,
# outside proot. All libraries are built from their release tarballs and linked in statically.
# Output: $OUT (default $W/Xvfb-meowtty-android-arm64).
#
# Compiler: the clang of the NDK on an x86_64 host (NDK, or ANDROID_NDK_LATEST_HOME on GitHub runners).
# On an arm64 host (the Linux system of the phone) the NDK compiler does not run: then the system clang builds with the
# sysroot and the runtime libraries of the NDK. Needs: clang lld llvm meson ninja-build pkg-config autoconf automake
# libtool make gcc python3 xz-utils curl.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
W=${1:-$HOME/xandroid}
API=28
NDK=${NDK:-${ANDROID_NDK_LATEST_HOME:-}}
[ -n "$NDK" ] || { echo "Set NDK to the folder of the Android NDK."; exit 1; }
T=$NDK/toolchains/llvm/prebuilt/linux-x86_64
P=$W/prefix
OUT=${OUT:-$W/Xvfb-meowtty-android-arm64}
mkdir -p "$W/src" "$W/bin" "$P/lib/pkgconfig"
cd "$W"

# The compiler, as one command for meson and autotools.
if [ "$(uname -m)" = x86_64 ]; then
  printf '#!/bin/sh\nexec %s --target=aarch64-linux-android%s "$@"\n' "$T/bin/clang" "$API" > bin/cc
  LLVM=$T/bin
else
  # The resource folder: the headers of the system clang, the runtime of the NDK.
  rt=$(ls -d "$T"/lib/clang/*/lib/linux)
  mkdir -p rd/lib/aarch64-unknown-linux-android$API
  ln -sfn "$(clang -print-resource-dir)/include" rd/include
  ln -sf "$rt/libclang_rt.builtins-aarch64-android.a" rd/lib/aarch64-unknown-linux-android$API/libclang_rt.builtins.a
  ln -sf "$rt/aarch64/libunwind.a" rd/lib/aarch64-unknown-linux-android$API/libunwind.a
  # The link flags only when it links: meson makes an unused flag an error.
  cat > bin/cc <<EOF
#!/bin/sh
link=1
for a; do case \$a in -c|-E|-S|-M|-MM) link= ;; esac; done
set -- --target=aarch64-linux-android$API --sysroot=$T/sysroot -resource-dir $W/rd "\$@"
[ -n "\$link" ] && set -- -fuse-ld=lld -rtlib=compiler-rt --unwindlib=libunwind "\$@"
exec clang "\$@"
EOF
  LLVM=$(dirname "$(command -v llvm-ar)")
fi
chmod +x bin/cc
# Android before API 30 has no memfd_create in libc, but the kernel has the call.
cat > compat.h <<'EOF'
#if defined(__ANDROID__) && __ANDROID_API__ < 30
#include <sys/syscall.h>
#include <unistd.h>
#define memfd_create(name, flags) ((int) syscall(__NR_memfd_create, (name), (flags)))
#define HAVE_MEMFD_CREATE 1
#endif
EOF
# Bionic has no values.h (libxshmfence wants MAXINT from it).
mkdir -p shim
printf '#include <limits.h>\n#define MAXINT INT_MAX\n' > shim/values.h
export CC="$W/bin/cc" AR="$LLVM/llvm-ar" RANLIB="$LLVM/llvm-ranlib" STRIP="$LLVM/llvm-strip"
export CFLAGS="-O2 -fPIC -I$W/shim -include $W/compat.h" CC_FOR_BUILD=gcc
export PKG_CONFIG_LIBDIR="$P/lib/pkgconfig:$P/share/pkgconfig" PKG_CONFIG_PATH=
cat > cross.txt <<EOF
[binaries]
c = '$W/bin/cc'
ar = '$AR'
strip = '$STRIP'
pkg-config = 'pkg-config'
[host_machine]
system = 'android'
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'
[built-in options]
c_args = ['-O2', '-fPIC', '-I$W/shim', '-include', '$W/compat.h']
c_link_args = ['-L$P/lib', '-lz']
default_library = 'static'
EOF
# Bionic has pthread and rt in libc. Some builds ask for the libraries by name: empty ones satisfy them.
for l in pthread rt; do [ -e "$P/lib/lib$l.a" ] || "$AR" rc "$P/lib/lib$l.a"; done
# zlib is in the sysroot; some configure scripts ask pkg-config for it.
printf 'Name: zlib\nDescription: zlib of the NDK\nVersion: 1.3\nLibs: -lz\n' > "$P/lib/pkgconfig/zlib.pc"

# name, URL, sha256: download, check and unpack into src/<name>.
fetch() {
  f=src/$(basename "$2")
  [ -e "$f" ] || curl -sSfL -o "$f" "$2"
  echo "$3  $f" | sha256sum -c --quiet
  rm -rf "src/$1" && mkdir "src/$1" && tar -xf "$f" -C "src/$1" --strip-components=1
  cd "src/$1"
}
# No "&&" here: set -e does not stop the script for a failure inside an && list.
m() { meson setup build --cross-file "$W/cross.txt" --prefix="$P" --libdir=lib --buildtype=release "$@"; ninja -C build install; cd "$W"; }
a() { ./configure --host=aarch64-linux-android --prefix="$P" --enable-static --disable-shared "$@"; make -j"$(nproc)" install; cd "$W"; }

X=https://www.x.org/releases/individual
# SKIP_DEPS=1: the libraries in the prefix stay; only the X server builds again.
if [ -z "${SKIP_DEPS:-}" ]; then
fetch xorgproto $X/proto/xorgproto-2024.1.tar.xz 372225fd40815b8423547f5d890c5debc72e88b91088fbfb13158c20495ccb59
m -Dlegacy=false
fetch xtrans $X/lib/xtrans-1.5.2.tar.xz 5c5cbfe34764a9131d048f03c31c19e57fb4c682d67713eab6a65541b4dff86c
a
fetch xcb-proto $X/proto/xcb-proto-1.17.0.tar.xz 2c1bacd2110f4799f74de6ebb714b94cf6f80fb112316b1219480fd22562148c
a
fetch libXau $X/lib/libXau-1.0.12.tar.xz 74d0e4dfa3d39ad8939e99bda37f5967aba528211076828464d2777d477fc0fb
a
fetch libxcb $X/lib/libxcb-1.17.0.tar.xz 599ebf9996710fea71622e6e184f3a8ad5b43d0e5fa8c4e407123c88a59a6d55
a --disable-devel-docs --without-doxygen
fetch libX11 $X/lib/libX11-1.8.12.tar.xz fa026f9bb0124f4d6c808f9aef4057aad65e7b35d8ff43951cef0abe06bb9a9a
a --disable-specs --without-xmlto --without-fop --disable-xf86bigfont --enable-malloc0returnsnull
fetch libxkbfile $X/lib/libxkbfile-1.1.3.tar.xz a9b63eea997abb9ee6a8b4fbb515831c841f471af845a09de443b28003874bec
m
fetch libfontenc $X/lib/libfontenc-1.1.8.tar.xz 7b02c3d405236e0d86806b1de9d6868fe60c313628b38350b032914aa4fd14c6
a
fetch libXfont2 $X/lib/libXfont2-2.0.7.tar.xz 8b7b82fdeba48769b69433e8e3fbb984a5f6bf368b0d5f47abeec49de3e58efb
a --disable-freetype --disable-devel-docs --disable-fc --disable-snfformat
fetch pixman https://www.cairographics.org/releases/pixman-0.44.2.tar.gz 6349061ce1a338ab6952b92194d1b0377472244208d47ff25bef86fc71973466
m -Dgtk=disabled -Dlibpng=disabled -Dtests=disabled -Ddemos=disabled -Dopenmp=disabled
fetch libdrm https://dri.freedesktop.org/libdrm/libdrm-2.4.124.tar.xz ac36293f61ca4aafaf4b16a2a7afff312aa4f5c37c9fbd797de9e3c0863ca379
m -Dintel=disabled -Dradeon=disabled -Damdgpu=disabled -Dnouveau=disabled -Dvmwgfx=disabled -Domap=disabled \
  -Dexynos=disabled -Dfreedreno=disabled -Dtegra=disabled -Dvc4=disabled -Detnaviv=disabled -Dcairo-tests=disabled \
  -Dman-pages=disabled -Dvalgrind=disabled -Dtests=false -Dinstall-test-programs=false -Dudev=false
fetch libxshmfence $X/lib/libxshmfence-1.3.3.tar.xz d4a4df096aba96fea02c029ee3a44e11a47eb7f7213c1a729be83e85ec3fde10
a --enable-futex
fetch nettle https://ftp.gnu.org/gnu/nettle/nettle-3.10.1.tar.gz b0fcdd7fc0cdea6e80dcf1dd85ba794af0d5b4a57e26397eee3bc193272d9132
a --disable-public-key --disable-documentation --disable-openssl --disable-mini-gmp
fi

fetch xorg-server $X/xserver/xorg-server-21.1.22.tar.xz 1a242c8917c49ba29ccc1f6021613d8a2b9805dd0d271a66ae9d09f4b0bb06b3
python3 "$HERE/patch-dri3.py"
python3 "$HERE/patch-android.py" "$P"
# The paths below are the ones in the Linux system; the app gives the real ones (-xkbdir, -fp, MEOWTTY_XKBCOMP).
meson setup build --cross-file "$W/cross.txt" --prefix="$P" --buildtype=release \
  -Dxorg=false -Dxephyr=false -Dxnest=false -Dxwin=false -Dxquartz=false -Dxvfb=true \
  -Dglx=false -Dglamor=false -Ddri1=false -Ddri2=false -Ddri3=true -Dudev=false -Dudev_kms=false -Dsystemd_logind=false \
  -Dhal=false -Ddocs=false -Ddevel-docs=false -Dsecure-rpc=false -Dmitshm=false -Dxdmcp=false -Dxdm-auth-1=false \
  -Dsha1=libnettle -Dxselinux=false -Dlinux_apm=false -Dlinux_acpi=false -Dvgahw=false -Dlibunwind=false \
  -Dxkb_dir=/usr/share/X11/xkb -Dxkb_bin_dir=/usr/bin -Dxkb_output_dir=/tmp -Ddefault_font_path=built-ins
ninja -C build hw/vfb/Xvfb
"$STRIP" -o "$OUT" build/hw/vfb/Xvfb
echo "Built $OUT"

#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PREFIX="$ROOT/toolchain"
export PATH="$PREFIX/bin:$PATH"
JOBS=${JOBS:-8}
mkdir -p "$ROOT/third_party" "$ROOT/build/binutils" "$ROOT/build/gcc"
[ -d "$ROOT/third_party/binutils-2.43.1" ] || tar -xJf "$ROOT/downloads/binutils-2.43.1.tar.xz" -C "$ROOT/third_party"
cd "$ROOT/build/binutils"
if [ ! -f Makefile ]; then
 "$ROOT/third_party/binutils-2.43.1/configure" --target=alpha-linux-gnu --prefix="$PREFIX" --disable-nls --disable-werror --disable-gdb --disable-sim --with-system-zlib > configure.log 2>&1
fi
make -j"$JOBS" MAKEINFO=true > build.log 2>&1
make install MAKEINFO=true > install.log 2>&1
[ -d "$ROOT/third_party/gcc-14.2.0" ] || tar -xJf "$ROOT/downloads/gcc-14.2.0.tar.xz" -C "$ROOT/third_party"
cd "$ROOT/build/gcc"
if [ ! -f Makefile ]; then
 "$ROOT/third_party/gcc-14.2.0/configure" --target=alpha-linux-gnu --prefix="$PREFIX" --with-system-zlib --enable-languages=c --without-headers --disable-bootstrap --disable-multilib --disable-nls --disable-shared --disable-threads --disable-libssp --disable-libquadmath --disable-libgomp --disable-libatomic --with-gmp=/opt/homebrew/opt/gmp --with-mpfr=/opt/homebrew/opt/mpfr --with-mpc=/opt/homebrew/opt/libmpc > configure.log 2>&1
fi
make -j"$JOBS" all-gcc MAKEINFO=true > build.log 2>&1
make install-gcc MAKEINFO=true > install.log 2>&1
"$PREFIX/bin/alpha-linux-gnu-gcc" --version

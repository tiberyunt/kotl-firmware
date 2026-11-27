#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
mkdir -p downloads
fetch() { if [ ! -f "downloads/$1" ]; then curl --fail --location --retry 3 "${2}" -o "downloads/$1"; fi; }
fetch binutils-2.43.1.tar.xz https://ftp.gnu.org/gnu/binutils/binutils-2.43.1.tar.xz
fetch gcc-14.2.0.tar.xz https://ftp.gnu.org/gnu/gcc/gcc-14.2.0/gcc-14.2.0.tar.xz
shasum -a 256 -c scripts/sources.sha256

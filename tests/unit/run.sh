#!/bin/bash
#
# Build and run the unit tests of src/misc.c.
#
#   tests/unit/run.sh [BUILD_DIR]
#
# Environment:
#   CC        the compiler                  [cc]
#   CFLAGS    extra compiler flags          [-O2 -g], e.g. -fsanitize=address
#   PKG_CONFIG                              [pkg-config]

set -euo pipefail

TOP=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=${1:-$TOP/tests/unit/build}
CC=${CC:-cc}
PKG_CONFIG=${PKG_CONFIG:-pkg-config}
read -r -a EXTRA <<< "${CFLAGS:--O2 -g}"
read -r -a PKG_FLAGS <<< "$("$PKG_CONFIG" --cflags --libs gtk+-3.0 vte-2.91)"

mkdir -p "$BUILD"
# The defines that src/Makefile gives misc.c
"$CC" -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Werror "${EXTRA[@]}" \
	-DSAFEMODE -DBINARY='"lilyterm"' -DPACKAGE='"LilyTerm"' -DVERSION='"test"' \
	-DETCDIR='"/etc/xdg"' -DICONDIR='"/usr/share/pixmaps"' -DLOCALEDIR='"/usr/share/locale"' \
	-I"$TOP/src" -o "$BUILD/test-misc" \
	"$TOP/tests/unit/test-misc.c" "$TOP/src/misc.c" "${PKG_FLAGS[@]}"

"$BUILD/test-misc"

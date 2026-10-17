#!/bin/sh
#
# Turn the output of the static build into the files of a release.
#
#   docker build -f docker/Dockerfile.static --target export --output dist .
#   docker/package.sh [DIST_DIR] [OUT_DIR]
#
# It writes, in OUT_DIR (default: release):
#   lilyterm-ARCH                        the bare binary: download, chmod +x, run
#   lilyterm-VERSION-linux-ARCH.tar.gz   the binary, its data files and install.sh
#   SHA256SUMS

set -eu

TOP=$(cd "$(dirname "$0")/.." && pwd)
DIST=${1:-$TOP/dist}
OUT=${2:-$TOP/release}

VERSION=${VERSION:-$(sed -n 's/^VERSION = //p' "$TOP/.default")}
ARCH=${ARCH:-$(uname -m)}
NAME=lilyterm-$VERSION-linux-$ARCH

[ -x "$DIST/lilyterm" ] || { echo "$DIST/lilyterm is missing: build the static binary first." >&2; exit 1; }

# Only ever publish a binary that really is standalone
if LC_ALL=C readelf -l "$DIST/lilyterm" | grep -q 'Requesting program interpreter' ||
   LC_ALL=C readelf -d "$DIST/lilyterm" | grep -q NEEDED; then
	echo "$DIST/lilyterm is not a static binary." >&2
	exit 1
fi

rm -rf "$OUT"
mkdir -p "$OUT/$NAME"

cp -a "$DIST/lilyterm" "$DIST/share" "$DIST/etc" "$OUT/$NAME/"
install -m 755 "$TOP/docker/install.sh" "$OUT/$NAME/install.sh"
install -m 644 "$TOP/COPYING" "$OUT/$NAME/COPYING"
cat > "$OUT/$NAME/README.txt" << EOF
LilyTerm $VERSION, static build for Linux $ARCH

One binary with GTK+ 3 and VTE linked in. It runs on X11 and on Wayland and
depends on no library of the system it runs on.

Run it from here:              ./lilyterm
Install it for yourself:       ./install.sh            (in ~/.local)
Install it for everyone:       sudo ./install.sh       (in /usr/local)
Remove it again:               ./install.sh --uninstall

Good to know:
 * The translations are only found in /usr/share/locale: install with
   PREFIX=/usr to get them. Otherwise the interface is in English.
 * On Wayland, theme and font settings are read from
   ~/.config/gtk-3.0/settings.ini and \$GTK_THEME, not from dconf.
 * IBus/Fcitx users on X11: start it with GTK_IM_MODULE=xim.

Source code, and the details: https://github.com/ebal/LilyTerm
LilyTerm is free software under the GNU GPL version 3, see COPYING.
EOF

# A reproducible archive: fixed order, owner and (if given) time stamps
tar -C "$OUT" --sort=name --owner=0 --group=0 --numeric-owner \
	${SOURCE_DATE_EPOCH:+--mtime="@$SOURCE_DATE_EPOCH"} \
	-cf - "$NAME" | gzip -9n > "$OUT/$NAME.tar.gz"
rm -rf "${OUT:?}/$NAME"

cp -a "$DIST/lilyterm" "$OUT/lilyterm-$ARCH"

( cd "$OUT" && sha256sum "lilyterm-$ARCH" "$NAME.tar.gz" > SHA256SUMS )

ls -l "$OUT"
cat "$OUT/SHA256SUMS"

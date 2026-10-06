#!/bin/sh
#
# Install (or remove) the static LilyTerm from an unpacked release tarball.
#
#   ./install.sh               install for the current user, in ~/.local
#   sudo ./install.sh          install for everyone, in /usr/local
#   PREFIX=/usr sudo ./install.sh
#   ./install.sh --uninstall   remove what was installed in that prefix
#
# The binary has no dependency, and nothing here is needed to run it:
# ./lilyterm works straight from this directory. Installing only adds the
# menu entry, the icon and the man page.

set -eu

HERE=$(cd "$(dirname "$0")" && pwd)

if [ -z "${PREFIX:-}" ]; then
	if [ "$(id -u)" -eq 0 ]; then
		PREFIX=/usr/local
	else
		PREFIX=$HOME/.local
	fi
fi

BIN=$PREFIX/bin/lilyterm
DESKTOP=$PREFIX/share/applications/lilyterm.desktop
ICON=$PREFIX/share/icons/hicolor/48x48/apps/lilyterm.png
MAN=$PREFIX/share/man/man1/lilyterm.1.gz
# The two paths that are compiled into the binary
SYSCONF=/etc/xdg/lilyterm.conf
LOCALEDIR=/usr/share/locale

if [ "${1:-}" = "--uninstall" ]; then
	rm -f "$BIN" "$DESKTOP" "$ICON" "$MAN"
	if [ "$PREFIX" = /usr ]; then
		for mo in "$HERE"/share/locale/*/LC_MESSAGES/lilyterm.mo; do
			[ -f "$mo" ] && rm -f "$LOCALEDIR/${mo#"$HERE"/share/locale/}"
		done
	fi
	echo "LilyTerm is removed from $PREFIX"
	[ -f "$SYSCONF" ] && echo "$SYSCONF is left in place, remove it yourself if you do not need it."
	exit 0
fi

install -D -m 755 "$HERE/lilyterm" "$BIN"
install -D -m 644 "$HERE/share/pixmaps/lilyterm.png" "$ICON"
install -D -m 644 "$HERE/share/man/man1/lilyterm.1.gz" "$MAN"

# An absolute path in the menu entry: $PREFIX/bin does not have to be in $PATH
install -d "$(dirname "$DESKTOP")"
sed -e "s|^TryExec=.*|TryExec=$BIN|" -e "s|^Exec=.*|Exec=$BIN|" \
	"$HERE/share/applications/lilyterm.desktop" > "$DESKTOP"
chmod 644 "$DESKTOP"

if [ "$(id -u)" -eq 0 ]; then
	# never overwrite a system wide configuration that is already there
	[ -e "$SYSCONF" ] || install -D -m 644 "$HERE/etc/xdg/lilyterm.conf" "$SYSCONF"
fi

# The translations are looked up in /usr/share/locale only
if [ "$PREFIX" = /usr ]; then
	for mo in "$HERE"/share/locale/*/LC_MESSAGES/lilyterm.mo; do
		[ -f "$mo" ] && install -D -m 644 "$mo" "$LOCALEDIR/${mo#"$HERE"/share/locale/}"
	done
fi

command -v update-desktop-database >/dev/null 2>&1 &&
	update-desktop-database -q "$PREFIX/share/applications" 2>/dev/null || true
command -v gtk-update-icon-cache >/dev/null 2>&1 &&
	gtk-update-icon-cache -q -t -f "$PREFIX/share/icons/hicolor" 2>/dev/null || true

echo "LilyTerm is installed: $BIN"
case ":$PATH:" in
	*":$PREFIX/bin:"*) ;;
	*) echo "Note: $PREFIX/bin is not in your PATH. The menu entry works anyway." ;;
esac
[ "$PREFIX" = /usr ] || echo "Note: the translations are only installed with PREFIX=/usr. The interface is in English."

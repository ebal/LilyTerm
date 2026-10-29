#!/bin/bash
#
# Build LilyTerm.app for macOS, with GTK+ 3, VTE and everything they need
# copied into it, so that it runs without Homebrew, and put it in a .dmg.
#
#   brew install gtk+3 vte3 librsvg adwaita-icon-theme pkgconf gettext
#   macos/bundle-app.sh [OUT_DIR]
#
# It writes, in OUT_DIR (default: release):
#   LilyTerm-VERSION-macos-ARCH.dmg   LilyTerm.app and a link to /Applications
#
# LilyTerm keeps the paths of its data files (the icon, the translations, the
# system profile) from the build. They point into /Applications/LilyTerm.app:
# that is where the app has to be.
#
# The app is signed ad hoc only, not by Apple: once copied from the .dmg, run
#   xattr -dr com.apple.quarantine /Applications/LilyTerm.app
#
# macOS still has bash 3.2: no associative arrays, no mapfile here.

set -euo pipefail

TOP=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:-$TOP/release}

die() { echo "bundle-app: $*" >&2; exit 1; }

[ "$(uname -s)" = Darwin ] || die "this builds a macOS app: run it on macOS"
command -v brew > /dev/null 2>&1 || die "brew is not in PATH"

VERSION=$(sed -n 's/^VERSION = //p' "$TOP/.default")
ARCH=$(uname -m)
NAME=LilyTerm-$VERSION-macos-$ARCH
BREW=$(brew --prefix)
# The oldest macOS it runs on: the one the Homebrew bottles were built for
MACOS_MIN=$(sw_vers -productVersion | cut -d. -f1).0

INSTALL_DIR=/Applications/LilyTerm.app
RES_PREFIX=$INSTALL_DIR/Contents/Resources

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
APP=$WORK/LilyTerm.app
CONTENTS=$APP/Contents
MACOS=$CONTENTS/MacOS
FRAMEWORKS=$CONTENTS/Frameworks
RES=$CONTENTS/Resources
FW_REF=@executable_path/../Frameworks

# ---- LilyTerm itself ----

echo "== building LilyTerm $VERSION"
BUILD=$WORK/build
mkdir -p "$BUILD"
tar -C "$TOP" --exclude ./.git --exclude ./release --exclude ./dist -cf - . | tar -C "$BUILD" -xf -
(
	cd "$BUILD"
	./configure --prefix="$RES_PREFIX" --sysconfdir="$RES_PREFIX/etc/xdg" --disable-gnome-control-center
	make
	make DESTDIR="$WORK/stage" install
)

mkdir -p "$MACOS" "$FRAMEWORKS"
mv "$WORK/stage$RES_PREFIX" "$RES"
# The program itself is the main executable of the app, no launcher script:
# macOS knows the app, and the Local Network permission it gave it (ssh to
# a computer at home), by the Mach-O main executable, and finds the new one
# after an update. A script has none: the permission went to the program it
# started, another program for macOS in every release. LilyTerm sets up its
# GTK+ 3 itself in the app (macos_app_init() in src/misc.c).
mv "$RES/bin/lilyterm" "$MACOS/LilyTerm"
rmdir "$RES/bin"

# ---- the libraries ----

# The libraries a Mach-O file needs from Homebrew, one per line
brew_refs() {
	local self
	self=$(otool -D "$1" | sed -n 2p)
	otool -L "$1" | tail -n +2 | awk '{print $1}' |
		grep -E "^($BREW/|/opt/homebrew/|/usr/local/|@rpath/|@loader_path/)" |
		grep -vxF "${self:-//none//}" || true
}

# Where a reference of a Mach-O file (copied from ORIGIN) is on disk
resolve_ref() {
	local ref=$1 origin=$2 name dir
	case $ref in
		@loader_path/*)
			dir=$(dirname "$origin")
			[ -e "$dir/${ref#@loader_path/}" ] && { echo "$dir/${ref#@loader_path/}"; return; }
			;;
		@rpath/*)
			name=${ref#@rpath/}
			for dir in "$(dirname "$origin")" "$BREW/lib" "$BREW"/opt/*/lib; do
				[ -e "$dir/$name" ] && { echo "$dir/$name"; return; }
			done
			;;
		*)
			[ -e "$ref" ] && { echo "$ref"; return; }
			;;
	esac
	return 1
}

# Copy the Homebrew libraries FILE needs into Frameworks, and point FILE at
# the copies; again for each copy.
bundle_file() {
	local file=$1 origin=$2 ref dep name
	for ref in $(brew_refs "$file"); do
		dep=$(resolve_ref "$ref" "$origin") || die "can not find $ref, needed by $origin"
		name=$(basename "$dep")
		if [ ! -e "$FRAMEWORKS/$name" ]; then
			cp -L "$dep" "$FRAMEWORKS/$name"
			chmod u+w "$FRAMEWORKS/$name"
			install_name_tool -id "$FW_REF/$name" "$FRAMEWORKS/$name" 2> /dev/null
			bundle_file "$FRAMEWORKS/$name" "$(realpath "$dep")"
		fi
		install_name_tool -change "$ref" "$FW_REF/$name" "$file" 2> /dev/null
	done
}

# Loadable modules (image loaders, input methods): copy them and their
# libraries, and write their cache with @RESOURCES@ in place of the path of
# the app, which LilyTerm fills in when it starts.
#   bundle_modules SRC_DIR DEST_DIR CACHE QUERY_COMMAND
bundle_modules() {
	local src=$1 dest=$2 cache=$3 query=$4 module name
	[ -d "$src" ] || { echo "   (no $src: built in)"; return 0; }
	ls "$src"/*.so > /dev/null 2>&1 || { echo "   (no module in $src)"; return 0; }
	mkdir -p "$dest"
	# Query the modules of Homebrew before rewriting the copies: the copies
	# only load from inside the app. The cache names each module by its path
	# (in the Cellar, or through opt/), and the input methods also name
	# where their translations are: point both into the app.
	"$query" "$src"/*.so | sed -E \
		-e '/^#/d' \
		-e "s|\"[^\"]*/$(basename "$src")/|\"@RESOURCES@/${dest#"$RES"/}/|g" \
		-e 's|"[^"]*/share/locale"|"@RESOURCES@/share/locale"|g' > "$cache"
	for module in "$src"/*.so; do
		name=$(basename "$module")
		cp -L "$module" "$dest/"
		chmod u+w "$dest/$name"
		# A module has an install name too, still the one in Homebrew
		install_name_tool -id "@executable_path/../Resources/${dest#"$RES"/}/$name" "$dest/$name" 2> /dev/null || true
		bundle_file "$dest/$name" "$(realpath "$module")"
	done
}

echo "== copying the libraries"
bundle_file "$MACOS/LilyTerm" "$MACOS/LilyTerm"

echo "== copying the image loaders"
PIXBUF_DIR=lib/gdk-pixbuf-2.0/2.10.0
mkdir -p "$RES/$PIXBUF_DIR"
bundle_modules "$BREW/$PIXBUF_DIR/loaders" "$RES/$PIXBUF_DIR/loaders" \
	"$RES/$PIXBUF_DIR/loaders.cache.in" "$BREW/bin/gdk-pixbuf-query-loaders"
touch "$RES/$PIXBUF_DIR/loaders.cache.in"

echo "== copying the input methods"
IM_DIR=lib/gtk-3.0/3.0.0
mkdir -p "$RES/$IM_DIR"
bundle_modules "$BREW/$IM_DIR/immodules" "$RES/$IM_DIR/immodules" \
	"$RES/$IM_DIR/immodules.cache.in" "$BREW/bin/gtk-query-immodules-3.0"
touch "$RES/$IM_DIR/immodules.cache.in"

# No GIO modules: GLib must not load the ones of a Homebrew it finds
mkdir -p "$RES/lib/gio/modules"

# ---- the data files of GTK+ ----

echo "== copying the GSettings schemas, icons and font settings"
mkdir -p "$RES/share/glib-2.0/schemas" "$RES/share/icons" "$RES/etc"
cp "$BREW"/share/glib-2.0/schemas/*.xml "$RES/share/glib-2.0/schemas/"
"$BREW/bin/glib-compile-schemas" "$RES/share/glib-2.0/schemas"

for theme in Adwaita hicolor; do
	[ -d "$BREW/share/icons/$theme" ] || die "the icon theme $theme is missing: brew install adwaita-icon-theme"
	cp -RL "$BREW/share/icons/$theme" "$RES/share/icons/"
	"$BREW/bin/gtk3-update-icon-cache" -q -f -t "$RES/share/icons/$theme" ||
		"$BREW/bin/gtk-update-icon-cache" -q -f -t "$RES/share/icons/$theme" || true
done

cp -RL "$BREW/etc/fonts" "$RES/etc/fonts"

# ---- the app ----

echo "== writing Info.plist and the icon"
cat > "$CONTENTS/Info.plist" << EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleName</key>
	<string>LilyTerm</string>
	<key>CFBundleDisplayName</key>
	<string>LilyTerm</string>
	<key>CFBundleIdentifier</key>
	<string>com.github.ebal.lilyterm</string>
	<key>CFBundleExecutable</key>
	<string>LilyTerm</string>
	<key>CFBundleIconFile</key>
	<string>LilyTerm</string>
	<key>CFBundlePackageType</key>
	<string>APPL</string>
	<key>CFBundleShortVersionString</key>
	<string>$VERSION</string>
	<key>CFBundleVersion</key>
	<string>$VERSION</string>
	<key>LSMinimumSystemVersion</key>
	<string>$MACOS_MIN</string>
	<key>LSApplicationCategoryType</key>
	<string>public.app-category.developer-tools</string>
	<key>NSHighResolutionCapable</key>
	<true/>
	<key>NSHumanReadableCopyright</key>
	<string>GPL-3.0-or-later</string>
	<key>NSLocalNetworkUsageDescription</key>
	<string>The programs in LilyTerm, as ssh, connect to computers on your local network.</string>
</dict>
</plist>
EOF


ICONSET=$WORK/LilyTerm.iconset
mkdir "$ICONSET"
for size in 16 32 128 256 512; do
	sips -z "$size" "$size" "$TOP/data/lilyterm.png" --out "$ICONSET/icon_${size}x${size}.png" > /dev/null
	sips -z "$((size * 2))" "$((size * 2))" "$TOP/data/lilyterm.png" --out "$ICONSET/icon_${size}x${size}@2x.png" > /dev/null
done
iconutil -c icns "$ICONSET" -o "$RES/LilyTerm.icns"

# ---- check that nothing points to Homebrew any more ----

echo "== checking that the app is self-contained"
MACHO_LIST=$WORK/macho.list
find "$APP" -type f \( -name '*.dylib' -o -name '*.so' \) > "$MACHO_LIST"
echo "$MACOS/LilyTerm" >> "$MACHO_LIST"
failed=0
while IFS= read -r file; do
	self=$(otool -D "$file" | sed -n 2p)
	if refs=$(otool -L "$file" | tail -n +2 | awk '{print $1}' | grep -vxF "${self:-//none//}" |
		grep -E "^($BREW/|/opt/homebrew/|/usr/local/|@rpath/|@loader_path/)"); then
		echo "   ${file#"$APP"/} still needs: $refs" >&2
		failed=1
	fi
done < "$MACHO_LIST"
for cache in "$RES/$PIXBUF_DIR/loaders.cache.in" "$RES/$IM_DIR/immodules.cache.in"; do
	if grep -qE "$BREW/|/opt/homebrew/|/usr/local/" "$cache"; then
		echo "   ${cache#"$APP"/} still points to Homebrew:" >&2
		grep -nE "$BREW/|/opt/homebrew/|/usr/local/" "$cache" >&2
		failed=1
	fi
done
[ $failed -eq 0 ] || die "the app is not self-contained"
echo "   $(wc -l < "$MACHO_LIST" | tr -d ' ') Mach-O files, all inside the app"

# ---- sign: Apple Silicon runs no unsigned code ----
#
# With SIGN_IDENTITY (a code signing certificate, in SIGN_KEYCHAIN if set)
# the app is the same app for macOS in every release: it keeps the
# permissions it was given, as Local Network (ssh to a computer at home),
# with its main executable a Mach-O (see above). Ad hoc, every build is
# another app, and ssh gets "No route to host" there after an update. The
# certificate may be self-signed: no timestamp then.

if [ -n "${SIGN_IDENTITY:-}" ]; then
	echo "== signing with \"$SIGN_IDENTITY\""
else
	echo "== signing (ad hoc: macOS forgets the permissions of LilyTerm with each update)"
fi
sign() {
	if [ -z "${SIGN_IDENTITY:-}" ]; then
		codesign --force --sign - "$1"
	elif [ -n "${SIGN_KEYCHAIN:-}" ]; then
		codesign --force --timestamp=none --sign "$SIGN_IDENTITY" --keychain "$SIGN_KEYCHAIN" "$1"
	else
		codesign --force --timestamp=none --sign "$SIGN_IDENTITY" "$1"
	fi
}
# the libraries and modules first; the main executable with the app, as
# the app (its identifier, its Info.plist)
while IFS= read -r file; do
	[ "$file" = "$MACOS/LilyTerm" ] || sign "$file"
done < "$MACHO_LIST"
sign "$APP"
codesign --verify --deep --strict "$APP"
file "$MACOS/LilyTerm" | grep -q 'Mach-O' || die "the main executable is not a Mach-O"
echo "   main executable: $(dwarfdump --uuid "$MACOS/LilyTerm" | awk '{print $2, $3}')"
# what macOS knows the app by: the same in every release with a certificate
# (codesign shows an implicit one, as here, after a "# ")
echo "   designated requirement: $(codesign -d -r- "$APP" 2>&1 | sed -n 's/^#* *designated => //p')"

# ---- the .dmg ----

echo "== writing $NAME.dmg"
DMG_ROOT=$WORK/dmg
mkdir -p "$DMG_ROOT" "$OUT"
cp -R "$APP" "$DMG_ROOT/"
ln -s /Applications "$DMG_ROOT/Applications"
cat > "$DMG_ROOT/Read me first.txt" << EOF
LilyTerm $VERSION for macOS on Apple Silicon (macOS $MACOS_MIN or later)

1. Drag LilyTerm to Applications. It has to be in /Applications.
2. LilyTerm is not signed by Apple, so macOS refuses to open it the first
   time. Open Terminal once and run:

     xattr -dr com.apple.quarantine /Applications/LilyTerm.app

3. Open LilyTerm from Launchpad, Spotlight or the Applications folder.

https://github.com/ebal/LilyTerm
EOF
rm -f "$OUT/$NAME.dmg"
hdiutil create -volname "LilyTerm $VERSION" -srcfolder "$DMG_ROOT" -fs HFS+ -format UDZO "$OUT/$NAME.dmg" > /dev/null
du -sh "$APP" "$OUT/$NAME.dmg"
echo "== done: $OUT/$NAME.dmg"

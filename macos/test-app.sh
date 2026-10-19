#!/bin/bash
#
# Test LilyTerm.app from the .dmg as a user gets it: copied to /Applications,
# and with Homebrew out of the way, so that it can only use what is inside.
#
#   macos/test-app.sh DMG [OUT_DIR]
#
# It needs sudo (to move Homebrew away for the test, and back), so it is
# meant for a CI runner. It writes, in OUT_DIR (default: test-output):
#   lilyterm-macos.log   what LilyTerm printed
#   lilyterm-macos.png   a screenshot, if the runner allows one

set -euo pipefail

DMG=${1:?usage: macos/test-app.sh DMG [OUT_DIR]}
OUT=${2:-test-output}
APP=/Applications/LilyTerm.app

die() { echo "test-app: $*" >&2; exit 1; }

mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
VERSION=$(basename "$DMG" | sed -n 's/^LilyTerm-\(.*\)-macos-.*\.dmg$/\1/p')
[ -n "$VERSION" ] || die "$DMG is not named LilyTerm-VERSION-macos-ARCH.dmg"

echo "== copying LilyTerm.app from $(basename "$DMG") to /Applications"
MOUNT=$(mktemp -d)
hdiutil attach -nobrowse -readonly -mountpoint "$MOUNT" "$DMG" > /dev/null
rm -rf "$APP"
cp -R "$MOUNT/LilyTerm.app" /Applications/
hdiutil detach "$MOUNT" > /dev/null
codesign --verify --deep --strict "$APP"

BREW=$(brew --prefix)
echo "== moving $BREW away for the test"
sudo mv "$BREW" "$BREW.hidden"
trap 'sudo mv "$BREW.hidden" "$BREW"' EXIT

echo "== lilyterm --version"
"$APP/Contents/MacOS/LilyTerm" --version | tee "$OUT/version.txt"
grep -q "^LilyTerm $VERSION" "$OUT/version.txt" || die "expected LilyTerm $VERSION"

# As a user starts it: a terminal with a login shell. (-e would first ask
# whether to run the command.)
echo "== starting LilyTerm"
LOG=$OUT/lilyterm-macos.log
"$APP/Contents/MacOS/LilyTerm" > "$LOG" 2>&1 &
PID=$!
sleep 10
if ! kill -0 "$PID" 2> /dev/null; then
	cat "$LOG"
	die "LilyTerm exited within 10 seconds"
fi
screencapture -x "$OUT/lilyterm-macos.png" 2> /dev/null || echo "   (no screenshot)"
kill "$PID"
wait "$PID" 2> /dev/null || true

echo "== what LilyTerm printed"
cat "$LOG"
# A library, module, schema or icon that the app looked for outside itself
if grep -nE 'CRITICAL|Failed to load|Could not load|cannot open|No such file|not found|Library not loaded' "$LOG"; then
	die "LilyTerm could not find some of its files"
fi
echo "== passed"

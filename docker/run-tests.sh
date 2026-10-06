#!/bin/bash
#
# Build LilyTerm and smoke-test it under X11 (Xvfb) and Wayland (headless sway).
#
# Usage:
#   run-tests                 build /src out-of-tree in /build, then test it
#   LILYTERM=/path/to/binary run-tests     only test an already built binary
#
# Environment:
#   SRC       source tree                      [/src]
#   BUILD     scratch build directory          [/build]
#   OUT       where logs/screenshots are kept  [$BUILD/test-output]
#   BACKENDS  which display servers to test    ["x11 wayland"]
#   CFLAGS    passed to ./configure            ["-Wall -O2 -g"]

set -u

SRC=${SRC:-/src}
BUILD=${BUILD:-/build}
OUT=${OUT:-$BUILD/test-output}
BACKENDS=${BACKENDS:-x11 wayland}
LILYTERM=${LILYTERM:-}

PASS=0
FAIL=0
PIDS=()

ok()   { PASS=$((PASS+1)); printf '\033[1;32m  PASS\033[0m %s\n' "$*"; }
ng()   { FAIL=$((FAIL+1)); printf '\033[1;31m  FAIL\033[0m %s\n' "$*"; }
note() { printf '\033[1;36m== %s\033[0m\n' "$*"; }

cleanup() {
	for pid in "${PIDS[@]:-}"; do
		[ -n "$pid" ] && kill "$pid" 2>/dev/null
	done
}
trap cleanup EXIT

# wait_for SECONDS COMMAND...: poll until COMMAND succeeds
wait_for() {
	local tries=$(($1 * 10)); shift
	while [ $tries -gt 0 ]; do
		"$@" >/dev/null 2>&1 && return 0
		sleep 0.1
		tries=$((tries-1))
	done
	return 1
}

mkdir -p "$OUT"

# ImageMagick 7 calls it magick, and nags about convert
MAGICK=$(command -v magick || command -v convert)

# ---------------------------------------------------------------- build ----

if [ -z "$LILYTERM" ]; then
	note "Building $SRC in $BUILD/lilyterm"
	mkdir -p "$BUILD/lilyterm"
	rsync -a --delete --exclude .git --exclude test-output "$SRC"/ "$BUILD/lilyterm"/
	cd "$BUILD/lilyterm" || exit 1
	make distclean >/dev/null 2>&1

	if CFLAGS="${CFLAGS:--Wall -O2 -g}" ./configure --prefix=/usr > "$OUT/configure.log" 2>&1; then
		ok "configure ($(grep -E '^(GTK|VTE) = ' .config | tr '\n' ' '))"
	else
		ng "configure (see $OUT/configure.log)"; cat "$OUT/configure.log"; exit 1
	fi

	if make > "$OUT/make.log" 2>&1; then
		ok "make ($(grep -c 'warning:' "$OUT/make.log") compiler warnings)"
	else
		ng "make (see $OUT/make.log)"; grep -E 'error|Error' "$OUT/make.log" | head -40; exit 1
	fi

	if make install DESTDIR="$BUILD/destdir" > "$OUT/install.log" 2>&1 &&
	   [ -x "$BUILD/destdir/usr/bin/lilyterm" ]; then
		ok "make install DESTDIR=..."
	else
		ng "make install (see $OUT/install.log)"
	fi
	LILYTERM=$BUILD/lilyterm/src/lilyterm
fi

note "Testing $LILYTERM"
file "$LILYTERM" 2>/dev/null | sed 's/^/  /'

# ------------------------------------------------------- no display tests --

if env -u DISPLAY -u WAYLAND_DISPLAY "$LILYTERM" -v 2>&1 | grep -q "LilyTerm"; then
	ok "lilyterm -v works without a display"
else
	ng "lilyterm -v without a display"
fi

if env -u DISPLAY -u WAYLAND_DISPLAY "$LILYTERM" --help 2>&1 | grep -q -- "--geometry"; then
	ok "lilyterm --help works without a display"
else
	ng "lilyterm --help without a display"
fi

# ------------------------------------------------------------ GUI tests ----

mkdir -p "$XDG_RUNTIME_DIR" && chmod 700 "$XDG_RUNTIME_DIR"

# Every test gets its own HOME so no profile is carried over, and runs with
# -s (separate process) unless it is the socket test.
run_gui_tests() {
	local backend=$1
	local work="$OUT/$backend"
	rm -rf "$work"; mkdir -p "$work/home"
	export HOME="$work/home" XDG_CONFIG_HOME="$work/home/.config"

	# A value with a tab in it used to end up as an invalid entry in the
	# child environment, and vte then refused to spawn anything at all.
	export LILYTERM_TEST_TAB="left	right" LILYTERM_TEST_EMPTY=

	# --- 1. a command runs inside the terminal, with a sane environment
	local marker="$work/env.txt"
	timeout 40 "$LILYTERM" -s -E sh -c "
		{ echo TERM=\$TERM; echo VTE_VERSION=\$VTE_VERSION; echo COLORTERM=\$COLORTERM;
		  echo WINDOWID=\$WINDOWID; echo SIZE=\$(stty size); tty; } > $marker.tmp
		mv $marker.tmp $marker
		printf '\\033[1;37;41m LilyTerm \\033[1;37;42m rendering \\033[1;37;44m test \\033[0m\\n'
		echo 'The quick brown fox jumps over the lazy dog 0123456789'
		sleep 12" > "$work/run1.log" 2>&1 &
	local pid=$!

	if wait_for 15 test -s "$marker"; then
		ok "[$backend] command runs in a pty ($(tr '\n' ' ' < "$marker"))"
	else
		ng "[$backend] command did not run in the terminal"; sed 's/^/      /' "$work/run1.log" | head -20
	fi
	grep -q '^TERM=xterm' "$marker" 2>/dev/null &&
		ok "[$backend] TERM is set" || ng "[$backend] TERM is not set"
	grep -q '/dev/pts/' "$marker" 2>/dev/null &&
		ok "[$backend] child has a controlling tty" || ng "[$backend] child has no tty"

	if [ "$backend" = x11 ]; then
		if wait_for 5 xdotool search --class lilyterm; then
			ok "[x11] toplevel window is mapped (WM_CLASS=$(xprop -id "$(xdotool search --class lilyterm | head -n1)" WM_CLASS 2>/dev/null | cut -d= -f2 | tr -d ' '))"
		else
			ng "[x11] no toplevel window found"
		fi
	fi

	# The terminal must really draw text: the 3 colored words and the
	# antialiased glyphs give far more than a handful of distinct colors.
	local shot="$work/screenshot.png" colors=0 tries=50
	while [ $tries -gt 0 ]; do
		if [ "$backend" = x11 ]; then
			xwd -root -silent 2>/dev/null | $MAGICK xwd:- "$shot" 2>/dev/null
		else
			grim "$shot" 2>/dev/null
		fi
		colors=$($MAGICK "$shot" -format %k info: 2>/dev/null || echo 0)
		[ "${colors:-0}" -ge 16 ] && break
		sleep 0.2
		tries=$((tries-1))
	done
	if [ "${colors:-0}" -ge 16 ]; then
		ok "[$backend] text is rendered ($colors distinct colors in $shot)"
	else
		ng "[$backend] nothing seems to be rendered (${colors:-0} distinct colors in $shot)"
	fi

	# LilyTerm must exit by itself, cleanly, when the last tab closes
	if wait "$pid"; then
		ok "[$backend] exits cleanly when the last tab closes"
	else
		ng "[$backend] exit status $? after the last tab closed"; sed 's/^/      /' "$work/run1.log" | head -20
	fi

	if grep -E 'CRITICAL|WARNING \*\*.*(VTE|Gtk|Gdk|GLib)|-WARNING|Segmentation|assertion|Gdk-ERROR|BadWindow|BadMatch' "$work/run1.log" > "$work/run1.bad"; then
		ng "[$backend] runtime criticals:"; sort "$work/run1.bad" | uniq -c | sed 's/^/      /' | head -20
	else
		ok "[$backend] no GLib/GTK criticals on stderr"
	fi

	# --- 2. -g/--geometry sets the terminal size (COLUMNSxROWS)
	local geometry
	for geometry in 100x30 132x43 80x24+10+10; do
		marker="$work/size-$geometry.txt"
		timeout 30 "$LILYTERM" -s -g "$geometry" -E sh -c "
			sleep 1; stty size > $marker.tmp; mv $marker.tmp $marker" > "$work/run-$geometry.log" 2>&1
		local want="${geometry%%+*}"
		want="${want#*x} ${want%x*}"
		if [ "$(cat "$marker" 2>/dev/null)" = "$want" ]; then
			ok "[$backend] --geometry $geometry gives a ${want% *} rows x ${want#* } columns terminal"
		else
			ng "[$backend] --geometry $geometry: stty size says '$(cat "$marker" 2>/dev/null)', wanted '$want'"
		fi
	done

	# --- 3. a second lilyterm hands its command to the running one over the socket
	marker="$work/first.txt"
	local marker2="$work/second.txt"
	timeout 40 "$LILYTERM" -E sh -c "echo \$PPID > $marker; sleep 6" > "$work/socket1.log" 2>&1 &
	pid=$!
	if wait_for 15 test -s "$marker"; then
		timeout 20 "$LILYTERM" -E sh -c "echo \$PPID > $marker2; sleep 1" > "$work/socket2.log" 2>&1
		local status=$?
		if wait_for 10 test -s "$marker2" && [ "$(cat "$marker")" = "$(cat "$marker2")" ]; then
			ok "[$backend] second instance opened a tab in the first one (pid $(cat "$marker"), client exit $status)"
		else
			ng "[$backend] socket hand-over failed (first=$(cat "$marker" 2>/dev/null) second=$(cat "$marker2" 2>/dev/null))"
			sed 's/^/      /' "$work/socket2.log" | head -10
		fi
	else
		ng "[$backend] first instance for the socket test did not start"
	fi
	wait "$pid" && ok "[$backend] socket server instance exits cleanly" ||
		ng "[$backend] socket server instance exit status $?"

	# --- 4. the sample profile from `lilyterm -p` is accepted with -u
	local profile="$work/sample.conf"
	if "$LILYTERM" -p > "$profile" 2> "$work/profile-dump.log" && grep -q '^\[main\]' "$profile"; then
		timeout 30 "$LILYTERM" -s -u "$profile" \
			-E sh -c "echo ok > $work/profile.txt" > "$work/profile.log" 2>&1
		if [ -s "$work/profile.txt" ] && ! grep -q CRITICAL "$work/profile.log"; then
			ok "[$backend] starts with the profile generated by -p ($(grep -c . "$profile") lines)"
		else
			ng "[$backend] failed to start with the profile generated by -p"
			sed 's/^/      /' "$work/profile.log" | head -10
		fi
	else
		ng "[$backend] lilyterm -p did not print a profile"
	fi
}

for backend in $BACKENDS; do
	case "$backend" in
	x11)
		note "X11 (Xvfb)"
		Xvfb :99 -screen 0 1920x1200x24 -nolisten tcp > "$OUT/xvfb.log" 2>&1 &
		PIDS+=($!)
		if ! wait_for 10 test -S /tmp/.X11-unix/X99; then
			ng "Xvfb did not start"; continue
		fi
		( export DISPLAY=:99 GDK_BACKEND=x11; unset WAYLAND_DISPLAY; run_gui_tests x11; echo "$PASS $FAIL" > "$OUT/.count" )
		read -r PASS FAIL < "$OUT/.count"
		;;
	wayland)
		note "Wayland (headless sway)"
		cat > "$OUT/sway.conf" <<-EOF
			output HEADLESS-1 resolution 1920x1200
			for_window [app_id=".*"] floating enable
			default_border none
		EOF
		rm -f "$XDG_RUNTIME_DIR"/wayland-*
		WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 LIBSEAT_BACKEND=noop \
			sway-headless -c "$OUT/sway.conf" > "$OUT/sway.log" 2>&1 &
		PIDS+=($!)
		if ! wait_for 10 test -S "$XDG_RUNTIME_DIR/wayland-1"; then
			ng "sway did not start"; sed 's/^/      /' "$OUT/sway.log" | tail -20; continue
		fi
		# No DISPLAY at all: proves that nothing falls back to X11/XWayland.
		( export WAYLAND_DISPLAY=wayland-1 GDK_BACKEND=wayland; unset DISPLAY; run_gui_tests wayland; echo "$PASS $FAIL" > "$OUT/.count" )
		read -r PASS FAIL < "$OUT/.count"
		;;
	esac
done

echo
note "Result: $PASS passed, $FAIL failed (logs in $OUT)"
[ "$FAIL" -eq 0 ]

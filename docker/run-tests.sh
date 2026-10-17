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
# In GitHub Actions a failure also becomes an annotation, which can be read
# on the run page without signing in (unlike the log)
if [ -n "${GITHUB_ACTIONS:-}" ]; then
	ng() { FAIL=$((FAIL+1)); printf '\033[1;31m  FAIL\033[0m %s\n::error title=Test failed::%s\n' "$*" "$*"; }
fi
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
	# --- 5. split panes: split right, split down, close panes again
	run_pane_tests "$backend" "$work"
}

has_x11_focus() { [ "$(xdotool getwindowfocus 2>/dev/null)" = "$1" ]; }

# press_until BACKEND COMBO COMMAND...: press COMBO until COMMAND succeeds.
# One press can get lost while a window is still mapping: try it twice.
press_until() {
	local backend=$1 keys=$2; shift 2
	send_keys "$backend" "$keys"
	wait_for 5 "$@" && return
	send_keys "$backend" "$keys"
	wait_for 10 "$@"
}

# send_keys BACKEND COMBO: press a key combination (e.g. ctrl+shift+e, alt+Down) in LilyTerm
send_keys() {
	# A key press only counts when LilyTerm has the keyboard focus: on a slow
	# machine the new window may not have it yet, so give it the focus first.
	if [ "$1" = x11 ]; then
		local id
		id=$(xdotool search --onlyvisible --class lilyterm 2>/dev/null | tail -n1)
		if [ -n "$id" ]; then
			xdotool windowfocus --sync "$id" 2>/dev/null
			wait_for 5 has_x11_focus "$id"
		fi
		xdotool key --clearmodifiers "$2"
	else
		swaymsg -q '[app_id="(?i)lilyterm"] focus' 2>/dev/null
		sleep 0.2
		# wtype creates a new virtual keyboard every time: give LilyTerm 100 ms
		# for its keymap, or the first keys can get lost
		local key=${2##*+} mods='' m args=(-s 100)
		case "$2" in *+*) mods=$(echo "${2%+*}" | tr + ' ');; esac
		for m in $mods; do args+=(-M "$m"); done
		# wtype types a bare argument as text: a named key (Down, F5, ...) needs -k
		if [ ${#key} -gt 1 ]; then args+=(-k "$key"); else args+=("$key"); fi
		for m in $mods; do args+=(-m "$m"); done
		wtype "${args[@]}"
	fi
}

# Every pane runs $SHELL: a script that reports the size of its terminal in
# panes/pane-N/size, until panes/pane-N/quit shows up. A key typed in the
# pane creates panes/pane-N/typed: that shows which pane has the focus.
run_pane_tests() {
	local backend=$1 work=$2
	local pdir="$work/panes"
	mkdir -p "$pdir"
	cat > "$pdir/shell" <<-'EOF'
		#!/bin/sh
		n=1
		while ! mkdir "$PANE_DIR/pane-$n" 2>/dev/null; do n=$((n+1)); done
		stty -icanon -echo min 0 time 0
		while [ ! -e "$PANE_DIR/pane-$n/quit" ]; do
			[ -n "$(dd bs=1 count=1 2>/dev/null)" ] && touch "$PANE_DIR/pane-$n/typed"
			stty size > "$PANE_DIR/pane-$n/size.tmp" && mv "$PANE_DIR/pane-$n/size.tmp" "$PANE_DIR/pane-$n/size"
			sleep 0.2
		done
	EOF
	chmod +x "$pdir/shell"
	size() { cat "$pdir/pane-$1/size" 2>/dev/null; }
	rows() { size "$1" | cut -d' ' -f1; }
	cols() { size "$1" | cut -d' ' -f2; }
	# for wait_for, which runs its arguments again on every try
	size_is() { [ "$(size "$1")" = "$2" ]; }
	rows_are() { [ "$(rows "$1")" = "$2" ]; }
	typed() { ls "$pdir"/pane-*/typed > /dev/null 2>&1; }
	# focused_pane: type a key, and print the number of the pane that got it
	focused_pane() {
		local f
		for _ in 1 2; do
			rm -f "$pdir"/pane-*/typed
			send_keys "$backend" f
			if wait_for 3 typed; then
				for f in "$pdir"/pane-*/typed; do f=${f%/typed}; echo "${f##*-}"; done
				return
			fi
		done
	}

	# The pane "shell" is a script, not a known shell: without this LilyTerm
	# asks for a confirmation before it closes a pane with Ctrl+Shift+W.
	# It also moves "split down" to Alt+Down, to check that the pane keys can be rebound.
	"$LILYTERM" -p | sed -e 's/^confirm_to_kill_running_command = .*/confirm_to_kill_running_command = 0/' \
		-e 's/^split_down_key = .*/split_down_key = Alt Down/' > "$pdir/profile"
	if ! grep -q '^split_down_key = Alt Down' "$pdir/profile"; then
		ng "[$backend] panes: lilyterm -p does not list split_down_key"
	fi
	SHELL="$pdir/shell" PANE_DIR="$pdir" timeout 120 "$LILYTERM" -s -u "$pdir/profile" -g 100x30 > "$work/panes.log" 2>&1 &
	local pid=$!
	if ! wait_for 15 size_is 1 "30 100"; then
		ng "[$backend] panes: the first pane did not start at 100x30 ($(size 1))"
		kill "$pid" 2>/dev/null; return
	fi
	sleep 1

	# split right: two panes side by side share the 100 columns
	if press_until "$backend" ctrl+shift+e test -s "$pdir/pane-2/size"; then
		sleep 1
		local c1 c2 r3 r2
		c1=$(cols 1); c2=$(cols 2)
		if [ "$(rows 1)" = 30 ] && [ "$(rows 2)" = 30 ] && [ "$c1" -ge 40 ] && [ "$c2" -ge 40 ] &&
		   [ $((c1 + c2)) -ge 95 ] && [ $((c1 + c2)) -le 100 ]; then
			ok "[$backend] Ctrl+Shift+E splits right: $(size 1 | tr ' ' x) + $(size 2 | tr ' ' x), the window keeps its size"
		else
			ng "[$backend] split right: pane sizes $(size 1 | tr ' ' x) and $(size 2 | tr ' ' x)"
		fi
	else
		ng "[$backend] Ctrl+Shift+E did not open a second pane"
	fi

	# split the new (focused) pane down
	if press_until "$backend" alt+Down test -s "$pdir/pane-3/size"; then
		sleep 1
		r2=$(rows 2); r3=$(rows 3)
		if [ "$(cols 3)" = "$(cols 2)" ] && [ "$r2" -ge 12 ] && [ "$r3" -ge 12 ] &&
		   [ $((r2 + r3)) -ge 27 ] && [ $((r2 + r3)) -le 30 ] && [ "$(size 1)" = "30 $c1" ]; then
			ok "[$backend] Alt+Down (rebound in the profile) splits the focused pane down: $(size 2 | tr ' ' x) over $(size 3 | tr ' ' x)"
		else
			ng "[$backend] split down: panes $(size 1 | tr ' ' x), $(size 2 | tr ' ' x), $(size 3 | tr ' ' x)"
		fi
	else
		ng "[$backend] Alt+Down (rebound in the profile) did not open a third pane"
	fi
	if [ "$backend" = x11 ]; then
		xwd -root -silent 2>/dev/null | $MAGICK xwd:- "$work/panes.png" 2>/dev/null
	else
		grim "$work/panes.png" 2>/dev/null
	fi

	# a pane whose shell exits goes away, and its neighbour gets the space back
	touch "$pdir/pane-3/quit"
	if wait_for 10 rows_are 2 30; then
		ok "[$backend] closing a pane gives its space back ($(size 2 | tr ' ' x))"
	else
		ng "[$backend] after closing pane 3, pane 2 is $(size 2 | tr ' ' x)"
	fi
	# panes 1 | 2 are left, and pane 2 has the focus: move it to pane 1 and close that one
	# (a lost Ctrl+Shift+N would close pane 2 instead: check the focus first)
	local focus tries=0
	focus=$(focused_pane)
	while [ "$focus" != 1 ] && [ $tries -lt 2 ]; do
		send_keys "$backend" ctrl+shift+n
		tries=$((tries+1))
		sleep 0.5
		focus=$(focused_pane)
	done
	if [ "$focus" != 1 ]; then
		ng "[$backend] Ctrl+Shift+N did not move the focus to pane 1 (the focus is on pane ${focus:-?})"
	elif press_until "$backend" ctrl+shift+w size_is 2 "30 100"; then
		ok "[$backend] Ctrl+Shift+N moves the focus, Ctrl+Shift+W closes that pane: back to one pane of 100x30"
	else
		ng "[$backend] after Ctrl+Shift+N, Ctrl+Shift+W: panes $(size 1 | tr ' ' x) and $(size 2 | tr ' ' x)"
	fi
	# a pane that is still open would keep LilyTerm running until its timeout
	touch "$pdir/pane-1/quit"

	touch "$pdir/pane-2/quit"
	if wait "$pid"; then
		ok "[$backend] exits cleanly when the last pane closes"
	else
		ng "[$backend] exit status $? after the last pane closed"
	fi
	if grep -E 'CRITICAL|WARNING \*\*.*(VTE|Gtk|Gdk|GLib)|-WARNING|Segmentation|assertion' "$work/panes.log" > "$work/panes.bad"; then
		ng "[$backend] criticals while splitting:"; sort "$work/panes.bad" | uniq -c | sed 's/^/      /' | head -20
	else
		ok "[$backend] no GLib/GTK criticals while splitting"
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
		# swaymsg (send_keys) talks to sway over this socket
		SWAYSOCK=$(ls "$XDG_RUNTIME_DIR"/sway-ipc.*.sock 2>/dev/null | head -n1)
		# No DISPLAY at all: proves that nothing falls back to X11/XWayland.
		( export WAYLAND_DISPLAY=wayland-1 GDK_BACKEND=wayland SWAYSOCK; unset DISPLAY; run_gui_tests wayland; echo "$PASS $FAIL" > "$OUT/.count" )
		read -r PASS FAIL < "$OUT/.count"
		;;
	esac
done

echo
note "Result: $PASS passed, $FAIL failed (logs in $OUT)"
[ "$FAIL" -eq 0 ]

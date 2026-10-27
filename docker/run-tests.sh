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
#             (empty: only the build and the tests that need no display)
#   CFLAGS    passed to ./configure            ["-Wall -O2 -g"]
#   SANITIZE  non-empty: build with AddressSanitizer and UndefinedBehavior-
#             Sanitizer, and fail if they report anything. Leaks are not
#             checked: GTK+ keeps a lot until the end on purpose.

set -u

SRC=${SRC:-/src}
BUILD=${BUILD:-/build}
OUT=${OUT:-$BUILD/test-output}
BACKENDS=${BACKENDS-x11 wayland}
LILYTERM=${LILYTERM:-}
SANITIZE=${SANITIZE:-}

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

# wait_for SECONDS COMMAND...: poll until COMMAND succeeds. The CI runner
# shares the host's Docker with other jobs: the first start of a display server
# or of a sanitized lilyterm can take a lot longer there than on an idle host.
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

# Every process (lilyterm, and each test's) writes what the sanitizers find
# to $OUT/sanitizer.PID; any such file fails the run.
SAN_FLAGS=
if [ -n "$SANITIZE" ]; then
	SAN_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
	export ASAN_OPTIONS="detect_leaks=0:log_path=$OUT/sanitizer"
	export UBSAN_OPTIONS="print_stacktrace=1:log_path=$OUT/sanitizer"
	rm -f "$OUT"/sanitizer.*
fi

# ImageMagick 7 calls it magick, and nags about convert
MAGICK=$(command -v magick || command -v convert)

# ---------------------------------------------------------------- build ----

BUILT=
if [ -z "$LILYTERM" ]; then
	BUILT=1
	note "Building $SRC in $BUILD/lilyterm"
	mkdir -p "$BUILD/lilyterm"
	rsync -a --delete --exclude .git --exclude test-output "$SRC"/ "$BUILD/lilyterm"/
	cd "$BUILD/lilyterm" || exit 1
	make distclean >/dev/null 2>&1

	if CFLAGS="${CFLAGS:--Wall -O2 -g}${SAN_FLAGS:+ $SAN_FLAGS}" ./configure --prefix=/usr > "$OUT/configure.log" 2>&1; then
		ok "configure ($(grep -E '^(GTK|VTE) = ' .config | tr '\n' ' '))"
	else
		ng "configure (see $OUT/configure.log)"; cat "$OUT/configure.log"; exit 1
	fi

	# A sanitizer report is only readable with the symbols: no strip then
	if make ${SANITIZE:+STRIP=true} > "$OUT/make.log" 2>&1; then
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

# The unit tests of src/misc.c, with the sanitizers too if SANITIZE is set.
# Only where this script built LilyTerm: the static test image has no compiler.
if [ -n "$BUILT" ]; then
	if CFLAGS="-O2 -g${SAN_FLAGS:+ $SAN_FLAGS}" "$BUILD/lilyterm/tests/unit/run.sh" "$BUILD/unit" > "$OUT/unit.log" 2>&1; then
		ok "unit tests ($(grep -c '^ok ' "$OUT/unit.log") passed)"
	else
		ng "unit tests (see $OUT/unit.log)"
		grep -E '^not ok|ERROR|error:|Bail out' "$OUT/unit.log" | head -20 | sed 's/^/      /'
	fi
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

	if wait_for 30 test -s "$marker"; then
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
	timeout 40 "$LILYTERM" -E sh -c "echo \$PPID > $marker; sleep 14" > "$work/socket1.log" 2>&1 &
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
		# The request travels as one string: a newline in the environment or in
		# an argument cut it short before 0.12.0 (the command was lost).
		local marker3="$work/third.txt" nl=$'\n'
		MULTI_LINE="a${nl}b" timeout 20 "$LILYTERM" -E sh -c \
			"printf '%s|%s|%s' \"\$PPID\" \"\$1\" \"\$MULTI_LINE\" > $marker3.tmp; mv $marker3.tmp $marker3; sleep 1" \
			x "one${nl}two" > "$work/socket3.log" 2>&1
		local want
		want="$(cat "$marker")|one${nl}two|a${nl}b"
		if wait_for 10 test -s "$marker3" && [ "$(cat "$marker3")" = "$want" ]; then
			ok "[$backend] newlines in the environment and the arguments survive the socket"
		else
			ng "[$backend] socket request with newlines: got '$(cat "$marker3" 2>/dev/null)', wanted '$want'"
			sed 's/^/      /' "$work/socket3.log" | head -10
		fi
		# A request bigger than the socket buffer (8 KB on macOS, ~200 KB here):
		# it was cut short before 0.12.0, as the server read only after the
		# client hung up, and the client did not wait to send the rest.
		local marker4="$work/fourth.txt" big
		big=$(head -c 100000 /dev/zero | tr '\0' x)
		BIG1=$big BIG2=$big BIG3=$big timeout 30 "$LILYTERM" -E sh -c \
			"echo \"\$PPID \${#BIG1} \${#BIG2} \${#BIG3}\" > $marker4.tmp; mv $marker4.tmp $marker4; sleep 1" \
			> "$work/socket4.log" 2>&1
		want="$(cat "$marker") 100000 100000 100000"
		if wait_for 10 test -s "$marker4" && [ "$(cat "$marker4")" = "$want" ]; then
			ok "[$backend] a 300 KB request goes through the socket whole"
		else
			ng "[$backend] big socket request: got '$(cat "$marker4" 2>/dev/null)', wanted '$want'"
			sed 's/^/      /' "$work/socket4.log" | head -10
		fi
	else
		ng "[$backend] first instance for the socket test did not start"
	fi
	wait "$pid" && ok "[$backend] socket server instance exits cleanly" ||
		ng "[$backend] socket server instance exit status $?"

	# --- 3b. a server killed with -9 leaves its socket file behind: the next
	#         lilyterm takes it over (no server holds the lock any more), and
	#         serves the one after it
	marker="$work/stale1.txt"
	timeout 40 "$LILYTERM" -E sh -c "echo \$PPID > $marker; sleep 30" > "$work/stale1.log" 2>&1 &
	pid=$!
	if wait_for 15 test -s "$marker"; then
		kill -9 "$(cat "$marker")"
		wait "$pid" 2>/dev/null
		local stale2="$work/stale2.txt" stale3="$work/stale3.txt"
		timeout 40 "$LILYTERM" -E sh -c "echo \$PPID > $stale2; sleep 8" > "$work/stale2.log" 2>&1 &
		pid=$!
		if wait_for 15 test -s "$stale2"; then
			timeout 20 "$LILYTERM" -E sh -c "echo \$PPID > $stale3; sleep 1" > "$work/stale3.log" 2>&1
			if wait_for 10 test -s "$stale3" && [ "$(cat "$stale2")" = "$(cat "$stale3")" ]; then
				ok "[$backend] the socket of a killed server is taken over, and serves the next lilyterm"
			else
				ng "[$backend] after a killed server: second=$(cat "$stale2") third=$(cat "$stale3" 2>/dev/null)"
				sed 's/^/      /' "$work/stale3.log" | head -10
			fi
		else
			ng "[$backend] no lilyterm started after the server was killed"
			sed 's/^/      /' "$work/stale2.log" | head -10
		fi
		wait "$pid" 2>/dev/null
	else
		ng "[$backend] the server to kill did not start"
	fi

	# --- 3c. two lilyterms started at the same moment: one becomes the server
	#         and the other one hands its command over. Both could become a
	#         server before, and the second took the socket of the first.
	local race1="$work/race1.txt" race2="$work/race2.txt" pid2
	timeout 40 "$LILYTERM" -E sh -c "echo \$PPID > $race1; sleep 5" > "$work/race1.log" 2>&1 &
	pid=$!
	timeout 40 "$LILYTERM" -E sh -c "echo \$PPID > $race2; sleep 5" > "$work/race2.log" 2>&1 &
	pid2=$!
	if wait_for 20 eval 'test -s "$race1" && test -s "$race2"' && [ "$(cat "$race1")" = "$(cat "$race2")" ]; then
		ok "[$backend] two lilyterms started at once share one server (pid $(cat "$race1"))"
	else
		ng "[$backend] two lilyterms started at once: pids $(cat "$race1" 2>/dev/null) and $(cat "$race2" 2>/dev/null)"
	fi
	wait "$pid" "$pid2" 2>/dev/null

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
	# --- 6. tabs with different font sizes
	run_font_tab_tests "$backend" "$work"
	# --- 6b. a key binding with Cmd (the Super key here, Command on macOS)
	run_cmd_key_test "$backend" "$work"
	# --- 6c. a new tab starts in the directory the shell reported (OSC 7)
	run_osc7_test "$backend" "$work"
	# --- 6d. the search bar finds text in the scrollback
	run_search_bar_test "$backend" "$work"
	# --- 7. the window title, word_chars, the bell and cursor_shape (X11: the
	#        mouse, the window name, WM_HINTS and xwd)
	[ "$backend" = x11 ] && run_word_and_bell_tests "$work"
	# --- 8. a click on a link (X11: the mouse)
	[ "$backend" = x11 ] && run_url_click_tests "$work"
	# --- 9. -e asks before it runs a command (X11: the window name)
	[ "$backend" = x11 ] && run_execute_confirm_test "$work"
	# --- 10. a click on an OSC 8 hyperlink (X11: the mouse)
	[ "$backend" = x11 ] && run_osc8_tests "$work"
}

# Links that the program marks with OSC 8: their text says nothing of where
# they go, so no pattern finds them. A web link runs web_browser, a file on
# this machine runs file_manager with file:///PATH; a file on another machine
# (ls --hyperlink over ssh) and another scheme run nothing. The target of the
# link under the mouse shows up in a corner by the time of the next click
# (after the 2 s of a link that runs nothing): the click must go through it.
# (A tooltip did not let it: the ssh: link was never clicked.)
run_osc8_tests() {
	local work=$1 odir="$1/osc8" id pid host
	host=$(hostname)
	mkdir -p "$odir"
	cat > "$odir/shell" <<-EOF
		#!/bin/sh
		link() { printf '\033]8;;%s\033\\\\%s\033]8;;\033\\\\\n' "\$1" "\$2"; }
		printf '\033[H\033[2J'
		link https://example.com/osc8-test 'click here'
		link 'file://$host/etc/hosts' 'local file'
		link file://some-other-host/etc/hosts 'remote file'
		link ssh://example.com 'ssh link'
		while [ ! -e "$odir/quit" ]; do sleep 0.2; done
	EOF
	printf '#!/bin/sh\nprintf "%%s\\n" "$@" > "%s/opened.tmp" && mv "%s/opened.tmp" "%s/opened"\n' \
		"$odir" "$odir" "$odir" > "$odir/open"
	chmod +x "$odir/shell" "$odir/open"
	"$LILYTERM" -p | sed -e "s|^web_browser = .*|web_browser = $odir/open|" \
		-e 's/^web_method = .*/web_method = 1/' \
		-e "s|^file_manager = .*|file_manager = $odir/open|" \
		-e 's/^file_method = .*/file_method = 1/' > "$odir/profile"
	SHELL="$odir/shell" timeout 60 "$LILYTERM" -s -u "$odir/profile" -g 80x6 > "$work/osc8.log" 2>&1 &
	pid=$!
	if ! wait_for 15 eval 'id=$(xdotool search --onlyvisible --class lilyterm 2>/dev/null | tail -n1); [ -n "$id" ]'; then
		ng "[x11] OSC 8: no LilyTerm window"; kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null; return
	fi
	sleep 1.5
	# set by xdotool getwindowgeometry --shell
	# shellcheck disable=SC2034
	local WIDTH HEIGHT X Y SCREEN WINDOW line want what
	eval "$(xdotool getwindowgeometry --shell "$id")"
	# click LINE (1 ... 6) at its 5th column; WANT: what open got, '' for nothing
	for line in 1 2 3 4; do
		case $line in
			1) want=https://example.com/osc8-test; what='a web link runs web_browser';;
			2) want=file:///etc/hosts; what='a file on this machine runs file_manager with file:///PATH';;
			3) want=''; what='a file on another machine runs nothing';;
			4) want=''; what='an ssh: link runs nothing';;
		esac
		rm -f "$odir/opened"
		xdotool mousemove --window "$id" $((WIDTH * 9 / 160)) $((HEIGHT * (2 * line - 1) / 12)) click 1
		if [ -n "$want" ]; then
			if wait_for 5 test -s "$odir/opened" && [ "$(cat "$odir/opened")" = "$want" ]; then
				ok "[x11] OSC 8: $what ($want)"
			else
				ng "[x11] OSC 8: $what: got '$(cat "$odir/opened" 2>/dev/null)', expected '$want'"
			fi
		else
			sleep 2
			if [ -e "$odir/opened" ]; then
				ng "[x11] OSC 8: $what, but it ran with '$(cat "$odir/opened")'"
			else
				ok "[x11] OSC 8: $what"
			fi
		fi
	done
	if ! kill -0 "$pid" 2>/dev/null; then
		ng "[x11] OSC 8: LilyTerm died"; sed 's/^/      /' "$work/osc8.log" | tail -10
	fi
	touch "$odir/quit"
	wait "$pid" 2>/dev/null
}

# type_text BACKEND TEXT: type TEXT into the focused widget of LilyTerm
type_text() {
	if [ "$1" = x11 ]; then
		xdotool type --delay 60 "$2"
	else
		wtype -s 100 "$2"
	fi
}

# Ctrl F opens the search bar; the text typed there is found as it is typed,
# the "." taken as it is (it matches any character in a regex, and the newer
# needleXone would be found then). Esc gives the focus back to the terminal,
# where Ctrl Delete copies the match and Ctrl Insert pastes it into the
# shell, which saves it in search/line. Had the bar not opened, the shell
# would have got the typed text and an Esc; had the focus stayed in the bar,
# the paste and the Return would have gone there.
run_search_bar_test() {
	local backend=$1 work=$2 sdir="$2/search" pid id='' before='' after=''
	mkdir -p "$sdir"
	cat > "$sdir/shell" <<-EOF
		#!/bin/sh
		printf '\033[H\033[2J'
		printf 'first line\nneedle.one\nfiller\nneedleXone\nlast line\n'
		IFS= read -r line && printf '%s\n' "\$line" > "$sdir/line.tmp" && mv "$sdir/line.tmp" "$sdir/line"
		while [ ! -e "$sdir/quit" ]; do sleep 0.2; done
	EOF
	chmod +x "$sdir/shell"
	SHELL="$sdir/shell" timeout 60 "$LILYTERM" -s -g 80x10 > "$work/search.log" 2>&1 &
	pid=$!
	if [ "$backend" = x11 ]; then
		if ! wait_for 15 eval 'id=$(xdotool search --onlyvisible --class lilyterm 2>/dev/null | tail -n1); [ -n "$id" ]'; then
			ng "[x11] search bar: no LilyTerm window"; kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null; return
		fi
		before=$(xdotool getwindowgeometry "$id" 2>/dev/null | grep Geometry)
	fi
	sleep 1.5
	send_keys "$backend" ctrl+f
	sleep 0.5
	type_text "$backend" needle.one
	sleep 0.5
	[ "$backend" = x11 ] && after=$(xdotool getwindowgeometry "$id" 2>/dev/null | grep Geometry)
	send_keys "$backend" Escape
	sleep 0.3
	send_keys "$backend" ctrl+Delete
	sleep 0.3
	send_keys "$backend" ctrl+Insert
	sleep 0.3
	send_keys "$backend" Return
	if wait_for 5 test -s "$sdir/line" && [ "$(cat "$sdir/line")" = needle.one ]; then
		ok "[$backend] the search bar finds 'needle.one' as typed, and Esc gives the focus back"
	else
		ng "[$backend] search bar: the shell got '$(cat -v "$sdir/line" 2>/dev/null)', expected 'needle.one'"
		sed 's/^/      /' "$work/search.log" | head -10
	fi
	if [ "$backend" = x11 ]; then
		if [ -n "$before" ] && [ "$before" = "$after" ]; then
			ok "[x11] the window keeps its size with the search bar open (${before##* })"
		else
			ng "[x11] the search bar changed the window: '$before' -> '$after'"
		fi
	fi
	if grep -E 'CRITICAL|WARNING \*\*.*(VTE|Gtk|Gdk|GLib)|-WARNING|Segmentation|assertion' "$work/search.log" > "$work/search.bad"; then
		ng "[$backend] criticals with the search bar:"; sort "$work/search.bad" | uniq -c | sed 's/^/      /' | head -10
	fi
	touch "$sdir/quit"
	wait "$pid" 2>/dev/null
}

# lilyterm -e asks before it runs its command, in a dialog that comes before
# the window has a tab. A check for "no tab, no dialog" once took that
# dialog away too.
run_execute_confirm_test() {
	local work=$1 pid
	timeout 20 "$LILYTERM" -s -e true > "$work/execute.log" 2>&1 &
	pid=$!
	if wait_for 10 eval 'xdotool search --onlyvisible --name "Confirm to execute" > /dev/null 2>&1'; then
		ok "[x11] -e asks before it runs its command"
	else
		ng "[x11] -e: no \"Confirm to execute\" dialog"
		sed 's/^/      /' "$work/execute.log" | head -10
	fi
	kill "$pid" 2>/dev/null
	wait "$pid" 2>/dev/null
}

# A double click selects a word by word_chars, a bell sets the urgency hint,
# and cursor_shape changes the cursor. The shell prints a line with ',' and '!' in it: the default word_chars
# has '!' but not ',' (VTE's own default is the other way round). A middle
# click pastes the selection back into the shell, which saves it in word/line.
run_word_and_bell_tests() {
	local work=$1 wdir="$1/word" id a b c
	mkdir -p "$wdir"
	a=$(printf 'a%.0s' $(seq 20)); b=$(printf 'b%.0s' $(seq 30)); c=$(printf 'c%.0s' $(seq 20))
	cat > "$wdir/shell" <<-EOF
		#!/bin/sh
		printf '\033]2;title-from-the-shell\007\033[H\033[2J$a,$b!$c\n'
		IFS= read -r line && printf '%s\n' "\$line" > "$wdir/line.tmp" && mv "$wdir/line.tmp" "$wdir/line"
		while [ ! -e "$wdir/ring" ]; do sleep 0.1; done
		printf '\a'
		sleep 3
	EOF
	chmod +x "$wdir/shell"
	SHELL="$wdir/shell" timeout 40 "$LILYTERM" -s -g 80x5 > "$work/word.log" 2>&1 &
	local pid=$!
	if ! wait_for 15 eval 'id=$(xdotool search --onlyvisible --class lilyterm 2>/dev/null | tail -n1); [ -n "$id" ]'; then
		ng "[x11] word_chars: no LilyTerm window"; kill "$pid" 2>/dev/null; return
	fi
	sleep 1
	# a title set by the program in the terminal (OSC 2) becomes the window title
	if wait_for 5 eval 'xdotool getwindowname "$id" 2>/dev/null | grep -q title-from-the-shell'; then
		ok "[x11] the title set by the shell becomes the window title"
	else
		ng "[x11] window title: '$(xdotool getwindowname "$id" 2>/dev/null)', expected the title set by the shell"
	fi
	# set by xdotool getwindowgeometry --shell
	# shellcheck disable=SC2034
	local WIDTH HEIGHT X Y SCREEN WINDOW
	eval "$(xdotool getwindowgeometry --shell "$id")"
	# the middle of the b's: columns 22 to 51 of 80, on the first line
	xdotool mousemove --window "$id" $((WIDTH * 36 / 80)) $((HEIGHT / 10)) click --repeat 2 --delay 80 1
	sleep 0.5
	xdotool click 2
	send_keys x11 Return
	if wait_for 5 test -s "$wdir/line" && [ "$(cat "$wdir/line")" = "$b!$c" ]; then
		ok "[x11] a double click selects a word by word_chars ('!' is part of it, ',' is not)"
	else
		ng "[x11] word_chars: a double click selected '$(cat "$wdir/line" 2>/dev/null)', expected '$b!$c'"
	fi

	# The urgent bell is only for a window without the focus: give the focus
	# to another window first (to this one before, so it gets a focus-out)
	timeout 20 "$LILYTERM" -s -g 40x5 -e sleep 15 > "$work/word-other.log" 2>&1 &
	local other_pid=$! other=''
	wait_for 15 eval 'other=$(xdotool search --onlyvisible --class lilyterm 2>/dev/null | grep -vx "$id" | tail -n1); [ -n "$other" ]'
	if [ -n "$other" ]; then
		xdotool windowfocus --sync "$id" 2>/dev/null; sleep 0.5
		xdotool windowfocus --sync "$other" 2>/dev/null; sleep 0.5
	fi
	touch "$wdir/ring"
	# XUrgencyHint is bit 8 of the flags in WM_HINTS
	urgent() {
		local flags
		flags=$(xprop -id "$id" -notype -f WM_HINTS 32x ' $0\n' WM_HINTS 2>/dev/null | awk '{print $2}')
		case "$flags" in 0x[0-9a-fA-F]*) [ $((flags & 256)) -ne 0 ];; *) false;; esac
	}
	if wait_for 5 urgent; then
		ok "[x11] a bell sets the urgency hint (urgent_bell)"
	else
		ng "[x11] urgent_bell: no urgency hint after a bell (WM_HINTS flags: $(xprop -id "$id" -notype -f WM_HINTS 32x ' $0' WM_HINTS 2>/dev/null))"
	fi
	wait "$pid" 2>/dev/null
	kill "$other_pid" 2>/dev/null; wait "$other_pid" 2>/dev/null

	# cursor_shape: the window looks different with a block (0) and an
	# underline (2) cursor; the cursor does not blink (cursor_blinks = 2)
	local shape sums=()
	printf '#!/bin/sh\nexec sleep 10\n' > "$wdir/sleep"; chmod +x "$wdir/sleep"
	for shape in 0 2; do
		"$LILYTERM" -p | sed -e "s/^cursor_shape = .*/cursor_shape = $shape/" \
			-e 's/^cursor_blinks = .*/cursor_blinks = 2/' > "$wdir/cursor-$shape.conf"
		SHELL="$wdir/sleep" timeout 20 "$LILYTERM" -s -u "$wdir/cursor-$shape.conf" -g 20x3 > "$work/cursor-$shape.log" 2>&1 &
		pid=$!; id=''
		wait_for 15 eval 'id=$(xdotool search --onlyvisible --class lilyterm 2>/dev/null | tail -n1); [ -n "$id" ]'
		sleep 1.5
		sums+=("$(xwd -id "$id" -silent 2>/dev/null | $MAGICK xwd:- rgb:- 2>/dev/null | md5sum | cut -d' ' -f1)")
		xwd -id "$id" -silent 2>/dev/null | $MAGICK xwd:- "$work/cursor-$shape.png" 2>/dev/null
		kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null
	done
	if ! grep -q '^cursor_shape = ' "$wdir/cursor-0.conf"; then
		ng "[x11] cursor_shape: lilyterm -p does not list it"
	elif [ -n "${sums[0]}" ] && [ "${sums[0]}" != "${sums[1]}" ]; then
		ok "[x11] cursor_shape changes the cursor (block and underline look different)"
	else
		ng "[x11] cursor_shape: the window looks the same with a block and an underline cursor (see cursor-*.png)"
	fi
}

# A click on a link runs web_browser with it. Every press and release of Ctrl
# drops and adds the link regexes again (disable_url_when_ctrl_pressed), and
# VTE numbers them on: LilyTerm used to take that number for the index of the
# command, which ran past the end of the list and crashed.
run_url_click_tests() {
	local work=$1 udir="$1/url" id url=https://example.com/lilyterm-test mail=lily@example.com
	mkdir -p "$udir"
	cat > "$udir/shell" <<-EOF
		#!/bin/sh
		printf '\033[H\033[2J$url\n$mail\n'
		while [ ! -e "$udir/quit" ]; do sleep 0.2; done
	EOF
	printf '#!/bin/sh\nprintf "%%s\\n" "$@" > "%s/opened.tmp" && mv "%s/opened.tmp" "%s/opened"\n' \
		"$udir" "$udir" "$udir" > "$udir/browser"
	chmod +x "$udir/shell" "$udir/browser"
	"$LILYTERM" -p | sed -e "s|^web_browser = .*|web_browser = $udir/browser|" \
		-e 's/^web_method = .*/web_method = 1/' \
		-e "s|^email_client = .*|email_client = $udir/browser|" \
		-e 's/^email_method = .*/email_method = 1/' > "$udir/profile"
	if ! grep -q "^web_browser = $udir/browser" "$udir/profile" || ! grep -q '^web_method = 1' "$udir/profile" ||
	   ! grep -q "^email_client = $udir/browser" "$udir/profile" || ! grep -q '^email_method = 1' "$udir/profile"; then
		ng "[x11] links: lilyterm -p does not list web_browser, web_method, email_client and email_method"; return
	fi
	SHELL="$udir/shell" timeout 40 "$LILYTERM" -s -u "$udir/profile" -g 80x5 > "$work/url.log" 2>&1 &
	local pid=$!
	if ! wait_for 15 eval 'id=$(xdotool search --onlyvisible --class lilyterm 2>/dev/null | tail -n1); [ -n "$id" ]'; then
		ng "[x11] links: no LilyTerm window"; kill "$pid" 2>/dev/null; return
	fi
	sleep 1
	for _ in 1 2 3; do send_keys x11 Control_L; done
	sleep 0.5
	# set by xdotool getwindowgeometry --shell
	# shellcheck disable=SC2034
	local WIDTH HEIGHT X Y SCREEN WINDOW
	eval "$(xdotool getwindowgeometry --shell "$id")"
	# the link is at the start of the first line
	xdotool mousemove --window "$id" $((WIDTH * 10 / 80)) $((HEIGHT / 10)) click 1
	if wait_for 5 test -s "$udir/opened" && [ "$(cat "$udir/opened")" = "$url" ]; then
		ok "[x11] a click on a link runs web_browser with it, also after Ctrl was pressed"
	elif ! kill -0 "$pid" 2>/dev/null; then
		ng "[x11] LilyTerm died on a click on a link after Ctrl was pressed"
		sed 's/^/      /' "$work/url.log" | tail -10
	else
		ng "[x11] links: web_browser got '$(cat "$udir/opened" 2>/dev/null)', expected '$url'"
	fi
	# An email address without mailto: is opened as a mailto: URL: open(1)
	# and xdg-open took "user@host" for a file. It is on the second line.
	rm -f "$udir/opened"
	xdotool mousemove --window "$id" $((WIDTH * 8 / 80)) $((HEIGHT * 3 / 10)) click 1
	if wait_for 5 test -s "$udir/opened" && [ "$(cat "$udir/opened")" = "mailto:$mail" ]; then
		ok "[x11] a click on an email address runs email_client with mailto:$mail"
	else
		ng "[x11] links: email_client got '$(cat "$udir/opened" 2>/dev/null)', expected 'mailto:$mail'"
	fi
	touch "$udir/quit"
	wait "$pid" 2>/dev/null
}

# When the tabs of a window use different font sizes, the window is resized
# in pixels instead of in characters. That used to crash LilyTerm as soon as
# the size of a tab changed, or another tab was chosen. Every tab runs a
# script that reports its terminal size in fonts/tab-N/size.
run_font_tab_tests() {
	local backend=$1 work=$2
	local fdir="$work/fonts"
	mkdir -p "$fdir"
	cat > "$fdir/shell" <<-'EOF'
		#!/bin/sh
		n=1
		while ! mkdir "$TAB_DIR/tab-$n" 2>/dev/null; do n=$((n+1)); done
		while [ ! -e "$TAB_DIR/quit" ]; do
			stty size > "$TAB_DIR/tab-$n/size.tmp" && mv "$TAB_DIR/tab-$n/size.tmp" "$TAB_DIR/tab-$n/size"
			sleep 0.2
		done
	EOF
	chmod +x "$fdir/shell"
	"$LILYTERM" -p | sed -e 's/^confirm_to_kill_running_command = .*/confirm_to_kill_running_command = 0/' \
		> "$fdir/profile"
	SHELL="$fdir/shell" TAB_DIR="$fdir" timeout 90 "$LILYTERM" -s -u "$fdir/profile" -g 80x24 \
		> "$work/fonts.log" 2>&1 &
	local pid=$!
	if ! wait_for 15 test -s "$fdir/tab-1/size"; then
		ng "[$backend] font sizes: the first tab did not start"
		kill "$pid" 2>/dev/null; return
	fi
	sleep 1
	if ! press_until "$backend" ctrl+t test -s "$fdir/tab-2/size"; then
		ng "[$backend] font sizes: Ctrl+T did not open a second tab"
		touch "$fdir/quit"; kill "$pid" 2>/dev/null; return
	fi
	sleep 1
	# A bigger font in the second tab only, then back and forth between the tabs
	local before
	before=$(cat "$fdir/tab-2/size")
	send_keys "$backend" ctrl+equal
	send_keys "$backend" ctrl+equal
	sleep 1
	for _ in 1 2 3; do
		send_keys "$backend" ctrl+Page_Up; sleep 0.5
		send_keys "$backend" ctrl+Page_Down; sleep 0.5
	done
	if kill -0 "$pid" 2>/dev/null; then
		ok "[$backend] tabs with different font sizes: switching tabs works (tab 2: $before -> $(cat "$fdir/tab-2/size"))"
	else
		ng "[$backend] LilyTerm died after a font size change in one of two tabs"
		sed 's/^/      /' "$work/fonts.log" | tail -10
	fi
	touch "$fdir/quit"
	wait "$pid" 2>/dev/null
}

# "Cmd T" in the profile: on Linux, Cmd is the Super key. Ctrl T, the default,
# must not open a tab then.
run_cmd_key_test() {
	local backend=$1 work=$2
	local cdir="$work/cmdkey"
	mkdir -p "$cdir"
	cat > "$cdir/shell" <<-'EOF'
		#!/bin/sh
		n=1
		while ! mkdir "$TAB_DIR/tab-$n" 2>/dev/null; do n=$((n+1)); done
		while [ ! -e "$TAB_DIR/quit" ]; do sleep 0.2; done
	EOF
	chmod +x "$cdir/shell"
	"$LILYTERM" -p | sed -e 's/^confirm_to_kill_running_command = .*/confirm_to_kill_running_command = 0/' \
		-e 's/^new_tab_key = .*/new_tab_key = Cmd T/' > "$cdir/profile"
	SHELL="$cdir/shell" TAB_DIR="$cdir" timeout 60 "$LILYTERM" -s -u "$cdir/profile" -g 80x24 \
		> "$work/cmdkey.log" 2>&1 &
	local pid=$!
	if ! wait_for 15 test -d "$cdir/tab-1"; then
		ng "[$backend] Cmd key: the first tab did not start"
		kill "$pid" 2>/dev/null; return
	fi
	sleep 1
	local super=super+t
	[ "$backend" = wayland ] && super=logo+t
	send_keys "$backend" ctrl+t
	sleep 1
	if [ -d "$cdir/tab-2" ]; then
		ng "[$backend] Ctrl+T opened a tab, though new_tab_key is Cmd T"
	elif press_until "$backend" "$super" test -d "$cdir/tab-2"; then
		ok "[$backend] new_tab_key = Cmd T: Super+T opens a tab, Ctrl+T does not"
	else
		ng "[$backend] new_tab_key = Cmd T: Super+T did not open a tab"
		sed 's/^/      /' "$work/cmdkey.log" | tail -10
	fi
	touch "$cdir/quit"
	wait "$pid" 2>/dev/null
}

# The first tab reports a directory with OSC 7, then removes the one it is in:
# /proc/PID/cwd is no directory then, and the new tab starts in the reported one.
run_osc7_test() {
	local backend=$1 work=$2
	local odir="$work/osc7"
	mkdir -p "$odir/reported" "$odir/gone"
	cat > "$odir/shell" <<-'EOF'
		#!/bin/sh
		n=1
		while ! mkdir "$TAB_DIR/tab-$n" 2>/dev/null; do n=$((n+1)); done
		if [ "$n" = 1 ]; then
			cd "$TAB_DIR/gone" || exit 1
			printf '\033]7;file://%s%s\033\\' "$(hostname)" "$TAB_DIR/reported"
			rmdir "$TAB_DIR/gone"
		fi
		pwd > "$TAB_DIR/tab-$n/pwd.tmp" && mv "$TAB_DIR/tab-$n/pwd.tmp" "$TAB_DIR/tab-$n/pwd"
		while [ ! -e "$TAB_DIR/quit" ]; do sleep 0.2; done
	EOF
	chmod +x "$odir/shell"
	"$LILYTERM" -p | sed -e 's/^confirm_to_kill_running_command = .*/confirm_to_kill_running_command = 0/' \
		> "$odir/profile"
	SHELL="$odir/shell" TAB_DIR="$odir" timeout 60 "$LILYTERM" -s -u "$odir/profile" -g 80x24 \
		> "$work/osc7.log" 2>&1 &
	local pid=$!
	if ! wait_for 15 test -s "$odir/tab-1/pwd"; then
		ng "[$backend] OSC 7: the first tab did not start"
		kill "$pid" 2>/dev/null; return
	fi
	sleep 1
	if press_until "$backend" ctrl+t test -s "$odir/tab-2/pwd" &&
	   [ "$(cat "$odir/tab-2/pwd")" = "$odir/reported" ]; then
		ok "[$backend] a new tab starts in the directory the shell reported (OSC 7)"
	else
		ng "[$backend] OSC 7: the new tab started in '$(cat "$odir/tab-2/pwd" 2>/dev/null)', wanted '$odir/reported'"
		sed 's/^/      /' "$work/osc7.log" | tail -10
	fi
	touch "$odir/quit"
	wait "$pid" 2>/dev/null
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

[ -n "$BACKENDS" ] || note "No X11 / Wayland tests (BACKENDS is empty)"
# read_counts BACKEND: take over the counts of a backend's subshell. If it
# stopped before it wrote them, its results are lost: that is a failure.
read_counts() {
	if [ -s "$OUT/.count" ]; then
		read -r PASS FAIL < "$OUT/.count"
	else
		ng "[$1] the tests stopped early: their results are lost"
	fi
}

for backend in $BACKENDS; do
	case "$backend" in
	x11)
		note "X11 (Xvfb)"
		Xvfb :99 -screen 0 1920x1200x24 -nolisten tcp > "$OUT/xvfb.log" 2>&1 &
		PIDS+=($!)
		if ! wait_for 30 test -S /tmp/.X11-unix/X99; then
			ng "Xvfb did not start"; sed 's/^/      /' "$OUT/xvfb.log" | tail -20; continue
		fi
		rm -f "$OUT/.count"
		( export DISPLAY=:99 GDK_BACKEND=x11; unset WAYLAND_DISPLAY; run_gui_tests x11; echo "$PASS $FAIL" > "$OUT/.count" )
		read_counts x11
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
		if ! wait_for 30 test -S "$XDG_RUNTIME_DIR/wayland-1"; then
			ng "sway did not start"; sed 's/^/      /' "$OUT/sway.log" | tail -20; continue
		fi
		# swaymsg (send_keys) talks to sway over this socket
		SWAYSOCK=$(ls "$XDG_RUNTIME_DIR"/sway-ipc.*.sock 2>/dev/null | head -n1)
		# No DISPLAY at all: proves that nothing falls back to X11/XWayland.
		rm -f "$OUT/.count"
		( export WAYLAND_DISPLAY=wayland-1 GDK_BACKEND=wayland SWAYSOCK; unset DISPLAY; run_gui_tests wayland; echo "$PASS $FAIL" > "$OUT/.count" )
		read_counts wayland
		;;
	esac
done

if [ -n "$SANITIZE" ]; then
	if ls "$OUT"/sanitizer.* > /dev/null 2>&1; then
		ng "AddressSanitizer / UndefinedBehaviorSanitizer reported errors:"
		for report in "$OUT"/sanitizer.*; do
			echo "      --- $(basename "$report")"
			sed 's/^/      /' "$report" | head -40
		done
	else
		ok "no AddressSanitizer / UndefinedBehaviorSanitizer report"
	fi
fi

echo
note "Result: $PASS passed, $FAIL failed (logs in $OUT)"
[ "$FAIL" -eq 0 ]

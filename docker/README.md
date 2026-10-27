# Building and testing LilyTerm in containers

Two images, both based on Alpine Linux (musl):

| File                | What it is for                                                         |
|---------------------|------------------------------------------------------------------------|
| `Dockerfile.dev`    | Build against the distribution's GTK+ 3 / VTE and run the test suite   |
| `Dockerfile.static` | Build one fully static binary that runs on X11 **and** Wayland         |

All commands are run from the top of the source tree.

## Build + test (dynamic)

```sh
docker build -f docker/Dockerfile.dev -t lilyterm-dev .
docker run --rm -v "$PWD":/src:ro lilyterm-dev
```

The source tree is mounted read-only and built out-of-tree in the container,
so nothing is written to your checkout. To keep the logs and screenshots:

```sh
docker run --rm -v "$PWD":/src:ro -v /tmp/lilyterm-test:/build/test-output lilyterm-dev
```

The image runs `docker/run-tests.sh` of the source in `/src`, not a copy of
its own: a change of the script needs no new image.

## The test suite (`run-tests.sh`)

`run-tests.sh` configures, builds and installs LilyTerm, runs the unit
tests of `src/misc.c` (`tests/unit/`: escape sequences, the socket data,
`-g` geometries, the environment of a new terminal), then starts it for
real under two headless display servers:

* **X11**: `Xvfb`, with `GDK_BACKEND=x11`
* **Wayland**: `sway` with the wlroots headless backend, with
  `GDK_BACKEND=wayland` and **no** `DISPLAY`, so nothing can fall back to
  XWayland

For each of them it checks that

* a command given with `-E` runs inside a pty with a sane environment,
* the window is mapped (X11) and a screenshot can be taken,
* LilyTerm exits with status 0 when the last tab closes,
* nothing is logged as `CRITICAL`/`WARNING` by GLib, GTK+ or VTE,
* `-g COLUMNSxROWS` produces exactly that terminal size (`stty size`),
* a second `lilyterm` hands its command to the running one over the socket,
* the sample profile printed by `-p` is accepted by `-u`,
* split panes: Ctrl+Shift+E splits right and a split down key rebound in
  the profile (Alt+Down) splits down, without changing the window size;
  Ctrl+Shift+N moves the focus, Ctrl+Shift+W and a pane's shell exiting
  give the space back, and the last pane closing ends LilyTerm. Keys are
  sent with `xdotool` (X11) and `wtype` (Wayland); each pane reports its own `stty size`,
* the window title and profile settings applied to VTE (X11 only: mouse
  clicks, the window name, `WM_HINTS` and window screenshots): a title set
  by the program in the terminal becomes the window title, a double click
  selects a word by `word_chars`, a
  bell in a window without the focus sets the urgency hint (`urgent_bell`),
  and `cursor_shape` changes how the cursor looks.

Useful knobs: `BACKENDS="x11"` or `BACKENDS="wayland"` to test one of them,
`BACKENDS=` (empty) for only the build and the tests that need no display,
`LILYTERM=/path/to/binary` to test an existing binary instead of building,
and `SANITIZE=1` to build with AddressSanitizer and UndefinedBehaviorSanitizer:
every report of theirs (in `test-output/sanitizer.PID`) fails the run. Leaks
are not checked, as GTK+ keeps memory until the end on purpose.

```sh
docker run --rm -e SANITIZE=1 -v "$PWD":/src:ro lilyterm-dev
tests/unit/run.sh                      # only the unit tests, on the host
```

## The static binary

```sh
docker build -f docker/Dockerfile.static --target export --output dist .
./dist/lilyterm
```

The result is a `static-pie` ELF: no interpreter, no `NEEDED` entry. It does
not care which libc, GTK+ or VTE the host has (or whether it has any).
`dist/share` and `dist/etc` hold the optional data files (translations, icon,
man page, desktop entry, system wide `lilyterm.conf`).

To run the same test suite against the static binary, in an image that has
neither GTK+ nor VTE installed:

```sh
docker build -f docker/Dockerfile.static --target test -t lilyterm-static-test .
docker run --rm lilyterm-static-test
```

### What is inside

| Component   | Version  | From                         |
|-------------|----------|------------------------------|
| GTK+        | 3.24.52  | built from source            |
| VTE         | 0.84.1   | built from source            |
| GLib        | 2.88.3   | built from source            |
| fontconfig  | 2.18.3   | built from source            |
| Pango       | 1.58.2   | built from source            |
| gdk-pixbuf  | 2.44.8   | built from source            |
| ATK         | 2.62.0   | built from source (libatk)   |
| GnuTLS      | 3.8.13   | built from source            |
| libepoxy    | 1.5.10   | built from source            |
| cairo, HarfBuzz, FreeType, ICU, X11, Wayland, xkbcommon, ... | Alpine | `*-static` packages |

Every tarball that is downloaded is pinned to a SHA-256 checksum. To move to a
newer release, change the URL and the checksum next to it.

### What a static binary can not do

A static musl binary has no `dlopen()`. Everything that GTK+ would normally
load as a plugin is either compiled in, or not available:

* **Input methods**: the ones shipped with GTK+ are built in (including the
  Wayland text-input one, and XIM). IBus/Fcitx work through those two; their
  own GTK+ modules can not be loaded.
* **Desktop settings on Wayland**: GTK+ reads theme, font and cursor settings
  through GSettings/dconf, whose backend is a GIO plugin. The static binary
  falls back to `~/.config/gtk-3.0/settings.ini` and the `GTK_THEME`
  environment variable. On X11 the settings come over XSETTINGS and work.
* **SVG icons**: the SVG loader is librsvg's plugin. Icons that only exist as
  SVG in the icon theme are not drawn. GTK+'s own built-in icons are PNG.
* **Screen readers**: the AT-SPI bridge is not linked in.
* **Printing** and **OpenGL**: not used by LilyTerm.
* **NSS**: users that only exist in LDAP/SSSD are not resolved by name.

Files that are still read from the host at run time: fonts and
`/etc/fonts`, the XKB data in `/usr/share/X11/xkb`, cursor and icon themes,
and the terminfo entry of `$TERM`. Every desktop system has them.

## Releases

`docker/package.sh` turns `dist/` into what a release carries, in `release/`:

| File                                   | What it is                                              |
|----------------------------------------|---------------------------------------------------------|
| `lilyterm-x86_64`                      | the bare binary: download, `chmod +x`, run              |
| `lilyterm-VERSION-linux-x86_64.tar.gz` | the binary, its data files, and `install.sh`            |
| `SHA256SUMS`                           | checksums of all the files of the release               |

The release also carries `LilyTerm-VERSION-macos-arm64.dmg`, built by the
GitHub workflow on macOS (see below).

`install.sh` (from the tarball) installs into `~/.local`, or into `/usr/local`
when run as root; `PREFIX=...` overrides it and `--uninstall` removes it again.

Every change is built and tested on Gitea first. The workflow in
`.gitea/workflows/build.yml` runs on every push to `main` and every pull
request: both test suites, X11 and Wayland included, the same tests again with
the sanitizers (`SANITIZE=1`), and the packaged static binary as a workflow
artifact. Its jobs run with the Docker of the Gitea host,
so the images and their build cache stay there between runs.

The same commits are pushed to GitHub once they passed on Gitea. The GitHub
workflow in `.github/workflows/build.yml` only runs for a tag `vX.Y.Z`: it
builds the static binary, runs the tests that need no display (`BACKENDS=`;
on the shared GitHub runners the key presses of the X11 and Wayland tests get
lost now and then), builds the macOS app on an Apple Silicon runner
(`macos/bundle-app.sh`) and tests it with Homebrew moved away
(`macos/test-app.sh`), and publishes the release with the .dmg. It can also be
started by hand for a branch (Actions > build > Run workflow): the same builds
and tests, without a release. Its compiled dependencies
are kept as a build cache in the GitHub container registry
(`ghcr.io/OWNER/lilyterm-buildcache`): a run takes a few minutes, and GLib,
GTK+ and VTE are only compiled again (about half an hour) when
`Dockerfile.static` changes.

The tag has to match `VERSION` in `.default`, which is what `lilyterm -v` prints:

```sh
# after setting VERSION in .default and committing it
git tag "v$(sed -n 's/^VERSION = //p' .default)"
git push origin --tags
```

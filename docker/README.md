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

## The test suite (`run-tests.sh`)

`run-tests.sh` configures, builds and installs LilyTerm, then starts it for
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
* the sample profile printed by `-p` is accepted by `-u`.

Useful knobs: `BACKENDS="x11"` or `BACKENDS="wayland"` to test one of them,
`LILYTERM=/path/to/binary` to test an existing binary instead of building.

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
| `SHA256SUMS`                           | checksums of the two                                    |

`install.sh` (from the tarball) installs into `~/.local`, or into `/usr/local`
when run as root; `PREFIX=...` overrides it and `--uninstall` removes it again.

The GitHub workflow in `.github/workflows/build.yml` runs both test suites on
every push and pull request, and keeps the packaged files as a workflow
artifact. The compiled dependencies are kept as a build cache in the GitHub
container registry (`ghcr.io/OWNER/lilyterm-buildcache`), shared by all
branches and tags: a run takes a few minutes, and GLib, GTK+ and VTE are only
compiled again (about half an hour) when `Dockerfile.static` changes.

Pushing a tag `vX.Y.Z` also publishes them as a GitHub release. The
tag has to match `VERSION` in `.default`, which is what `lilyterm -v` prints:

```sh
# after setting VERSION in .default and committing it
git tag "v$(sed -n 's/^VERSION = //p' .default)"
git push origin --tags
```

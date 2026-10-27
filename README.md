# LilyTerm

LilyTerm is a light and easy to use terminal emulator for Linux and
macOS, based on the [VTE](https://gitlab.gnome.org/GNOME/vte) library. It
has tabs, split panes, profiles, and key bindings that you can change from
the right click menu.

This repository is a modernized fork of
[Tetralet/LilyTerm](https://github.com/Tetralet/LilyTerm), whose last
release was in 2019. The fork builds against current GTK+ 3 (3.24) and
VTE (0.84), runs natively on Wayland and on X11, and is also released as
one fully static binary that runs on any x86_64 Linux system. On macOS
(Apple Silicon) it runs with the native backend of GTK+ 3, from a .dmg
with everything inside, or installed with Homebrew.

![Build](https://img.shields.io/github/actions/workflow/status/ebal/LilyTerm/build.yml?branch=main)
![Release](https://img.shields.io/github/v/release/ebal/LilyTerm)
![License](https://img.shields.io/github/license/ebal/LilyTerm)

## Features

- Tabs: rename, reorder, and drag them to another window (a tab with
  split panes cannot be dragged yet).
- Split panes: split a tab side by side or one above the other, as many
  times as you want.
- Profiles: save the current tabs, colors and fonts with [Save settings]
  in the right click menu.
- Key bindings: change or disable every key binding with [Set key binding]
  in the right click menu.
- Search: `Ctrl+F` (`Cmd+F` on macOS) opens a search bar over the top
  right corner, which finds text in the scrollback as you type. `Enter`
  goes to the match before, `Shift+Enter` to the one after, `Esc` closes
  it. `Aa` makes it case sensitive, `.*` takes the text as a regular
  expression.
- Hyperlinks: click a web or `file://` URL, or an email address, to open
  it. An email address opens as a `mailto:` URL in your mail client.
  Links that programs mark with OSC 8 (`ls --hyperlink`, `gcc`, `git`,
  `systemctl`, ...) open the same way, and show where they go in the
  corner of the window when the mouse is over them. Only web, ftp, `mailto:` and `file://` links open,
  and a `file://` link only to a file on this machine (not one that `ls`
  printed on a server over ssh).
- Paths: click a path to open it with the file manager command, as
  `open` / `xdg-open` do: `/etc/hosts`, `~/notes.txt`, `./build`,
  `src/main.c` and, with a line number as compilers print them,
  `main.c:42:5`. A relative path is taken in the directory of the shell.
  Only a file or directory that exists opens.
- Tab names: a tab shows what runs in it, or the directory of the shell,
  as `~/src/LilyTerm` for a directory in your home. The whole name is in
  its tooltip.
- One running LilyTerm for all windows: a second `lilyterm` command opens
  its window in the process that already runs, over a socket. Start
  LilyTerm with `-s` to run a window in its own process.
- Wayland and X11, with the same binary.

## Installation

### Static binary (any x86_64 Linux)

The [releases page](https://github.com/ebal/LilyTerm/releases) has one
static binary, `lilyterm-x86_64`. GTK+ and VTE are linked in, so it does
not need any library of your system.

```bash
curl -LO https://github.com/ebal/LilyTerm/releases/latest/download/lilyterm-x86_64
curl -LO https://github.com/ebal/LilyTerm/releases/latest/download/SHA256SUMS
sha256sum --check --ignore-missing SHA256SUMS
chmod +x lilyterm-x86_64
./lilyterm-x86_64
```

To also install the icon, the desktop entry, the manual and the
translations, use the tarball from the same page:

```bash
tar xf lilyterm-*-linux-x86_64.tar.gz
cd lilyterm-*-linux-x86_64
./install.sh              # into ~/.local
sudo ./install.sh         # into /usr/local, for every user
./install.sh --uninstall  # remove it again
```

Set `PREFIX=/usr` to install somewhere else. The translations are only
found when LilyTerm is installed with `PREFIX=/usr`.

The static binary cannot load plugins. Because of this:

- On Wayland, it reads the theme and font settings from
  `~/.config/gtk-3.0/settings.ini` and `$GTK_THEME`, not from dconf.
- Icons that exist only as SVG in your icon theme are not shown.
- If you use IBus or Fcitx on X11, start it with `GTK_IM_MODULE=xim`.

[docker/README.md](docker/README.md) has the full list.

### macOS (Apple Silicon)

The [releases page](https://github.com/ebal/LilyTerm/releases) has
`LilyTerm-VERSION-macos-arm64.dmg`: LilyTerm.app with GTK+ 3 and VTE
inside, so it needs nothing else.

1. Open the .dmg and drag LilyTerm to Applications. It has to be in
   `/Applications`, where it finds its icon, translations and settings.
2. LilyTerm is not signed by Apple, so macOS refuses to open it the
   first time ("cannot be opened" or "is damaged"). Remove the quarantine
   flag once, in Terminal:

   ```bash
   xattr -dr com.apple.quarantine /Applications/LilyTerm.app
   ```

3. Open LilyTerm from Launchpad, Spotlight or the Applications folder.
   It starts a login shell, so your `~/.zprofile` is read as in Terminal.

The .dmg is built for the macOS version of the GitHub runner and later
(`LSMinimumSystemVersion` in the app). Intel Macs are not supported.

On macOS, window transparency is off (GTK+ 3 would draw the title bar
clear as well). LilyTerm has a menu bar there, and its key bindings use
the Command key (see [Key bindings](#key-bindings)), so most Ctrl keys go
to the programs in the terminal. Links open with `open`, also with a
profile that says `xdg-open` (from Linux, or with xdg-utils installed).

#### Or with Homebrew

```bash
brew install ebal/tap/lilyterm
lilyterm
```

Homebrew builds LilyTerm from source, with GTK+ 3 and VTE from Homebrew,
and installs the `lilyterm` command, not an app. The
[tap's README](https://github.com/ebal/homebrew-tap) has the details.

### From source

#### Requirements

- GTK+ 3.22 or later (`gtk+-3.0`), VTE 0.50 or later for GTK+ 3
  (`vte-2.91`) and GLib 2.32 or later, with their development files
- A C compiler, `make`, `pkg-config` and `gettext`

On Arch Linux, install the `gtk3`, `vte3` and `gettext` packages. On
Debian and Ubuntu, install `libgtk-3-dev`, `libvte-2.91-dev` and
`gettext`.

#### Build and install

```bash
git clone https://github.com/ebal/LilyTerm.git
cd LilyTerm
./configure --prefix=/usr --sysconfdir=/etc/xdg
make
sudo make install
```

`./configure --help` shows the other options. `sudo make uninstall`
removes LilyTerm again.

## Usage

Start LilyTerm with three tabs, in your home directory:

```bash
lilyterm -t 3 -d ~
```

Run a command in a new tab, and keep the tab open after the command ends:

```bash
lilyterm -H -e htop
```

`lilyterm --help` lists every option. `man lilyterm` is the full manual.

### Key bindings

These are the defaults. You can change each of them, or disable it, with
[Set key binding] in the right click menu.

| Keys                    | Action                                         |
|-------------------------|------------------------------------------------|
| `Ctrl+T`                | New tab in the current directory               |
| `Ctrl+PgUp` / `PgDn`    | Previous / next tab                            |
| `Ctrl+Home` / `End`     | First / last tab                               |
| `Ctrl+F1` ... `F12`     | Tab 1 ... 12                                   |
| `Ctrl+Shift+E`          | Split the pane side by side                    |
| `Ctrl+Shift+O`          | Split the pane one above the other             |
| `Ctrl+Shift+N`          | Move the focus to the next pane                |
| `Ctrl+Shift+W`          | Close the pane (the last pane closes the tab)  |
| `Ctrl+F`                | Search bar (`F3` / `Shift+F3`: next / previous match) |
| `Ctrl+Delete` / `Insert`| Copy / paste with the clipboard                |
| `Shift+Delete` / `Insert`| Copy / paste with the primary selection       |
| `Ctrl+=` / `-` / `Enter`| Bigger / smaller / reset font                  |
| `Alt+F11` / `Alt+Enter` | Full window / full screen                      |
| ``Ctrl+` ``             | Disable or enable hyperlinks, function keys and the menu |

The right click menu also has [Split right], [Split down] and
[Close pane].

On macOS the defaults use the Command key instead. The keys not listed
here stay as in the table above.

| Keys                    | Action                                         |
|-------------------------|------------------------------------------------|
| `Cmd+T` / `Cmd+N`       | New tab / new window in the current directory  |
| `Cmd+W`                 | Close the tab                                  |
| `Cmd+Shift+[` / `]`     | Previous / next tab                            |
| `Cmd+Home` / `End`      | First / last tab                               |
| `Cmd+Alt+[` / `]`       | Move the tab forward / backward                |
| `Cmd+Alt+Up` / `Down`   | Move the tab to first / last                   |
| `Cmd+Shift+I`           | Rename the tab                                 |
| `Cmd+1` ... `9`         | Tab 1 ... 9                                    |
| `Cmd+D` / `Cmd+Shift+D` | Split the pane side by side / one above the other |
| `Cmd+]` / `Cmd+Shift+W` | Next pane / close the pane                     |
| `Cmd+C` / `Cmd+V` / `Cmd+A` | Copy / paste / select all                  |
| `Cmd+F`                 | Search bar (`Cmd+G` / `Cmd+Shift+G`: next / previous match) |
| `Cmd+K`                 | Clear the scrollback                           |
| `Cmd+=` / `-` / `0`     | Bigger / smaller / reset font                  |
| `Cmd+Ctrl+F`            | Full screen                                    |
| `Cmd+Q` / `Cmd+H` / `Cmd+M` | Quit / hide / minimize                     |

In a profile, `Cmd` is the Command key on macOS and the Super (Win) key on
Linux, e.g. `new_tab_key = Cmd T`. On macOS, a profile saved before 0.12.0
(on macOS or Linux) has its keys that are still the Linux defaults read as
the macOS ones. A profile saved by 0.12.0 or later is used as it is. To
keep a Ctrl key, set it again with [Set key binding] and save the profile.

### Configuration

| File                             | What it is                    |
|----------------------------------|-------------------------------|
| `/etc/xdg/lilyterm.conf`         | The system wide configuration |
| `~/.config/lilyterm/default.conf`| Your profile                  |

`lilyterm -p` prints a sample profile with every setting and its
description. `lilyterm -u FILE` starts LilyTerm with another profile.

The running LilyTerm listens on a socket in `$XDG_RUNTIME_DIR` (or in
`/tmp` without one), `.LilyTerm_USER` followed by the display name. It holds a
lock on the `.lock` file next to it for as long as it runs, so the next
`lilyterm` knows that it is there even when it is busy.

## Development

The `docker/` directory has two container images. One builds LilyTerm
against the system libraries and runs the test suite on X11 (Xvfb) and on
Wayland (headless sway). The other builds the static binary.

```bash
docker build -f docker/Dockerfile.dev -t lilyterm-dev .
docker run --rm -v "$PWD":/src:ro lilyterm-dev
docker run --rm -e SANITIZE=1 -v "$PWD":/src:ro lilyterm-dev   # with ASan and UBSan

docker build -f docker/Dockerfile.static --target export --output dist .
./dist/lilyterm
```

The test suite starts with the unit tests of `tests/unit/` (also
`tests/unit/run.sh` on its own). The macOS app is built by `macos/bundle-app.sh`, on macOS with GTK+ 3
and VTE from Homebrew, and tested by `macos/test-app.sh` with Homebrew
moved away; GitHub Actions runs both on an Apple Silicon runner.

[docker/README.md](docker/README.md) explains the tests, the static
build and the release process. Every change is built and tested on Gitea
first; GitHub Actions builds and publishes a release for every tag
`vX.Y.Z`.

## Contributing

Bug reports and pull requests are welcome on the
[issue tracker](https://github.com/ebal/LilyTerm/issues). Commit messages
follow [Conventional Commits](https://www.conventionalcommits.org/).
Run the test suite in `docker/` before you open a pull request.

## Changes

[CHANGELOG.md](CHANGELOG.md) lists the changes of every release, in the
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) format. Since
0.9.10 the versions follow [Semantic Versioning](https://semver.org/).

## Authors

LilyTerm was written by Lu, Chao-Ming (Tetralet) and its contributors.
Evaggelos Balaskas maintains this fork.

## License

LilyTerm is free software under the
[GNU General Public License version 3](COPYING).

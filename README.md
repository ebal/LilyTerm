# LilyTerm

LilyTerm is a light and easy to use terminal emulator for Linux, based on
the [VTE](https://gitlab.gnome.org/GNOME/vte) library. It has tabs, split
panes, profiles, and key bindings that you can change from the right click
menu.

This repository is a modernized fork of
[Tetralet/LilyTerm](https://github.com/Tetralet/LilyTerm), whose last
release was in 2019. The fork builds against current GTK+ 3 (3.24) and
VTE (0.84), runs natively on Wayland and on X11, and is also released as
one fully static binary that runs on any x86_64 Linux system.

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
- Hyperlinks: click a web or `file://` URL, or an email address, to open
  it.
- One running LilyTerm for all windows: a second `lilyterm` command opens
  its window in the process that already runs, over a socket.
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

### From source

#### Requirements

- GTK+ 3 (`gtk+-3.0`) and VTE for GTK+ 3 (`vte-2.91`), with their
  development files
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
| `Ctrl+F`                | Find text (`F3` / `Shift+F3`: next / previous) |
| `Ctrl+Delete` / `Insert`| Copy / paste with the clipboard                |
| `Shift+Delete` / `Insert`| Copy / paste with the primary selection       |
| `Ctrl++` / `-` / `Enter`| Bigger / smaller / reset font                  |
| `Alt+F11` / `Alt+Enter` | Full window / full screen                      |
| ``Ctrl+` ``             | Disable or enable hyperlinks, function keys and the menu |

The right click menu also has [Split right], [Split down] and
[Close pane].

### Configuration

| File                             | What it is                    |
|----------------------------------|-------------------------------|
| `/etc/xdg/lilyterm.conf`         | The system wide configuration |
| `~/.config/lilyterm/default.conf`| Your profile                  |

`lilyterm -p` prints a sample profile with every setting and its
description. `lilyterm -u FILE` starts LilyTerm with another profile.

## Development

The `docker/` directory has two container images. One builds LilyTerm
against the system libraries and runs the test suite on X11 (Xvfb) and on
Wayland (headless sway). The other builds the static binary.

```bash
docker build -f docker/Dockerfile.dev -t lilyterm-dev .
docker run --rm -v "$PWD":/src:ro lilyterm-dev

docker build -f docker/Dockerfile.static --target export --output dist .
./dist/lilyterm
```

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

[ChangeLog.md](ChangeLog.md) lists the changes of every release.

## Authors

LilyTerm was written by Lu, Chao-Ming (Tetralet) and its contributors.
Evaggelos Balaskas maintains this fork.

## License

LilyTerm is free software under the
[GNU General Public License version 3](COPYING).

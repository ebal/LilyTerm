# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html)
since 0.9.10. The releases up to 0.9.9.4 are those of the original LilyTerm by
Lu, Chao-Ming (Tetralet); their entries are converted from its ChangeLog.

## [Unreleased]

## [0.12.0] - 2026-10-20

### Added

- Add Command key shortcuts on macOS: Cmd+T, Cmd+W, Cmd+N, Cmd+C /
  Cmd+V, Cmd+1 ... 9, Cmd+F, Cmd+D and more, and Cmd+Q, Cmd+H and Cmd+M to
  quit, hide and minimize. They work with any keyboard layout (Greek,
  Russian, ...).
- Add a `Cmd` modifier for the key bindings: the Command key on macOS, the
  Super (Win) key on Linux.
- Add a menu bar on macOS (LilyTerm, Shell, Edit, View and Window), with
  the keys of the key bindings.
- Open a link with Cmd+click on macOS, where the profile asks for
  Ctrl+click.
- Start a new tab in the directory the shell reported (OSC 7) when the one
  of its process is unknown, as for a root shell or a removed directory.
  A directory reported by a shell on another machine (over ssh) is never
  used.

### Changed

- Change the default key bindings on macOS from Ctrl to Cmd, so most Ctrl
  keys go to the programs in the terminal (Ctrl+[ is Esc in vim again).
  A profile saved before 0.12.0 gets the new keys too, for every key that
  is still its Linux default; profiles now say `key_binding_version` in
  `[key]`. To keep a Ctrl key, set it again with [Set key binding] and
  save the profile.
- Ignore a request from a `lilyterm` older than 0.12.0 in a running
  LilyTerm 0.12.0: it cannot tell a complete one from one cut short.
  Restart LilyTerm after upgrading.

### Fixed

- Fix a heap buffer overflow when pasting text that ends with a backslash
  and a newline with [join the lines], and a read before the buffer when
  nothing was left after the join.
- Fix a second `lilyterm` losing its command: a newline in the
  environment (an exported bash function) or in an argument cut the
  request short, and so did a request bigger than the socket buffer
  (8 KB on macOS). A request cut short by a client that died is no longer
  run.
- Fix [New window with profile] quitting a LilyTerm started with `-s`
  (it sent the window to another LilyTerm), and losing it with a big
  environment.
- Fix a LilyTerm that could not send its request removing the socket of
  the running one, and the socket server stopping after one failed
  request.
- Fix programs whose name holds a space or a parenthesis: their parent and
  foreground process were read wrong (tab titles, the directory of a new
  tab, the warning about running programs).
- Fix the size of a window made from a dragged tab when the tabs bar is
  hidden.
- Open links with `open` on macOS also with a profile saved by an older
  LilyTerm, or on Linux, that says `xdg-open`.
- Fix memory leaks on every paste and in the list of running programs.

## [0.11.2] - 2026-10-20

### Fixed

- Fix a crash, or the wrong program being run, on a click on a link after
  Ctrl was pressed: each press of Ctrl renumbers the link patterns in
  current VTE releases, and LilyTerm took the new number for the kind of
  link.
- Open links with `open` on macOS, which has no `xdg-open`.

## [0.11.1] - 2026-10-20

### Fixed

- Fix a crash when the tabs of a window use different font sizes, for
  example after Ctrl+= in one tab, and another tab is chosen or a tab
  changes its size. It happened since VTE 0.38, on Linux and on macOS.

## [0.11.0] - 2026-10-19

### Added

- Show the running command and the current directory in the tab names and
  the window title on macOS, mark root privileges, and ask before closing a
  tab with running programs, as on Linux. macOS has no `/proc`: the data
  comes from sysctl and libproc.
- Add unit tests for the string and parsing helpers (`tests/unit/`), run by
  the test suite.
- Add `SANITIZE=1` to the test suite, to run it with AddressSanitizer and
  UndefinedBehaviorSanitizer; Gitea CI does so on every push and fails on
  any report.
- Add `packaging/arch/PKGBUILD`, which builds the release tag and runs the
  unit tests.

### Changed

- Point `--help`, the About page and the bug report address to
  github.com/ebal/LilyTerm instead of the original project, which stopped
  in 2019.
- Hand the command of a second `lilyterm` over to the running one without
  waiting one second.

### Removed

- Remove the advice to use the GTK+ 2 version from the About page.

### Fixed

- Fix a double free of the socket channel after a read error.
- Fix a GTK+ warning in the find dialog when the selection holds only line
  breaks; it falls back to the last search now.

### Security

- Send the environment and the command line of a second `lilyterm` only to
  a socket server of the same user. Without `$XDG_RUNTIME_DIR` the socket is
  in `/tmp`, where another user could create it first and receive them.

## [0.10.1] - 2026-10-19

### Added

- Release LilyTerm for macOS on Apple Silicon as a .dmg
  (`LilyTerm-VERSION-macos-arm64.dmg`), with GTK+ 3 and VTE inside the app,
  so it needs no Homebrew. It is not signed by Apple: after dragging it to
  Applications, run `xattr -dr com.apple.quarantine /Applications/LilyTerm.app`
  once.
- Build the app on an Apple Silicon runner of GitHub Actions and test it
  with Homebrew moved away (`macos/bundle-app.sh`, `macos/test-app.sh`).

### Fixed

- Stop reading past the end of a link regex in the profile that ends with a
  backslash.
- Use the right default for `address_end` when only some of the link
  regexes are customized, so that a link no longer takes a trailing "." or
  "," with it.

## [0.10.0] - 2026-10-19

LilyTerm runs on macOS, built from the same code as on Linux. There the key
bindings use Ctrl, not Cmd.

### Added

- Run on macOS with the native Quartz backend of GTK+ 3, without X11 or
  XQuartz: `brew install ebal/tap/lilyterm`.
- Show the LilyTerm icon in the Dock, quit with [Quit] in the Dock as with
  [Quit] in the right click menu, and open the first window in front of the
  terminal LilyTerm was started from.

### Changed

- Turn window transparency off on macOS when `use_rgba` is left to auto:
  GTK+ 3 draws the title bar of an RGBA window clear there, so the window
  could not be moved. `use_rgba = 1` still turns it on.

### Fixed

- Give the theme colors a valid alpha and keep the dimmed colors in [0, 1]:
  VTE refused the foreground color on macOS ("vte_terminal_set_colors:
  assertion 'valid_color(foreground)' failed").
- Fix a write past the end of `sun_path` with a long socket path on macOS and
  the BSDs, where it has 104 bytes, not 108.
- Strip the binary with the options of Apple's `strip` on macOS.

### Security

- Check that a socket request comes from the same user with `getpeereid()`
  on macOS and the BSDs, where `SO_PEERCRED` does not exist and the check
  was skipped.

## [0.9.10] - 2026-10-18

LilyTerm follows Semantic Versioning from this release on: 0.9.10 follows
0.9.9.9.

### Removed

- Drop GTK+ 2 and old GTK+ 3, VTE and GLib releases: LilyTerm needs GTK+
  3.22, VTE 0.50 (`vte-2.91`) and GLib 2.32 or later, which `./configure`
  checks.
- Remove `--with-gtk` and `--enable-background`: VTE 0.38 removed the
  background image and the transparency of VTE.

### Fixed

- Make `cursor_shape`, `urgent_bell` and `word_chars` work again with VTE
  0.38 and later: the settings were ignored, `lilyterm -p` did not list them
  and their menu items were gone. `word_chars` still takes ranges like `A-Z`.
- Update the tab name and the window title again with the title a program
  sets, the running command and the current directory: with GTK+ 3 they never
  changed after a tab was opened.
- Fix a crash when a split pane closed: the current directory of a page
  could be freed while it was still in use.

## [0.9.9.9] - 2026-10-17

The first release with the fixes of 0.9.9.7 and 0.9.9.8, whose tags never got
a release because the X11 and Wayland tests failed on the GitHub runner;
these tests run only locally from now on.

### Fixed

- Check which pane has the focus in the split pane tests before closing one,
  and send keys without modifiers correctly on Wayland.

## [0.9.9.8] - 2026-10-17

The first release with the fixes of 0.9.9.7, whose tag never got a release.

### Fixed

- Make the split pane tests reliable on slow machines: give the window the
  keyboard focus before each key press, and press a split key again if no
  pane appears.

## [0.9.9.7] - 2026-10-17

### Changed

- Rewrite the README for the modern LilyTerm.

### Fixed

- Show the icons on GTK+ 3, instead of a "missing image" placeholder: map the
  GTK+ stock ids (`gtk-close`) to icon names (`window-close`), symbolic ones
  for the small icons, and leave out an icon that no installed theme has.
- Restore the label of the Paste button of [Paste to every Vte Terminal].
- Fall back to the `lilyterm` icon of the icon theme for the window when it
  is not installed in the pixmaps directory (the static build in `~/.local`).

## [0.9.9.6] - 2026-10-17

A split tab can not be dragged to another window yet, and while a tab is
split the window is not resized in whole characters.

### Added

- Split a tab into panes, side by side (Ctrl+Shift+E) or one above the other
  (Ctrl+Shift+O); move the focus with Ctrl+Shift+N and close a pane with
  Ctrl+Shift+W, or use the new items of the right click menu. A pane opens in
  the directory of the one it was split from, the window keeps its size, and
  the divider between panes can be dragged.
- Add a "Split panes" group to [Set key binding]; the pane keys are saved in
  the profile (`split_right_key`, `split_down_key`, `next_pane_key`,
  `close_pane_key`).

### Changed

- Give the focus to the pane under the mouse on a right click, so that the
  menu acts on that pane.

## [0.9.9.5] - 2026-10-06

Bring LilyTerm up to date with GTK+ 3.24, VTE 0.84, GCC 15+ and Wayland.

### Added

- Run natively on Wayland, with the same binary as on X11.
- Add container images to build and test LilyTerm (`docker/Dockerfile.dev`)
  and to build a fully static binary (`docker/Dockerfile.static`); see
  docker/README.md.
- Add a test suite that runs LilyTerm under a headless X server and a
  headless Wayland compositor (`docker/run-tests.sh`).
- Honor `PKG_CONFIG`, `PKG_CONFIG_FLAGS` and `LIBS` when building.
- Publish releases with a static binary and a tarball with an install script
  from a GitHub workflow (`docker/package.sh`, `docker/install.sh`).

### Changed

- Build with GTK+ 3 and vte-2.91 by default; GTK+ 2 is still available with
  `./configure --with-gtk=2.0`.
- Open the context menu with `gtk_menu_popup_at_pointer()` (GTK+ 3.22).
- Stop using Xlib directly: `XParseGeometry()` is replaced, and the X window
  ID is only asked for when running on X11.
- Build with C23 compilers (no empty `()` prototypes) and with musl libc
  (`realpath()` instead of `canonicalize_file_name()`); `configure` and the
  Makefiles no longer need `whereis`.

### Fixed

- Spawn terminals with VTE 0.62 and later, which refused environment entries
  that are not "NAME=value" and the `G_SPAWN_CHILD_INHERITS_STDIN` flag.
- Let the variables LilyTerm sets for a new tab (LANG, LC_ALL, ...) override
  the inherited ones.
- Fix a "Source ID was not found" critical when closing a tab whose child had
  already exited.
- Fix reading uninitialized values for a geometry without a size, like
  "+10+10".

### Security

- Create the socket in `$XDG_RUNTIME_DIR` instead of `/tmp`, and refuse
  requests from other users (`SO_PEERCRED`).

## [0.9.9.4] - 2013-02-28

It is a hotfix version of 0.9.9.3.

### Fixed

- Also dim the foreground color when inactive.
- The cursor_color settings doesn't work correctly.
  Very thanks to aeglos for reporting this bug.
- The color of cursor should not been reset after changing the color of
  foreground color. Very thanks to aeglos and cmchao for reporting this bug.
- Don't try to clean the environs when launching LilyTerm.
  Very thanks to captnfab for reporting this bug.
- [Disable functions keys] didn't apply immediately.
- Fix several possible memory leaks and segfault errors.

## [0.9.9.3] - 2013-02-23

### Added

- Roll back autotools build system.
- Support for -n/--tab_names command line option to specify tab names,
  separate with `<space>`. Very Thanks daroczig for this idea.
- Added  a [-j/--join] command line option to integrate new created tabs
  to the last accessed window. VERY THANKS daroczig for this great idea.
- Implement a dialog for adjusting the colors of ANSI themes.
  Very Thanks PaBLoX-CL for this idea.
- Better behavior for setting ANSI color themes.

### Fixed

- Better compatible with OpenBSD.
  Very thanks vext01 for reporting this bug.
- Fix compilation error due to missing comment.
  Very thanks Jekyll Wu for fixing this bug.
- Fix compilation failure by missing #includes.
  Very thanks Jekyll Wu for fixing this bug.

## [0.9.9.2] - 2012-06-16

It is a hotfix version of 0.9.9.1.

### Added

- More lovely configure script output.
- Better compatible with `hier` file systems used by FreeBSD/OpenBSD.
- Better compatible with FreeBSD.

### Fixed

- Smarter 'whereis' commands for finding the full path of a program.
  Very thanks to Enlik for reporting this bug. close #11.
- Use '=' in the configure test expression.
  Very thanks to Enlik for reporting this bug.
- Don't exit if invalid option is given when running configure script.

## [0.9.9.1] - 2012-06-06

It is a hotfix version of 0.9.9.

### Added

- Added a new option "embedded_copy_paste_menu" to embed the copy/paste
  menu to the main right click menu, and is enabled by default.
  VERY THANKS to Slavko for this great idea.
- Added a new command line option [-H/--hold], may hold the terminal window
  open when the command following -e/-x terminated.
  VERY THANKS to Douglas McFadzean for this great idea.
- Applied new Portuguese translation. VERY THANKS to Rafael Ferreira!
- de.po, ru.po, uk.po, sk.po, it.po and pt_BR.po have been updated.
  Very thanks to Mario Blttermann, Vladimir Smolyar, Slavko, Marco Paolone,
  and Rafael Ferreira for the translation!

### Fixed

- Urgent bell should be disabled if the window is getting focus.
  VERY THANKS to daspostloch for reporting this bug.
- Respect to the PATH environment, or the -e option won't work if the command
  is not in /usr/bin.
  VERY THANKS to Douglas McFadzean for reporting this bug.
- Added a missed '\' before '-' in the manpage file.
  VERY THANKS to Slavko for the patch.
- Update the ansi color codes of xterm theme.
  Very thanks to caleb for reporting this bug!
- Short the help messages to fit in 80 columns.
  VERY THANKS to Rafael Ferreira for reporting this bug.
- Using ETCDIR instead of g_get_system_config_dirs().
  VERY THANKS to Rafael Ferreira for reporting this bug!
- The settings for [always show/hide the tabs bar] don't be saved as excepted.
  VERY THANKS to BV1AL for reporting this bug.

## [0.9.9] - 2012-04-04

Trying to migrate to GTK3+ but failed. Still having big trouble in handling geometry.

### Added

- May use `<Win>` and `<Menu>` keys as function key.
  Thanks BV1AL for this idea.
- Added a menuitem "Always hide the tabs bar".
  Thanks BV1AL for this idea.
- Added a menuitem "Always show the tabs bar".
  Thanks Slavko for this idea.
- Applied new Polish translation.
  VERY THANKS to Bogusz Kowalski.
- Add a new menu item [Visible bell].
- Add a new menu item [Urgent bell].
  VERY THANKS to captnfab for this idea and patch.
  Be notice that for performance reason, the Urgent bell will disable
  if the LilyTerm window is in focus.
  i.e. It only works only when the LilyTerm window is losing focus.
- Add new menu item [View clipboard] and [View primary clipboard].
- Add a new menu item [Paste to every Vte Terminal].
  VERY THANKS to Bogusz Kowalski for this idea.
- Add a new menu item [Launch File Manager].
  Thanks Slavko for this idea.
- Confirm before pasting more than 1 lines to the Vte Terminal.
  May add a programs to "paste_texts_whitelist" in profile so that
  LilyTerm will not warn you when trying to paste lines to it.
  Set confirm_to_paste = 0 in user profile to disable this feature.
- The function key "Edit Label" is disabled by default,
  and the menu item [Rename this tab] is shown by default.
- Add a menu item [Add other locales...] to [Change text encoding]
  menu subitem. Thanks BV1AL for this idea.
- Add a new menu item [Reload settings].
- A new key binding `<Ctrl N>`, open a new windows with current directory.
  Thanks mrknacky for this idea.
- Using `<Ctrl E>` to edit tab name is disable by default.
  Thanks Rodolfo Quesada-Zumbado for this idea.
- Support solarized ansi color themes from
  http://ethanschoonover.com/solarized.
  Thanks medicalwei for this idea.
- New handmade build system. autotools had been dropped.
- Added a button `<Join and paste>` to [Confirm to paste texts] dialog.
- Add a new make target: update-po to po/Makefile.
  Thanks caleb for this idea.
- Better about dialog. Thanks Mario Blättermann for this idea.
- Added a new menu item: [Invert Color].
- Add a new option and a new menuitem 'Cursor shape'.
  Thanks ralesi for this great idea.
- Add a new option and a new menuitem 'Change the color of cursor'.
  Thanks ralesi for this great idea.
- Add a new option and a new menuitem 'Auto save settings'.

### Fixed

- Remove '>' from URL matching.
  Thanks BrainWorker for reporting this bug.
- Fix some typo and syntax in translations texts.
  VERY THANKS to Vladimir Smolyar.

## [0.9.9~rc9] - 2011-06-15

It will be the version of 0.9.9,
Release as a rc version in case I forget something.

### Added

- Add "screen" to foreground_program_whitelist.
- A new menu item "Window opacity when inactive" and a new check box menu item
  "Dim window when inactive", may dim the window when inactive if the screen
  is composited.
  Thanks keenerd for this great idea.
- Add a new color theme 'grayscale'. Thanks BV1AL for this idea.
- Applied new Franch translation.
  VERY THANKS to Niels Martignène.
- Applied new Slovak translation.
  VERY THANKS to Slavko.

### Fixed

- A race condition segfault bug if the locale settings is incorrect and the
  -e option is failed at the same time.
- The profile path in help message may not updated after saving the profile.
  Thanks 夢見草 for reporting this bug.
- Some translation strings missed.
  Thanks Vladimir Smolyar and Niels Martignène for reporting this bug.

## [0.9.9~rc8] - 2010-12-10

It is a hotfix version for 0.9.9\~rc7.

### Fixed

- Can not keep correct geometry size under kwin/xfwm4.
  VERY THANKS to Marco Paolone and Vladimir Smolyar for reporting this
  stupid bug.

## [0.9.9~rc7] - 2010-12-06

It is the final preview version of LilyTerm 0.9.9.
CALL FOR TRANSLATORS!

### Added

- Applied new Flemish/Dutch translations.
  VERY THANKS to Adrian Buyssens.
- New Dialog: [Find String], but it is almost unusable.
  Please visit https://bugzilla.gnome.org/show_bug.cgi?id=627886 for
  more details.
- New function key: `<Ctrl>``<F>` to launch the search string dialog,
  `<F3>` to find the next string matching the search regex,
  and `<Shift>``<F3>` to find the previous string matching the search regex.
- New command line option [-g/--geometry] and new profile option
  [geometry], may specify the geometry (size and position) of window
  when starting.
  Very thanks Adrian Buyssens and Hámorszky Balázs for this idea.
- Better [Set function keys] dialog.

### Fixed

- The default function key of 'Copy the text to clipboard' is changed
  into 'Ctrl Delete', and the default function key of 'Paste the text
  in clipboard' is changed into 'Ctrl Insert',
  Thanks Renato for reporting this bug.
- The profile option 'window_opacity', 'background_saturation',
  'font_resize_ratio', 'window_resize_ratio', 'brightness' and
  'inactive_brightness' won't be saved correctly under it_IT locale.
  VERY THANKS to Marco Paolone for reporting this strange bug.

## [0.9.9~rc6] - 2010-10-25

### Added

- Defensive programming, making LilyTerm behave in a predictable manner
  despite unexpected inputs.

### Fixed

- No response under Ubuntu.
  Thanks 老杜, 夢見草 and bb for reporting this bug.
- Switch to $HOME if the init dir is invalid.

## [0.9.9~rc5] - 2010-09-26

### Added

- Better support the --name and --title option from Gtk+2.
  VERY THANKS to caleb and letoh for this idea.
- May set the 'default_locale' and 'locales list' via right click menu.

## [0.9.9~rc4] - 2010-09-21

### Fixed

- Fixed a segfault bug when launched with -e option.

## [0.9.9~rc3] - 2010-09-21

### Fixed

- Fixed a stupid double free() segfault bug.
  VERY THANKS to Aeglos Lin, Solomon Huang and letoh for
  reporting and testing this bug.

## [0.9.9~rc2] - 2010-09-21

### Fixed

- Speed up when initing and starting.
  VERY THANKS to caleb and letoh for reporting this bug.

## [0.9.9~rc] - 2010-09-18

It is the final version of LilyTerm 0.9.9. CALL FOR TRANSLATORS!

### Added

- Applied new Russian and Ukrainian translations.
  VERY THANKS to Vladimir.
- Applied updated Simplified Chinese translation.
  VERY THANKS to GoGoNKT.
- New function key `<Shift>``<Left/Right>`:
  May Emulate a mouse Scroll Up/Down event on Vte Terminal box.
  Thanks qotsa for this idea, and caleb for the codes!
- New function key `<Shift>``<Up/Down>`:
  Asks to Scroll Up/Down 1 line on Vte Terminal box.
  Thanks qotsa for this idea.
- New [Usage] dialog.
- Better [Set function keys] dialog.
- New profile option: "window_title_append_package_name"
  May get rid of 'LilyTerm' on the window title.
  Thanks qotsa for this idea.
- New menuitem [Set background image], and New profile option:
  "background_image" and "scroll_background".
  Thanks caleb for this idea.
- May apply new settings from another profile directly via right click
  menu.
  Thanks BV1AL for this idea.
- [New tab with locale] may pop up a warning dialog if the locale is
  invalid.
- Move some rare used menuitems into [Misc] submenu.
- May use a simplified locale name (like 'zh_TW') in profile option
  'default_locale' and 'locales_list'.
- Using vte_terminal_set_background_image_file(vte, "/dev/null") to
  clear the background image. Thanks caleb for this great idea.
- Manual page updated.

### Removed

- The profile option "default_encoding" and "encodings_list"
  is obsoleted. LilyTerm will detect correct encoding automaticly.
- The profile option "fixed_page_width" is obsoleted.
  The page width will always be fixed on LilyTerm now.
- The profile option "show_get_function_key_menu" is obsoleted.

### Fixed

- The foreground texts will also dim when inactive.
- The profile option "show_transparent_menu" is renamed to
  "show_background_menu".
- The profile option "window_shows_current_page" is renamed to
  "window_title_shows_current_page".
- Fix some typo and syntax in translations texts.
  VERY THANKS to Vladimir.
- Sometimes translations will be mixed with different language
  when switching between locales.
  VERY THANKS to Vladimir for reporting and testing this bug.
- Better behavior when draging a tab to another window.
- May use `<Shift>`+`<non-alphanumeric>` in function keys.

## [0.9.9~pre7] - 2010-07-13

### Added

- New menuitem: [Set the function key] instead of [Get the function
  key value].
- A warning dialog if some feature needs GTK >= 2.12 or VTE >= 0.16.

### Fixed

- Better compatibility for vte (0.25.1) and gtk (< 2.10).
- Added csh, dash, ksh, tcsh, and zsh to foreground_program_whitelist
  and background_program_whitelist.
  Thanks nasofe for reporting this bug.
- Better behavior when there is no '/proc' or the '/proc' is empty.
- Better behavior if the child process has became zombie or exited
  immediately.

## [0.9.9~pre6] - 2010-05-31

### Added

- Applied new Spanish translation.
  VERY THANKS to P.L. Francisco.
- New menuitem: "Change ANSI color theme".
- New menuitem: "Adjust the brightness when inactive".
- New menuitem: "Erase Binding".

### Fixed

- Better compatibility for gtk (2.8 \~ 2.20) and vte (0.16 \~ 0.24).
- The default value for scrollback_lines sets back to 1024.
  scrollback_lines = -1 only works under vte > 0.22.3.
  Very thanks 蒜頭 for reporting this bug.

## [0.9.9~pre5] - 2010-05-01

The default user profile is moved to \~/.config/lilyterm/default.conf.
Please do it manually and sorry for the inconvenience.

### Added

- Applied new Italian translation.
  VERY THANKS to Marco Paolone.
- Reduce the size of [X] button on the tab.
  VERY THANKS to caleb for this idea.
- New profile option: "default_encoding".
  May specify the default encoding when starting up.
- New profile option: "default_locale".
  May specify the default locale when starting up.
- New profile option: "fullscreen"
  May change to fullscreen mode when starting up.
  Thanks BV1AL for this idea.
- New menuitem: "New window with profile".
  May launch a new window of Lilyterm with specified profile listed
  in \~/.config/lilyterm/.
- New menuitem: "Save settings as".
  May save the settings into a new profile.
- Now [transparent_background] and [background_color] may use at the
  same time even though the window is not composited.
- Set the default value of profile option "scrollback_lines" to -1,
  It means the lines of scrollback history is unlimited.
  VERY THANKS to caleb for this idea.
- The default key for move_tab_forward changed into "Ctrl ]".
- The default key for move_tab_backward changed into "Ctrl [".

### Fixed

- Fix a segfault bug when saving settings if the 1st page was closed.
- Closing a tab that is not on focus with clicking the [X] button on
  it may cause wrong tab to be focus.
  VERY THANKS to caleb for reporting this bug.
- LilyTerm will confirm twice if there is a foreground running program
  in a tab when closing it.

## [0.9.9~pre4] - 2010-02-26

### Added

- The default key for "move_tab_forward" recover to "Ctrl Left".
  The default key for "move_tab_backward" recover to "Ctrl Right".
- New profile option: "execute_command_whitelist".
  Don't need to confirm for executing a program if it's in the
  whitelist.
- New profile option: "execute_command_in_new_tab".
  May launch executed command in a new tab of last actived window,
  instead of opening a new window. It is enabled by default.
  Thanks to nasofe for this idea.
- New profile option: "foreground_program_whitelist".
  If a program is running on foreground, Don't need to confirm for
  terminating it if it's in the whitelist.
- New profile option: "background_program_whitelist".
  If a program is running in background, Don't need to confirm for
  terminating it if it's in the whitelist.
- New profile option: "show_close_button_on_tab".
  May Show a close button [X] on current tab.
  It is enabled by default.
  Thanks Andrew for this idea.
- New profile option: "show_close_button_on_all_tabs".
  May Show a close button [X] on all tabs.
  It is disabled by default.
- Show a list of all running program of a LilyTerm window and confirm
  to close it if click the [X] of the window title.

## [0.9.9~pre3] - 2010-02-19

### Added

- Man page updated.
- New profile option: "show_exit_menu".
  May enable/disable menu item [Exit LilyTerm].
  It is disabled by default.
- The default key for move_tab_forward changed into "Ctrl Insert".
- The default key for move_tab_backward changed into "Ctrl Delete".
- A confirm window when trying to close a tab with child process in it,
  and an option to use kill() or not.

### Fixed

- Better behavior when closing multi-page and confirming to close
  running application.
- Trying not to use kill() when there is no child process of this
  page, or the bash history will not write back to \~/.bash_history.
  Thanks to Douglas McFadzean, 老杜, winlin, and robert for reporting
  this bug.
- Some compile warning under Debian Lenny AMD64 system.
- "confirm_to_execute_command" will always set to "1" when starting
  up without -e option.
  VERY THANKS to Douglas McFadzean for reporting this bug.

## [0.9.9~pre2] - 2010-02-03

### Fixed

- [New tab with specified locale] will always set the encoding of new
  tab to "ANSI_X3.4-1968".

## [0.9.9~pre1] - 2010-02-02

### Added

- Code cleanup.
- Added a man page.
- Update the year of copyright to 2008-2010.
  Thanks to caleb for reporting this bug.
- Added a -? command option.
- New profile option: "confirm_to_execute_command".
  May disable/enable the confirm dialog when executing a command.
  It is enabled by default.
  VERY THANKS to letoh and Douglas McFadzean for this idea.
- New profile option: "encodings_list" and "locales_list".
  Profile option "locales_list" is splited into two options:
  "encodings_list" and "locales_list".
  Please review and update your profile to response this change.

### Fixed

- Ignore anything beyond -e or -x options.
  VERY THANKS to Douglas McFadzean for reporting this stupid bug.

## [0.9.8] - 2009-12-15

### Added

- Applied updated German translation.
  VERY THANKS to Mario Blättermann.
- Applied updated Turkish translation.
  VERY THANKS to Samed Beyribey.
- Applied updated Simplified Chinese translation.
  VERY THANKS to caleb.
- A check box to exit lilyterm directly when closing multi pages or
  terminating a running command.
- May disable/enable the confirm dialog when closing multi pages via
  editing the profile. It is disabled by default.
- Using -l/--login option to make the shell be invoked as a login
  shell. VERY THANKS to caleb for reporting this bug.
- Support gnome-default-applications-properties, may configure
  lilyterm as a default x terminal emulator entry in the default
  applications preferences in GNOME control center.
  Please see the following links for more details:
  http://live.gnome.org/ControlCenter/AddingDefaultApplications
  You need to complie lilyterm with gnome-control-center-dev package
  (>2.26.0 I think) to enable this options.
  VERY THANKS to caleb for reporting this bug and providing the
  lilyterm.xml.
- New menu item: [Dim text when inactive].
- New menu item: [Exit LilyTerm].

### Fixed

- Wrong pages may be closed when adjusting the color of page name.
- Wrong pages may be closed after confirm to close multi pages.
- New added tab may not get focus correctly, and it may cause segfault
  if close this tab immediately.
- The window title may not be updated when creating a new window.
- A compile warning when using clang as default compiler.
  VERY THANKS to caleb for reporting this bug.

## [0.9.7] - 2009-09-06

Relicense under GPLv3.

Drop the support of gtk+2 < 2.10

Due to the huge changes of profile, the configure file is renamed to
lilyterm.conf.

Redesign the web site. VERY THANKS to Cameron Sampson.

### Added

- New menu item: [Copy URL], [Copy] and [Paste], But it is disabled
  by default. (FIX: enabled by default in 0.9.7\~pre16)
  You may enable it via editing the profile. (0.9.7\~pre6)
  VERY THANKS to Balázs Hámorszky for the patch and idea.
- Applied initial Turkish translation. (0.9.7)
  VERY THANKS to Samed Beyribey.
- Applied initial Simplified Chinese translation. (0.9.7\~pre1)
  VERY THANKS to caleb.
- Applied updated German translation. (0.9.7)
  VERY THANKS to Mario Blättermann.
- Drag and drop a tab to another LilyTerm window, or drop it to root
  window for creating a new LilyTerm window. (0.9.7\~pre1)
- Add -d/--directory command line options support. (0.9.7\~pre1)
- New menu item: [Show the tabs bar] and [Hide the tabs bar].
  For drag and drop a single tab easier. (0.9.7\~pre1)
- New menu item: [Cursor Blinks]. (0.9.7\~pre1)
  Thanks Hydonsingore for this idea.
- Use `<Alt>``<F11>` to switch fullscreen/unfullscreen state. (0.9.7\~pre2)
  Thanks BV1AL for this idea.
  BTW, fluxbox don't work well with this function, Please update to
  fluxbox 1.1.1 to fix this problem.
- May custom the ansi color theme via editing the profile. (0.9.7\~pre3)
  Please click menu item [Save Settings] first, then edit it manually.
  Possible values are linux, xterm, rxvt, tango, and vte_fixed.
  The default value is "vte_fixed". (0.9.7\~pre5)
  Thanks gnome-terminal and caleb for the color codes.
- May custom the ansi colors used in terminal respectively via editing
  the profile. (0.9.7\~pre3)
  Thanks 予兒 and freealan for this idea.
- New menu item: [Adjust the brightness of ANSI colors]. (0.9.7\~pre4)
- New menu item: [Audible Bell]. (0.9.7\~pre5)
- May enable visible bell via setting `visible_bell = 1` in profile.
  Thanks freealan for this idea. (0.9.7\~pre5)
- May using `<Ctrl>``<Print Screen>` to dump the run time data of current
  vte for debugging if compiled with -DDEBUG. (0.9.7\~pre7)
- Add a format version in profile. (0.9.7\~pre12)
- Add a socket version when sending socket data. (0.9.7\~pre12)
- Support -T/--title option. (0.9.7\~pre12)
  Please visit the following URL for more details:
  http://www.debian.org/doc/debian-policy/ch-customized-programs.html.
- A confirm dialog for -e option. (0.9.7\~pre13)
- Bold the command line in the confirm dialog for -e option.
  (0.9.7\~pre14)
- Bold the key value in the [Get the function key value] dialog.
  (0.9.7\~pre14)
- May custom the 'emulate terminal type' via editing the profile.
  default value is 'xterm'. (0.9.7\~pre15)
- The scrollbar will be hidden if menu item [Scrollback Lines] is
  disabled.
  Thanks BV1AL for this idea. (0.9.7\~pre15)
- The text of vte will be dim when lost focus.
  May use "inactive_brightness =" in profile to disable this feature.
  (0.9.7\~pre15)
- Hide the scrollbar when startup via editing the profile. (0.9.7\~pre15)
- May custom the 'Delete' or 'Backspace' keys values via editing the
  profile. (0.9.7\~pre15)
  Thanks oahong and caleb for this idea.
- Menu item [Copy] will copy to the pclipboard too.
- The tab bar and scroll bar will hidden when fullscreen. (0.9.7\~pre16)
  Thanks BV1AL for this idea.
- Menu item [Copy URL], [Copy] and [Paste] are enabled by default.
  (0.9.7\~pre16)
- The default function key of fullscreen is changed to `<Ctrl>``<F>`.
  (0.9.7\~pre17)
- May use 'true_fullscreen =0/1' to control the tabs bar and scroll bar
  will be hidden or not when fullscreen.
  Thanks caleb for this idea. (0.9.7\~pre17)
- Bold the profile path in the dialogs. (0.9.7\~pre17)
- fullscreen was splited into two function keys: (0.9.7\~pre18)
  `<Ctrl>``<F>` = Full window, hide window decoration and fullcreen.
  `<Alt>``<Return>` = Also Hide tab bar and scroll bar when fullscreen.
- Better behavior when keeping window geometry. (0.9.7\~pre18)
- Support for window title, like xterm. (0.9.7\~pre18)
  Thanks lun-b612 and adrian for this idea. (0.9.7\~pre18)
- Convert the text on tab to UTF-8 if the encoding of this tab is
  non-UTF8. (0.9.7\~pre19)
- Shows a tool tips on tab. (0.9.7\~pre20)
- Reformated profile. (0.9.7\~pre20)
- A warning dialog if the specified profile is invalid. (0.9.7\~pre20)
- Convert the text on the dialog to UTF-8 if the encoding of this tab
  is non-UTF8. (0.9.7\~pre21)
- Add version info on "Usage" dialog, and the text is also updated.
  Thanks BV1AL for this idea. (0.9.7\~pre21)
- Better support if the profile is a symbolic link. (0.9.7\~pre21)
  Thanks letoh and lloydhuang for the technique support.
- No warning dialog if there is no "format version" in profile.
  Thanks caleb for this idea. (0.9.7)
- The new page will be placed next to the current page. (0.9.7)

### Fixed

- Use g_get_current_dir() instead of g_getenv("PWD"). (0.9.7\~pre1)
  VERY THANKS to caleb for reporting this bug.
- The window title will be updated when losing focus. (0.9.7\~pre1)
- Fix the encoding of 2nd LilyTerm if it's launched with
  "zh_TW lilyterm" (not "zh_TW.Big5 lilyterm") when the 1st LilyTerm
  is launched with "zh_TW.UTF-8 lilyterm". (0.9.7\~pre7)
- Better behavior when run with LC_ALL=zh_TW (not zh_TW.Big5).
  (0.9.7\~pre7)
  Thanks to Balázs Hámorszky for reporting and testing this bug.
- Using gdk_spawn_on_screen_with_pipes() instead of vfork()/execvp()
  when clicking on a hyperlink, or it will cause zombie process.
  (0.9.7\~pre8)
  Thanks to icman for reporting this bug.
- Fixed a double free() segfault bug in update_page_name().
  (0.9.7\~pre8)
  VERY THANKS to Balázs Hámorszky for reporting this bug.
- Function keys don't work with ibus >= 1.1.0.20090508.
  ibus will modify the value of key->state causing lilyterm could not
  get correct key value.
  VERY THANKS to nasofe for reporting this bug. (0.9.7\~pre11)
- Better behavior if there were " " in environments or -e option.
  (0.9.7\~pre13)
- Sometimes the tab name won't update to pwd when exiting from a
  running command. (0.9.7\~pre14)
- A segfault bug if LC_* is unset in environments.
  VERY THANKS to caleb for reporting this bug. (0.9.7\~pre14)
- A lot of memory leak fix with valgrind. (0.9.7\~pre14)
- A strange segfault error when using GSList with menuitem.
  Thanks to YoungKing for reporting this bug. (0.9.7\~pre14)
- Better behavior when handling environments. (0.9.7\~pre15)
- A strange "dialog(20): menu_active_window = NULL" dialog after a new
  version of LilyTerm was installed.
  Thanks Aeglos Lin for reporting this bug. (0.9.7\~pre15)
- The VTE_CJK_WIDTH is setted to 'narrow' by default. (0.9.7\~pre15).
- The environ and command for hyperlink should be separated with `<tab>`.
  (0.9.7\~pre15)
- Needless "MODE1" (=Alt) in [Get Function Key Value]. (0.9.7\~pre16)
- Wrong tab may be closed when confirming to close multi tabs.
  (0.9.7\~pre17)
- The tabs bar won't be shown when changing the tab's color if it was
  set to hidden manual. (0.9.7\~pre17)
- The geometry may be incorrect when recover from fullscreen.
  Thanks BV1AL for reporting this bug. (0.9.7\~pre18)
- Can't launch the later LilyTerm window in current pwd. (0.9.7\~pre18)
- Using `<Ctrl>``<Q>` to closing current tab may show incorrect running
  foreground cmdline on dialog. (0.9.7\~pre19)
- The tab name will not be updated if the text is non-UFT-8.
  (0.9.7\~pre19)
- Update the year of copyright to 2008-2009.
  Thanks to caleb for reporting this bug. (0.9.7\~pre19)
- A strange "dialog(3): menu_active_window = NULL" dialog when logout
  from X. (0.9.7\~pre20)
- Can't use relative path to specify the profile.
  Thanks BV1AL for reporting this bug. (0.9.7\~pre20)
- Don't take the parameter after -e option. (0.9.7\~pre20)
- The default key for "Hide window decoration and full screen" changed
  to `<Alt>``<F11>`. Thanks BV1AL for reporting this bug. (0.9.7\~pre21)
- Add "src/profile.c" to po/POTFILES.in.
  Thanks oahong for reporting this bug. (0.9.7)
- Bold text will also be dim when lost focus. (0.9.7)
- The geometry may be incorrect when draging a page. (0.9.7)
- The text of running command for hyperlink on error dialog will be
  separated with space (not tab). (0.9.7)
- Better compatibility with hyperlink. Now it supports "," and ";" in the
  URL. Thanks BV1AL for reporting this bug. (0.9.7)

## [0.9.6] - 2008-07-25

For GJK users:

If your vte is >= 0.16.14 (Debian Lenny), and you want to enable
VTE_CJK_WIDTH, Please make sure that you have applied the patch on
http://bugzilla.gnome.org/show_bug.cgi?id=542795.

### Added

- Applied updated German translation.
  VERY THANKS to Mario Blättermann.
- Switch VTE_CJK_WIDTH = wide/narrow via right click menu.
- Better hyperlink support. You may specify the VTE_CJK_WIDTH and
  environment used for the hyperlink via editing the profile.
- Every LilyTerm window may use separate environment in single process
  mode.
- `<Ctrl>`<`> will disable mouse right click menu too.

### Fixed

- Better behavior when run with LC_ALL=zh_TW (not zh_TW.Big5).
  Thanks Balázs Hámorszky for reporting this bug.
- The label of GtkNotebook won't be on-focus-able when tab number = 2.
  Thanks PCMan for fixing this bug.
- Better compatibility with hyperlink.

## [0.9.5] - 2008-07-08

### Added

- Applied updated German translation.
  VERY THANKS to Mario Blättermann.
- Hyperlink support. You may specify the command used for the hyperlink
  via editing the profile.
  You may use `<Ctrl>`<`> to disable it for temporarily.
  You may disable it by setting `enable_hyperlink = 0` in profile.
- Disable font anti-alias via setting `font_anti_alias = 2` in profile.
- Make the tab to fill the tab bar via setting `fill_tab_bar = 1`
  in profile.
- Use `tabbar_position = 1` to move the tab bar to the bottom of
  window.
- Use `scrollbar_position = 2` to place the scroll bar on the right
  of vte terminal.
  Using `scrollbar_position = 0` to don't use scroll bar.
- You may specify the font/window resize ratio by editing the
  `font_resize_ratio` and `window_resize_ratio` in profile.
- Support audible_bell by default.

### Fixed

- Better compatibility with -e command line option.

## [0.9.4] - 2008-07-01

For GCIN Users:

If it always segfault when close the 1st page of LilyTerm,
Please update GCIN to 1.4.1 to avoid this problem.

LilyTerm supports 'Single Process Mode' from this version.

When LilyTerm is running with 'Single Process Mode', Multi LilyTerm will
use only one process. The newer launched LilyTerm will be just a new window
of 1st launched LilyTerm, so that we may reduce the usage of memory.

Unlike roxterm, gnome-terminal or xfce4-terminal, Every single window of
LilyTerm is independent. They may has it's own settings and profile, and
won't bother each other.

The disadvantage of 'Single Process' mode is, if the LilyTerm crashed, All
the LilyTerm will be dead at the same time.

You may use -s/--separate command line options to run LilyTerm in 'Separate
Process Mode'.

### Added

- Applied updated German translation.
  VERY THANKS to Mario Blättermann.
- A warning dialog when creating child process fail.
- Add [New tab with specified locale] to right click menu.
  You need to set the locales_list with full locale name in profile to
  enable this.
  for instance with locales_list = ja_JP.EUC-JP zh_CN.GB2312 zh_TW.Big5.
- If you used a specified profile with -p option, [Save Settings] will
  save the settings on the specified profile.
- Add -x and -t/--tab command line options support.
  -x is the same as -e/-execute. For compatible with gnome-terminal.
  (BTW, xterm is support -e only.)
  And, you may use -t/--tab {number} to open multi tabs when startup.
- Shows the encoding on page name.
  You may disable it by put `page_shows_encoding = 0` in your profile.

## [0.9.3] - 2008-06-07

The default function key to close a tab is changed to `<Ctrl>``<Q>`.

### Added

- The text on tab will be bold when on focus or action.
  You may use bold_current_page_name = 0 in profile to disable this.
- The text color of tab will be changed if running command with root
  privileges.
- The text color can be changed by right click menu.

### Fixed

- Applied updated German translation.
  VERY THANKS to Mario Blättermann.
- The page name won't be updated if page_shows_current_cmdline = 0 and
  page_shows_current_dir = 1.
- Better compatibility with fluxbox and evilwm. Now the size of
  LilyTerm should be increased/decreased correctly in all window
  managers.

## [0.9.2] - 2008-05-27

A hotfix for 0.9.1

### Fixed

- All the function keys will be disabled after confirm to close a
  running vte terminal.

## [0.9.1] - 2008-05-25

The size of LilyTerm can't decrease in fluxbox and evilwm. They are
insensitive in gtk+2 focus event, which is used for keep correct geometry
in LilyTerm.

There is another project named LilyTerm already, so that LilyTerm will
renamed to LunaTerm, and relicense to GPLv3 in next release.

### Added

- Applied updated German translation. VERY THANKS to Mario Blättermann.
- Support colorful tab. You may specify the color with:

page_cmdline_color = darkblue
page_dir_color = darkgreen
page_custom_color = darkred
page_normal_color = black

in your profile, or use `use_color_page = 0` to disable it.

- Shows the PWD (current directory) on page name.
  Enable it will NOT cause performance problem. Lilyterm with 100 tabs
  causes around 2.3% CPU loading on a Celeron 850 PC.
  You may use `page_shows_current_dir = 0` in profile to disable it.
- Shows PWD or Cmdline on page name will stop updating when LilyTerm
  is not on focus.
- You may use `page_width` in your profile to specify the max size of
  page, and use `fixed_page_width` to fix the size of page width.
- Always move window to top-left when initing.
- You may use `show_get_function_key_menu` to disable "Get function
  key value" menu item.
- Add comments in profile.

### Fixed

- page_shows_current_cmdline only updates for current page.

## [0.9.0] - 2008-05-09

### Added

- Applied German translation. VERY THANKS to Mario Blättermann.
- New menu item: [Scrollback lines] and [Clean scrollback lines of
  this tab].
  Thanks lpinkliow for this idea.
- New menu item: [Change the foreground color] and [Change the
  background color]. You man move the cursor in the color selection
  dialog and see the result in the vtebox at the same time.
  You man use `show_color_selection_menu = 0` in your profile to disable
  color selection menu items.
- New menu item: [Save settings].

### Removed

- menu item [Profile sample] is obsoleted.

### Fixed

- `<backspace>` don't work under emacs.
  Thanks yoyo for reporting this bug.
- page_shows_current_cmdline only enabled under GNU/Linux for temporary.

## [0.8.9] - 2008-05-01

### Added

- Window opacity is supported, and it will enabled by default if the
  screen is composited.
  You may use `window_opacity = 0.15` in your profile to specify the
  opacity.
  You may use `transparent_window = 0` in your profile to disable it.
- May use `show_transparent_menu = 0` to disable transparent/opacity
  menu items.
- New menu item: [Transparent Window] and [Window Opacity].

### Fixed

- rgba won't be enabled by default if the screen is composited.
- Better compatibility when the font rendering / theme changed too fast.

## [0.8.8] - 2008-04-27

### Added

- [Increase window size] and [Decrease window size] will resize fonts
  with */ 1.12.
- Using `<Ctrl>``<+>` and `<Ctrl>``<->` will resize fonts with +- 1.

### Fixed

- Better compatibility with evilwm.
  Thanks to caleb for reporting this bug.
- rgba don't work with xcompmgr even though `use_rgba = 1`.
  Thanks to caleb for reporting this bug.
- Better behavior when using transparent background.
- Better behavior when resizing font/window size.

## [0.8.7] - 2008-04-25

### Added

- If you're using a composited window manager, rgba will be enabled by
  default.
  You may use use_rgba = 0 to turn off True Opacity if you're using
  a composited window manager but you don't want to enable it in
  LilyTerm.
  You may use use_rgba = 1 to turn on True Opacity if you're using
  xcompmgr, which can't be resolved by gdk_screen_is_composited().
- you may use show_resize_menu = 0 to disable [Increase window size],
  [Decrease window size], [Reset to default font/size] and [Reset to
  system font/size] in menu.
- Now [Change Font] in right click menu will change the font of all
  tabs.
- Now [Transparent Background] and [Background Saturation] will apply
  to all tabs.
- New menu item: [Reset to default font/size] will reset every tab to
  user default font and size.
- New menu item: [Reset to system font/size] will reset every tab to
  system default font (Monospace 12) and size (80x24).

### Removed

- menu item [Change Font for current tab] is obsoleted.

### Fixed

- Now LilyTerm works fine with kwin. (It is the window manager of KDE).
  Thanks to swyear and caleb for reporting this bug.
- Better compatibility with some xorg build in video modules when rgba
  is enabled. If you still encounter trouble, like black screen,
  transparent or colorless text, please insure the following setting:
  	option "XAANoOffscreenPixmaps"
  is in your /etc/X11/xorg.conf.
- Better compatibility when the font rendering / theme changed.
- Use `<Ctrl>`<`> (NOT `<Ctrl>``<\~>`) to disable function keys.
  Thanks to BV1AL for reporting this bug.
- `screen_width` is rename to `column` in profile.
- `screen_height` is rename to `row` in profile.

## [0.8.6] - 2008-04-19

### Added

- support gtk-alternative-button-order.
- Use `<Ctrl>`<`> to disable/enable function keys.
- Support true opacity (rgba) now.
  (auto detect the screen is composited or not)

### Fixed

- The size of vtebox won't be changed if the font rendering / theme
  changed. (hope so.)

## [0.8.5] - 2008-04-12

### Added

- Reuse the page names.
- Shows the foreground program command on tab.
- Shows the foreground program command on window title.
- Confirm to close a tab if there was a running application.
- Use kill() to kill running shell when use `<Ctrl>``<W>` or `<Alt>``<F4>`
  to force close this tab. or the sub-process of this tab will be
  closed at the same time.
- Add [Change the font of every tab] to menu.

### Fixed

- The serial no of the tab will shown when reorder the tab if
  page_names_no=0.
- Use `<Ctrl>``<O>` to select all the text in vte box.
  `<Ctrl>``<S>` is used in rtorrent to start a torrent download.
- The size of vtebox won't be changed if the themes changed.

## [0.8.4] - 2008-04-07

### Added

- `<Ctrl>``<F1\~F12>` switch to 1st \~ 12th tab.
- Add [Get the function key value] to menu.
- Mutli tab names. but it had been disabled by default.
  You need add page_names = blah blah blah (separate with space)
  in profile to enable it.
- Add a serial no in tab name.
- Use `<Ctrl>``<S>` to select all the text in vte box.
  `<Ctrl>``<A>` is the function key of screen.
- Use `<Ctrl>``<X>` to copy the selected text to clipboard.
  `<Ctrl>``<C>` is used to terminate a program in shell.
- Use `<Ctrl>``<V>` to paste the text in clipboard.
- Use `<Ctrl>``<+>` to Increase font size
- Use `<Ctrl>``<->` to Decrease font size
- Use `<Ctrl>``<Enter>` to Reset font_size
- Add [Increase window size], [Decrease window size],
  [Reset font and window size] to right click menu.
- Add [Profile sample] to menu.
- Add [Usage] to menu.
- Add [Change font for this tab] to menu.
- Menu have icons now.

### Fixed

- `screen_with` is rename to `screen_width` in profile.

## [0.8.3] - 2008-04-01

### Added

- Input method menu. but it had been disabled by default.
  You need add show_input_method_menu = 1 in your profile to enable it.
- System config supported. /etc/xdg/lilyterm.rc by default.
- Confirm before close multi tabs.

### Fixed

- Fixed a segmentation fault error when closing LilyTerm by click [X]
  on the window title bar directly.

Using autotools to generate configure/Makefile now.

## [0.8.2] - 2008-03-20

### Added

- Change the background saturation by right click.
- Now supports -u/--user_profile to use a specified profile.
- Reset current tab by right click.

### Fixed

- A patch from caleb to trim lilyterm. THANKS to caleb!

## [0.8.1] - 2008-03-19

No more input method patches. Please patch libvte instead!
Now LilyTerm works fine with SCIM.

### Added

- Using transparent background or not by right click.
- May use `pre_tab_key = NULL` to disable a function key.

## [0.8.0] - 2008-03-16

### Added

- Add L10n/i18n support. Call for translators!
- Add command line options support.
  Now -h/--help -v/--version -p/-profile and -e/-execute supported.

### Fixed

- The window size of Lilyterm now can resize correctly when
  adding/removing tabs without dirty patches.
- The window size of Lilyterm will change if opened too many tabs.

## [0.7.9] - 2008-03-14

### Added

- Use `<Ctrl>``<Left>` to move current page forward.
- Use `<Ctrl>``<Right>` to move current page backward.
- Use `<Ctrl>``<Up>` to move current page to first.
- Use `<Ctrl>``<Down>` to move current page to last.

### Fixed

- `<a>`-`<z>` was transfered into `<A>`-`<Z>` when solving function key.
  Thanks Silice for reporting this bug.

## [0.7.8] - 2008-03-12

### Fixed

- The size of resized window will be recovered to original size if
  opening a new tab later.
- Improve (again) the performance when solving function key.

Add a refile example `lilyterm.rc` at /usr/share/doc/lilyterm/examples/.

## [0.7.7] - 2008-03-11

`pre_tab_key` is rename to `prev_tab_key` in rcfile.

### Added

- show the text column x row information when move/drag the window with
  mouse. (need Window Manager supports it too.)
- The window of LilyTerm now can resize correctly.
- The window increment when resizing is based on the size of single
  character.
- May use `pre_tab_key = ` to disable a function key.
  But it won't increase the performance.

### Fixed

- FIX: `<backspace>` don't work in screen.

## [0.7.6] - 2008-03-08

### Added

- Now we can use `<Ctrl>``<E>` to change the name of current tab.

## [0.7.5] - 2008-03-07

### Fixed

- Function keys won't work if there is no default rc file. a stupid bug.

## [0.7.4] - 2008-03-07

### Added

- LilyTerm has a window icon now.

### Fixed

- Will go to next tab when close current tab. a stupid bug...
- Improve (again) the performance when solving function key.

## [0.7.3] - 2008-03-05

### Added

- Improve the performance when solving function key.
- The directory of new tab will use current tab's PWD.

## [0.7.2] - 2008-03-04

### Added

- Now we can configure the function key via config file.
- Default Default Keys:
    New Tab: Ctrl T
    Close Tab: Ctrl W
    Pre Tab: Ctrl Page_Up
    Next Tab: Ctrl Page_Down
    First Tab: Ctrl Home
    Last Tab: Ctrl End

COPYING:
Change from 3-clause BSD to 2-clause BSD.

Add Copyright in every file.
Add lilyterm.desktop.
Add a lots of LilyTerm icons.

## [0.7.1] - 2008-03-02

### Added

- Use `<Ctrl>``<Home>` to switch to the first page.
- Use `<Ctrl>``<End>` to switch to the first page.
- `<Shift>``<Insert>` to pase clipboard. It was done in libvte.

Makefile:
Remove useless '--libs' when compile *.o.
Thanks to letoh again!

INSTALL:
Update some typo.
`make` will compile *with* the dirty im-patch.
`make no-im-patch` will complile without the dirty im-patch.

AUTHORS:
Update homepage, blog, phorum, IRC information.

### Fixed

- A segmentation fault error fixed.
  VERY THANKS to letoh.bbs@bbs.sayya.org!
- Default Function Keys:
  Use `<Ctrl>``<Insert>` to add a new page.
  Use `<Ctrl>``<Delete>` to close current page.
  This bug is reported by letoh:
  `<Ctrl>``<W>` was used by vim to delete a word.
  `<Ctrl>``<T>` was used by vim to pop tag stack.

## [0.7.0] - 2008-03-02

### Changed

- Initial release.
- Started recording ChangeLog.

## [0.1.0] - 2008-02-25

### Changed

- Initial revision.

[unreleased]: https://github.com/ebal/LilyTerm/compare/v0.12.0...HEAD
[0.12.0]: https://github.com/ebal/LilyTerm/compare/v0.11.2...v0.12.0
[0.11.2]: https://github.com/ebal/LilyTerm/compare/v0.11.1...v0.11.2
[0.11.1]: https://github.com/ebal/LilyTerm/compare/v0.11.0...v0.11.1
[0.11.0]: https://github.com/ebal/LilyTerm/compare/v0.10.1...v0.11.0
[0.10.1]: https://github.com/ebal/LilyTerm/compare/v0.10.0...v0.10.1
[0.10.0]: https://github.com/ebal/LilyTerm/compare/v0.9.10...v0.10.0
[0.9.10]: https://github.com/ebal/LilyTerm/compare/v0.9.9.9...v0.9.10
[0.9.9.9]: https://github.com/ebal/LilyTerm/compare/v0.9.9.8...v0.9.9.9
[0.9.9.8]: https://github.com/ebal/LilyTerm/compare/v0.9.9.7...v0.9.9.8
[0.9.9.7]: https://github.com/ebal/LilyTerm/compare/v0.9.9.6...v0.9.9.7
[0.9.9.6]: https://github.com/ebal/LilyTerm/compare/v0.9.9.5...v0.9.9.6
[0.9.9.5]: https://github.com/ebal/LilyTerm/releases/tag/v0.9.9.5
[0.9.9.4]: https://github.com/Tetralet/LilyTerm/compare/v0.9.9.3...v0.9.9.4
[0.9.9.3]: https://github.com/Tetralet/LilyTerm/compare/0.9.9.2...v0.9.9.3
[0.9.9.2]: https://github.com/Tetralet/LilyTerm/compare/0.9.9.1...0.9.9.2
[0.9.9.1]: https://github.com/Tetralet/LilyTerm/compare/0.9.9...0.9.9.1
[0.9.9]: https://github.com/Tetralet/LilyTerm/releases/tag/0.9.9

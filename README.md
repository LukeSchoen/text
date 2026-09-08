# text

A small, fast Windows text editor with a C-owned document model and native Win32
rendering. The checked-in `text.exe` is ready to run; pass a file path on the command
line to open it directly.

## Highlights

- Memory-mapped, read-only backing for opened files.
- Piece-list editing backed by stable append-only blocks, so inserting into a large
  file does not move the untouched file contents.
- Lazy line discovery and viewport-only rendering.
- Logical-offset selections, grouped undo/redo, box selection, and line movement.
- Optimistic save conflict detection: other processes may read and write open files,
  while stale editor windows must explicitly reload or overwrite.
- One document per window with fast open/save/close cycles.

## Install and uninstall

Run `install.cmd` beside `text.exe` to register file associations, script edit
actions, and a **text** shortcut in your Windows account's Start menu. Then open
Start or taskbar search and type **text**. Windows may take a moment to discover
the shortcut; the installer cannot guarantee the first search result.

Keep the folder in place, or rerun `install.cmd` after moving it to update the
shortcut and associations. `uninstall.cmd` removes the registrations and shortcuts
(if they still point to this installation). Neither script requires administrator
rights. Reinstalling also removes the old **Notepad (Text)** alias if it points to
this installation.

## Keyboard shortcuts

- `Ctrl+O`, `Ctrl+S`, `Ctrl+Shift+S`: open, save, and save as.
- `Ctrl+F`: find text.
- `Ctrl+Z`, `Ctrl+Y`/`Ctrl+Shift+Z`: undo and redo.
- `Ctrl+C`, `Ctrl+X`, `Ctrl+V`: clipboard operations. With no selection, copy and cut
  operate on the current line.
- `Ctrl+Left`/`Ctrl+Right`, `Ctrl+Backspace`, `Ctrl+Delete`: word operations.
- `Alt+Shift+Arrow`: rectangular selection.
- `Alt+Up`/`Alt+Down`: move selected lines.
- `Ctrl+D`: open commands from `doCommands.txt`.
- `Ctrl+Alt+D`: run the current document through Codex.
- `Ctrl+Shift+D`: close the document and then run it through Codex.
- `Ctrl++`, `Ctrl+-`, `Ctrl+0`, `Ctrl+MouseWheel`: zoom.
- `Esc`: close a popup or exit; `Shift+Esc` exits without a save prompt.

## Build

The canonical build is Windows-only and uses the checked-in `cpc.exe` plus an
installed matching CPrime runtime containing `include` and `lib` directories.

```bat
build.cmd
```

This rebuilds the checked-in `text.exe` and embeds `document.png` as its application
icon. Diagnostic repro builds are isolated under `.build\repro`:

```bat
build.cmd repro
```

Keep `doCommands.txt` beside any distributed executable if the `Ctrl+D` command
popup should be available. See [repro.md](repro.md) for diagnostic logging.

GitHub Actions also cross-checks the sources with MSVC; `cpc.exe` remains the
canonical local compiler and stays tracked with `text.exe`.

## Tests

Exactly five test entry points are maintained. They share `test_runner.ps1` and
reuse current builds under `.build`; only changed sources/dependencies rebuild.
The tiers are disjoint: run both normal and pedantic for full coverage in that
category. Core is independent and is not rerun by the other tiers.

| Command | Coverage | Desktop input |
| --- | --- | --- |
| `runtests.cmd` | Lean document/UTF-8/viewport checks | None |
| `RunTests_Pedantic.cmd` | Combined 300 MiB mapped deletion, insertion, boundary and snapshot regression | None |
| `RunTests_UserSim.cmd` | Three smoke scenarios: word editing, cut/paste undo/redo, mouse selection/copy | Keyboard/mouse |
| `RunTests_UserSimPedantic.cmd` | Remaining GUI regressions, including visual, Unicode and paste cases | Keyboard/mouse |
| `RunTests_Core.cmd` | Standalone rope API: ranges, ownership, randomized edits, failures, sharing and balance | None |

Every test reports its elapsed time on success or failure. User simulation also
reports time spent in input, condition waits and application startup; startup can
include condition waits. Build and large-fixture setup are timed separately.

```bat
RunTests_UserSim.cmd --list
RunTests_UserSimPedantic.cmd --list
RunTests_UserSimPedantic.cmd --build-only
RunTests_UserSim.cmd tab_mid_word_roundtrip_and_caret
RunTests_Core.cmd "C:\fixtures\large.txt"
```

`--list` enumerates selected GUI tests without opening the app or sending input.
`--build-only` is supported by all five commands. Named GUI tests work in either
GUI tier and override that tier's default selection; unknown names fail.
The optional core fixture path exercises mapped-file ownership and large shared
cuts. No suite implicitly depends on the ignored local `test.txt`.
Screenshots from visual tests go to `.build/look`.

GUI tests use real keyboard and mouse input, so run the UserSim commands only
when the desktop is available. The other three commands never launch an editor
window. The former Fast, Look and Utf8 commands have been absorbed into these
five entry points; there are no compatibility runners to maintain.

Combined cases retain both sets of assertions while sharing setup: mapped
insert/delete, scrollbar bottom/release, mid-word Tab/Shift+Tab and caret,
selected-line unindent/no-op, multiline Delete/Backspace/Undo, selection at both
document edges, and line copying with/without a newline. Unicode
navigation, deletion, paste and persistence share the scalar scenarios instead
of running a second corruption-repro executable. Real input is batched per
shortcut and per text chunk, and tall fixtures use buffered writes.

## Repository layout

- `main.c`: document model, editor behavior, native window, and renderer.
- `repro_logger.c` / `repro_logger.h`: optional diagnostic logging.
- `text_blackbox_tests.c`: public-input GUI behavior tests.
- `text_tests.c`: document/renderer API checks and mapped-file regressions.
- `core/core_tests.c`: standalone rope API tests.
- `test_runner.ps1`: shared build and tier dispatch for the five test commands.
- `doCommands.txt`: runtime command-popup entries.
- `document.png`: source application icon.

Licensed under the [Apache License 2.0](LICENSE).

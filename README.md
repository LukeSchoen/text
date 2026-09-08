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
actions, and a **Text** shortcut in your Windows account's Start menu. Then open
Start or taskbar search and type **Text**. Windows may take a moment to discover
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

Test executables and screenshots are generated under the ignored `.build` directory.
The suites generate their own text fixtures; the ignored local `test.txt` is not a
test dependency.

```bat
runtests.cmd
RunTests_Pedantic.cmd
runTests_Fast.cmd
runTests_Look.cmd
runTests_Look.cmd pedantic
```

- `runtests.cmd` runs the lean default GUI checks.
- `RunTests_Pedantic.cmd` runs established GUI regression coverage.
- `runTests_Fast.cmd` exercises piece-list edits against a generated 300 MiB sparse
  file.
- `runTests_Look.cmd pedantic` captures the larger visual regression set.

GUI tests use real keyboard and mouse input, so avoid interacting with the desktop
while they run.

## Repository layout

- `main.c`: document model, editor behavior, native window, and renderer.
- `repro_logger.c` / `repro_logger.h`: optional diagnostic logging.
- `text_blackbox_tests.c`: public-input GUI behavior tests.
- `text_fast_tests.c`: document-core large-file checks.
- `doCommands.txt`: runtime command-popup entries.
- `document.png`: source application icon.

Licensed under the [Apache License 2.0](LICENSE).

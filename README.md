# text - [Download Windows Demo Zip File](https://github.com/LukeSchoen/text/archive/refs/heads/main.zip)

Clean Simple Windows Text Editor

IDE-style typing-tech. without any dependencies. Written in C.
includes high performance text engine.

- Optimised for practical editing of text files.
- quick open/save/close cycles
- One source file (`main.c`)
- Not an IDE/markdown/heavy-code-editor.

## Running
1. Run `text.exe` (Optional: pass a file path as an argument to open it directly.)

## Building
Is Windows-only (Due to win32 API).

## Usage (aka IDE-style text-fetures)

### 1) line copy/cut behavior
If nothing is selected:
- `Ctrl+C` copies the current line.
- `Ctrl+X` cuts the current line.
- `Ctrl+V` pasting the new here.

### 2) Word-level movement and deletion
- `Ctrl+Left` / `Ctrl+Right`: move by words.
- `Ctrl+Backspace`: delete previous word chunk.
- `Ctrl+Delete`: delete next word chunk.

### 3) 2D Box (and column) select
- `Alt+Shift+Arrow Keys` expands a rectangular selection.
- Typing/paste/delete applies across the selected column block.

### 4) Move lines up/down
- `Alt+Up` / `Alt+Down` swaps the current line with the one above/below.

### 5) Find Text
- `Ctrl+F` opens find (or uses selected text as the query).
- `Return` finds next match.

### 6) - Undo/redo tracks grouped edits.

### 7) Zoom / readability controls
- `Ctrl++` increase font size
- `Ctrl+-` decrease font size
- `Ctrl+0` reset font size
- `Ctrl+MouseWheel` also adjusts font size

### 8) Large file handling
- Line indexing is discovered lazily and doesn't hold up file loading, file measurement is completed afterwards in timed slices for 'instant loading' of large files.
- Text engine implements basic rope string mechanics, allowing near unlimited editing anywhere without UI stalls even in very large files.
(internally a unique string backing store is used to implement reference counting and careful pointer offfsets and reindexing avoid work)

## Keyboard Shortcuts
- `Ctrl+O`: Open file
- `Ctrl+S`: Save file
- `Ctrl+F`: Find
- `Ctrl+Z`: Undo
- `Ctrl+Y` or `Ctrl+Shift+Z`: Redo
- `Ctrl+A`: Select all
- `Ctrl+C`: Copy selection (or current line if no selection)
- `Ctrl+X`: Cut selection (or current line if no selection)
- `Ctrl+V`: Paste
- `Alt+Up` / `Alt+Down`: Move current line
- `Shift+Esc`: Immediate exit (no save)
- `Alt+Shift+Arrows`: Box selection
- `Esc`: Close find or exit editor
- `Ctrl+Shift+D` ask codex to run this text file (allowing you to treat text.exe kind of like an IDE for tasks in english).

## Known Limits
- Single-document window.
- Windows-only implementation.
- syntax highlighting stripped out and plugins disabled.
- written over ~2 days in ~100 commitsl; temper expectations. 

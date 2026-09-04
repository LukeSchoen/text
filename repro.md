# Repro Logging

Run `build.cmd repro` to create `.build\repro\textRepro.exe`. It writes
`.build\repro\log.txt` on each launch (overwrite mode).

## Flags
Edit `repro_flags.txt` (same folder as `textRepro.exe`) with one token per line:
- `wheel`
- `vscroll`
- `size`
- `scrollbar`
- `input`
- `edit`
- `semantic`
- `all`

The default `.build\repro\repro_flags.txt` created by `build.cmd repro` enables
`semantic` only, so normal bug repro logs stay small.

## Post-mortem repros
For interactive repros, start `.build\repro\textRepro.exe`, perform the bug,
then close the window.

`semantic` adds low-noise milestone records:
- `file_load` when a file is loaded by command line, the open dialog, or file drop
- `drag_select` once at mouse-up when a stream or box selection exists
- `close_screenshot` when close is requested

On close, the repro build writes `.build\repro\close_screenshot.bmp` before save
prompts or window teardown. The file is overwritten on each close attempt.

Enable `input` and `edit` only when a repro needs detailed key, mouse, or text mutation traces.

## Scroll bug run suggestion
For scroll regressions, enable:
- `wheel`
- `vscroll`
- `scrollbar`
- `size`

`wheel` lines now include:
- `stuck=1` when wheel input changed no top-line (`tl_before == tl_after`)
- `streak=<n>` consecutive stuck wheel events
- `totalStuck=<n>` cumulative stuck wheel events for the run

The log footer includes:
- `summary wheelStuckTotal=<n> wheelStuckMaxStreak=<n>`

## Input/edit logging
`input` logs keyboard and left-mouse events only when they changed editor state.

`edit` adds compact edit summaries to those same records:
- `ins@<off>+<len>="<preview>"`
- `del@<off>+<len>="<preview>"`

Event records include before/after values for:
- caret offset/line/col
- selection anchor/active
- top line / first column
- document length
- stream/box selection state

Previews are escaped and truncated, so the log stays small during typing and selection repros.

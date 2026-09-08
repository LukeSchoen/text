# Paste corruption reproduction

## Fix and confirmation

The confirmed UTF-8 corruption is now fixed in `main.c`. Rendering decodes complete
Unicode scalars across piece boundaries. Movement, deletion, mouse/column mapping
and box editing use scalar boundaries while the document continues to use byte
offsets and piece storage. UTF-16 keyboard surrogate pairs are combined before
conversion to UTF-8.

The correctness regressions now live in `text_blackbox_tests.c` as
`paste_boundaries`, `unicode_scalars` and `unicode_columns`. Run
`RunTests_UserSimPedantic.cmd` for GUI coverage, or `runtests.cmd` for the
renderer/decoder checks in `text_tests.c`. The standalone repro executable and
Utf8 runner have been removed; the investigation commands below are historical.

Confirmed with real keyboard/mouse input:

- Original Backspace and second-paste reproductions, including exact saved bytes.
- Two-, three- and four-byte characters (`é`, `€`, emoji), loaded, pasted and typed.
- Left/Right, Backspace/Delete, selection/copy/replacement, undo/redo and save.
- Mouse positioning, vertical movement and box edits after multibyte characters.
- ASCII paste/undo/redo around the 64 KiB append-block boundary.
- Unit checks for decoding across pieces, viewport clipping and malformed UTF-8.

The 300 MiB backend tests pass. The broader suite passed 35 of 38 existing GUI
tests. These three also failed when run against the unchanged HEAD implementation:
`paste_selectissue_text_then_backspace_lines`,
`type_selectissue_text_then_backspace_lines`, and
`double_click_word_selection_copy`. They remain outside this fix.

The editor still uses one grid cell per Unicode scalar; this change does not add
grapheme-cluster navigation or complex-script layout. It prevents splitting valid
UTF-8 encodings and does not repair files already corrupted by the old behavior.

Fixed screenshots/files use `.build/paste-fixed*`; the historical artifacts below
retain their original names. Current test output: `.build/utf8-fixed-results.txt`.

## Original investigation (before the fix)

Reproduced on 2026-09-09 with the then-unchanged `main.c`, built using the repository's `cpc.exe`.
This is a confirmed cause matching the reported symptoms; it does not establish that the original incident had the same trigger.

## Minimal manual reproduction

1. Paste `café` into a document, optionally after existing ASCII text.
2. Observe `cafÃ©` on screen, although copying the whole text still returns valid `café`.
3. Press Backspace once at the end. The editor deletes only the final byte of `é`, leaving invalid UTF-8.
4. Save: the original `C3 A9` encoding of `é` has become a lone `C3`.

An alternative directly involving a second paste:

1. Open a UTF-8 file containing `LEFT café`.
2. Press Ctrl+End, then Left once.
3. Paste the ASCII letter `X`.
4. Save. The ending is now `63 61 66 C3 58 A9`, splitting the two bytes of `é` around `X`.
   Decoding the document returns `LEFT caf\uFFFDX\uFFFD`.

## Explanation

- `paste_clipboard` converts Windows Unicode clipboard text to UTF-8 correctly (`main.c:5818`).
- `visible_span` casts each individual UTF-8 byte to `WCHAR` instead of decoding a character (`main.c:2297`). This causes the initial display problem without changing stored bytes.
- `move_caret_left` decrements the byte offset by one (`main.c:5888`), allowing the caret inside a multibyte character. A subsequent paste inserts at that offset (`main.c:5838`), producing invalid UTF-8.
- Ordinary Backspace also deletes one byte (`main.c:4275`), so it can leave an incomplete character. This is actual persisted corruption, not just rendering.

The reproduced failures do not require append-block relocation or stale mapped-file data.
Byte offsets can remain the document model's primary positions, but the current UI treats individual bytes as complete characters.

## Verification and artifacts

At investigation time, `paste_corruption_repro.c` was a standalone investigation harness using existing black-box helpers.
All editor actions use real keyboard input. Clipboard setup supplies paste input; document text and saved files are read for observation.
The original harness returned success when the documented corrupt bytes were reproduced.
No implementation files or normal/pedantic test scripts were changed during that initial investigation.

Verified:

- ASCII insertion in the middle of mapped and typed text at 1, 65,535, 65,536, 65,537 and 131,072 bytes, including undo and redo: passed.
- Unicode paste after mapped ASCII and typed ASCII, then Backspace: invalid saved bytes reproduced in both cases.
- Loaded Unicode followed by Backspace: same invalid saved bytes.
- ASCII paste inside a loaded multibyte character: invalid saved bytes reproduced.
- Unicode cases reproduced across repeated runs. One intermediate run missed the first ASCII paste; a full rerun passed all ASCII controls and reproduced all Unicode failures.

These bounded ASCII checks do not rule out another ASCII-only corruption bug.

Build and run from the repository root (create `.build` first if absent):

```powershell
.\cpc.exe -o .build\text_under_test.exe main.c -luser32 -lgdi32 -lcomdlg32 -lshell32 -luxtheme -ldwmapi -lmsimg32
.\cpc.exe -o .build\paste_corruption_repro.exe paste_corruption_repro.c -luser32 -lgdi32
.\.build\paste_corruption_repro.exe .build\text_under_test.exe
```

Local evidence from the run:

- `.build/paste-before.bmp`: display immediately after valid Unicode paste.
- `.build/paste-after.bmp`: display after Backspace.
- `.build/paste-corrupted.txt`: saved document ending in lone `C3`.
- `.build/paste-split.bmp`: display after ASCII paste inside the character.
- `.build/paste-split.txt`: saved document containing `C3 58 A9`.
- `.build/paste-corruption-results.txt`: final run output.

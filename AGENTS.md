# Agent Rules

- If the request is test-only (for example, "bring over tests"), do not replace, rewrite, or swap core implementation files (`main.c`, document model, editor architecture) from another project. Only add/adapt test assets unless the user explicitly asks for core code changes.
- Assume weirdness is real, blame code not the test/harness especially for simple off by one erorrs.
- Do not add or use hidden test-only hooks that directly mutate editor state for behavioral coverage.
- GUI behavior tests must drive the app through public behavior only: real keyboard input, real mouse input.
- Do not use direct editor mutation messages such as `WM_SETTEXT`, `EM_SETSEL`, `EM_REPLACESEL`, Scintilla setter messages, or helper wrappers around them to set up or perform GUI behavior coverage. Use keyboard/mouse input instead.
- Direct window messages are allowed only for non-behavioral observation/cleanup, such as reading text/selection for assertions, marking the document unmodified before closing, querying geometry, or closing the app.
- If a regression cannot be covered without bypassing behavior, stop and call that out instead of adding a shortcut.
- Keep default development test runs lean. Put already working regression coverage into `RunTests_Pedantic.cmd`, and keep the normal test script focused verification or targeted explicitly requested tests.

*note: intermittently flaky tests are to be expected since the user if often working while these tests run, dont get caught up on it, so long as you get it running after a few tries assume its reliable and fine*

# Fast Text Core Design

The editor must not use a Win32 text control, RichEdit, or Scintilla as the document model. The document model is C-owned data. The window only displays the current viewport and receives public keyboard/mouse input.

The core problem to avoid is contiguous-buffer editing. A 300 MB file must not become slow because inserting one byte in the middle forces a 150 MB `memmove`, a full reallocation, or a full line-index rebuild. Edits must be represented as small structural changes.

## Document Storage

Use a simple piece-list model.

The document is a linked list of lightweight pieces. Each piece references bytes that live in one backing store:

- `ORIGINAL`: bytes from the loaded file. For large files this should be memory mapped and read-only.
- `ADD`: bytes appended to editor-owned append-only memory blocks.

A piece contains:

- backing kind: original or add
- pointer or backing id
- start offset inside that backing
- byte length
- cached line-break count if available
- previous and next piece links

The initial document for an opened file is one piece:

```text
[ ORIGINAL: start=0, len=file_size ]
```

The initial document for a new unsaved file is either an empty list or one empty add piece.

The add backing must not reallocate existing bytes in a way that invalidates piece pointers. Use one of these simple approaches:

- A linked list of fixed or growing append blocks.
- A vector of add blocks where each block is individually allocated and never moved.
- Large committed pages from `VirtualAlloc` where text is appended until the block is full, then a new block is allocated.

Do not store add text in one `realloc` buffer if pieces point into it. Reallocation would invalidate existing pointers.

## Core Invariant

The logical document is the concatenation of all pieces.

Pieces are cheap metadata. Moving, inserting, and deleting text should usually modify only a few pieces, not the bytes of the original file.

Adjacent pieces may be coalesced only when they reference the same backing and are physically adjacent:

```text
[ ADD block A off=10 len=5 ][ ADD block A off=15 len=3 ]
```

can become:

```text
[ ADD block A off=10 len=8 ]
```

Never coalesce pieces that only happen to contain adjacent logical text but live in unrelated backing ranges.

## Finding an Offset

Most operations begin by resolving a logical byte offset to:

- piece pointer
- byte offset inside that piece
- cumulative document offset before that piece

Version one can find this with a linear walk from a nearby cursor/cache. This is acceptable if the implementation keeps a "last resolved position" cache and most editing happens near the caret.

Later, if needed, replace or supplement the linked list with a tree where each node stores subtree byte length and line count. The public document operations should not depend on linear list internals, so this upgrade stays contained.

## Insert

To insert bytes at logical offset `P`:

1. Append inserted bytes to the ADD backing store. This returns a stable backing reference plus offset/length.
2. Resolve `P` to a piece and local offset.
3. If `P` is at a piece boundary, insert one new ADD piece at that boundary.
4. If `P` is in the middle of a piece, split the piece into left and right references to the same backing, then insert the ADD piece between them.
5. Coalesce with neighboring ADD pieces only when backing ranges are adjacent.
6. Mark line/cache data dirty only for the affected area.

Example: insert `"X"` into a huge original block:

```text
before:
[ ORIGINAL 0..300MB ]

after:
[ ORIGINAL 0..150MB ][ ADD "X" ][ ORIGINAL 150MB..300MB ]
```

No original bytes move. No giant allocation happens. The edit is pointer work plus one tiny append.

Typing repeatedly at the same caret should remain instant. The common case should append to the current ADD block and extend the neighboring ADD piece when possible:

```text
[ ORIGINAL left ][ ADD "hello" ][ ORIGINAL right ]
```

Typing `!` at the end of that add piece should become:

```text
[ ORIGINAL left ][ ADD "hello!" ][ ORIGINAL right ]
```

not a new piece for every character.

## Delete

To delete byte range `[P, P + N)`:

1. Resolve the start and end offsets.
2. If the deletion begins or ends inside a piece, split those boundary pieces.
3. Unlink all pieces fully covered by the deletion.
4. Keep the uncovered left and right boundary pieces.
5. Coalesce adjacent pieces where legal.
6. Keep removed ADD backing bytes allocated until the document is closed or a compaction pass runs. Do not free small ranges on every delete.

Example:

```text
before:
[ ORIGINAL A ][ ADD B ][ ORIGINAL C ][ ADD D ]

delete covers ADD B and part of ORIGINAL C

after:
[ ORIGINAL A ][ ORIGINAL remaining tail of C ][ ADD D ]
```

Deleting from the original file never modifies the memory map. It only removes references from the piece list.

## Replace / Change

Replace is delete plus insert at the same logical offset.

For simple character replacement:

1. Delete the selected/ranged bytes.
2. Insert the replacement bytes from ADD backing.
3. Emit one undo transaction containing both operations.

The implementation should expose replace as a first-class document API even if internally it is delete plus insert, because higher layers should not have to duplicate transaction and caret behavior.

## Copy / Read Range

Reading text must stream across pieces into a caller-provided buffer or callback.

Do not flatten the full document to read a selection, save a file, search, render, or tokenize. All of these must iterate piece spans:

```text
for each piece intersecting requested range:
    consume piece bytes
```

Saving the file writes pieces sequentially to disk. Search scans pieces sequentially. Rendering asks only for visible line slices.

After a successful save, the document should be compacted back into one contiguous editor-owned block:

```text
before save:
[ ORIGINAL left ][ ADD edit ][ ORIGINAL right ][ ADD paste ]

after save:
[ ADD/FLAT whole_saved_document ]
```

This save-time flattening is allowed because save is already the moment where the whole document must be streamed to disk. It gives the editor a clean baseline after persistence:

- the piece list becomes one piece again
- undo and redo buffers are cleared
- obsolete ADD blocks are freed
- the old ORIGINAL mapping can be released if the document is now represented by the flat saved block

Do not flatten on every edit. Flattening belongs to explicit full-document operations such as save, not typing, paste, delete, paint, caret movement, or scroll.

## Line Indexing

Line data must not require scanning the whole file before the first paint.

Keep line information separate from pieces:

- known line starts or checkpoints
- piece-local newline counts where available
- dirty ranges after edits

Version one can use lazy line discovery with fixed byte budgets. It must never scan hundreds of MB just to open, paint, move the caret, or scroll one tick.

When edits happen:

- invalidate line data from the edit point forward, or
- maintain piece newline counts and update a higher-level checkpoint table

The simple safe rule for v1 is: preserve all line checkpoints before the changed offset, discard checkpoints after it, then lazily rediscover forward as needed.

For hostile files with one gigantic line, horizontal movement and painting must not walk from the start of the line to the viewport column. Store enough byte offset information or use byte-column assumptions for the initial ASCII-oriented renderer so horizontal scrolling can jump near the requested byte range.

## Caret And Selection

The caret stores logical byte offset as the primary position.

Line/column is view state derived from line checkpoints and local scanning, not the source of truth. This prevents caret movement from depending on Win32 control character positions.

Selections store logical byte ranges:

```text
anchor_offset
active_offset
```

Rendering converts only visible portions of the selection to screen rectangles.

## Undo / Redo

Undo records logical operations, not whole document snapshots.

For insert:

- undo deletes the inserted logical range
- redo reinserts the same ADD backing range

For delete:

- undo reinserts the removed pieces
- redo removes them again

This means deleted pieces should be retained as metadata for undo. ADD backing bytes remain stable, and ORIGINAL backing bytes remain mapped/readable.

An undo transaction may contain multiple primitive operations, such as replace or paste.

## Rendering Contract

The renderer may request only the visible rows plus a small fixed margin.

The renderer must never ask the document to flatten the whole file. It should request line slices or byte ranges and stream piece spans into a small viewport buffer.

Hard rule:

```text
paint cost ~= visible_rows * visible_columns
```

Paint cost must not scale with file size.

## Memory Ownership

Original mapped data is owned by the mapped-file object.

ADD data is owned by append blocks.

Pieces own no text bytes. They only reference backing bytes.

Undo records may own piece metadata, but not duplicate large text ranges.

When the document closes:

1. free piece nodes
2. free undo/redo metadata
3. free ADD blocks
4. unmap ORIGINAL backing

## Public Document API Shape

Keep the editing surface small and C-oriented:

```c
doc_open_mapped(path)
doc_close()
doc_len()
doc_resolve_offset(offset)
doc_read_range(offset, len, callback, user)
doc_insert(offset, bytes, len)
doc_delete(offset, len)
doc_replace(offset, delete_len, bytes, insert_len)
doc_line_start(line)
doc_line_from_offset(offset)
doc_save(path)
```

The window layer should call these APIs. It should not know or care whether text lives in the original map, add blocks, or future trees.

## Implementation Priorities

1. Replace contiguous heap editing with piece-list insert/delete.
2. Keep memory-mapped original data as immutable backing.
3. Add append-only ADD blocks.
4. Make rendering stream visible byte ranges from pieces.
5. Keep edit operations near O(number of touched pieces), not O(file size).
6. Add local offset caches so repeated typing at the caret is effectively O(1).
7. Extend neighboring ADD pieces during repeated typing so normal editing does not create one piece per key.
8. After save, flatten to one piece and clear undo/redo state.
9. Only after the simple linked list is correct, consider a balanced tree for pathological millions-of-pieces workloads.

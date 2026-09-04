# Paradym: Fast Large-File Editing Manual

This manual defines how to build a text editor where edits remain fast even on very large files.

Core idea: the editor does not treat the document as one mutable byte array. Instead, it treats the document as an ordered list of references to byte ranges that already exist. Most edits only change metadata (piece links and lengths), not bulk text bytes.

## 1. Performance Contract

For normal editing operations (typing, paste, delete, backspace, replace, selection delete):

- Time should scale with touched pieces, not total file size.
- No operation should require moving hundreds of MB of memory.
- No full-document flattening on edit paths.
- Paint/search/read should stream ranges, not materialize whole document.

Practical target:

- `edit cost ~= O(k)` where `k` is pieces touched near the edit point.
- Not `O(N)` where `N` is total file bytes.

## 2. Storage Model

Use a piece-list (rope-like behavior via structural references).

Each piece stores:

- `prev`, `next`
- backing kind: `ORIGINAL` or `ADD`
- pointer/base reference to backing bytes
- `start` (or direct pointer) within backing
- `len`
- optional cached newline count

Backings:

- `ORIGINAL`: immutable bytes from loaded file (prefer memory mapping)
- `ADD`: append-only blocks owned by editor

Important rule: bytes referenced by a piece must have stable addresses for the life of that reference.

## 3. Why This Is Fast

A contiguous buffer editor is slow for large files because middle inserts/deletes force `memmove` and often reallocation.

Piece model avoids that:

- Insert: append new text once into `ADD`, splice one piece in list.
- Delete: remove/split/unlink pieces.
- Replace: delete + insert in one transaction.

The text "appears" to change because logical order of references changes. Existing original bytes remain untouched.

## 4. Core Invariants

Always keep these true:

1. Document bytes equal concatenation of all pieces in order.
2. Pieces never own copied megabyte strings; they reference backing ranges.
3. `ORIGINAL` backing is immutable.
4. `ADD` backing is append-only.
5. Coalesce only when two adjacent pieces reference physically adjacent ranges in same backing.
6. Piece links and total length remain consistent after every edit.

## 5. Offset Resolution

Most operations start by resolving logical offset `P` to:

- piece containing `P`
- local offset in that piece
- cumulative logical base before that piece

Version 1 may use linear walk plus "last location" cache near caret.

Keep this API isolated so you can later switch to a tree with subtree lengths without rewriting editor features.

## 6. Insert Algorithm

To insert `text[0..n)` at logical offset `P`:

1. Append bytes to `ADD` backing; receive stable `(ptr, len)`.
2. Resolve `P`.
3. If `P` is inside a piece, split piece into left/right views.
4. Insert new `ADD` piece between left and right.
5. Coalesce with neighbors only when legally adjacent.
6. Update document length and line metadata for changed area.

Complexity expectation: near-constant metadata work for local typing.

Typing optimization:

- When caret continues at end of current `ADD` piece and new bytes are contiguous in same add block, extend that piece instead of creating a new node per key.

## 7. Delete Algorithm

To delete range `[P, P+N)`:

1. Resolve start and end.
2. Split boundary pieces if start/end fall inside pieces.
3. Unlink all fully covered pieces.
4. Keep uncovered boundary parts.
5. Coalesce around seam if legal.
6. Update length and line metadata.

Do not eagerly free subranges of `ADD` bytes per delete. Keep backing blocks until close or compaction.

## 8. Replace Algorithm

Replace is delete + insert at same logical offset under one undo transaction:

1. Delete selected range.
2. Insert replacement bytes.
3. Keep caret/selection semantics at API layer.

Expose replace as first-class API even if internally composed.

## 9. Read/Render/Search/Save

Never require full flatten for routine work.

- Read range: iterate intersecting pieces, stream spans to callback/buffer.
- Render: request only visible lines/slices.
- Search: scan piece spans sequentially.
- Save: stream all pieces to disk in order.

Flattening policy:

- Allowed on explicit full-document operations like successful save/rebase.
- Not allowed on typing, scrolling, caret motion, or normal repaint.

## 10. Line Index Strategy

Avoid full-file line scan at open.

- Keep lazy checkpoints and scanned frontier.
- Scan with fixed budget when UI asks beyond known lines.
- On edit, invalidate from edit point forward or adjust cheaply when safe.

Result: opening and first paint stay responsive on huge files.

## 11. Undo/Redo Without Full Snapshots

Prefer logical operation records:

- Insert undo: delete inserted range.
- Delete undo: reinsert removed piece references/ranges.

This keeps history proportional to edits, not full file size.

## 12. Memory Ownership

- Mapped file owns original bytes.
- Add blocks own appended bytes.
- Pieces own only metadata.
- Document close frees pieces, undo metadata, add blocks, then unmaps original.

## 13. API Shape (C-Oriented)

Minimal surface:

```c
bool doc_open_mapped(const wchar_t *path);
void doc_close(void);
u64 doc_len(void);
Resolve doc_resolve_offset(u64 off);
void doc_read_range(u64 off, u64 len, SpanFn fn, void *user);
bool doc_insert(u64 off, const char *bytes, u64 len);
bool doc_delete(u64 off, u64 len);
bool doc_replace(u64 off, u64 del_len, const char *bytes, u64 ins_len);
u64 doc_line_start(u64 line);
u64 doc_line_from_offset(u64 off);
bool doc_save(const wchar_t *path);
```

UI should only call document API; it must not depend on internal storage layout.

## 14. Anti-Patterns (Do Not Do)

- Rebuild entire contiguous document buffer on each edit.
- Call `realloc` on add storage when pieces hold pointers into it.
- Full-file rescan for line info after every keystroke.
- Flatten whole document for paint/search/copy.
- Use a Win32 text control as source of truth for document state.

## 15. Validation Checklist

If an edit path regresses, verify:

1. Any operation doing whole-document copy?
2. Any operation forcing full line-index rebuild?
3. Excessive piece fragmentation from typing?
4. Illegal coalescing across unrelated backing ranges?
5. Rendering requesting more than viewport + margin?

If those are controlled, large-file editing remains predictably fast.

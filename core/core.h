#ifndef TEXT_CORE_H
#define TEXT_CORE_H

#include <stddef.h>
#include <stdint.h>

/* In-memory byte rope. No encoding, lines, files, or platform dependencies.
   Zero initialization uses malloc/free. Handles own references: never copy a
   Core by assignment; use core_clone. All output handles must be initialized.
   Single-threaded; externally synchronize handles sharing storage.
   Offsets and lengths are bytes. Invalid ranges fail, never silently clamp.
   Mutations leave handles unchanged on allocation/range failure.
   Data adopted with core_from_memory must remain immutable until release.
   Its release callback runs exactly once, after the final shared reference.
   Ownership transfers only on success (also for a zero-length buffer).
   Allocator and release contexts must outlive every derived handle. */
typedef void *(*CoreAllocFn)(void *user, size_t bytes);
typedef void (*CoreFreeFn)(void *user, void *ptr);
typedef void (*CoreReleaseFn)(void *user, const void *data, uint64_t len);
typedef struct CoreAllocator {
    CoreAllocFn alloc;
    CoreFreeFn free;
    void *user;
} CoreAllocator;
typedef struct CoreNode CoreNode;
typedef struct CoreBuffer CoreBuffer;
typedef struct CorePool CorePool;
typedef struct Core {
    CoreNode *root;          /* private */
    CoreBuffer *append;      /* private: stable append-only allocation */
    CoreAllocator allocator;
    CorePool *pool;          /* private: reusable node slots */
    uint64_t start, len;     /* private: retained root range */
} Core;

/* Initialize a fresh handle. allocator=NULL selects malloc/free; otherwise
   both callbacks are required. Do not reinitialize a live handle. */
int core_init(Core *rope, const CoreAllocator *allocator);
void core_dispose(Core *rope);
uint64_t core_len(const Core *rope);
void core_clone(Core *out, const Core *source);
int core_from_memory(Core *out, const void *data, uint64_t len,
                     CoreReleaseFn release, void *user);
/* Retain a root plus byte range. No allocation or tree traversal, including
   nested slices. Releasing old output contents can still free old storage. */
int core_slice(Core *out, const Core *source, uint64_t start, uint64_t len);
/* Replace [start,start+len) with the entire insert rope. NULL means delete.
   Self-insertion and shared subtrees are supported. */
int core_replace(Core *rope, uint64_t start, uint64_t len, const Core *insert);
int core_insert(Core *rope, uint64_t start, const void *data, uint64_t len);
/* Remove a range and return it without copying bytes; out must differ from
   rope. Both handles remain unchanged on failure. */
int core_cut(Core *rope, uint64_t start, uint64_t len, Core *out);

/* Borrow a contiguous run beginning at offset. At EOF returns len=0.
   Valid until mutation/disposal of the handle. Does not allocate or scan.
   For one traversal use core_runs: O(log(pieces)+runs), cancellable by sink. */
typedef struct CoreRun { const char *data; uint64_t len; } CoreRun;
int core_run(const Core *rope, uint64_t offset, CoreRun *out);
typedef int (*CoreRunFn)(void *user, const char *data, uint64_t len);
int core_runs(const Core *rope, uint64_t start, uint64_t len,
              CoreRunFn sink, void *user);
int core_read(const Core *rope, uint64_t start, uint64_t len, void *out);

/* Logical leaf count: O(1) for full roots, O(log pieces) for partial views.
   Height is the retained tree height (O(1)), including for a partial view. */
uint64_t core_pieces(const Core *rope);
unsigned core_height(const Core *rope);

/* Costs: clone and slice retain a root/range in O(1), with no allocation.
   Edit lookup and splice work touch O(log pieces) metadata, never old bytes.
   Node slots are recycled; the allocator is called only when a pool grows.
   Unshared contiguous typing extends its leaf and ancestor byte counters
   without allocating nodes. Shared paths keep their historical versions.
   core_insert must copy new input: O(bytes + log pieces).
   Releasing the last owner costs O(unique nodes freed); an output handle's
   old contents also incur this cost. Partial views can retain the whole root.
   No content-hash deduplication: sharing preserves buffer identity. */
#endif

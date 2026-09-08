#include "core.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

struct CoreBuffer {
    size_t refs;
    const char *data;
    uint64_t len, used;
    CoreReleaseFn release;
    void *user;
    CoreAllocator allocator;
    int owned;
};
struct CoreNode {
    size_t refs;
    uint64_t len, pieces;
    unsigned height;
    CorePool *pool;
    CoreNode *left, *right;
    CoreBuffer *buffer;
    uint64_t offset;
};

typedef struct CoreSlab {
    struct CoreSlab *next;
    CoreNode nodes[64];
} CoreSlab;
struct CorePool {
    size_t refs; /* one per allocated node and owning handle */
    CoreAllocator allocator;
    CoreSlab *slabs;
    CoreNode *free;
    size_t available;
    CoreNode first; /* the first leaf needs no separate slab allocation */
};

static void *allocate(CoreAllocator a, size_t n)
{ return a.alloc ? a.alloc(a.user, n) : malloc(n); }
static void deallocate(CoreAllocator a, void *p)
{ if (p) { if (a.free) a.free(a.user, p); else free(p); } }
static void pool_drop(CorePool *p)
{
    CoreSlab *slab, *next;
    if (!p || --p->refs) return;
    for (slab = p->slabs; slab; slab = next) {
        next = slab->next; deallocate(p->allocator, slab);
    }
    deallocate(p->allocator, p);
}
static CorePool *ensure_pool(Core *r)
{
    if (!r->pool) {
        CorePool *p = (CorePool *)allocate(r->allocator, sizeof(*p));
        if (!p) return NULL;
        memset(p, 0, sizeof(*p)); p->refs = 1; p->allocator = r->allocator;
        p->free = &p->first; p->available = 1; r->pool = p;
    }
    return r->pool;
}
/* Reserve before changing any owned links. Once this succeeds, the splice
   cannot fail halfway through and needs no rollback tree or text snapshot. */
static int pool_reserve(CorePool *p, size_t count)
{
    while (p->available < count) {
        CoreSlab *slab = (CoreSlab *)allocate(p->allocator, sizeof(*slab));
        if (!slab) return 0;
        slab->next = p->slabs; p->slabs = slab;
        for (unsigned i = 0; i < 64; ++i) {
            slab->nodes[i].left = p->free; p->free = &slab->nodes[i];
        }
        p->available += 64;
    }
    return 1;
}
static CoreNode *node_new(CorePool *p)
{
    CoreNode *n;
    if (!p->free && !pool_reserve(p, 1)) return NULL;
    n = p->free; p->free = n->left; --p->available;
    memset(n, 0, sizeof(*n)); n->refs = 1; n->pool = p;
    if (p->refs == SIZE_MAX) abort();
    ++p->refs; return n;
}
static unsigned height(CoreNode *n) { return n ? n->height : 0; }
static uint64_t length(CoreNode *n) { return n ? n->len : 0; }
static CoreNode *retain(CoreNode *n)
{ if (n) { if (n->refs == SIZE_MAX) abort(); ++n->refs; } return n; }
static CoreBuffer *buffer_retain(CoreBuffer *b)
{ if (b) { if (b->refs == SIZE_MAX) abort(); ++b->refs; } return b; }
static void buffer_drop(CoreBuffer *b)
{
    if (!b || --b->refs) return;
    if (b->release) b->release(b->user, b->data, b->len);
    if (b->owned) deallocate(b->allocator, (void *)b->data);
    deallocate(b->allocator, b);
}
static void drop(CoreNode *n)
{
    if (!n || --n->refs) return;
    drop(n->left); drop(n->right); buffer_drop(n->buffer);
    CorePool *p = n->pool;
    n->left = p->free; p->free = n; ++p->available; pool_drop(p);
}
static CoreNode *leaf(CorePool *a, CoreBuffer *b, uint64_t off, uint64_t len)
{
    CoreNode *n = node_new(a);
    if (!n) return NULL;
    n->len = len; n->pieces = 1; n->height = 1;
    n->buffer = buffer_retain(b); n->offset = off;
    return n;
}
/* Consuming operations transfer one owned reference per input/output.
   Exclusive nodes are edited directly. Only shared nodes need a private slot. */
static CoreNode *exclusive(CorePool *p, CoreNode *n)
{
    CoreNode *copy;
    if (n->refs == 1) return n;
    copy = node_new(p); if (!copy) abort(); /* covered by the reservation */
    copy->len = n->len; copy->pieces = n->pieces; copy->height = n->height;
    copy->left = retain(n->left); copy->right = retain(n->right);
    copy->buffer = buffer_retain(n->buffer); copy->offset = n->offset;
    drop(n); return copy;
}
static void refresh(CoreNode *n)
{
    if (n->buffer) { n->pieces = 1; n->height = 1; return; }
    n->len = n->left->len + n->right->len;
    n->pieces = n->left->pieces + n->right->pieces;
    n->height = 1 + (height(n->left) > height(n->right) ? height(n->left) : height(n->right));
}
static CoreNode *branch_take(CorePool *p, CoreNode *l, CoreNode *r)
{
    CoreNode *n;
    if (!l) return r;
    if (!r) return l;
    if (l->buffer && r->buffer && l->buffer == r->buffer && l->offset + l->len == r->offset) {
        l = exclusive(p, l); l->len += r->len; drop(r); return l;
    }
    n = node_new(p); if (!n) abort();
    n->left = l; n->right = r; refresh(n); return n;
}
static CoreNode *rotate_left(CorePool *p, CoreNode *n)
{
    CoreNode *r = exclusive(p, n->right);
    n->right = r->left; r->left = n;
    refresh(n); refresh(r); return r;
}
static CoreNode *rotate_right(CorePool *p, CoreNode *n)
{
    CoreNode *l = exclusive(p, n->left);
    n->left = l->right; l->right = n;
    refresh(n); refresh(l); return l;
}
static CoreNode *balance_take(CorePool *p, CoreNode *n)
{
    refresh(n);
    if (height(n->left) > height(n->right) + 1) {
        if (height(n->left->left) < height(n->left->right))
            n->left = rotate_left(p, exclusive(p, n->left));
        return rotate_right(p, n);
    }
    if (height(n->right) > height(n->left) + 1) {
        if (height(n->right->right) < height(n->right->left))
            n->right = rotate_right(p, exclusive(p, n->right));
        return rotate_left(p, n);
    }
    return n;
}
static CoreNode *join_take(CorePool *p, CoreNode *l, CoreNode *r)
{
    if (!l || !r) return l ? l : r;
    if (height(l) > height(r) + 1) {
        l = exclusive(p, l);
        l->right = join_take(p, l->right, r);
        return balance_take(p, l);
    }
    if (height(r) > height(l) + 1) {
        r = exclusive(p, r);
        r->left = join_take(p, l, r->left);
        return balance_take(p, r);
    }
    return branch_take(p, l, r);
}
static void split_take(CorePool *p, CoreNode *n, uint64_t off, CoreNode **l, CoreNode **r)
{
    CoreNode *a, *b, *x, *y;
    if (!off) { *l = NULL; *r = n; return; }
    if (off == n->len) { *l = n; *r = NULL; return; }
    n = exclusive(p, n);
    if (n->buffer) {
        *r = leaf(p, n->buffer, n->offset + off, n->len - off);
        if (!*r) abort();
        n->len = off; *l = n; return;
    }
    a = n->left; b = n->right;
    n->left = n->right = NULL; drop(n); /* reuse the detached branch slot */
    if (off < a->len) {
        split_take(p, a, off, &x, &y); *l = x; *r = join_take(p, y, b);
    } else {
        split_take(p, b, off - a->len, &x, &y); *l = join_take(p, a, x); *r = y;
    }
}
static CoreNode *range_take(CorePool *p, CoreNode *n, uint64_t off, uint64_t len)
{
    CoreNode *left, *middle, *right;
    split_take(p, n, off, &left, &middle); drop(left);
    split_take(p, middle, len, &middle, &right); drop(right);
    return middle;
}
static int valid(const Core *r, uint64_t off, uint64_t len)
{ return r && off <= core_len(r) && len <= core_len(r) - off; }
static void commit(Core *r, CoreNode *n)
{ CoreNode *old = r->root; r->root = n; r->start = 0; r->len = length(n); drop(old); }

int core_init(Core *r, const CoreAllocator *a)
{
    if (!r || (a && (!a->alloc || !a->free))) return 0;
    memset(r, 0, sizeof(*r)); if (a) r->allocator = *a; return 1;
}
void core_dispose(Core *r)
{
    if (!r) return;
    drop(r->root); buffer_drop(r->append);
    r->root = NULL; r->append = NULL; r->start = r->len = 0;
    pool_drop(r->pool); r->pool = NULL;
}
uint64_t core_len(const Core *r) { return r ? r->len : 0; }
static uint64_t piece_index(CoreNode *n, uint64_t off)
{
    uint64_t index = 0;
    while (!n->buffer) {
        if (off < n->left->len) n = n->left;
        else { off -= n->left->len; index += n->left->pieces; n = n->right; }
    }
    return index;
}
uint64_t core_pieces(const Core *r)
{
    if (!r || !r->len) return 0;
    if (!r->start && r->len == r->root->len) return r->root->pieces;
    return piece_index(r->root, r->start + r->len - 1) - piece_index(r->root, r->start) + 1;
}
unsigned core_height(const Core *r) { return r ? height(r->root) : 0; }
static void set_view(Core *out, CoreNode *root, uint64_t start, uint64_t len)
{
    CoreNode *old = out->root;
    out->root = len ? retain(root) : NULL;
    out->start = len ? start : 0; out->len = len;
    drop(old);
}
void core_clone(Core *out, const Core *src)
{
    if (!out || !src || out == src) return;
    set_view(out, src->root, src->start, src->len);
}
int core_from_memory(Core *out, const void *data, uint64_t len,
                     CoreReleaseFn release, void *user)
{
    CoreBuffer *b; CoreNode *n;
    if (!out || (len && !data) || len > SIZE_MAX) return 0;
    if (!len) {
        core_dispose(out);
        if (release) release(user, data, len);
        return 1;
    }
    b = (CoreBuffer *)allocate(out->allocator, sizeof(*b));
    if (!b) return 0;
    memset(b, 0, sizeof(*b));
    b->refs = 1; b->data = (const char *)data; b->len = len;
    b->allocator = out->allocator;
    CorePool *pool = ensure_pool(out);
    n = pool ? leaf(pool, b, 0, len) : NULL;
    if (!n) { buffer_drop(b); return 0; }
    b->release = release; b->user = user;
    buffer_drop(b); buffer_drop(out->append); out->append = NULL;
    commit(out, n); return 1;
}
int core_slice(Core *out, const Core *src, uint64_t off, uint64_t len)
{
    if (!out || !valid(src, off, len)) return 0;
    set_view(out, src->root, src->start + off, len); return 1;
}
int core_replace(Core *r, uint64_t off, uint64_t len, const Core *insert)
{
    CoreNode *l, *tail, *middle = NULL, *deleted, *root;
    uint64_t total, ins = core_len(insert);
    CorePool *pool;
    unsigned source_height, insert_height;
    size_t reserve;
    if (!valid(r, off, len)) return 0;
    total = core_len(r);
    if (ins > UINT64_MAX - (total - len)) return 0;
    if (!len && !ins) return 1;
    if (len == total) {
        set_view(r, ins ? insert->root : NULL, ins ? insert->start : 0, ins);
        return 1;
    }
    if (!ins && (!off || off + len == total)) {
        set_view(r, r->root, r->start + (!off ? len : 0), total - len);
        return 1;
    }
    pool = ensure_pool(r); if (!pool) return 0;
    source_height = height(r->root); insert_height = ins ? height(insert->root) : 0;
    /* Four source-boundary splits, two insert-boundary splits and two joins.
       Budget private slots for
       shared paths and rotations before transferring ownership. Whole-root
       concatenation only needs the difference in heights plus rotations. */
    if (!len && (!off || off == total) && !r->start && total == length(r->root) &&
        (!ins || (!insert->start && ins == length(insert->root)))) {
        unsigned difference = source_height > insert_height ? source_height - insert_height : insert_height - source_height;
        reserve = 8 * ((size_t)difference + 4);
    } else reserve = 32 * ((size_t)source_height + insert_height + 4);
    unsigned maximum_height = source_height > insert_height ? source_height : insert_height;
    if (maximum_height < 7) {
        size_t small_tree_bound = ((size_t)4 << maximum_height) + 8;
        if (reserve > small_tree_bound) reserve = small_tree_bound;
    }
    if (!pool_reserve(pool, reserve)) return 0;
    /* Capture insert before touching r: self insertion and overlapping views
       must retain their previous byte ordering. */
    if (ins) middle = range_take(pool, retain(insert->root), insert->start, ins);
    root = r->root; r->root = NULL;
    root = range_take(pool, root, r->start, total);
    split_take(pool, root, off, &l, &tail);
    split_take(pool, tail, len, &deleted, &tail); drop(deleted);
    root = join_take(pool, join_take(pool, l, middle), tail);
    r->root = root; r->start = 0; r->len = length(root);
    return 1;
}
static CoreNode *locate(CoreNode *n, uint64_t *off)
{
    while (n && !n->buffer) {
        if (*off < n->left->len) n = n->left;
        else { *off -= n->left->len; n = n->right; }
    }
    return n;
}
/* All ancestors must be exclusive: a leaf can have refs=1 yet still belong
   to a shared root. No changes are made until the whole path is checked. */
static int append_in_place(Core *r, uint64_t off, const void *data, uint64_t len)
{
    CoreNode *path[128], *n = r->root;
    CoreBuffer *b = r->append;
    unsigned count = 0;
    uint64_t local;
    if (!off || r->start || r->len != length(n) || !b || len > b->len - b->used) return 0;
    local = off - 1;
    while (n) {
        if (n->refs != 1 || count == 128) return 0;
        path[count++] = n;
        if (n->buffer) break;
        if (local < n->left->len) n = n->left;
        else { local -= n->left->len; n = n->right; }
    }
    if (!n || local + 1 != n->len || n->buffer != b || n->offset + n->len != b->used) return 0;
    memcpy((char *)b->data + b->used, data, (size_t)len);
    b->used += len;
    for (unsigned i = 0; i < count; ++i) path[i]->len += len;
    r->len += len; return 1;
}
int core_insert(Core *r, uint64_t off, const void *data, uint64_t len)
{
    CoreBuffer *b; CoreNode *n, *previous;
    Core fragment = {0};
    uint64_t capacity, begin, extend = 0, local;
    int fresh = 0, ok;
    if (!valid(r, off, 0) || (len && !data) || len > SIZE_MAX ||
        len > UINT64_MAX - core_len(r)) return 0;
    if (!len) return 1;
    if (append_in_place(r, off, data, len)) return 1;
    b = r->append;
    if (!b || len > b->len - b->used) {
        capacity = len > 65536 ? len : 65536;
        if (capacity <= SIZE_MAX - 65535)
            capacity = (capacity + 65535) & ~UINT64_C(65535);
        b = (CoreBuffer *)allocate(r->allocator, sizeof(*b));
        if (!b) return 0;
        memset(b, 0, sizeof(*b)); b->refs = 1; b->allocator = r->allocator;
        b->data = (const char *)allocate(r->allocator, (size_t)capacity);
        if (!b->data) { buffer_drop(b); return 0; }
        b->owned = 1; b->len = capacity; fresh = 1;
    }
    begin = b->used;
    /* Extend the preceding run when typing at its physical end. Historical
       versions keep their old leaf length; their bytes are never modified. */
    if (off) {
        local = r->start + off - 1; previous = locate(r->root, &local);
        if (previous && local + 1 == previous->len && previous->buffer == b &&
            previous->offset + previous->len == begin) extend = previous->len < off ? previous->len : off;
    }
    CorePool *pool = ensure_pool(r);
    n = pool ? leaf(pool, b, begin - extend, extend + len) : NULL;
    if (!n) { if (fresh) buffer_drop(b); return 0; }
    memcpy((char *)b->data + begin, data, (size_t)len);
    fragment.root = n; fragment.len = n->len;
    ok = core_replace(r, off - extend, extend, &fragment);
    drop(n);
    if (!ok) { if (fresh) buffer_drop(b); return 0; }
    b->used += len;
    if (fresh) { buffer_drop(r->append); r->append = b; }
    return 1;
}
int core_cut(Core *r, uint64_t off, uint64_t len, Core *out)
{
    Core part = {0};
    if (!out || out == r || !valid(r, off, len)) return 0;
    part.allocator = out->allocator;
    if (!core_slice(&part, r, off, len)) return 0;
    if (!core_replace(r, off, len, NULL)) { core_dispose(&part); return 0; }
    set_view(out, part.root, part.start, part.len); core_dispose(&part); return 1;
}
int core_run(const Core *r, uint64_t off, CoreRun *out)
{
    CoreNode *n;
    if (!out || !valid(r, off, 0)) return 0;
    out->data = NULL; out->len = 0;
    if (off == core_len(r)) return 1;
    uint64_t remaining = r->len - off;
    off += r->start; n = locate(r->root, &off);
    out->data = n->buffer->data + n->offset + off;
    out->len = n->len - off;
    if (out->len > remaining) out->len = remaining;
    return 1;
}
static int runs(CoreNode *n, uint64_t off, uint64_t len, CoreRunFn fn, void *user)
{
    uint64_t take;
    if (!len) return 1;
    if (n->buffer) return fn(user, n->buffer->data + n->offset + off, len);
    if (off >= n->left->len) return runs(n->right, off - n->left->len, len, fn, user);
    take = n->left->len - off; if (take > len) take = len;
    if (!runs(n->left, off, take, fn, user)) return 0;
    return runs(n->right, 0, len - take, fn, user);
}
int core_runs(const Core *r, uint64_t off, uint64_t len, CoreRunFn fn, void *user)
{
    if (!fn || !valid(r, off, len)) return 0;
    return runs(r->root, r->start + off, len, fn, user);
}
typedef struct Read { char *out; } Read;
static int read_run(void *user, const char *data, uint64_t len)
{
    Read *r = (Read *)user;
    memcpy(r->out, data, (size_t)len); r->out += len; return 1;
}
int core_read(const Core *r, uint64_t off, uint64_t len, void *out)
{
    Read read;
    if ((len && !out) || len > SIZE_MAX) return 0;
    read.out = (char *)out; return core_runs(r, off, len, read_run, &read);
}

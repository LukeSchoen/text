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
    CoreAllocator allocator;
    CoreNode *left, *right;
    CoreBuffer *buffer;
    uint64_t offset;
};

static void *allocate(CoreAllocator a, size_t n)
{ return a.alloc ? a.alloc(a.user, n) : malloc(n); }
static void deallocate(CoreAllocator a, void *p)
{ if (p) { if (a.free) a.free(a.user, p); else free(p); } }
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
    deallocate(n->allocator, n);
}
static CoreNode *leaf(CoreAllocator a, CoreBuffer *b, uint64_t off, uint64_t len)
{
    CoreNode *n = (CoreNode *)allocate(a, sizeof(*n));
    if (!n) return NULL;
    memset(n, 0, sizeof(*n));
    n->refs = 1; n->len = len; n->pieces = 1; n->height = 1;
    n->allocator = a; n->buffer = buffer_retain(b); n->offset = off;
    return n;
}
/* All constructors borrow inputs and return one owned reference. */
static CoreNode *branch(CoreAllocator a, CoreNode *l, CoreNode *r)
{
    CoreNode *n;
    if (!l) return retain(r);
    if (!r) return retain(l);
    if (UINT64_MAX - l->len < r->len) return NULL;
    if (l->buffer && r->buffer && l->buffer == r->buffer &&
        l->offset + l->len == r->offset)
        return leaf(a, l->buffer, l->offset, l->len + r->len);
    n = (CoreNode *)allocate(a, sizeof(*n));
    if (!n) return NULL;
    memset(n, 0, sizeof(*n));
    n->refs = 1; n->len = l->len + r->len;
    n->pieces = l->pieces + r->pieces;
    n->height = 1 + (height(l) > height(r) ? height(l) : height(r));
    n->allocator = a; n->left = retain(l); n->right = retain(r);
    return n;
}
static CoreNode *balance(CoreAllocator a, CoreNode *l, CoreNode *r)
{
    CoreNode *x = NULL, *y = NULL, *out = NULL;
    if (height(l) > height(r) + 1) {
        if (height(l->left) >= height(l->right)) {
            x = branch(a, l->right, r);
            if (x) out = branch(a, l->left, x);
        } else {
            x = branch(a, l->left, l->right->left);
            y = branch(a, l->right->right, r);
            if (x && y) out = branch(a, x, y);
        }
    } else if (height(r) > height(l) + 1) {
        if (height(r->right) >= height(r->left)) {
            x = branch(a, l, r->left);
            if (x) out = branch(a, x, r->right);
        } else {
            x = branch(a, l, r->left->left);
            y = branch(a, r->left->right, r->right);
            if (x && y) out = branch(a, x, y);
        }
    } else return branch(a, l, r);
    drop(x); drop(y); return out;
}
static CoreNode *join(CoreAllocator a, CoreNode *l, CoreNode *r)
{
    CoreNode *x, *out;
    if (!l || !r) return branch(a, l, r);
    if (height(l) > height(r) + 1) {
        x = join(a, l->right, r);
        if (!x) return NULL;
        out = balance(a, l->left, x);
    } else if (height(r) > height(l) + 1) {
        x = join(a, l, r->left);
        if (!x) return NULL;
        out = balance(a, x, r->right);
    } else return branch(a, l, r);
    drop(x); return out;
}
/* A range shares every fully enclosed subtree. Only its boundary paths are
   rebuilt. In particular, a whole-rope slice is just a reference increment. */
static CoreNode *slice(CoreAllocator a, CoreNode *n, uint64_t off, uint64_t len)
{
    CoreNode *l, *r, *out;
    uint64_t left_len;
    if (!len) return NULL;
    if (!off && len == n->len) return retain(n);
    if (n->buffer) return leaf(a, n->buffer, n->offset + off, len);
    left_len = n->left->len;
    if (off >= left_len) return slice(a, n->right, off - left_len, len);
    if (len <= left_len - off) return slice(a, n->left, off, len);
    l = slice(a, n->left, off, left_len - off);
    if (!l) return NULL;
    r = slice(a, n->right, 0, len - (left_len - off));
    out = r ? join(a, l, r) : NULL;
    drop(l); drop(r); return out;
}
static int valid(const Core *r, uint64_t off, uint64_t len)
{ return r && off <= core_len(r) && len <= core_len(r) - off; }
static void commit(Core *r, CoreNode *n)
{ CoreNode *old = r->root; r->root = n; drop(old); }

int core_init(Core *r, const CoreAllocator *a)
{
    if (!r || (a && (!a->alloc || !a->free))) return 0;
    memset(r, 0, sizeof(*r)); if (a) r->allocator = *a; return 1;
}
void core_dispose(Core *r)
{
    if (!r) return;
    drop(r->root); buffer_drop(r->append);
    r->root = NULL; r->append = NULL;
}
uint64_t core_len(const Core *r) { return r ? length(r->root) : 0; }
uint64_t core_pieces(const Core *r) { return r && r->root ? r->root->pieces : 0; }
unsigned core_height(const Core *r) { return r ? height(r->root) : 0; }
void core_clone(Core *out, const Core *src)
{
    CoreNode *n;
    if (!out || !src || out == src) return;
    n = retain(src->root); core_dispose(out); out->root = n;
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
    n = leaf(out->allocator, b, 0, len);
    if (!n) { buffer_drop(b); return 0; }
    b->release = release; b->user = user;
    buffer_drop(b); core_dispose(out); out->root = n; return 1;
}
int core_slice(Core *out, const Core *src, uint64_t off, uint64_t len)
{
    CoreNode *n;
    if (!out || !valid(src, off, len)) return 0;
    n = slice(out->allocator, src->root, off, len);
    if (len && !n) return 0;
    commit(out, n); return 1;
}
int core_replace(Core *r, uint64_t off, uint64_t len, const Core *insert)
{
    CoreNode *l = NULL, *tail = NULL, *x = NULL, *out = NULL;
    uint64_t total, right_len, ins = core_len(insert);
    if (!valid(r, off, len)) return 0;
    total = core_len(r);
    if (ins > UINT64_MAX - (total - len)) return 0;
    if (!len && !ins) return 1;
    right_len = total - off - len;
    l = slice(r->allocator, r->root, 0, off);
    if (off && !l) goto fail;
    tail = slice(r->allocator, r->root, off + len, right_len);
    if (right_len && !tail) goto fail;
    x = join(r->allocator, l, insert ? insert->root : NULL);
    if ((off || ins) && !x) goto fail;
    out = join(r->allocator, x, tail);
    if ((off || ins || right_len) && !out) goto fail;
    drop(l); drop(tail); drop(x); commit(r, out); return 1;
fail:
    drop(l); drop(tail); drop(x); drop(out); return 0;
}
static CoreNode *locate(CoreNode *n, uint64_t *off)
{
    while (n && !n->buffer) {
        if (*off < n->left->len) n = n->left;
        else { *off -= n->left->len; n = n->right; }
    }
    return n;
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
    b = r->append;
    if (!b || len > b->len - b->used) {
        capacity = len > 65536 ? len : 65536;
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
        local = off - 1; previous = locate(r->root, &local);
        if (previous && local + 1 == previous->len && previous->buffer == b &&
            previous->offset + previous->len == begin) extend = previous->len;
    }
    n = leaf(r->allocator, b, begin - extend, extend + len);
    if (!n) { if (fresh) buffer_drop(b); return 0; }
    memcpy((char *)b->data + begin, data, (size_t)len);
    fragment.root = n;
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
    commit(out, part.root); return 1;
}
int core_run(const Core *r, uint64_t off, CoreRun *out)
{
    CoreNode *n;
    if (!out || !valid(r, off, 0)) return 0;
    out->data = NULL; out->len = 0;
    if (off == core_len(r)) return 1;
    n = locate(r->root, &off);
    out->data = n->buffer->data + n->offset + off;
    out->len = n->len - off; return 1;
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
    return runs(r->root, off, len, fn, user);
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

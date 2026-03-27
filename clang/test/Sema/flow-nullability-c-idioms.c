// Comprehensive C idiom tests for flow-sensitive nullability analysis.
// Real C code is macro-heavy, uses malloc/free patterns, errno checks,
// container_of tricks, callback patterns, and varargs. These patterns
// must work correctly — C is half the target audience for this feature.
// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -Wno-nullable-to-nonnull-conversion -std=c11 %s -verify

typedef unsigned long size_t;
typedef _Bool bool;
#define true 1
#define false 0
#define NULL ((void *)0)
#define offsetof(type, member) __builtin_offsetof(type, member)

// Simulated stdlib declarations
void * _Nullable malloc(size_t);
void * _Nullable calloc(size_t, size_t);
void * _Nullable realloc(void * _Nullable, size_t);
void free(void * _Nullable);
void abort(void) __attribute__((noreturn));
void exit(int) __attribute__((noreturn));

struct Node {
    int value;
    struct Node * _Nullable next;
};

struct Buffer {
    char * _Nullable data;
    size_t len;
    size_t cap;
};

struct Node * _Nullable getNode(void);
int getInt(void);

// === Macro-heavy null-check patterns ===
// Real C code wraps null checks in macros. The analysis should see through them.

#define CHECK_NULL(ptr) do { if (!(ptr)) return; } while(0)
#define CHECK_NULL_RET(ptr, ret) do { if (!(ptr)) return (ret); } while(0)
#define ASSERT_NONNULL(ptr) do { if (!(ptr)) abort(); } while(0)
#define DEREF(p) ((p)->value)
#define SAFE_DEREF(p, fallback) ((p) ? (p)->value : (fallback))

void test_check_null_macro(struct Node *p) {
    CHECK_NULL(p);
    p->value = 1; // OK — macro expanded to if(!p) return
}

int test_check_null_ret_macro(struct Node *p) {
    CHECK_NULL_RET(p, -1);
    return p->value; // OK
}

void test_assert_nonnull_macro(struct Node *p) {
    ASSERT_NONNULL(p);
    p->value = 1; // OK — abort() is noreturn
}

void test_deref_macro(struct Node *p) {
    int v = DEREF(p); // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
    (void)v;
}

void test_deref_macro_guarded(struct Node *p) {
    if (p) {
        int v = DEREF(p); // OK — narrowed before macro
        (void)v;
    }
}

void test_safe_deref_macro(struct Node *p) {
    int v = SAFE_DEREF(p, -1); // OK — ternary checks p
    (void)v;
}

// === malloc/free patterns ===
// malloc returns nullable (can fail), need to check before use.

void test_malloc_no_check(void) {
    // In C, void* implicitly converts to struct Node*, preserving _Nullable
    struct Node * _Nullable n = malloc(sizeof(struct Node));
    n->value = 1; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
    free(n);
}

void test_malloc_checked(void) {
    struct Node *n = (struct Node *)malloc(sizeof(struct Node));
    if (!n) return;
    n->value = 1; // OK — narrowed
    n->next = NULL;
    free(n);
}

void test_malloc_abort(void) {
    struct Node *n = (struct Node *)malloc(sizeof(struct Node));
    if (!n) abort();
    n->value = 1; // OK — abort is noreturn
    free(n);
}

void test_calloc_checked(void) {
    struct Node *n = (struct Node *)calloc(1, sizeof(struct Node));
    if (!n) return;
    n->value = 1; // OK
    free(n);
}

// === realloc pattern (returns nullable) ===

void test_realloc(struct Buffer * _Nonnull buf) {
    char * _Nullable new_data = (char *)realloc(buf->data, buf->cap * 2);
    if (!new_data) return;
    buf->data = new_data;
    buf->cap *= 2;
}

// === Linked list construction ===

struct Node * _Nullable list_prepend(struct Node * _Nullable head, int val) {
    struct Node *n = (struct Node *)malloc(sizeof(struct Node));
    if (!n) return head;
    n->value = val; // OK — checked
    n->next = head;
    return n;
}

void list_free(struct Node * _Nullable head) {
    struct Node * _Nullable p = head;
    while (p) {
        struct Node * _Nullable next = p->next; // OK — p narrowed
        free(p);
        p = next;
    }
}

int list_sum(struct Node * _Nullable head) {
    int sum = 0;
    for (struct Node * _Nullable p = head; p; p = p->next) {
        sum += p->value; // OK — narrowed by loop condition
    }
    return sum;
}

// === Callback / function pointer patterns ===

typedef void (*node_visitor_fn)(struct Node * _Nonnull, void * _Nullable);

void list_foreach(struct Node * _Nullable head, node_visitor_fn _Nonnull fn, void * _Nullable ctx) {
    for (struct Node * _Nullable p = head; p; p = p->next) {
        fn(p, ctx); // OK — p narrowed
    }
}

// === errno-style error checking ===

struct File;
struct File * _Nullable file_open(const char * _Nonnull path);
int file_read(struct File * _Nonnull f, char * _Nonnull buf, int len);
void file_close(struct File * _Nonnull f);

int test_errno_pattern(void) {
    struct File * _Nullable f = file_open("/tmp/test");
    if (!f) return -1;
    // f is narrowed past early return
    char buf[256];
    int n = file_read(f, buf, 256); // OK
    file_close(f); // OK
    return n;
}

// === container_of macro pattern ===
// A ubiquitous C pattern (Linux kernel, etc.) that computes a container
// from a member pointer. The result is always nonnull if the member is.

#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

struct list_head {
    struct list_head * _Nullable next;
    struct list_head * _Nullable prev;
};

struct my_item {
    int data;
    struct list_head link;
};

void test_container_of(struct list_head * _Nullable pos) {
    if (!pos) return;
    // container_of produces a pointer from arithmetic — nonnull if pos is nonnull
    struct my_item *item = container_of(pos, struct my_item, link);
    item->data = 42; // OK — arithmetic on non-null pointer
}

// === Multi-level cleanup with goto ===
// The goto cleanup pattern is the C equivalent of RAII.

int test_multi_level_cleanup(void) {
    int ret = -1;
    struct Node *a = (struct Node *)malloc(sizeof(struct Node));
    if (!a) goto out;

    struct Node *b = (struct Node *)malloc(sizeof(struct Node));
    if (!b) goto free_a;

    a->value = 1; // OK — narrowed past goto
    b->value = 2; // OK — narrowed past goto
    a->next = b;
    ret = a->value + b->value;

free_a:
    free(a);
out:
    return ret;
}

// === Bitfield struct with nullable pointer ===

struct Options {
    unsigned verbose : 1;
    unsigned debug : 1;
    struct Node * _Nullable config;
};

void test_bitfield_struct(struct Options * _Nonnull opts) {
    if (opts->config) {
        opts->config->value = opts->verbose; // OK — narrowed
    }
}

// === Null check via helper function result ===
// The analysis is intraprocedural — can't see inside helper functions.

static bool is_valid(const struct Node * _Nullable p) {
    return p != NULL;
}

void test_helper_check(struct Node *p) {
    // is_valid returns bool but the analysis can't know it checks for null.
    // This is an accepted limitation of intraprocedural analysis.
    if (is_valid(p)) {
        p->value = 1; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
    }
}

// === Nested macro expansion ===

#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

void test_likely_macro(struct Node *p) {
    if (LIKELY(p != NULL)) {
        p->value = 1; // OK
    }
}

void test_unlikely_null(struct Node *p) {
    if (UNLIKELY(p == NULL)) return;
    p->value = 1; // OK
}

// === Flexible array member ===

struct FlexArray {
    int count;
    struct Node * _Nullable items[];
};

void test_flex_array(struct FlexArray * _Nonnull fa) {
    for (int i = 0; i < fa->count; i++) {
        struct Node * _Nullable item = fa->items[i];
        if (item) {
            item->value = i; // OK
        }
    }
}

// === void** output parameter pattern ===
// Common C pattern: function fills in an output pointer.

int get_node_out(struct Node * _Nullable * _Nonnull out);

void test_output_param(void) {
    struct Node * _Nullable n = NULL;
    if (get_node_out(&n) == 0 && n) {
        n->value = 42; // OK — checked via &&
    }
}

// === Static assert + null check (compile-time vs runtime) ===

_Static_assert(sizeof(struct Node) > 0, "Node must have size");

void test_with_static_assert(struct Node *p) {
    _Static_assert(sizeof(*p) == sizeof(struct Node), "size match");
    // _Static_assert doesn't affect flow, p is still nullable
    if (p) {
        p->value = 1; // OK
    }
}

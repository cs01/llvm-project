// Tests for unannotated pointers under nullable-default.
// Under -fnullability-default=nullable, unannotated pointers are nullable.
// These patterns correctly warn — the fix is to add null checks or _Nonnull.
// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -std=c++17 %s -verify

typedef unsigned long size_t;
typedef unsigned char uint8_t;

// --- Case 1: array subscript on unannotated pointer parameter ---
// buffers is nullable → subscript is a deref → warns.

inline bool getData(const uint8_t** buffers, int readIndex) {
    auto buffer = buffers[readIndex]; // expected-warning{{dereferencing nullable pointer}}
    return buffer != nullptr;
}

// --- Case 2: lambda deleter parameter ---
// w is nullable under the default → dereference warns.

struct Widget {
    int x;
    ~Widget() {}
};

void test_deleter() {
    auto* ptr = new Widget;
    auto deleter = [](Widget* w) {
        w->~Widget(); // expected-warning{{dereferencing nullable pointer}}
    };
    deleter(ptr);
}

// --- Case 3: return value from function doing pointer arithmetic on this ---
// getBuffer() return type is nullable → array subscript warns.

struct Buffer {
    int offset;
    uint8_t* getBuffer() {
        return reinterpret_cast<uint8_t*>(this) + offset;
    }
    void use() {
        uint8_t val = getBuffer()[0]; // expected-warning{{dereferencing nullable pointer}}
    }
};

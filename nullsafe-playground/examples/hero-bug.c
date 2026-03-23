// Standard clang compiles this with ZERO warnings.
// But there's a null pointer crash hiding here.
// Nullsafe C catches it at compile time.

#include <stdlib.h>

typedef struct {
    int* data;
    int size;
} Buffer;

Buffer* create_buffer(int n) {
    Buffer* buf = malloc(sizeof(Buffer));
    buf->data = malloc(n * sizeof(int));  // warning: buf might be NULL
    buf->size = n;                        // warning: buf might be NULL
    return buf;
}

void fill_buffer(Buffer* buf) {
    for (int i = 0; i < buf->size; i++) { // warning: buf might be NULL
        buf->data[i] = i;                 // warning: buf->data might be NULL
    }
}

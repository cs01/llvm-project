// Functions can invalidate narrowing
void may_modify(int** ptr);

void example(int* data) {
    if (data) {
        may_modify(&data);  // might set data to null
        *data = 42;  // warning - data might be null now!
    }
}
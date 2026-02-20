void unsafe(int *data) {
  *data = 42; // warning - data might be null!
}

void safe(int *data) {
  if (data) {
    *data = 42; // OK - data is non-null here
  }
}

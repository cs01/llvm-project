int main(int argc, char *_Nullable *__counted_by(argc + 500) argv) {
  char *_Nullable p = argv[argc + 499];
  return p ? *p : 0;
}

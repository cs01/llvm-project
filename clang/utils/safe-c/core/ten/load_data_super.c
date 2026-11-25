// darknet. Structs with owners are filled through a pointer, not returned by value.
typedef struct image { int w, h, c; float *_Owned _Nullable data; } image;
typedef struct matrix { int rows, cols; float *_Owned _Nullable *_Owned _Nullable __counted_by(rows) vals; } matrix;
typedef struct data { int shallow; matrix X; matrix y; } data;
_Bool load_image_color(image *out, int path);
_Bool random_crop_image(image *out, const image *im, int w, int h);
void flip_image(image *im);
_Bool resize_image(image *out, const image *im, int w, int h);
void free_image(image *im) { free(_Take(im->data)); }
_Bool matrix_rows(matrix *m, int n, int cols) {
  float *_Owned _Nullable *_Owned _Nullable v = calloc((unsigned long)n, sizeof *v);
  if (!v) return 0;
  m->vals = v; m->rows = n; m->cols = cols;
  return 1;
}
void set_row(matrix *m, int i, float *_Owned _Nullable row) { if (!m->vals || i >= m->rows) { free(row); return; } m->vals[i] = row; }
_Bool load_data_super(data *d, const int *__counted_by(n) paths, int n, int w, int h, int scale) {
  if (n <= 0 || !matrix_rows(&d->X, n, w * h * 3) || !matrix_rows(&d->y, n, w * scale * h * scale * 3)) return 0;
  for (int i = 0; i < n; ++i) {
    image im = {0}, crop = {0}, resize = {0};
    _Bool ok = load_image_color(&im, paths[i]) && random_crop_image(&crop, &im, w * scale, h * scale);
    if (ok && rand() % 2) flip_image(&crop);
    if (ok) ok = resize_image(&resize, &crop, w, h);
    set_row(&d->X, i, _Take(resize.data));
    set_row(&d->y, i, _Take(crop.data));
    free_image(&im);
  }
  return 1;
}

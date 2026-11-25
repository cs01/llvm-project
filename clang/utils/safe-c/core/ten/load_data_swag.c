// darknet. As load_data_super; the random path is an index.
typedef struct image { int w, h; float *_Owned _Nullable data; } image;
typedef struct matrix { int rows, cols; float *_Owned _Nullable *_Owned _Nullable __counted_by(rows) vals; } matrix;
typedef struct data { int w, h; matrix X; matrix y; } data;
_Bool load_image_color(image *out, int path);
_Bool crop_image(image *out, const image *im, int x, int y, int w, int h);
_Bool resize_image(image *out, const image *im, int w, int h);
void flip_image(image *im);
_Bool make_matrix(matrix *m, int rows, int cols);
int rand_int(int lo, int hi);
void fill_truth(int path, float *_Nullable row, int classes, int flip);
void free_image(image *im) { free(_Take(im->data)); }
float *_Nullable row0(const matrix *m) { if (!m->vals || m->rows < 1) return NULL; return m->vals[0]; }
_Bool load_data_swag(data *d, int n, int classes) {
  int path = rand() % n;
  image orig = {0}, cropped = {0}, sized = {0};
  if (!load_image_color(&orig, path)) return 0;
  d->w = orig.w; d->h = orig.h;
  if (!make_matrix(&d->X, 1, orig.h * orig.w * 3) || !make_matrix(&d->y, 1, (4 + classes) * 90)) { free_image(&orig); return 0; }
  int left = rand_int(-9, 9), top = rand_int(-9, 9);
  _Bool ok = crop_image(&cropped, &orig, left, top, orig.w - left, orig.h - top) && resize_image(&sized, &cropped, orig.w, orig.h);
  int flip = rand() % 2;
  if (ok && flip) flip_image(&sized);
  if (d->X.vals && d->X.rows > 0) d->X.vals[0] = _Take(sized.data);
  fill_truth(path, row0(&d->y), classes, flip);
  free_image(&orig); free_image(&cropped);
  return ok;
}

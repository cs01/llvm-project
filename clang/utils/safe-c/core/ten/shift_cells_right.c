// sc-im. The moving pointer 'pp--' becomes an index.
struct ent { int row, col; };
struct row { int n; struct ent *_Owned _Nullable *_Owned _Nullable __counted_by(n) cell; };
struct sheet { int maxcol, maxcols, currow, curcol; int nrows; struct row *_Owned _Nullable __counted_by(nrows) tbl; };
_Bool growtbl(struct sheet *sh, int cols);
void shift_row(struct row *w, int curcol, int maxcol, int d) {
  if (!w->cell) return;
  for (int c = maxcol; c >= curcol + d; c--) {
    struct ent *_Owned _Nullable e = _Take(w->cell[c - d]);
    if (e) e->col += d;
    w->cell[c] = e;
  }
}
void shift_cells_right(struct sheet *sh, int deltarows, int deltacols) {
  sh->maxcol += deltacols;
  if (sh->maxcol >= sh->maxcols && !growtbl(sh, sh->maxcol)) { sh->maxcol = sh->maxcols - 1; return; }
  if (!sh->tbl) return;
  for (int r = sh->currow; r < sh->currow + deltarows; r++) shift_row(&sh->tbl[r], sh->curcol, sh->maxcol, deltacols);
}

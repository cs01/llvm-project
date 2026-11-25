// sc-im. Table of rows of owned cells; copying a pointer down becomes a move.
struct ent { int row, col; };
struct row { int n; struct ent *_Owned _Nullable *_Owned _Nullable __counted_by(n) cell; };
struct sheet { int maxrow, maxrows, currow, curcol; int nrows; struct row *_Owned _Nullable __counted_by(nrows) tbl; };
_Bool growtbl(struct sheet *sh, int rows);
struct ent *_Owned _Nullable take_cell(struct row *w, int c) { if (!w->cell || c < 0 || c >= w->n) return NULL; return _Take(w->cell[c]); }
void put_cell(struct row *w, int c, struct ent *_Owned _Nullable e) { if (!w->cell || c < 0 || c >= w->n) { free(e); return; } w->cell[c] = e; }
void shift_cells_down(struct sheet *sh, int deltarows, int deltacols) {
  sh->maxrow += deltarows;
  if (sh->maxrow >= sh->maxrows && !growtbl(sh, sh->maxrow)) { sh->maxrow = sh->maxrows - 1; return; }
  if (!sh->tbl) return;
  for (int r = sh->maxrow; r > sh->currow + deltarows - 1; r--)
    for (int c = sh->curcol; c < sh->curcol + deltacols; c++) {
      struct row *from = &sh->tbl[r - deltarows];
      struct ent *_Owned _Nullable e = take_cell(from, c);
      if (e) e->row += deltarows;
      struct row *to = &sh->tbl[r];
      put_cell(to, c, e);
    }
}

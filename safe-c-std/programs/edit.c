/* A small text editor: edit FILE
   Arrows, Home, End, Page Up, Page Down; ^S save, ^Q quit, ^Z undo, ^Y redo, ^F find, ^G find again.
   The file is one text. An edit replaces some bytes of it with others; undo does the opposite. */
#include "../args.h"
#include "../file.h"
#include "../vec.h"
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/* At 'at', 'removed' bytes became 'added' bytes; both are kept in history.bytes, one after the other. */
struct change { size_t at, removed, added, bytes; };
VEC(changes, struct change);
struct history {
  struct changes changes;
  struct text bytes;
  size_t now; /* changes before this one are done, the others undone */
  size_t saved;
};
struct editor {
  struct text doc;
  struct history history;
  struct text screen, find, message;
  size_t cursor, top, left;
  int rows, columns, quitting, done;
};

enum key { KEY_END = -1, KEY_UP = 1000, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_HOME, KEY_LAST, KEY_PAGE_UP, KEY_PAGE_DOWN, KEY_DELETE, KEY_OTHER };
#define CTRL(c) ((c) & 0x1f)

/* ---- the terminal (the system calls are the program's only unsafe code) ---- */

_Trusted __unsafe static int terminal_raw(struct termios *before) {
  if (!isatty(0) || tcgetattr(0, before) != 0)
    return 0;
  struct termios raw = *before;
  raw.c_iflag &= ~(tcflag_t)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(tcflag_t)OPOST;
  raw.c_cflag |= CS8;
  raw.c_lflag &= ~(tcflag_t)(ECHO | ICANON | IEXTEN | ISIG);
  return tcsetattr(0, TCSAFLUSH, &raw) == 0;
}
_Trusted __unsafe static void terminal_restore(const struct termios *before) { tcsetattr(0, TCSAFLUSH, before); }
_Trusted __unsafe static int terminal_size(int *rows, int *columns) {
  struct winsize w = {0, 0, 0, 0};
  if (ioctl(1, TIOCGWINSZ, &w) != 0 || w.ws_row < 3 || w.ws_col < 8)
    return 0;
  *rows = w.ws_row;
  *columns = w.ws_col;
  return 1;
}

static int read_key(void) {
  int c = getc(stdin);
  if (c != 27)
    return c == EOF ? KEY_END : c;
  if (getc(stdin) != '[')
    return KEY_OTHER;
  c = getc(stdin);
  switch (c) {
  case 'A': return KEY_UP;
  case 'B': return KEY_DOWN;
  case 'C': return KEY_RIGHT;
  case 'D': return KEY_LEFT;
  case 'H': return KEY_HOME;
  case 'F': return KEY_LAST;
  }
  if (c < '0' || c > '9' || getc(stdin) != '~')
    return KEY_OTHER;
  switch (c) {
  case '1': case '7': return KEY_HOME;
  case '4': case '8': return KEY_LAST;
  case '3': return KEY_DELETE;
  case '5': return KEY_PAGE_UP;
  case '6': return KEY_PAGE_DOWN;
  }
  return KEY_OTHER;
}

/* ---- changes ---- */

/* Forgets what was undone, and keeps a change that is about to be made. */
static int remember(struct history *h, size_t at, struct str removed, struct str added) {
  struct change c = {at, removed.len, added.len, 0};
  while (h->changes.len > h->now) {
    struct change undone;
    changes_pop(&h->changes, &undone);
    h->bytes.len = undone.bytes;
    if (h->saved > h->changes.len)
      h->saved = (size_t)-1;
  }
  c.bytes = h->bytes.len;
  if (!text_add(&h->bytes, removed.p, removed.len) || !text_add(&h->bytes, added.p, added.len) ||
      !changes_add(&h->changes, c)) {
    h->bytes.len = c.bytes;
    return 0;
  }
  h->now++;
  return 1;
}

static void replace(struct editor *e, size_t at, size_t n, struct str with) {
  if (!remember(&e->history, at, str_part(text_str(&e->doc), at, n), with) ||
      !text_splice(&e->doc, at, n, with)) {
    text_set(&e->message, STR("out of memory"));
    return;
  }
  e->cursor = at + with.len;
}

static void undo(struct editor *e) {
  struct change c;
  if (e->history.now == 0)
    return;
  changes_get(&e->history.changes, e->history.now - 1, &c);
  struct str old = str_part(text_str(&e->history.bytes), c.bytes, c.removed);
  if (!text_splice(&e->doc, c.at, c.added, old))
    return;
  e->history.now--;
  e->cursor = c.at + c.removed;
}

static void redo(struct editor *e) {
  struct change c;
  if (!changes_get(&e->history.changes, e->history.now, &c))
    return;
  struct str new = str_part(text_str(&e->history.bytes), c.bytes + c.removed, c.added);
  if (!text_splice(&e->doc, c.at, c.removed, new))
    return;
  e->history.now++;
  e->cursor = c.at + c.added;
}

/* ---- lines ---- */

static size_t line_start(struct str s, size_t at) {
  while (at > 0 && at <= s.len && s.p[at - 1] != '\n')
    at--;
  return at;
}
static size_t line_end(struct str s, size_t at) {
  return at + str_find(str_part(s, at, s.len), '\n');
}
/* The start of the line 'by' lines on from the one at is in. */
static size_t line_move(struct str s, size_t at, int by) {
  at = line_start(s, at);
  for (; by < 0 && at > 0; by++)
    at = line_start(s, at - 1);
  for (; by > 0 && line_end(s, at) < s.len; by--)
    at = line_end(s, at) + 1;
  return at;
}
static void move_lines(struct editor *e, int by) {
  struct str s = text_str(&e->doc);
  size_t column = e->cursor - line_start(s, e->cursor);
  size_t to = line_move(s, e->cursor, by), end = line_end(s, to);
  e->cursor = to + column < end ? to + column : end;
}

/* ---- the screen ---- */

static void draw_line(struct text *out, struct str line, size_t left, size_t columns) {
  struct str s = str_part(line, left, columns);
  for (size_t i = 0; i < s.len; i++) {
    char c = s.p[i];
    if ((unsigned char)c < 32 || c == 127)
      c = '?';
    text_add(out, &c, 1);
  }
  struct str clear = STR("\x1b[K\r\n");
  text_add(out, clear.p, clear.len);
}

static void draw(struct editor *e) {
  struct str doc = text_str(&e->doc);
  size_t start = line_start(doc, e->cursor), column = e->cursor - start;
  size_t wide = (size_t)e->columns;
  if (start < e->top)
    e->top = start;
  while (line_move(doc, e->top, e->rows - 2) < start)
    e->top = line_move(doc, e->top, 1);
  if (column < e->left)
    e->left = column;
  if (column >= e->left + wide)
    e->left = column - wide + 1;

  text_set(&e->screen, STR("\x1b[?25l\x1b[H"));
  int row = 0, cursor_row = 0;
  for (size_t at = e->top; row < e->rows - 1; row++) {
    if (at > doc.len) {
      draw_line(&e->screen, STR("~"), 0, wide);
      continue;
    }
    if (at == start)
      cursor_row = row;
    size_t end = line_end(doc, at);
    draw_line(&e->screen, str_part(doc, at, end - at), e->left, wide);
    at = end + 1;
  }
  struct str message = text_str(&e->message);
  struct str shown = str_part(message, 0, wide);
  text_add(&e->screen, shown.p, shown.len);
  char at[40];
  int n = snprintf(at, sizeof at, "\x1b[K\x1b[%d;%zuH\x1b[?25h", cursor_row + 1, column - e->left + 1);
  if (n > 0 && (size_t)n < sizeof at)
    text_add(&e->screen, at, (size_t)n);
  struct str all = text_str(&e->screen);
  fwrite(all.p, 1, all.len, stdout);
  fflush(stdout);
}

/* ---- commands ---- */

/* Reads what to find on the bottom row: 1, or 0 if it was given up. */
static int ask(struct editor *e) {
  text_set(&e->find, STR(""));
  for (;;) {
    text_set(&e->message, STR("find: "));
    struct str a = text_str(&e->find);
    text_add(&e->message, a.p, a.len);
    draw(e);
    int k = read_key();
    if (k == '\r' || k == '\n')
      return e->find.len > 0;
    if (k == KEY_END || k == KEY_OTHER || k == CTRL('c'))
      return 0;
    if ((k == 127 || k == CTRL('h')) && e->find.len > 0)
      e->find.len--;
    if (k >= 32 && k < 127) {
      char c = (char)k;
      text_add(&e->find, &c, 1);
    }
  }
}

static void find_next(struct editor *e) {
  struct str doc = text_str(&e->doc), what = text_str(&e->find);
  if (what.len == 0)
    return;
  for (size_t i = 1; i <= doc.len; i++) {
    size_t at = (e->cursor + i) % (doc.len + 1);
    if (str_eq(str_part(doc, at, what.len), what)) {
      e->cursor = at;
      return;
    }
  }
  text_set(&e->message, STR("not found"));
}

static void press(struct editor *e, int k, const char *__null_terminated path) {
  struct str doc = text_str(&e->doc);
  int was_quitting = e->quitting;
  e->quitting = 0;
  text_set(&e->message, STR(""));
  switch (k) {
  case KEY_END: e->done = 1; break;
  case KEY_UP: move_lines(e, -1); break;
  case KEY_DOWN: move_lines(e, 1); break;
  case KEY_PAGE_UP: move_lines(e, 2 - e->rows); break;
  case KEY_PAGE_DOWN: move_lines(e, e->rows - 2); break;
  case KEY_LEFT: e->cursor -= e->cursor > 0; break;
  case KEY_RIGHT: e->cursor += e->cursor < doc.len; break;
  case KEY_HOME: e->cursor = line_start(doc, e->cursor); break;
  case KEY_LAST: e->cursor = line_end(doc, e->cursor); break;
  case KEY_DELETE:
    if (e->cursor < doc.len)
      replace(e, e->cursor, 1, STR(""));
    break;
  case 127:
  case CTRL('h'):
    if (e->cursor > 0)
      replace(e, e->cursor - 1, 1, STR(""));
    break;
  case CTRL('z'): undo(e); break;
  case CTRL('y'): redo(e); break;
  case CTRL('f'):
    if (ask(e))
      find_next(e);
    break;
  case CTRL('g'): find_next(e); break;
  case CTRL('s'):
    if (file_write(path, doc)) {
      e->history.saved = e->history.now;
      text_set(&e->message, STR("saved"));
    } else {
      text_set(&e->message, STR("could not save"));
    }
    break;
  case CTRL('q'):
    if (e->history.saved == e->history.now || was_quitting) {
      e->done = 1;
    } else {
      e->quitting = 1;
      text_set(&e->message, STR("not saved; ^Q again to quit"));
    }
    break;
  case '\r':
  case '\n': replace(e, e->cursor, 0, STR("\n")); break;
  default:
    if (k == '\t' || (k >= 32 && k < 256)) {
      char c = (char)k;
      struct str s = {&c, 1};
      replace(e, e->cursor, 0, s);
    }
  }
}

int main(int argc, char *__null_terminated *__counted_by(argc) argv) {
  if (argc != 2)
    return fputs("usage: edit FILE\n", stderr), 1;
  struct editor e = {.rows = 24, .columns = 80};
  file_read(argv[1], &e.doc);
  text_set(&e.message, STR("^S save  ^Q quit  ^Z undo  ^Y redo  ^F find"));
  struct termios before;
  int raw = terminal_raw(&before);
  while (!e.done) {
    terminal_size(&e.rows, &e.columns);
    draw(&e);
    press(&e, read_key(), argv[1]);
  }
  fputs("\x1b[2J\x1b[H", stdout);
  if (raw)
    terminal_restore(&before);
  text_free(&e.doc);
  text_free(&e.history.bytes);
  changes_free(&e.history.changes);
  text_free(&e.screen);
  text_free(&e.find);
  text_free(&e.message);
  return 0;
}

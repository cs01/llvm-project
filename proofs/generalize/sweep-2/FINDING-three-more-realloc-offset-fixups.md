# Three more: nghttp2, libarchive, curl's example

Found by a text-level scanner (since removed, see ../../PATTERNS.md) over ten
freshly cloned trees. All three are the same shape as
[expat `storeRawNames`](../expat/FINDING-storerawnames-freed-pointer.md), all
three are past the allocation-failure guard, none is reported upstream.

The idiom they share is worth naming, because it is clearly a *learned pattern*
rather than three independent slips: **re-base every cached interior pointer by
adding the new block to its old offset.** The offset is computed by subtracting
the old base, which is exactly the pointer `realloc` just freed.

## 1. nghttp2 `nghttp2_buf_reserve` -- the densest instance found so far

`lib/nghttp2_buf.c`, in the library itself:

```c
ptr = nghttp2_mem_realloc(mem, buf->begin, new_cap);
if (ptr == NULL) {
  return NGHTTP2_ERR_NOMEM;
}

buf->pos  = ptr + (buf->pos  - buf->begin);
buf->last = ptr + (buf->last - buf->begin);
buf->mark = ptr + (buf->mark - buf->begin);
buf->begin = ptr;
```

Six reads of indeterminate pointer values in three lines: each subtraction has
`buf->begin` on the right, freed by the call above, and `buf->pos`, `buf->last`
and `buf->mark` all point into that same freed block on the left. `buf->begin`
is repaired one line too late, three times over.

nghttp2 is the HTTP/2 implementation inside curl, nginx, Apache httpd and
Node.js, so this is the widest-reach instance in the ledger.

## 2. libarchive `lafe_line_reader` -- same idiom, two subtractions

`libarchive_fe/line_reader.c`:

```c
p = realloc(lr->buff, new_buff_size + 1);
if (p == NULL)
        lafe_errc(1, ENOMEM, "Line too long in %s", lr->pathname);
lr->buff_end  = p + (lr->buff_end  - lr->buff);
lr->line_end  = p + (lr->line_end  - lr->buff);
lr->line_start = lr->buff = p;
```

`lafe_errc` exits, so everything after the guard is the success path. This is
the bsdtar/bsdcpio front-end rather than the core library.

## 3. curl `log_failed_transfers.c` -- in the examples, which is the point

`docs/examples/`:

```c
newbuf = realloc(mem->buf, newsize);
if(!newbuf)
  return -1;
if(mem->recent && mem->buf != newbuf)
  mem->recent = newbuf + (mem->recent - mem->buf);
mem->buf = newbuf;
```

Both a comparison and a subtraction against the freed `mem->buf`. Not in
libcurl, so the direct impact is nil -- and it is arguably the *most* worth
fixing of the three, because example code in a project of curl's reach is
copied into other people's programs verbatim. It is where an idiom like this
gets taught.

## The fix is the same in all three

Take the offsets before the call, when the base is still a valid pointer:

```c
const size_t off_pos  = buf->pos  - buf->begin;
const size_t off_last = buf->last - buf->begin;
const size_t off_mark = buf->mark - buf->begin;
ptr = nghttp2_mem_realloc(mem, buf->begin, new_cap);
if (ptr == NULL) return NGHTTP2_ERR_NOMEM;
buf->begin = ptr;
buf->pos = ptr + off_pos;  buf->last = ptr + off_last;  buf->mark = ptr + off_mark;
```

Integers survive a reallocation; pointers into the old block do not.

## Sweep coverage

Ten trees cloned for this pass. What was actually examined, which is the only
thing that makes a zero meaningful:

| Tree | C files | realloc-shaped calls | Hits |
|---|---|---|---|
| curl | 626 | 21 | 1 (example only) |
| libgit2 | 527 | 28 | 0 |
| libjpeg-turbo | 322 | 8 | 0 |
| libarchive | 239 | 33 | 1 |
| nghttp2 | 160 | 3 | 1 |
| mbedtls | 116 | 2 | 0 |
| brotli | 116 | **0** | -- uses its own pool allocator, no realloc |
| libpng | 101 | **0** | -- `png_malloc`/`png_free`, no realloc |
| pcre2 | 56 | 2 | 0 |
| lz4 | 53 | 6 | 0 |

**lz4 came back clean**, which is worth recording: it was predicted to carry the
zstd idioms (same author lineage) and does not carry this one.

A process note, caught by the coverage line and not by me: the first run of this
sweep raced the background clones and reported brotli, libarchive and libgit2 as
`0 C files`. libarchive has 699 and libgit2 792. Without that line the sweep
would have recorded three trees as clean that had not been read at all -- and
libarchive is where one of these three findings is.

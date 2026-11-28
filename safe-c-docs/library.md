# Library functions that are errors

Safe C keeps the C library. The compiler knows what each function frees, keeps and returns; of 118 C99 functions, 98 are checked as
they are (`clang/utils/safe-c/libc/check.sh`). These are errors that say what to use instead. The table is made from the compiler's
own list.

A library in a system header can say what it frees in its declarations, and the compiler reads that before its own list. Two forms
work: GCC's `malloc(deallocator, index)`, which glibc's headers already use, and Clang's `ownership_returns` and `ownership_takes`:

```c
void conn_close(int flags, struct conn *c);
struct conn *conn_open(const char *host) __attribute__((malloc(conn_close, 2)));
```

What `conn_open` returns is an owner, and only `conn_close` may free it. Freeing it with something else is an error, and so is
`pclose` on what `fopen` returned.

<!-- generated:libc -->
Each is a compile error that says what to use (47 functions):

| Function | Why | Use instead |
|---|---|---|
| `gets` | has no size | `fgets` |
| `strcpy`, `stpcpy` | has no size | `strlcpy` |
| `strncpy`, `stpncpy` | may leave no terminator | `strlcpy` |
| `strcat`, `strncat` | has no size | `strlcat` |
| `sprintf`, `vsprintf` | has no size | `snprintf`, `vsnprintf` |
| `wcscpy` | has no size | `wcslcpy` |
| `strtok`, `strtok_r` | writes into the string and remembers it between calls | `str_before` and `str_after` (text.h), or `strcspn` for where each word ends |
| `strsep` | writes into the string and moves your pointer to it | `str_before` and `str_after` (text.h), or `strcspn` for where each part ends |
| `realpath` | writes a path of any length into the buffer it is given | `realpath(path, NULL)`, and free the result |
| `getwd` | writes a path of any length | `getcwd(buffer, sizeof buffer)` |
| `tmpnam`, `tempnam`, `mktemp` | names a file that another program may create first | `mkstemp` |
| `asctime` | writes into one buffer the whole program shares | `strftime` |
| `asctime_r` | writes a string of any length | `strftime` |
| `ctime_r` | writes a string of any length | `localtime_r` and `strftime` |
| `putenv` | keeps the string it is given | a small function marked `__unsafe` that calls `setenv`, before any `getenv` |
| `setenv`, `unsetenv` | may free what 'getenv' returned | a small function marked `__unsafe` that calls it, before any `getenv` |
| `strxfrm` | returns a length that may be more than it wrote | `strcoll` |
| `setbuf` | does not know how big the buffer is | the buffer the stream has |
| `setvbuf`, `setbuffer` | keeps the buffer it is given | the buffer the stream has |
| `gethostbyname` | returns memory the next call changes | a small function marked `__unsafe` that calls `getaddrinfo` |
| `gethostbyaddr` | returns memory the next call changes | a small function marked `__unsafe` that calls `getnameinfo` |
| `inet_ntoa` | returns memory the next call changes | a small function marked `__unsafe` that calls `inet_ntop` |
| `getlogin` | returns memory the next call changes | `getlogin_r` |
| `ttyname` | returns memory the next call changes | `ttyname_r` |
| `ptsname` | returns memory the next call changes | `ptsname_r` |
| `swab` | reads and writes a length it cannot check | a loop |
| `valloc` | returns memory that is not zeroed | `calloc` |
| `memccpy` | returns a pointer past what it wrote | a loop |
| `vfork` | shares the parent's memory | a small function marked `__unsafe` that calls `posix_spawn` |
| `brk`, `sbrk` | takes memory away from under 'malloc' | `malloc` |
| `freopen` | closes its stream even when it fails | `fclose` and `fopen` |
| `localeconv` | returns memory the next call changes | a small function marked `__unsafe` that copies what you need |
| `setjmp`, `longjmp` | goes back past frees and checks | a result that says what failed |
| `atoi`, `atol` | is undefined for a number too big | `strtol` |
| `atoll` | is undefined for a number too big | `strtoll` |
<!-- end generated:libc -->

Where C has no safe way to do something, [safe-c-std](../safe-c-std) has a small header for it.

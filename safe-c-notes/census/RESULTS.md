# Safe C census

| project | LOC | TUs | total | err/kLOC | ownership | null | terminated-string | bounds-single | bounds-other | unchecked-libc | casts-types | init | format-varargs | escape | other | notes |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| llhttp | 11322 | 3 | 1434 | 126.66 | 0 | 0 | 219 | 639 | 0 | 0 | 0 | 0 | 0 | 576 | 0 | release branch (prebuilt C) |
| parson | 2496 | 1 | 278 | 111.38 | 0 | 2 | 45 | 214 | 3 | 0 | 13 | 0 | 1 | 0 | 0 |  |
| lua | 26062 | 32 | 2874 | 110.28 | 2 | 1 | 210 | 1154 | 123 | 0 | 1373 | 4 | 7 | 0 | 0 |  |
| libyaml | 10487 | 8 | 1065 | 101.55 | 2 | 0 | 42 | 832 | 7 | 0 | 182 | 0 | 0 | 0 | 0 | YAML_VERSION_* via -D |
| bearssl | 70415 | 294 | 7027 | 99.79 | 3 | 0 | 11 | 6743 | 17 | 0 | 246 | 1 | 0 | 0 | 6 |  |
| sds | 1558 | 1 | 118 | 75.74 | 3 | 0 | 20 | 74 | 20 | 0 | 0 | 0 | 1 | 0 | 0 |  |
| expat | 15316 | 3 | 977 | 63.79 | 0 | 0 | 564 | 393 | 18 | 0 | 2 | 0 | 0 | 0 | 0 | expat_config.h from cmake configure into build/expat; xmltok_impl/ns are #included so not standalone TUs |
| stb_image | 6968 | 1 | 438 | 62.86 | 7 | 1 | 15 | 313 | 102 | 0 | 0 | 0 | 0 | 0 | 0 | one-line TU |
| cmark | 16449 | 19 | 889 | 54.05 | 1 | 3 | 35 | 768 | 5 | 0 | 77 | 0 | 0 | 0 | 0 | cmake configure into build/cmark |
| lz4 | 12002 | 5 | 587 | 48.91 | 7 | 0 | 142 | 398 | 3 | 0 | 34 | 0 | 0 | 1 | 2 |  |
| redis | 16783 | 7 | 638 | 38.01 | 7 | 0 | 43 | 545 | 29 | 0 | 4 | 0 | 1 | 0 | 9 | 7 files only |
| inih | 461 | 1 | 12 | 26.03 | 0 | 0 | 5 | 7 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |  |
| sqlite | 268344 | 1 | 6010 | 22.4 | 0 | 9 | 816 | 3014 | 238 | 0 | 1921 | 1 | 0 | 0 | 11 | amalgamation 3.54.1 |
| zlib | 22711 | 15 | 395 | 17.39 | 1 | 7 | 8 | 363 | 6 | 0 | 0 | 0 | 1 | 9 | 0 |  |

## Totals by category

- bounds-single: 15457
- casts-types: 3852
- terminated-string: 2175
- escape: 586
- bounds-other: 571
- ownership: 33
- other: 28
- null: 23
- format-varargs: 11
- init: 6
- unchecked-libc: 0

## Messages by category (all projects, top per project)

### ownership
- **lua** (2)
  - 1x `'X' frees its parameter 'X', but callers are not told and may use it afterwards; declare it 'X' (safec.h: 'X')` e.g. lauxlib.c:1065:10
  - 1x `parameter in intra-TU function should be marked [[clang::lifetimebound]]` e.g. lgc.c:163:41
- **libyaml** (2)
  - 2x `'X' frees its parameter 'X', but callers are not told and may use it afterwards; declare it 'X' (safec.h: 'X')` e.g. src/api.c:43:26
- **bearssl** (3)
  - 3x `parameter in intra-TU function should be marked [[clang::lifetimebound]]` e.g. inc/bearssl_x509.h:1094:54
- **sds** (3)
  - 3x `'X' frees its parameter 'X', but callers are not told and may use it afterwards; declare it 'X' (safec.h: 'X')` e.g. sds.c:889:12
- **stb_image** (7)
  - 7x `'X' frees its parameter 'X', but callers are not told and may use it afterwards; declare it 'X' (safec.h: 'X')` e.g. stb_image.h:1166:14
- **cmark** (1)
  - 1x `'X' frees its parameter 'X', but callers are not told and may use it afterwards; declare it 'X' (safec.h: 'X')` e.g. src/cmark.c:23:27
- **lz4** (7)
  - 7x `'X' frees its parameter 'X', but callers are not told and may use it afterwards; declare it 'X' (safec.h: 'X')` e.g. lib/lz4file.c:68:21
- **redis** (7)
  - 4x `'X' frees its parameter 'X', but callers are not told and may use it afterwards; declare it 'X' (safec.h: 'X')` e.g. src/zmalloc.c:32:10
  - 3x `parameter in cross-TU function should be marked [[clang::lifetimebound]]` e.g. src/adlist.h:57:33
- **zlib** (1)
  - 1x `'X' frees its parameter 'X', but callers are not told and may use it afterwards; declare it 'X' (safec.h: 'X')` e.g. zutil.c:332:10

### null
- **parson** (2)
  - 2x `initializing a variable of nonnull type 'X' with null` e.g. parson.c:100:36
- **lua** (1)
  - 1x `initializing a variable of nonnull type 'X' (aka 'X') with null` e.g. lapi.c:1434:38
- **stb_image** (1)
  - 1x `initializing a variable of nonnull type 'X' (aka 'X') with null` e.g. stb_image.h:1031:13
- **cmark** (3)
  - 2x `assigning nullable pointer to nonnull member 'X'` e.g. src/iterator.c:24:18
  - 1x `returning nullable pointer from function with nonnull return type` e.g. src/iterator.c:17:12
- **sqlite** (9)
  - 5x `initializing a variable of nonnull type 'X' (aka 'X') with null` e.g. sqlite3.c:28020:43
  - 4x `initializing a variable of nonnull type 'X' with null` e.g. sqlite3.c:7243:18
- **zlib** (7)
  - 6x `assigning nullable pointer to nonnull member 'X'` e.g. compress.c:38:19
  - 1x `returning nullable pointer from function with nonnull return type` e.g. zutil.c:326:12

### terminated-string
- **llhttp** (219)
  - 216x `assigning to 'X' (aka 'X') from incompatible type 'X' is an unsafe operation; use 'X' or 'X' to perform this conversion` e.g. src/llhttp.c:1261:24
  - 1x `pointer arithmetic on 'X' pointer 'X' can only increase the value by one` e.g. src/api.c:126:49
  - 1x `passing 'X' (aka 'X') to parameter of incompatible type 'X' requires a linear search for the terminator; use 'X' to perform this conversion explicitly` e.g. src/llhttp.c:10175:39
- **parson** (45)
  - 13x `array subscript on 'X' pointer 'X' is not allowed` e.g. parson.c:321:26
  - 8x `passing 'X' to parameter of incompatible type 'X' (aka 'X') is an unsafe operation; use 'X' or 'X' to perform this conversion` e.g. parson.c:260:26
  - 8x `pointer arithmetic on 'X' pointer 'X' can only increase the value by one` e.g. parson.c:402:31
- **lua** (210)
  - 57x `pointer arithmetic on 'X' pointer 'X' can only increase the value by one` e.g. lbaselib.c:64:3
  - 42x `assigning to 'X' from incompatible type 'X' (aka 'X') requires a linear search for the terminator; use 'X' to perform this conversion explicitly` e.g. lbaselib.c:104:7
  - 24x `returning 'X' from a function with incompatible result type 'X' (aka 'X') is an unsafe operation; use 'X' or 'X' to perform this conversion` e.g. lauxlib.c:770:10
- **libyaml** (42)
  - 39x `passing 'X' to parameter of incompatible type 'X' (aka 'X') is an unsafe operation; use 'X' or 'X' to perform this conversion` e.g. src/dumper.c:322:34
  - 1x `returning 'X' (aka 'X') from a function with incompatible result type 'X' (aka 'X') requires a linear search for the terminator; use 'X' to perform this conversion explicitly` e.g. src/api.c:66:12
  - 1x `assigning to 'X' (aka 'X') from incompatible type 'X' (aka 'X') requires a linear search for the terminator; use 'X' to perform this conversion explicitly` e.g. src/emitter.c:1816:5
- **bearssl** (11)
  - 3x `passing 'X' (aka 'X') to parameter of incompatible type 'X' (aka 'X') requires a linear search for the terminator; use 'X' to perform this conversion explicitly` e.g. src/codec/pemenc.c:105:12
  - 2x `returning 'X' from a function with incompatible result type 'X' (aka 'X') is an unsafe operation; use 'X' or 'X' to perform this conversion` e.g. inc/bearssl_ssl.h:1886:9
  - 2x `array subscript on 'X' pointer 'X' is not allowed` e.g. src/ssl/prf.c:44:27
- **sds** (20)
  - 9x `pointer arithmetic on 'X' pointer 'X' can only increase the value by one` e.g. sds.c:859:31
  - 6x `passing 'X' (aka 'X') to parameter of incompatible type 'X' requires a linear search for the terminator; use 'X' to perform this conversion explicitly` e.g. sds.c:156:22
  - 2x `passing 'X' to parameter of incompatible type 'X' (aka 'X') is an unsafe operation; use 'X' or 'X' to perform this conversion` e.g. sds.c:185:29
- **expat** (564)
  - 314x `pointer arithmetic on 'X' pointer 'X' can only increase the value by one` e.g. lib/xmlrole.c:160:36
  - 182x `array subscript on 'X' pointer 'X' is not allowed` e.g. lib/xmltok.c:147:10
  - 27x `assigning to 'X' (aka 'X') from incompatible type 'X' is an unsafe operation; use 'X' or 'X' to perform this conversion` e.g. lib/xmlparse.c:1531:23
- **stb_image** (15)
  - 5x `assigning to 'X' (aka 'X') from incompatible type 'X' (aka 'X') requires a linear search for the terminator; use 'X' to perform this conversion explicitly` e.g. stb_image.h:4605:14
  - 5x `pointer arithmetic on 'X' pointer 'X' can only increase the value by one` e.g. stb_image.h:4606:32
  - 2x `array subscript on 'X' pointer 'X' is not allowed` e.g. stb_image.h:6883:23
- **cmark** (35)
  - 11x `passing 'X' to parameter of incompatible type 'X' (aka 'X') is an unsafe operation; use 'X' or 'X' to perform this conversion` e.g. src/commonmark.c:151:36
  - 6x `assigning to 'X' from incompatible type 'X' (aka 'X') requires a linear search for the terminator; use 'X' to perform this conversion explicitly` e.g. src/commonmark.c:280:10
  - 6x `returning 'X' from a function with incompatible result type 'X' (aka 'X') is an unsafe operation; use 'X' or 'X' to perform this conversion` e.g. src/node.c:314:12
- **lz4** (142)
  - 50x `pointer arithmetic on 'X' pointer 'X' can only increase the value by one` e.g. lib/lz4.c:1031:37
  - 23x `passing 'X' (aka 'X') to parameter of incompatible type 'X' (aka 'X') requires a linear search for the terminator; use 'X' to perform this conversion explicitly` e.g. lib/lz4.c:1271:60
  - 16x `passing 'X' (aka 'X') to parameter of incompatible type 'X' requires a linear search for the terminator; use 'X' to perform this conversion explicitly` e.g. lib/lz4.c:1763:40
- **redis** (43)
  - 17x `passing 'X' to parameter of incompatible type 'X' (aka 'X') is an unsafe operation; use 'X' or 'X' to perform this conversion` e.g. src/sds.c:122:47
  - 9x `pointer arithmetic on 'X' pointer 'X' can only increase the value by one` e.g. src/sds.c:931:31
  - 8x `passing 'X' (aka 'X') to parameter of incompatible type 'X' requires a linear search for the terminator; use 'X' to perform this conversion explicitly` e.g. src/sds.c:212:22
- **inih** (5)
  - 2x `returning 'X' (aka 'X') from a function with incompatible result type 'X' requires a linear search for the terminator; use 'X' to perform this conversion explicitly` e.g. ini.c:59:12
  - 2x `array subscript on 'X' pointer 'X' is not allowed` e.g. ini.c:88:36
  - 1x `converting 'X' (aka 'X') to 'X' (aka 'X') can treat memory as the wrong type` e.g. ini.c:275:34
- **sqlite** (816)
  - 369x `passing 'X' to parameter of incompatible type 'X' (aka 'X') is an unsafe operation; use 'X' or 'X' to perform this conversion` e.g. sqlite3.c:33830:41
  - 192x `array subscript on 'X' pointer 'X' is not allowed` e.g. sqlite3.c:25879:23
  - 62x `pointer arithmetic on 'X' pointer 'X' can only increase the value by one` e.g. sqlite3.c:25900:5
- **zlib** (8)
  - 3x `passing 'X' to parameter of incompatible type 'X' (aka 'X') is an unsafe operation; use 'X' or 'X' to perform this conversion` e.g. gzlib.c:212:22
  - 3x `passing 'X' (aka 'X') to parameter of incompatible type 'X' requires a linear search for the terminator; use 'X' to perform this conversion explicitly` e.g. gzlib.c:294:20
  - 2x `returning 'X' from a function with incompatible result type 'X' (aka 'X') is an unsafe operation; use 'X' or 'X' to perform this conversion` e.g. gzlib.c:530:12

### bounds-single
- **llhttp** (639)
  - 444x `pointer arithmetic on single pointer 'X' is out of bounds; consider adding 'X' to 'X'` e.g. src/llhttp.c:256:21
  - 99x `assigning to 'X' from incompatible type 'X' casts away 'X' qualifier; use 'X' or 'X' to perform this conversion` e.g. src/llhttp.c:1216:19
  - 64x `cannot assign to indexable pointer with type 'X' from __single pointer to incomplete type 'X'; consider declaring pointer 'X' as 'X'` e.g. src/llhttp.c:7202:11
- **parson** (214)
  - 123x `array subscript on single pointer 'X' must use a constant index of 0 to be in bounds` e.g. parson.c:257:23
  - 68x `pointer arithmetic on single pointer 'X' is out of bounds; consider adding 'X' to 'X'` e.g. parson.c:251:13
  - 12x `converting 'X' to 'X' (aka 'X') can treat memory as the wrong type` e.g. parson.c:450:28
- **lua** (1154)
  - 363x `pointer arithmetic on single pointer 'X' is out of bounds; consider adding 'X' to 'X'` e.g. lauxlib.c:580:12
  - 187x `converting 'X' to 'X' (aka 'X') can treat memory as the wrong type` e.g. lauxlib.c:485:15
  - 185x `array subscript on single pointer 'X' must use a constant index of 0 to be in bounds` e.g. ldblib.c:360:33
- **libyaml** (832)
  - 522x `pointer arithmetic on single pointer 'X' is out of bounds; consider adding 'X' to 'X'` e.g. src/dumper.c:177:21
  - 134x `array subscript on single pointer 'X' must use a constant index of 0 to be in bounds` e.g. src/dumper.c:179:58
  - 61x `converting 'X' (aka 'X') to 'X' can treat memory as the wrong type` e.g. src/loader.c:286:10
- **bearssl** (6743)
  - 4740x `array subscript on single pointer 'X' must use a constant index of 0 to be in bounds` e.g. inc/bearssl_ssl.h:1349:61
  - 701x `converting 'X' to 'X' can treat memory as the wrong type` e.g. src/aead/gcm.c:129:10
  - 639x `pointer arithmetic on single pointer 'X' is out of bounds` e.g. src/aead/gcm.c:129:10
- **sds** (74)
  - 37x `pointer arithmetic on single pointer 'X' is out of bounds; consider adding 'X' to 'X'` e.g. sds.h:93:20
  - 31x `array subscript on single pointer 'X' must use a constant index of 0 to be in bounds` e.g. sds.h:88:28
  - 5x `pointer arithmetic on single pointer 'X' is out of bounds` e.g. sds.h:135:37
- **expat** (393)
  - 133x `array subscript on single pointer 'X' must use a constant index of 0 to be in bounds` e.g. lib/siphash.h:151:15
  - 103x `converting 'X' (aka 'X') to 'X' can treat memory as the wrong type` e.g. lib/xmltok_impl.c:154:15
  - 37x `pointer arithmetic on single pointer 'X' is out of bounds; consider adding 'X' to 'X'` e.g. lib/siphash.h:215:11
- **stb_image** (313)
  - 125x `array subscript on single pointer 'X' must use a constant index of 0 to be in bounds` e.g. stb_image.h:1267:35
  - 68x `returning 'X' from a function with incompatible result type 'X' casts away 'X' qualifier; use 'X' or 'X' to perform this conversion` e.g. stb_image.h:1207:45
  - 46x `pointer arithmetic on single pointer 'X' is out of bounds; consider adding 'X' to 'X'` e.g. stb_image.h:1680:18
- **cmark** (768)
  - 587x `pointer arithmetic on single pointer 'X' is out of bounds; consider adding 'X' to 'X'` e.g. src/houdini_href_e.c:70:28
  - 143x `array subscript on single pointer 'X' must use a constant index of 0 to be in bounds` e.g. src/houdini_href_e.c:66:37
  - 17x `converting 'X' to 'X' (aka 'X') can treat memory as the wrong type` e.g. src/blocks.c:76:7
- **lz4** (398)
  - 135x `pointer arithmetic on single pointer 'X' is out of bounds; consider adding 'X' to 'X'` e.g. lib/lz4file.c:249:7
  - 117x `converting 'X' to 'X' (aka 'X') can treat memory as the wrong type` e.g. lib/lz4file.c:168:24
  - 54x `array subscript on single pointer 'X' must use a constant index of 0 to be in bounds` e.g. lib/lz4file.c:237:76
- **redis** (545)
  - 230x `array subscript on single pointer 'X' must use a constant index of 0 to be in bounds` e.g. src/sds.h:69:28
  - 182x `pointer arithmetic on single pointer 'X' is out of bounds; consider adding 'X' to 'X'` e.g. src/sds.h:102:20
  - 26x `converting 'X' to 'X' can treat memory as the wrong type` e.g. src/adlist.c:67:17
- **inih** (7)
  - 3x `converting 'X' to 'X' can treat memory as the wrong type` e.g. ini.c:275:34
  - 2x `array subscript on single pointer 'X' must use a constant index of 0 to be in bounds` e.g. ini.c:89:13
  - 1x `pointer arithmetic on single pointer 'X' is out of bounds; consider adding 'X' to 'X'` e.g. ini.c:49:50
- **sqlite** (3014)
  - 1642x `array subscript on single pointer 'X' must use a constant index of 0 to be in bounds` e.g. sqlite3.c:25578:29
  - 284x `converting 'X' to 'X' (aka 'X') can treat memory as the wrong type` e.g. sqlite3.c:25624:38
  - 164x `converting 'X' (aka 'X') to 'X' (aka 'X') can treat memory as the wrong type` e.g. sqlite3.c:41111:21
- **zlib** (363)
  - 181x `array subscript on single pointer 'X' must use a constant index of 0 to be in bounds` e.g. gzwrite.c:350:22
  - 82x `pointer arithmetic on single pointer 'X' is out of bounds; consider adding 'X' to 'X'` e.g. crc32.c:645:16
  - 35x `converting 'X' to 'X' (aka 'X') can treat memory as the wrong type` e.g. gzclose.c:17:13

### bounds-other
- **parson** (3)
  - 1x `'X' is unavailable: unavailable with -fbounds-safety. Use "X" instead.` e.g. parson.c:298:18
  - 1x `passing 'X' to 'X' drops its bounds: the parameter is 'X', so 'X' reads or writes through it unchecked; call a function whose parameter declares its size (for strings: memcpy, memcmp, strlcpy, snprintf)` e.g. parson.c:586:57
  - 1x `converting 'X' to 'X' can treat memory as the wrong type` e.g. parson.c:1411:26
- **lua** (123)
  - 94x `converting 'X' (aka 'X') to 'X' (aka 'X') can treat memory as the wrong type` e.g. lcode.c:99:7
  - 6x `initializing 'X' (aka 'X') with an expression of incompatible type 'X' (aka 'X') casts away 'X' qualifier; use 'X' or 'X' to perform this conversion` e.g. lauxlib.c:630:11
  - 5x `converting 'X' (aka 'X') to 'X' can treat memory as the wrong type` e.g. lapi.c:1092:12
- **libyaml** (7)
  - 5x `converting 'X' to 'X' can treat memory as the wrong type` e.g. src/api.c:130:12
  - 1x `'X' is unavailable: unavailable with -fbounds-safety. Use "X" instead.` e.g. src/dumper.c:254:5
  - 1x `passing 'X' to 'X' drops its bounds: the parameter is 'X', so 'X' reads or writes through it unchecked; call a function whose parameter declares its size (for strings: memcpy, memcmp, strlcpy, snprintf)` e.g. src/dumper.c:254:13
- **bearssl** (17)
  - 17x `initializing 'X' with an expression of incompatible type 'X' casts away 'X' qualifier; use 'X' or 'X' to perform this conversion` e.g. src/ssl/ssl_hs_client.c:1184:8
- **sds** (20)
  - 11x `converting 'X' to 'X' can treat memory as the wrong type` e.g. sds.c:105:9
  - 8x `initializing 'X' with an expression of incompatible type 'X' casts away 'X' qualifier; use 'X' or 'X' to perform this conversion` e.g. sds.h:111:13
  - 1x `assigning to 'X' from incompatible type 'X' casts away 'X' qualifier; use 'X' or 'X' to perform this conversion` e.g. sds.c:648:21
- **expat** (18)
  - 8x `converting 'X' to 'X' (aka 'X') can treat memory as the wrong type` e.g. lib/xmlparse.c:948:4
  - 4x `converting 'X' to 'X' can treat memory as the wrong type` e.g. lib/xmlparse.c:965:10
  - 2x `converting 'X' (aka 'X') to 'X' (aka 'X') can treat memory as the wrong type` e.g. lib/xmlparse.c:4003:55
- **stb_image** (102)
  - 90x `converting 'X' (aka 'X') to 'X' makes up an address that no bounds or lifetime check can follow; keep the pointer instead of the integer (or an index into the array it points into), or convert to 'X' if only the address value is needed` e.g. stb_image.h:1207:45
  - 4x `converting 'X' to 'X' (aka 'X') can treat memory as the wrong type` e.g. stb_image.h:1330:38
  - 4x `passing 'X' to 'X' drops its bounds: the parameter is 'X', so 'X' reads or writes through it unchecked; call a function whose parameter declares its size (for strings: memcpy, memcmp, strlcpy, snprintf)` e.g. stb_image.h:7045:16
- **cmark** (5)
  - 4x `passing 'X' to 'X' drops its bounds: the parameter is 'X', so 'X' reads or writes through it unchecked; call a function whose parameter declares its size (for strings: memcpy, memcmp, strlcpy, snprintf)` e.g. src/commonmark.c:147:15
  - 1x `initializing 'X' with an expression of incompatible type 'X' casts away 'X' qualifier; use 'X' or 'X' to perform this conversion` e.g. src/chunk.h:50:24
- **lz4** (3)
  - 2x `converting 'X' (aka 'X') to 'X' makes up an address that no bounds or lifetime check can follow; keep the pointer instead of the integer (or an index into the array it points into), or convert to 'X' if only the address value is needed` e.g. lib/lz4.c:3011:12
  - 1x `initializing 'X' with an expression of incompatible type 'X' casts away 'X' qualifier; use 'X' or 'X' to perform this conversion` e.g. lib/lz4hc.c:2196:32
- **redis** (29)
  - 16x `converting 'X' to 'X' can treat memory as the wrong type` e.g. src/sds.c:122:28
  - 8x `initializing 'X' with an expression of incompatible type 'X' casts away 'X' qualifier; use 'X' or 'X' to perform this conversion` e.g. src/sds.h:119:13
  - 3x `passing 'X' to 'X' drops its bounds: the parameter is 'X', so 'X' reads or writes through it unchecked; call a function whose parameter declares its size (for strings: memcpy, memcmp, strlcpy, snprintf)` e.g. src/zmalloc.c:260:25
- **sqlite** (238)
  - 66x `converting 'X' (aka 'X') to 'X' (aka 'X') can treat memory as the wrong type` e.g. sqlite3.c:48240:38
  - 42x `converting 'X' (aka 'X') to 'X' can treat memory as the wrong type` e.g. sqlite3.c:55206:19
  - 33x `initializing 'X' with an expression of incompatible type 'X' casts away 'X' qualifier; use 'X' or 'X' to perform this conversion` e.g. sqlite3.c:87884:19
- **zlib** (6)
  - 4x `converting 'X' to 'X' can treat memory as the wrong type` e.g. gzread.c:661:15
  - 1x `converting 'X' to 'X' (aka 'X') can treat memory as the wrong type` e.g. gzlib.c:289:12
  - 1x `assigning to 'X' from incompatible type 'X' casts away 'X' qualifier; use 'X' or 'X' to perform this conversion` e.g. gzwrite.c:470:10

### unchecked-libc

### casts-types
- **parson** (13)
  - 12x `accessing 'X' of a union whose members have different types can read memory as a pointer it does not hold; use a struct with a tag field that says which member is live` e.g. parson.c:789:22
  - 1x `conversion between pointers to functions with incompatible bound attributes` e.g. parson.c:96:27
- **lua** (1373)
  - 1373x `accessing 'X' of a union whose members have different types can read memory as a pointer it does not hold; use a struct with a tag field that says which member is live` e.g. lcode.c:99:36
- **libyaml** (182)
  - 182x `accessing 'X' of a union whose members have different types can read memory as a pointer it does not hold; use a struct with a tag field that says which member is live` e.g. src/dumper.c:140:5
- **bearssl** (246)
  - 246x `accessing 'X' of a union whose members have different types can read memory as a pointer it does not hold; use a struct with a tag field that says which member is live` e.g. inc/bearssl_hmac.h:172:17
- **expat** (2)
  - 2x `conversion between pointers to functions with incompatible bound attributes` e.g. lib/xmlparse.c:1373:26
- **cmark** (77)
  - 76x `accessing 'X' of a union whose members have different types can read memory as a pointer it does not hold; use a struct with a tag field that says which member is live` e.g. src/blocks.c:272:59
  - 1x `conversion between pointers to functions with incompatible bound attributes` e.g. src/cmark.c:31:55
- **lz4** (34)
  - 34x `accessing 'X' of a union whose members have different types can read memory as a pointer it does not hold; use a struct with a tag field that says which member is live` e.g. lib/lz4.c:1582:65
- **redis** (4)
  - 3x `accessing 'X' of a union whose members have different types can read memory as a pointer it does not hold; use a struct with a tag field that says which member is live` e.g. src/dict.c:1457:11
  - 1x `converting 'X' to 'X' (aka 'X') can treat memory as the wrong type` e.g. src/dict.c:331:12
- **sqlite** (1921)
  - 1918x `accessing 'X' of a union whose members have different types can read memory as a pointer it does not hold; use a struct with a tag field that says which member is live` e.g. sqlite3.c:33809:65
  - 3x `converting 'X' to 'X' (aka 'X') can treat memory as the wrong type` e.g. sqlite3.c:46276:17

### init
- **lua** (4)
  - 4x `variable 'X' may be uninitialized when used here` e.g. lobject.c:180:42
- **bearssl** (1)
  - 1x `initializer element is not a compile-time constant` e.g. src/ssl/ssl_hs_client.c:174:2
- **sqlite** (1)
  - 1x `initializer element is not a compile-time constant` e.g. sqlite3.c:135864:46

### format-varargs
- **parson** (1)
  - 1x `format string is not a string literal` e.g. parson.c:298:30
- **lua** (7)
  - 7x `format string is not a string literal` e.g. lstrlib.c:1203:40
- **sds** (1)
  - 1x `format string is not a string literal` e.g. sds.c:553:44
- **redis** (1)
  - 1x `format string is not a string literal` e.g. src/sds.c:611:44
- **zlib** (1)
  - 1x `format string is not a string literal` e.g. gzwrite.c:485:40

### escape
- **llhttp** (576)
  - 576x `'X' can switch off Safe C'X'__unsafe'` e.g. src/llhttp.c:1245:7
- **lz4** (1)
  - 1x `inline assembly can switch off Safe C'X'__unsafe'` e.g. lib/xxhash.c:277:5
- **zlib** (9)
  - 9x `inline assembly can switch off Safe C'X'__unsafe'` e.g. crc32.c:646:9

### other
- **bearssl** (6)
  - 6x `cannot extract the upper bound of 'size_t' (aka 'unsigned long') because it is not a pointer` e.g. src/ssl/ssl_hs_client.c:1765:53
- **lz4** (2)
  - 2x `cannot extract the upper bound of 'size_t' (aka 'unsigned long') because it is not a pointer` e.g. lib/lz4.c:1986:61
- **redis** (9)
  - 8x `'extend_to_usable' is marked 'alloc_size', so callers give its result a type as fresh memory, but this returns something else; return the result of malloc, calloc or another 'alloc_size' function (or a local holding it), or null` e.g. src/zmalloc.c:240:12
  - 1x `cannot extract the upper bound of 'size_t' (aka 'unsigned long') because it is not a pointer` e.g. src/listpack.c:970:52
- **sqlite** (11)
  - 7x `pointee type of the pointer argument must be an integer or a non-wide pointer` e.g. sqlite3.c:113359:28
  - 2x `cannot extract the upper bound of 'size_t' (aka 'unsigned long') because it is not a pointer` e.g. sqlite3.c:80426:31
  - 2x `invalid application of 'sizeof' to an incomplete type 'FuncDef[]' (aka 'struct FuncDef[]')` e.g. sqlite3.c:137820:43

## System header errors

## Compiler crashes
none

## Skipped TUs (failed plain C)


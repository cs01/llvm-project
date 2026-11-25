# Ten functions chosen by hash (2026-11-24)

From 302 functions (of 2,622, in the 260 measured files of the 110-project sample) whose body stores a pointer in memory other than
a fresh allocation or null: the ten with the lowest sha1 of path and name. Written for the new ownership checker.

| function | result |
|---|---|
| shift_cells_down, shift_cells_right (sc-im) | accepted; copies of owners become moves, same algorithm |
| caca_set_dither_color (libcaca), main (icmptunnel) | accepted; a field pointing at a string literal becomes an enum or an array |
| load_data_super, load_data_swag (darknet) | accepted; structs with owners filled through a pointer, not returned by value (changes callers) |
| logger_start (jpeg2png) | rejected: no rule for fprintf; and the struct may not hold the caller's FILE (changes callers) |
| luaD_precall, ll_loadfunc (lua 5.1), kvs_new (streem) | not written: pointers into a moving stack, void ** registry and dlsym, a pointer boxed in a generic value |

The helper functions and types around them are stand-ins, so this checks that the logic can be said in the language. Nothing
here was run or compared with the original. 5 planted ownership bugs: see git log for the result.

# Native update JSON reader

Include `common/lib/libemberupdate/Makefile.inc` in a base program and include
`json.h`. The reader uses the base jsmn source and libc. It has no shared
library or runtime service dependency.

`eu_json_parse(bytes, length)` decodes one complete JSON value. `eu_json_file(path, limit)`
reads a bounded regular file without following its final symlink.
Both return an owned tree or NULL on failure. Call `eu_json_free` once on the root.
`eu_get(object, key)` returns a borrowed node. `eu_string(node)` returns
borrowed decoded text, or NULL for a missing or non-string node.

The update contracts accept signed 64-bit integers, strings, booleans, null,
arrays, and objects. The reader verifies UTF-8, decodes Unicode escape pairs,
and rejects duplicate decoded keys, embedded NULs, and fractions.
Limits are 16 MiB, 262144 tokens, 64 nesting levels, and 128 fields per object.
Callers validate their schema and use smaller input limits for profiles and plans.

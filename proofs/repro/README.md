# Reproducing every finding

One script per finding. Each prints the evidence rather than asserting it, and
each says where the source tree it needs comes from.

```sh
./run-all.sh                 # everything that has a repro
./01-zstd-initdstream.sh     # or one at a time
```

Source trees are found via environment variables, defaulting to `~/<name>`:

```sh
ZSTD=~/facebook/zstd  ZLIB=~/zlib  EXPAT=~/expat  SQLITE=~/sqlite  ./run-all.sh
```

A missing tree makes that script skip with a note, not fail. Clone what you
need:

```sh
git clone --depth 1 https://github.com/facebook/zstd  ~/facebook/zstd
git clone --depth 1 https://github.com/madler/zlib    ~/zlib
git clone --depth 1 https://github.com/libexpat/libexpat ~/expat
```

Needs `cbmc` **6 or newer** (Ubuntu's apt ships 5.95 and results are not
comparable) and a built `clang` from this tree for the scripts that use the
contract grammar. The proof assumptions, controls, and known limitations are
recorded beside each reproduction and in [`../FINDINGS.md`](../FINDINGS.md).

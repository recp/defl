# 🗜️ defl - deflate (WIP)

A high-performance DEFLATE/ZLIB decompression implementation in C. Optimized for minimal memory usage and maximum throughput.

- 📌 To get best performance try to compiler sources directly into project instead of external linking
- 📌 Feel free to report any bugs security issues by opening an issue

### Features

- 🔗 Option to inflate non-contiguous regions e.g. PNG IDATs
- ⚡ High-performance
- 🗜️ Full DEFLATE/ZLIB format support
- 💾 Minimal memory footprint
- 🔄 Streaming decompression support **(WIP)**
- 🛡️ Robust error handling
- 📦 No external dependencies


#### Usage 1: Use Non-Contiguous Chunk Api

```c
#include <defl/infl.h>

infl_stream_t st;
UnzResult     res;

infl_init(&st, dst, dstlen, 1); /* 1: INFL_ZLIB or jsut pass INFL_ZLIB */

...
infl_include(st, src, srclen);
infl_include(st, src, srclen);
...

/* decompress non-contiguous regions e.g. PNG IDATs without need to merge IDATs */
res = infl(st);
```

#### Usage 1: Use Contiguous Chunk Api

```c
#include <defl/infl.h>

UnzResult res;

/* decompress contiguous regions */
res = infl_buf(src, srclen, dst, dstlen flags);
```

## TODO

- [x] implement inflate
- [ ] implement deflate
- [ ] tests

## 🔨 Build

todo

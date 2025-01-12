# 🗜️ defl - deflate (WIP)

A high-performance DEFLATE/ZLIB decompression implementation in C. Optimized for minimal memory usage and maximum throughput.

- 📌 To get best performance try to compiler sources directly into project instead of external linking
- 📌 Feel free to report any bugs security issues by opening an issue

---

I'm using `defl` at [im](https://github.xom/recp/im) project to decode PNG IDATs. The main goal of the project is to allow decode IDATs without joining them. The size of IDATs may vary, for instance lot of 1byte IDAT may exist. So in this case a hybrid approach could be benefical to reduce memory usage while increase performance a bit. The hybrid approach ( joining small data and use chunks for large data ) may be provided by this project or by im or unz. **delf** also used in [unz](https://github.com/recp/unz) which is an another unzipping / compression library (WIP). 

### Design

Instead of embedding deflate anf huffman impl into my project, I decided to split **defl** and **huff** projects into separate repos to let others use these common requirements for some projects, also allowing each one improved independently over time 

<p align="center">
  <img src="https://github.com/user-attachments/assets/fad3d19c-e867-44d7-872a-600854e7b863" alt="" height="300px">
</p>

### Features

- 🔗 Option to inflate non-contiguous regions e.g. PNG IDATs
- ⚡ High-performance
- 🗜️ Full DEFLATE/ZLIB format support
- 💾 Minimal memory footprint
- 🔄 Streaming decompression support **(WIP)**
- 🛡️ Robust error handling

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
res = infl_buf(src, srclen, dst, dstlen 1); /* 1: INFL_ZLIB or jsut pass INFL_ZLIB */

/* or without detailed result check */
if (!infl_buf(src, srclen, dst, dstlen 1)) {
  goto err; // return -1 ... 
}
```

## TODO

- [x] implement inflate
- [ ] implement deflate
- [ ] tests

## 🔨 Build

todo

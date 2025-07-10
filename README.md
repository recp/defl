# 🗜️ defl - deflate (WIP)

<br>
<p align="center">
    <a href="https://github.com/recp/defl/actions/workflows/test.yml">
        <img src="https://github.com/recp/defl/actions/workflows/test.yml/badge.svg"
             alt="Build Status">
    </a>
    <a href="https://codecov.io/github/recp/defl" > 
     <img src="https://codecov.io/github/recp/defl/graph/badge.svg?token=L1FH51M848"/> 
    </a>
</p>

A high-performance, small DEFLATE/ZLIB decompression implementation in C. Optimized for minimal memory usage and maximum throughput.

- 📌 To get best performance try to compile sources directly into project instead of external linking
- 📌 Feel free to report any bugs security issues by opening an issue
- 📌 Any performance improvements are welcome!

---

I'm using `defl` at [im](https://github.xom/recp/im) project to decode PNG IDATs. **The main goal of the project is** to allow decode IDATs without joining them. The size of IDATs may vary, for instance lot of 1byte IDAT may exist. So in this case a hybrid approach could be benefical to reduce memory usage while increase performance a bit. The hybrid approach ( joining small data and use chunks for large data ) may be provided by this project or by im or unz. **delf** also used in [unz](https://github.com/recp/unz) which is an another unzipping / compression library (WIP). 

🚨 Don't use this in production until tests are ready

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

---

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

#### Usage 2: Use Stream Api ( TODO: WIP )

```c
#include <defl/infl.h>

infl_stream_t st;
UnzResult     res;

infl_init(&st, dst, dstlen, 1); /* 1: INFL_ZLIB or jsut pass INFL_ZLIB */

/* decompress when new data avail */
res = infl_stream(st, src, srclen);

...

/* decompress again when previous response is UNZ_UNFINISHED */
if (res == UNZ_UNFINISHED) {
  res = infl_stream(st, src, srclen);
}

...
if (res == UNZ_UNFINISHED) {
  res = infl_stream(st, src, srclen);
}
```

#### Usage 3: Use Contiguous Chunk Api

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


## Building

```bash
# create build directory
mkdir build && cd build

# configure and build
cmake ..
make -j$(nproc)
```

### or with tests

Test is optional to reduce build time if not needed, so it must be enabled explicitly `-DDEFL_USE_TEST=ON`.

```bash
# configure with tests enabled
cmake -DDEFL_USE_TEST=ON ..
make

# generate test data ( if not exists in test/data)
# make gen_test_data

# run tests
make test

# make fuzz
```

### with different configurations

```bash
# debug build
cmake -DCMAKE_BUILD_TYPE=Debug -DDEFL_USE_TEST=ON ..
make

# release build (default)
cmake -DCMAKE_BUILD_TYPE=Release ..
make

# with specific compiler
cmake -DCMAKE_C_COMPILER=clang -DDEFL_USE_TEST=ON ..
make
```

## TODO

- [x] implement inflate
  - [ ] implement inflate stream
- [ ] implement deflate
- [x] tests
- [x] build
- [ ] documentation

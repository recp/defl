# 🗜️ deflate

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

### Usage

`infl_include()`, `infl_buf()` and `infl_stream()` includes memory as a readonly pointer. So dont free source memory until decompress. On exception is that small chunks are accumulated in internal buffer to reduce lot of chunk allocations. This design prevents duplicationg compressed data while decoding. If you really need to free source data then you can manually create chunks for duplicate whoe data if needed ( free later ). **defl** doesnt manage memory for you, only memory for chunks and internal structure. Once decompression is finished call `infl_destroy()` to free some resources. An alternative destroy function may be provided to destroy all internal caches ( if any ) if **defl** no longer needed at any point on runtime.

#### Usage 1: Use Non-Contiguous Chunk Api

**defl** supports chunk based decompression to avoid duplicate compressed data if it is already on memory. `infl_include()` will include readonly pointer to compressed data. It will create chunks to each call but also accumulate small chunks togerher to prevent lot of allocations. `infl_destroy()` will free these allocated memories.

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

infl_destroy(st);
```

#### Usage 2: Use Contiguous Chunk Api

`infl_buf()` will decompress and free resources in one call.

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

#### Usage 3: Use Stream Api

With streaming api you can decompress 1 byte at a time ( or more bytes ). For instance instead of downloading large zip, you can decompress each time you received data on fly.

```c
#include <defl/infl.h>

infl_stream_t st;
UnzResult     res;

infl_init(&st, dst, dstlen, 1); /* 1: INFL_ZLIB or jsut pass INFL_ZLIB */

/* decompress when new data avail */
res = infl_stream(st, src1, srclen1);

...

/* decompress again when previous response is UNZ_UNFINISHED */
if (res == UNZ_UNFINISHED) {
  res = infl_stream(st, src2, srclen2);
}

...
if (res == UNZ_UNFINISHED) {
  res = infl_stream(st, src3, srclen3);
}

infl_destroy(st);
```

## Example: Decode DEFLATE Chunk in PNG

Using Chunk Based API:

```C
...
infl_stream_t *pngdefl;
...

switch (chk_type) {
    ...
    case IM_PNG_TYPE('I','H','D','R'): {
        pngdefl = infl_init(im->data.data, (uint32_t)im->len, 1);
    } break;
    case IM_PNG_TYPE('I','D','A','T'): {
        /* With the new chunking system, small IDAT chunks will be automatically
         * appended together, while large ones will be allocated directly.
         * This is much more efficient for PNG files with many small IDAT chunks.
         */
        infl_include(pngdefl, p, chk_len);
    } break;
...
}

...

/* compress */
if (infl(pngdefl)) {
  goto err;
}

/* undo filters */
...

infl_destroy(pngdefl);
```

Using Streaming API:


```C
...
infl_stream_t *pngdefl;
...

switch (chk_type) {
    ...
    case IM_PNG_TYPE('I','H','D','R'): {
        pngdefl = infl_init(im->data.data, (uint32_t)im->len, 1);
    } break;
    case IM_PNG_TYPE('I','D','A','T'): {
        /* or streaming api */
        infl_stream(pngdefl, p, chk_len);
    } break;
...
}

/* undo filters */
...

infl_destroy(pngdefl);
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
- [x] implement inflate stream
- [x] tests
- [x] build
- [ ] implement deflate

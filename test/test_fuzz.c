/*
 * Copyright (C) 2025 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <defl/infl.h>

/* Platform-specific includes */
#ifdef _WIN32
#include <io.h>
#define read _read
#else
#include <unistd.h>
#endif

/* maximum output buffer size for fuzzing */
#define MAX_OUTPUT_SIZE (1024 * 1024)  /* 1MB */

/* LibFuzzer entry point */
#ifdef LIBFUZZER
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  static uint8_t *output = NULL;
  infl_stream_t  *stream;
  size_t          pos, i;
  int             attempts, result;

  /* allocate output buffer once */
  if (!output) {
    output = malloc(MAX_OUTPUT_SIZE);
    if (!output) return 0;
  }

  infl_buf(data, size, output, MAX_OUTPUT_SIZE, 0);  /* try as raw DEFLATE */
  infl_buf(data, size, output, MAX_OUTPUT_SIZE, INFL_ZLIB); /* try as ZLIB */

  /* try chunked input with various chunk sizes */
  if (size > 10) {
    stream = infl_init(output, MAX_OUTPUT_SIZE, 0);
    if (stream) {
      /* test various streaming patterns */
      pos      = 0;
      attempts = 0;
      result   = UNZ_UNFINISHED;

      while (pos < size && attempts < 1000) {
        size_t chunk = 1 + (data[pos] % 64);  /* 1-64 byte chunks */
        if (pos + chunk > size) chunk = size - pos;

        result = infl_stream(stream, data + pos, chunk);
        pos   += chunk;
        attempts++;

        if      (result == UNZ_OK)         { break;    } /* completed successfully */
        else if (result == UNZ_UNFINISHED) { continue; } /* need more data         */
        else                               { break;    } /* error occurred         */
      }

      /* try final empty call to finish */
      if (result == UNZ_UNFINISHED && pos >= size) {
        result = infl_stream(stream, NULL, 0);
      }

      infl_destroy(stream);
    }

    /* test byte-by-byte streaming */
    stream = infl_init(output, MAX_OUTPUT_SIZE, 0);
    if (stream) {
      for (i = 0; i < size && i < 100; i++) {
        int result = infl_stream(stream, data + i, 1);
        if (result == UNZ_OK || result < UNZ_OK) break;
      }
      infl_destroy(stream);
    }
  }

  return 0;
}
#endif

/* AFL persistent mode support */
#ifdef AFL_PERSISTENT

int main(int argc, char *argv[]) {
  uint8_t *data, *output;
  size_t   size;

  if (!(data = malloc(100000);) || !(output = malloc(MAX_OUTPUT_SIZE))) {
    fprintf(stderr, "Memory allocation failed\n");
    return 1;
  }

#ifdef __AFL_HAVE_MANUAL_CONTROL
  __AFL_INIT();
#endif

  while (__AFL_LOOP(10000)) {
    /* Read from stdin */
#ifdef _WIN32
    size = _read(0, data, 100000);
#else
    size = read(0, data, 100000);
#endif

    if (size > 0) {
      /* test various configurations */
      infl_buf(data, size, output, MAX_OUTPUT_SIZE, 0);
      infl_buf(data, size, output, MAX_OUTPUT_SIZE, INFL_ZLIB);
      /* test with small output buffer */
      infl_buf(data, size, output, 100, 0);
    }
  }

  free(data);
  free(output);
  return 0;
}
#endif

/* Standalone fuzzer for testing */
#if !defined(LIBFUZZER) && !defined(AFL_PERSISTENT)

static uint32_t rng_state = 0x12345678;
static uint32_t simple_rand(void) {
  rng_state = rng_state * 1103515245 + 12345;
  return rng_state;
}

/* generate semi-valid DEFLATE data for better coverage */
static size_t generate_fuzz_input(uint8_t *out, size_t max_size) {
  size_t   size, pos;
  int      format, i;
  uint16_t len;

  size   = (simple_rand() % max_size) + 1;
  pos    = 0;
  /* randomly choose format */
  format = simple_rand() % 4;

  switch (format) {
    case 0: /* valid uncompressed block */
      if (size >= 10) {
        out[pos++] = 0x01;  /* BFINAL=1, BTYPE=00 */
        len = simple_rand() % 100;
        out[pos++] = len & 0xFF;
        out[pos++] = (len >> 8) & 0xFF;
        out[pos++] = ~(len & 0xFF);
        out[pos++] = ~((len >> 8) & 0xFF);

        for (i = 0; i < len && pos < size; i++) {
          out[pos++] = simple_rand() & 0xFF;
        }
      }
      break;
    case 1: /* valid static block */
      out[pos++] = 0x03;  /* BFINAL=1, BTYPE=01 */
      while (pos < size - 1) {
        /* random static Huffman codes */
        if (simple_rand() % 10 == 0) {
          out[pos++] = 0x00;  /* EOB */
          break;
        } else {
          out[pos++] = 0x30 + (simple_rand() % 144);
        }
      }
      break;
    case 2: /* ZLIB wrapped */
      out[pos++] = 0x78;
      out[pos++] = 0x9C;
      /* fall through to random data */
    default: /* random data */
      while (pos < size) {
        out[pos++] = simple_rand() & 0xFF;
      }
      break;
  }
  return pos;
}

/* mutation strategies */
static void mutate_data(uint8_t *data, size_t size) {
  int     strategy, idx;
  size_t  start, len, i;
  uint8_t tmp;

  strategy = simple_rand() % 5;

  switch (strategy) {
    case 0: /* bit flip */
      if (size > 0) {
        idx = simple_rand() % (uint32_t)size;
        data[idx] ^= 1 << (simple_rand() % 8);
      }
      break;
    case 1: /* byte replacement */
      if (size > 0) {
        idx = simple_rand() % (uint32_t)size;
        data[idx] = simple_rand() & 0xFF;
      }
      break;
    case 2: /* insert byte */
      if (size > 1) {
        idx = simple_rand() % (uint32_t)(size - 1);
        memmove(data + idx + 1, data + idx, size - idx - 1);
        data[idx] = simple_rand() & 0xFF;
      }
      break;
    case 3: /* delete byte */
      if (size > 2) {
        idx = simple_rand() % (uint32_t)(size - 1);
        memmove(data + idx, data + idx + 1, size - idx - 1);
      }
      break;
    case 4: /* shuffle chunk */
      if (size > 10) {
        start = simple_rand() % (uint32_t)(size - 10);
        len   = (simple_rand() % 10) + 1;
        for (i = 0; i < len / 2; i++) {
          tmp = data[start + i];
          data[start + i] = data[start + len - 1 - i];
          data[start + len - 1 - i] = tmp;
        }
      }
      break;
  }
}

int main(int argc, char *argv[]) {
  uint8_t *data, *output;
  int      iterations, crashes, errors, success;
  int      i, ret;
  size_t   size, j;

  iterations = 10000;
  crashes    = 0;
  errors     = 0;
  success    = 0;

  if (!(data = malloc(100000))) {
    fprintf(stderr, "Memory allocation failed\n");
    return 1;
  }

  if (!(output = malloc(MAX_OUTPUT_SIZE))) {
    fprintf(stderr, "Memory allocation failed\n");
    free(data);
    return 1;
  }

  if (argc > 1) {
    iterations = atoi(argv[1]);
  }

  printf("Running %d fuzz iterations...\n", iterations);

  for (i = 0; i < iterations; i++) {
    /* generate or mutate input */
    if (i % 2 == 0) {
      size = generate_fuzz_input(data, 10000);
    } else {
      size = (simple_rand() % 10000) + 1;
      for (j = 0; j < size; j++) {
        data[j] = simple_rand() & 0xFF;
      }
      mutate_data(data, size);
    }

    /* test decompression */
    ret = infl_buf(data, (uint32_t)size, output, MAX_OUTPUT_SIZE, 0);
    if (ret == UNZ_OK) {
      success++;
    } else if (ret == UNZ_ERR || ret == UNZ_EFULL) {
      errors++;
    } else {
      crashes++;
      printf("Unexpected return code %d at iteration %d\n", ret, i);
    }

    /* progress */
    if ((i + 1) % 1000 == 0) {
      printf("Progress: %d/%d (success=%d, errors=%d, crashes=%d)\n",
             i + 1, iterations, success, errors, crashes);
    }
  }

  printf("\nFuzz test complete:\n");
  printf("  Total iterations: %d\n", iterations);
  printf("  Successful: %d (%.1f%%)\n", success, 100.0 * success / iterations);
  printf("  Expected errors: %d (%.1f%%)\n", errors, 100.0 * errors / iterations);
  printf("  Crashes/Issues: %d\n", crashes);

  free(data);
  free(output);

  return crashes > 0 ? 1 : 0;
}
#endif /* standalone fuzzer */


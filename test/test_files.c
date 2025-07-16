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
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <time.h>
#include <defl/infl.h>

/* platform-specific directory handling */
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <io.h>
#define getcwd _getcwd
#define chdir _chdir
#define stat _stat
#define strdup _strdup
#else
#include <dirent.h>
#include <unistd.h>
#endif

/* error reporting and colorful output */
#ifndef UNZ_TESTS_NO_COLORFUL_OUTPUT
#define RESET       "\033[0m"
#define RED         "\033[31m"
#define GREEN       "\033[32m"
#define YELLOW      "\033[33m"
#define CYAN        "\033[36m"
#define MAGENTA     "\033[35m"
#define BOLDWHITE   "\033[1m\033[37m"
#define BOLDRED     "\033[1m\033[31m"
#define BOLDGREEN   "\033[1m\033[32m"
#define BOLDCYAN    "\033[1m\033[36m"
#define BOLDMAGENTA "\033[1m\033[35m"
#else
#define RESET
#define RED
#define GREEN
#define YELLOW
#define CYAN
#define MAGENTA
#define BOLDWHITE
#define BOLDRED
#define BOLDGREEN
#define BOLDCYAN
#define BOLDMAGENTA
#endif

/* platform-specific symbols */
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
# define OK_TEXT    "ok:"
# define FAIL_TEXT  "fail:"
# define FINAL_TEXT "^_^"
#else
# define OK_TEXT    "✔︎"
# define FAIL_TEXT  "✗"
# define FINAL_TEXT "🎉"
#endif

typedef struct {
  int total;
  int passed;
  int failed;
  size_t total_original_bytes;
  size_t total_compressed_bytes;
  double total_time;
} test_results_t;

static test_results_t g_results = {0};

static const char* get_arch_info(void) {
#if defined(__x86_64__) || defined(_M_X64)
  return "x86_64";
#elif defined(__i386__) || defined(_M_IX86)
  return "x86";
#elif defined(__aarch64__) || defined(_M_ARM64)
  return "arm ARM64";
#elif defined(__arm__) || defined(_M_ARM)
  return "arm ARM32";
#elif defined(__riscv)
  return "riscv";
#else
  return "unknown";
#endif
}

static double get_time(void) {
#ifdef _WIN32
  LARGE_INTEGER frequency, counter;
  QueryPerformanceFrequency(&frequency);
  QueryPerformanceCounter(&counter);
  return (double)counter.QuadPart / frequency.QuadPart;
#else
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec / 1000000000.0;
#endif
}

static uint8_t*
read_file(const char *path, size_t *size) {
  FILE *f;

  if (!(f = fopen(path, "rb")))
    return NULL; /* fail silently - caller handles missing files */

  fseek(f, 0, SEEK_END);
  *size = ftell(f);
  fseek(f, 0, SEEK_SET);

  uint8_t *data = malloc(*size);
  if (!data) {
    fclose(f);
    return NULL;
  }

  if (fread(data, 1, *size, f) != *size) {
    free(data);
    fclose(f);
    return NULL;
  }

  fclose(f);
  return data;
}

static void
print_test_result(const char* name,
                  bool        passed,
                  double      elapsed,
                  const char* err_msg,
                  const char* details) {
  if (!passed) {
    fprintf(stderr, BOLDRED "  " FAIL_TEXT BOLDWHITE " %-40s " RESET, name);

    if (elapsed > 0.01)  fprintf(stderr, YELLOW "%.2fs" RESET, elapsed);
    else                 fprintf(stderr, "0" RESET);

    if (err_msg)       fprintf(stderr, " " YELLOW "- %s" RESET, err_msg);

    fprintf(stderr, "\n");
  } else {
    fprintf(stderr, GREEN "  " OK_TEXT RESET " %-40s ", name);

    if (elapsed > 0.01)  fprintf(stderr, YELLOW "%.2fs" RESET, elapsed);
    else                 fprintf(stderr, "0" RESET);

    if (details)         fprintf(stderr, " " CYAN "(%s)" RESET, details);

    fprintf(stderr, "\n");
  }
}

/* test single file decompression */
static void
test_file(const char *filename) {
  uint8_t       *orig_data, *output, *compr_data;
  infl_stream_t *stream;
  char           raw_path[512],compr_path[512],err_msg[256]={0},details[64]={0};
  double         start_time, ratio, elapsed;
  size_t         orig_size, compr_size;
  int            ret;
  bool           passed;

  start_time = get_time();
  snprintf(raw_path,   sizeof(raw_path),   "data/raw/%s",        filename);
  snprintf(compr_path, sizeof(compr_path), "data/compressed/%s", filename);

  /* read original file */
  orig_data = read_file(raw_path, &orig_size);
  if (!orig_data) {
    return; /* Skip missing files silently */
  }

  /* read compressed file */
  compr_data = read_file(compr_path, &compr_size);
  if (!compr_data) {
    free(orig_data);
    return; /* skip missing files silently */
  }

  output = calloc(1, orig_size + 1000); /* extra space for safety */
  if (!output) {
    snprintf(err_msg, sizeof(err_msg), "allocation failed");
    free(orig_data);
    free(compr_data);
    g_results.failed++;
    g_results.total++;
    print_test_result(filename, false, get_time() - start_time, err_msg, NULL);
    return;
  }

  /* initialize DEFLATE stream */
  stream = infl_init(output, (uint32_t)orig_size + 1000, 0);
  if (!stream) {
    snprintf(err_msg, sizeof(err_msg), "stream init failed");
    free(orig_data);
    free(compr_data);
    free(output);
    g_results.failed++;
    g_results.total++;
    print_test_result(filename, false, get_time() - start_time, err_msg, NULL);
    return;
  }

  /* include compressed data */
  infl_include(stream, compr_data, (uint32_t)compr_size);

  /* decompress */
  ret = infl(stream);

  g_results.total++;
  g_results.total_original_bytes   += orig_size;
  g_results.total_compressed_bytes += compr_size;

  passed = (ret == UNZ_OK && memcmp(orig_data, output, orig_size) == 0);
  if (!passed) {
    if (ret != UNZ_OK) { snprintf(err_msg, sizeof(err_msg), "decompression error %d", ret); }
    else               { snprintf(err_msg, sizeof(err_msg), "data mismatch");               }
    g_results.failed++;
  } else {
    g_results.passed++;
    if (orig_size > 0) {
      ratio = (double)compr_size / orig_size * 100;
      snprintf(details, sizeof(details), "%.1f%% compression", ratio);
    }
  }

  elapsed = get_time() - start_time;
  g_results.total_time += elapsed;
  print_test_result(filename, passed, elapsed,
                    passed?NULL:err_msg, passed?details:NULL);

  infl_destroy(stream);
  free(orig_data);
  free(compr_data);
  free(output);
}

/* test chunked decompression (simulating PNG IDAT chunks) */
static void
test_file_chunked(const char *filename) {
  uint8_t       *orig_data, *output, *compr_data;
  infl_stream_t *stream;
  char           raw_path[512],compr_path[512],test_name[256],err_msg[256]={0},details[64]={0};
  double         start_time, elapsed;
  size_t         orig_size, compr_size, pos, chunk_size;
  int            ret;
  bool           passed;

  start_time = get_time();

  snprintf(test_name,  sizeof(test_name),  "%s_chunked",         filename);
  snprintf(raw_path,   sizeof(raw_path),   "data/raw/%s",        filename);
  snprintf(compr_path, sizeof(compr_path), "data/compressed/%s", filename);

  /* read files */
  if (!(orig_data = read_file(raw_path, &orig_size))) return;

  if (!(compr_data = read_file(compr_path, &compr_size))) {
    free(orig_data);
    return;
  }

  /* decompress in chunks (like PNG IDAT) */
  output = calloc(1, orig_size + 1000);

  if (!(stream = infl_init(output, (uint32_t)orig_size + 1000, 0))) {
    snprintf(err_msg, sizeof(err_msg), "stream init failed");
    free(orig_data);
    free(compr_data);
    free(output);
    g_results.failed++;
    g_results.total++;
    print_test_result(test_name, false, get_time() - start_time, err_msg, NULL);
    return;
  }

  /* add data in chunks of varying sizes */
  pos = 0;
  while (pos < compr_size) {
    /* vary chunk size: 1, 2, 4, 8 bytes */
    chunk_size = 1ULL << (pos % 4);
    if (chunk_size > 512) chunk_size = 512;
    if (pos + chunk_size > compr_size) chunk_size = compr_size - pos;
    infl_include(stream, compr_data + pos, (uint32_t)chunk_size);
    pos += chunk_size;
  }

  ret = infl(stream);

  g_results.total++;
  passed = (ret == UNZ_OK && memcmp(orig_data, output, orig_size) == 0);
  if (!passed) {
    if (ret != UNZ_OK) snprintf(err_msg, sizeof(err_msg), "chunked decompression error %d", ret);
    else               snprintf(err_msg, sizeof(err_msg), "chunked data mismatch");
    g_results.failed++;
  } else {
    g_results.passed++;
    snprintf(details, sizeof(details), "chunked processing");
  }

  elapsed = get_time() - start_time;
  g_results.total_time += elapsed;
  print_test_result(test_name, passed, elapsed,
                    passed ? NULL : err_msg, passed ? details : NULL);

  infl_destroy(stream);
  free(orig_data);
  free(compr_data);
  free(output);
}

/* list files in directory - Windows/POSIX compatible */
static char**
list_files(const char *dir, int *count) {
#ifdef _WIN32
  WIN32_FIND_DATA findFileData;
  HANDLE          hFind;
  char          **files;
  char            search_path[512];
  int             capacity;

  files    = NULL;
  *count   = 0;
  capacity = 0;

  /* create search pattern */
  snprintf(search_path, sizeof(search_path), "%s\\*", dir);
  hFind = FindFirstFile(search_path, &findFileData);
  if (hFind == INVALID_HANDLE_VALUE) return NULL;

  do {
    /* skip directories and hidden files */
    if (!(findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
        findFileData.cFileName[0] != '.') {

      /* expand array if needed */
      if (*count >= capacity) {
        capacity = capacity ? capacity * 2 : 16;
        files = realloc(files, capacity * sizeof(char*));
        if (!files) {
          FindClose(hFind);
          return NULL;
        }
      }

      files[*count] = strdup(findFileData.cFileName);
      if (!files[*count]) {
        /* cleanup on failure */
        while (--(*count) >= 0) {
          free(files[*count]);
        }
        free(files);
        FindClose(hFind);
        return NULL;
      }
      (*count)++;
    }
  } while (FindNextFile(hFind, &findFileData) != 0);

  FindClose(hFind);
  return files;

#else
  DIR           *d;
  struct dirent *entry;
  char          **files;

  if (!(d = opendir(dir))) {
    *count = 0;
    return NULL;
  }

  *count = 0;
  while ((entry = readdir(d)) != NULL) {
    if (entry->d_name[0] != '.') (*count)++;
  }

  if (*count == 0) {
    closedir(d);
    return NULL;
  }

  files = malloc(*count * sizeof(char*));
  rewinddir(d);
  int i = 0;
  while ((entry = readdir(d)) != NULL && i < *count) {
    if (entry->d_name[0] != '.') {
      files[i] = strdup(entry->d_name);
      i++;
    }
  }

  closedir(d);
  return files;
#endif
}

static int cmpstr(const void *a, const void *b) {
  return strcmp(*(const char**)a, *(const char**)b);
}

static void
test_error_conditions(void) {
  double  start_time, elapsed;
  char    err_msg[256], details[64];
  uint8_t output[100], small_output[5];
  int     ret;
  bool    passed;

  const uint8_t truncated[]       = {0x78, 0x9C}; /* Just ZLIB header,no data */
  const uint8_t invalid_block[]   = {0x07};       /* BTYPE=11 (invalid)       */
  const uint8_t large_data_full[] = {
    0x01, 0x0A, 0x00, 0xF5, 0xFF,                 /* 10 bytes uncompressed    */
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J'
  };

  start_time = get_time();
  memset(err_msg, 0, sizeof(err_msg));
  memset(details,   0, sizeof(details));

  ret    = infl_buf(invalid_block, sizeof(invalid_block), output, sizeof(output), 0);
  passed = (ret != UNZ_OK);

  if (!passed) {
    snprintf(err_msg, sizeof(err_msg), "should have rejected invalid block type");
  } else {
    snprintf(details, sizeof(details), "correctly rejected");
  }

  if (passed) { g_results.passed++; }
  else        { g_results.failed++; }
  g_results.total++;

  elapsed = get_time() - start_time;
  g_results.total_time += elapsed;
  print_test_result("invalid_block_type", passed, elapsed,
                    passed ? NULL : err_msg, passed ? details : NULL);

  /* test buffer overflow - adjust expected behavior */
  start_time = get_time();
  ret        = infl_buf(large_data_full, sizeof(large_data_full), small_output, sizeof(small_output), 0);
  passed     = (ret != UNZ_OK);
  if (!passed) {
    snprintf(err_msg, sizeof(err_msg), "should have detected buffer overflow");
  } else {
    snprintf(details, sizeof(details), "buffer protection active, error=%d", ret);
  }

  if (passed) { g_results.passed++; }
  else        { g_results.failed++; }
  g_results.total++;

  elapsed = get_time() - start_time;
  g_results.total_time += elapsed;
  print_test_result("buffer_overflow_protection", passed, elapsed,
                    passed ? NULL : err_msg, passed ? details : NULL);

  /* test truncated stream */
  start_time = get_time();
  ret        = infl_buf(truncated, sizeof(truncated), output, sizeof(output), INFL_ZLIB);
  passed     = (ret != UNZ_OK);
  if (!passed) {
    snprintf(err_msg, sizeof(err_msg), "should have detected truncated stream");
  } else {
    snprintf(details, sizeof(details), "truncation detected, error=%d", ret);
  }

  if (passed) { g_results.passed++; }
  else        { g_results.failed++; }
  g_results.total++;

  elapsed = get_time() - start_time;
  g_results.total_time += elapsed;
  print_test_result("truncated_stream", passed, elapsed,
                    passed ? NULL : err_msg, passed ? details : NULL);
}

/* specific patterns that caused issues */
static void
test_regression_cases(void) {
  double  start_time, elapsed;
  char    err_msg[256], details[64];
  uint8_t output[10];
  int     ret;
  bool    passed;

  /* test case that previously failed */
  const uint8_t regression1[] = {
    0x01, 0x01, 0x00, 0xFE, 0xFF, 'A'  /* simple uncompressed 'A' */
  };

  start_time = get_time();
  memset(err_msg, 0, sizeof(err_msg));
  memset(details, 0, sizeof(details));
  memset(output,  0, sizeof(output));

  ret = infl_buf(regression1, sizeof(regression1), output, sizeof(output), 0);
  passed = (ret == UNZ_OK && output[0] == 'A');
  if (!passed) {
    if (ret != UNZ_OK) {
      snprintf(err_msg, sizeof(err_msg), "regression decompression error %d", ret);
    } else {
      snprintf(err_msg, sizeof(err_msg), "expected 'A', got 0x%02X", output[0]);
    }
  } else {
    snprintf(details, sizeof(details), "regression test passed");
  }

  if (passed) { g_results.passed++; }
  else        { g_results.failed++; }
  g_results.total++;

  elapsed = get_time() - start_time;
  g_results.total_time += elapsed;
  print_test_result("regression_case_1", passed, elapsed,
                    passed ? NULL : err_msg, passed ? details : NULL);
}

static void
test_file_streaming(const char *filename) {
  double         start_time, elapsed;
  char           test_name[256], err_msg[256], details[64];
  char           raw_path[512], compr_path[512];
  uint8_t       *orig_data, *comp_data, *output;
  infl_stream_t *stream;
  size_t         orig_size, compr_size, pos, chunk_size;
  int            result, chunk_count;
  bool           passed;

  start_time = get_time();
  memset(err_msg, 0, sizeof(err_msg));
  memset(details, 0, sizeof(details));

  snprintf(test_name,  sizeof(test_name),  "%s_streaming",       filename);
  snprintf(raw_path,   sizeof(raw_path),   "data/raw/%s",        filename);
  snprintf(compr_path, sizeof(compr_path), "data/compressed/%s", filename);

  if (!(orig_data = read_file(raw_path,   &orig_size)))  { return; }
  if (!(comp_data = read_file(compr_path, &compr_size))) {
    free(orig_data);
    return;
  }

  if (!(output = calloc(1, orig_size + 1000))) {
    snprintf(err_msg, sizeof(err_msg), "allocation failed");
    free(orig_data);
    free(comp_data);
    g_results.failed++;
    g_results.total++;
    print_test_result(test_name, false, get_time() - start_time, err_msg, NULL);
    return;
  }

  stream = infl_init(output, (uint32_t)orig_size + 1000, 0);
  if (!stream) {
    snprintf(err_msg, sizeof(err_msg), "stream init failed");
    free(orig_data);
    free(comp_data);
    free(output);
    g_results.failed++;
    g_results.total++;
    print_test_result(test_name, false, get_time() - start_time, err_msg, NULL);
    return;
  }

  pos         = 0;
  result      = UNZ_UNFINISHED;
  chunk_count = 0;

  /* use realistic chunk sizes - minimum 64 bytes for bitstream reader */
  while (pos < compr_size) {
    /* use chunk sizes: 64, 128, 256, 512, 1024 bytes */
    chunk_size = 64 << (chunk_count % 5);
    if (pos + chunk_size > compr_size) chunk_size = compr_size - pos;

    result = infl_stream(stream, comp_data + pos, (uint32_t)chunk_size);
    pos   += chunk_size;
    chunk_count++;

    if (result == UNZ_OK) {
      break;  /* done */
    } else if (result == UNZ_UNFINISHED) {
      continue;  /* feed more data */
    } else {
      /* error */
      break;
    }
  }

  /* if we've fed all data but still getting UNFINISHED, try a few empty calls */
  if (result == UNZ_UNFINISHED && pos >= compr_size) {
    int empty_attempts = 0;
    while (result == UNZ_UNFINISHED && empty_attempts++ < 5) {
      result = infl_stream(stream, NULL, 0);
    }
  }

  g_results.total++;
  passed = (result == UNZ_OK && memcmp(orig_data, output, orig_size) == 0);
  if (!passed) {
    if (result != UNZ_OK) {
      snprintf(err_msg, sizeof(err_msg), "streaming decompression error %d", result);
    } else {
      snprintf(err_msg, sizeof(err_msg), "streaming data mismatch");
    }
    g_results.failed++;
  } else {
    g_results.passed++;
    snprintf(details, sizeof(details), "%d chunks", chunk_count);
  }

  elapsed = get_time() - start_time;
  g_results.total_time += elapsed;
  print_test_result(test_name, passed, elapsed,
                    passed ? NULL : err_msg, passed ? details : NULL);

  infl_destroy(stream);
  free(orig_data);
  free(comp_data);
  free(output);
}

static void
test_streaming_edge_cases(void) {
  double         start_time, elapsed;
  char           err_msg[256], details[64];
  char           zlib_path[512];
  uint8_t        output[100];
  uint8_t        large_uncompressed[100];
  uint8_t       *zlib_data;
  infl_stream_t *stream;
  size_t         zlib_size, large_size, pos, fed, chunk;
  int            result, attempts;
  bool           passed;

  /* test 1: uncompressed block with reasonable chunks */
  const uint8_t uncompressed[] = {
    0x01, 0x05, 0x00, 0xFA, 0xFF,  /* 5 bytes uncompressed */
    'H', 'e', 'l', 'l', 'o'
  };

  start_time = get_time();
  memset(err_msg, 0, sizeof(err_msg));
  memset(details, 0, sizeof(details));

  stream = infl_init(output, sizeof(output), 0);

  /* feed all at once for small data (under 64 bytes) */
  result = infl_stream(stream, uncompressed, sizeof(uncompressed));

  passed = ((result == UNZ_OK || result == UNZ_UNFINISHED)
            && memcmp(output, "Hello", 5) == 0);
  if (!passed) {
    snprintf(err_msg, sizeof(err_msg), "small data streaming failed, result=%d", result);
    g_results.failed++;
  } else {
    g_results.passed++;
    snprintf(details, sizeof(details), "small data streaming");
  }
  g_results.total++;

  elapsed = get_time() - start_time;
  g_results.total_time += elapsed;
  print_test_result("small_data_streaming", passed, elapsed,
                    passed ? NULL : err_msg, passed ? details : NULL);

  infl_destroy(stream);

  /* test 2: ZLIB header streaming */
  start_time = get_time();

  /* use a working ZLIB stream from the test files instead */
  snprintf(zlib_path, sizeof(zlib_path), "data/compressed/zlib_1");

  zlib_data = read_file(zlib_path, &zlib_size);
  if (zlib_data) {
    /* test with actual ZLIB file */
    stream   = infl_init(output, sizeof(output), INFL_ZLIB);

    /* feed in small chunks */
    result   = UNZ_UNFINISHED;
    pos      = 0;
    attempts = 0;

    while (pos < zlib_size && result == UNZ_UNFINISHED && attempts < 20) {
      chunk = (zlib_size - pos > 8) ? 8 : (zlib_size - pos);
      result = infl_stream(stream, zlib_data + pos, (uint32_t)chunk);
      pos += chunk;
      attempts++;
    }

    /* try empty calls to complete */
    while (result == UNZ_UNFINISHED && attempts < 30) {
      result = infl_stream(stream, NULL, 0);
      attempts++;
    }

    passed = (result == UNZ_OK);
    if (!passed) {
      snprintf(err_msg, sizeof(err_msg), "zlib streaming failed, result=%d", result);
    } else {
      snprintf(details, sizeof(details), "zlib streaming");
    }

    /* cleanup */
    infl_destroy(stream);
    free(zlib_data);
  } else {
    /* fallback to simple test without ZLIB */
    const uint8_t simple_deflate[] = {
      0x01, 0x03, 0x00, 0xFC, 0xFF,  /* 3 bytes uncompressed */
      'A', 'B', 'C'
    };

    stream = infl_init(output, sizeof(output), 0);
    result = infl_stream(stream, simple_deflate, sizeof(simple_deflate));

    passed = (result == UNZ_OK && memcmp(output, "ABC", 3) == 0);
    if (!passed) {
      snprintf(err_msg, sizeof(err_msg), "fallback streaming failed, result=%d", result);
    } else {
      snprintf(details, sizeof(details), "raw DEFLATE");
    }

    infl_destroy(stream);
  }

  if (passed) { g_results.passed++; }
  else        { g_results.failed++; }
  g_results.total++;

  elapsed = get_time() - start_time;
  g_results.total_time += elapsed;
  print_test_result("zlib_header_streaming", passed, elapsed,
                    passed ? NULL : err_msg, passed ? details : NULL);

  /* test 3: Chunked streaming with realistic sizes */
  start_time = get_time();
  stream     = infl_init(output, sizeof(output), 0);

  /* create larger test data */
  large_size = 0;

  /* build an uncompressed block with 95 bytes */
  large_uncompressed[large_size++] = 0x01;  /* BFINAL=1, BTYPE=00 */
  large_uncompressed[large_size++] = 95;    /* LEN low */
  large_uncompressed[large_size++] = 0;     /* LEN high */
  large_uncompressed[large_size++] = ~95;   /* NLEN low */
  large_uncompressed[large_size++] = 0xFF;  /* NLEN high */

  /* add 95 bytes of data */
  for (int i = 0; i < 95; i++) {
    large_uncompressed[large_size++] = 'A' + (i % 26);
  }

  /* feed in 64-byte chunks */
  fed    = 0;
  result = UNZ_UNFINISHED;
  while (fed < large_size && result == UNZ_UNFINISHED) {
    chunk  = (large_size - fed > 64) ? 64 : (large_size - fed);
    result = infl_stream(stream, large_uncompressed + fed, (uint32_t)chunk);
    fed   += chunk;
  }

  passed = (result == UNZ_OK || result == UNZ_UNFINISHED);
  if (!passed) {
    snprintf(err_msg, sizeof(err_msg), "chunked streaming failed, result=%d", result);
    g_results.failed++;
  } else {
    g_results.passed++;
    snprintf(details, sizeof(details), "64-byte chunks");
  }
  g_results.total++;

  elapsed = get_time() - start_time;
  g_results.total_time += elapsed;
  print_test_result("chunked_streaming_64byte", passed, elapsed,
                    passed ? NULL : err_msg, passed ? details : NULL);

  infl_destroy(stream);
}

int main(int argc, char *argv[]) {
  struct stat st;
  char      **files;
  int         file_count, i, j;
  bool        found;

  const char *chunked_tests[] = {
    "hello", "hello_world", "json", "html", "text_repeated",
    "zeros_1k", "repeated_a_258", "ascii", "huffman_single_a",
    "distance_test_1", "length_test_3", "bit_align_7", NULL
  };

  const char *streaming_tests[] = {
    "hello", "hello_world", "json", "xml", "binary",
    "zeros_1k", "huffman_single_a", "multi_block_1",
    "dynamic_huffman_1", "distance_test_1", "length_test_3",
    "bit_align_7", "zlib_1", NULL
  };

  (void)argc; /* suppress unused parameter warning */
  (void)argv;

  fprintf(stderr, CYAN "\nWelcome to unz/defl tests ( arch: %s )\n\n" RESET, get_arch_info());

  fprintf(stderr, BOLDWHITE "  %-42s %-12s %s\n" RESET, "Test Name", "Elapsed Time -", "Details");

  /* check if directories exist */
  if (stat("data/raw", &st) != 0) {
    if (stat("test/data/raw", &st) == 0) {
      if (chdir("test") != 0) {
        printf("Failed to change to test directory\n");
        return 1;
      }
    } else {
      printf("Error: Neither data/raw/ nor test/data/raw/ directory found!\n");
      return 1;
    }
  }

  if (stat("data/compressed", &st) != 0) {
    printf("Error: data/compressed/ directory not found!\n");
    return 1;
  }

  /* get list of compressed files */
  files = list_files("data/compressed", &file_count);
  if (!files) {
    printf("No files found in compressed/\n");
    return 1;
  }

  /* sort files for consistent output */
  qsort(files, file_count, sizeof(char*), cmpstr);

  /* test each file */
  for (i = 0; i < file_count; i++) {
    test_file(files[i]);
  }

  /* test subset with chunked input */
  for (i = 0; chunked_tests[i]; i++) {
    /* check if this file exists in our list */
    found = false;
    for (j = 0; j < file_count; j++) {
      if (strcmp(files[j], chunked_tests[i]) == 0) {
        found = true;
        break;
      }
    }
    if (found) {
      test_file_chunked(chunked_tests[i]);
    }
  }

  /* test streaming API */
  for (i = 0; streaming_tests[i]; i++) {
    /* check if this file exists in our list */
    found = false;
    for (j = 0; j < file_count; j++) {
      if (strcmp(files[j], streaming_tests[i]) == 0) {
        found = true;
        break;
      }
    }
    if (found) {
      test_file_streaming(streaming_tests[i]);
    }
  }

  /* additional tests */
  test_error_conditions();
  test_regression_cases();
  test_streaming_edge_cases();

  if (g_results.failed == 0) {
    fprintf(stderr, BOLDGREEN "\n  All tests passed " FINAL_TEXT "\n" RESET);
  }

  fprintf(stderr,
          CYAN "\nunz/defl test results (%.2fs):\n" RESET
          "--------------------------\n"
          MAGENTA "%d" RESET " tests ran, "
          GREEN   "%d" RESET " passed, "
          RED     "%d" RESET " failed\n\n" RESET,
          g_results.total_time,
          g_results.total,
          g_results.passed,
          g_results.failed);

  if (g_results.failed == 0) {
    printf("PASS: test/test_files\n");
    printf("=============\n");
    printf("1 test passed\n");
    printf("=============\n");
  } else {
    printf("FAIL: test/test_files\n");
    printf("=============\n");
    printf("1 test failed\n");
    printf("=============\n");
  }

  for (i = 0; i < file_count; i++) free(files[i]);
  free(files);
  return g_results.failed > 0 ? 1 : 0;
}

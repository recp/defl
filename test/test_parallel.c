/*
 * Copyright (C) 2026 Recep Aslantas
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

#include <defl/infl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WORKERS 16

/* raw fixed-Huffman DEFLATE containing 4096 copies of 'A'. */
static const uint8_t input[] = {
  0x73, 0x74, 0x1c, 0x05, 0xa3, 0x60, 0x14, 0x8c, 0x82, 0x51,
  0x30, 0x0a, 0x46, 0xc1, 0x28, 0x18, 0x05, 0xa3, 0x60, 0x14,
  0x8c, 0x82, 0x51, 0x30, 0x0a, 0x46, 0xc1, 0x70, 0x07, 0x00
};

static atomic_int ready, start, failures;
static int        mode;

static void*
decode(void *arg) {
  uint8_t        output[4096], chunks[2][64];
  infl_stream_t *stream;
  size_t         i, half;
  int            result, iteration;

  (void)arg;

  half = sizeof(input) / 2;
  memcpy(chunks[0], input, half);
  memcpy(chunks[1], input + half, sizeof(input) - half);
  stream = infl_init(output, sizeof(output), 0);

  atomic_fetch_add_explicit(&ready, 1, memory_order_release);
  while (!atomic_load_explicit(&start, memory_order_acquire)) {}

  if (!stream) {
    atomic_fetch_add(&failures, 1);
    return NULL;
  }

  for (iteration = 0; iteration < 32; iteration++) {
    memset(output, 0, sizeof(output));
    infl_reset(stream, output, sizeof(output), 0);

    if (mode == 2) {
      result = infl_stream(stream, chunks[0], (uint32_t)half);
      if (result >= UNZ_OK)
        result = infl_stream(stream, chunks[1], (uint32_t)(sizeof(input) - half));
    } else {
      if (mode == 1) {
        infl_include(stream, chunks[0], (uint32_t)half);
        infl_include(stream, chunks[1], (uint32_t)(sizeof(input) - half));
      } else {
        infl_include(stream, input, sizeof(input));
      }
      result = infl(stream);
    }

    if (result != UNZ_OK || infl_output_pos(stream) != sizeof(output)) {
      atomic_fetch_add(&failures, 1);
      break;
    }

    for (i = 0; i < sizeof(output); i++) {
      if (output[i] != 'A') {
        atomic_fetch_add(&failures, 1);
        break;
      }
    }
  }

  infl_destroy(stream);
  return NULL;
}

int
main(int argc, char **argv) {
  pthread_t threads[WORKERS];
  int       i, started = 0;

  if (argc != 2)
    return 2;

  mode = atoi(argv[1]);
  if (mode < 0 || mode > 2)
    return 2;

  for (i = 0; i < WORKERS; i++) {
    if (pthread_create(&threads[i], NULL, decode, NULL) != 0)
      break;

    started++;
  }

  while (atomic_load_explicit(&ready, memory_order_acquire) != started) {}
  atomic_store_explicit(&start, 1, memory_order_release);

  for (i = 0; i < started; i++) {
    pthread_join(threads[i], NULL);
  }

  if (started != WORKERS || atomic_load(&failures) != 0) {
    fprintf(stderr, "parallel decode failed: %d workers, %d failures\n",
            started, atomic_load(&failures));
    return 1;
  }

  return 0;
}

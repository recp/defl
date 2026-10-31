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
#include <stdio.h>
#include <string.h>

static int
stored_chunks(size_t chunk_size, int mixed) {
  uint8_t        input[1405], output[1400];
  infl_stream_t *stream;
  size_t         i, offset, count;
  int            result, round;

  input[0] = 1;
  input[1] = (uint8_t)sizeof(output);
  input[2] = (uint8_t)(sizeof(output) >> 8);
  input[3] = (uint8_t)~input[1];
  input[4] = (uint8_t)~input[2];
  for (i = 0; i < sizeof(output); i++) {
    input[i + 5] = (uint8_t)(i * 17u + 31u);
  }

  if (!(stream = infl_init(output, sizeof(output), 0)))
    return 1;

  result = UNZ_OK;
  for (round = 0; round < 8; round++) {
    memset(output, 0, sizeof(output));
    infl_reset(stream, output, sizeof(output), 0);
    offset = 0;
    while (offset < sizeof(input)) {
      count = sizeof(input) - offset;
      if (count > chunk_size)
        count = chunk_size;
      if (!mixed && chunk_size >= sizeof(input)) {
        result = infl_set_input(stream, input, sizeof(input));
        if (result != UNZ_OK || infl_set_input(stream, input, sizeof(input)) != UNZ_ERR) {
          result = UNZ_ERR;
          break;
        }
      } else {
        infl_include(stream, input + offset, (uint32_t)count);
      }
      offset += count;
    }
    if (result != UNZ_OK)
      break;
    result = infl(stream);
    if (result != UNZ_OK || infl_output_pos(stream) != sizeof(output)
        || memcmp(output, input + 5, sizeof(output)) != 0) {
      result = UNZ_ERR;
      break;
    }
  }
  infl_destroy(stream);
  return result != UNZ_OK;
}

static int
reset_overflow(void) {
  uint8_t        input[8193] = {0}, output[1];
  infl_stream_t *stream;
  size_t         i;
  int            round;

  if (!(stream = infl_init(output, sizeof(output), 0)))
    return 1;
  for (round = 0; round < 3; round++) {
    infl_reset(stream, output, sizeof(output), 0);
    for (i = 0; i < 1050; i++) {
      infl_include(stream, input, sizeof(input));
    }
  }
  infl_destroy(stream);
  return 0;
}

int
main(void) {
  static const size_t sizes[] = { 1, 2, 3, 13, 1405 };
  uint8_t             output[1];
  infl_stream_t      *stream;
  size_t              i;
  int                 result;

  if (infl_set_input(NULL, NULL, 0) != UNZ_ERR)
    return 1;
  if (!(stream = infl_init(output, sizeof(output), 0)))
    return 1;

  result = infl_set_input(stream, NULL, 0) != UNZ_OK
           || infl_set_input(stream, NULL, 1) != UNZ_ERR;
  infl_destroy(stream);
  if (result)
    return 1;

  if (reset_overflow())
    return 1;

  for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
    if (stored_chunks(sizes[i], 0) || stored_chunks(sizes[i], 1)) {
      fprintf(stderr, "borrowed input failed at chunk size %zu\n", sizes[i]);
      return 1;
    }
  }
  return 0;
}

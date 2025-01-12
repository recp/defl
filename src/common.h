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

#ifndef src_common_h
#define src_common_h

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

/**
 * get and link the huff library (static prererable) at:
 * https://github.com/recp/huff
 */
#include <huff/huff.h>

#include "../include/defl/common.h"

#ifdef __GNUC__
#  define unlikely(expr) __builtin_expect(!!(expr), 0)
#  define likely(expr)   __builtin_expect(!!(expr), 1)
#else
#  define unlikely(expr) (expr)
#  define likely(expr)   (expr)
#endif

#define ARRAY_LEN(ARR) (sizeof(ARR) / sizeof(ARR[0]))

typedef struct unz__chunk_t  unz_chunk_t;
typedef struct unz__chunk_t  defl_chunk_t;
typedef struct unz__stream_t defl_stream_t;

struct unz__chunk_t {
  struct unz__chunk_t *next;
  const uint8_t       *p;
  const uint8_t       *end;
  size_t               bitpos;
};

#define BITS_TYPE uint_fast64_t
#define BITS_SZF  (sizeof(BITS_TYPE)*8)

typedef struct unz__bitstate_t {
  struct unz__chunk_t *chunk;
  bitstream_t          pbits; /* back buff  */
  BITS_TYPE            bits;  /* front buff */
  uint_fast16_t        nbits;
  uint_fast16_t        npbits;
} unz__bitstate_t;

struct unz__stream_t {
  unz_chunk_t   *start;
  unz_chunk_t   *end;
  unz_chunk_t   *it;

  void          *header;

  void          *(*malloc)(size_t);
  void          *(*realloc)(void *, size_t);
  void           (*free)(void *);

  size_t         bitpos; /* bit position in all */
  uint8_t       *dst;
  uint32_t       dstlen;
  size_t         dstpos;
  size_t         srclen; /* sum_of(chunk->len)  */

  unz__bitstate_t bs;
};

#endif /* src_common_h */

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

/* chunk pool configuration - optimization for PNG IDAT chunks */
#define UNZ_CHUNK_POOL_SIZE 32          /* for images with many IDATs        */
#define UNZ_CHUNK_PAGE_SIZE 32768       /* 32KB - typical for PNG IDAT       */
#define UNZ_CHUNK_APPEND_THRESHOLD 8192 /* 8KB - append if smaller than this */

/* chunk structure pool - for reducing calloc overhead */
#define UNZ_CHUNK_STRUCT_POOL_SIZE 1024 /* for large images                  */

/* cache line size for alignment */
#define CACHE_LINE_SIZE 64

#define MAX_CODELEN_CODES 19
#define MAX_LITLEN_CODES  288
#define MAX_DIST_CODES    32

typedef struct unz__chunk_t  unz_chunk_t;
typedef struct unz__chunk_t  defl_chunk_t;
typedef struct unz__stream_t defl_stream_t;

struct unz__chunk_t {
  struct unz__chunk_t *next;
  const uint8_t       *p;
  const uint8_t       *end;
  size_t               bitpos;
  uint8_t             *buffer;        /* owned buffer for appendable chunks  */
  size_t               buffer_size;   /* total buffer size                   */
  size_t               used;          /* used bytes in buffer                */
  bool                 is_pooled;     /* true if from pre-allocated pool     */
  bool                 is_appendable; /* true if we can append to this chunk */
};

#define BITS_TYPE uint_fast64_t
#define BITS_SZF  (sizeof(BITS_TYPE)*8)

typedef struct unz__bitstate_t {
  struct unz__chunk_t *chunk;
  const uint8_t       *p;
  const uint8_t       *end;
  bitstream_t          pbits; /* back buff  */
  BITS_TYPE            bits;  /* front buff */
  unsigned             nbits;
  unsigned             npbits;
} unz__bitstate_t;

typedef enum {
  INFL_STATE_NONE = 0,
  INFL_STATE_HEADER,
  INFL_STATE_BLOCK_HEADER, 
  INFL_STATE_RAW,
  INFL_STATE_FIXED,
  INFL_STATE_DYNAMIC_HEADER,
  INFL_STATE_DYNAMIC_CODELEN,
  INFL_STATE_DYNAMIC_LITLEN,
  INFL_STATE_DYNAMIC_BLOCK,
  INFL_STATE_DONE
} infl_state_t;

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
  int            flags;

  unz__bitstate_t bs;
  
  /* streaming state start */
  infl_state_t   stream_state;
  uint_fast8_t   stream_btype;
  uint_fast8_t   stream_bfinal;
  
  /* raw block state for resume */
  struct {
    uint16_t len;
    uint16_t remlen;
    uint8_t  resuming;
  } raw_state;
  
  /* dynamic huffman state */
  struct {
    int               hlit, hdist, hclen;
    int               i, n, repeat, prev;
    uint_fast8_t      lens[MAX_LITLEN_CODES + MAX_DIST_CODES];
    huff_table_ext_t  tlit, tdist;
    uint8_t           tlit_valid, tdist_valid;
  } dyn_state;
  /* streaming state end */
  
  /* chunk pool management */
  unz_chunk_t    *chunk_pool[UNZ_CHUNK_POOL_SIZE];
  uint8_t        *chunk_buffers[UNZ_CHUNK_POOL_SIZE];
  int             pool_used;
  unz_chunk_t    *current_appendable; /* current chunk for appending small data */
  
  /* chunk structure pool - for non-appendable chunks */
  unz_chunk_t    *chunk_struct_pool[UNZ_CHUNK_STRUCT_POOL_SIZE];
  int             struct_pool_used;
  int             struct_pool_available;
  
  /* statistics for tuning (optional - can be ifdef'd out in release) */
#ifdef UNZ_STATS
  size_t          total_appends;
  size_t          total_directs;
  size_t          pool_hits;
  size_t          pool_misses;
#endif
};

#endif /* src_common_h */

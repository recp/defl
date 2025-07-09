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

#include "../common.h"
#include "../../include/defl/infl.h"

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#  include <arm_neon.h>
#endif

#if defined(__AVX2__)
#  include <immintrin.h>
#endif

/* initialize chunk pool */
static bool
infl_init_chunk_pool(infl_stream_t * __restrict stream) {
  int i;
  
  stream->current_appendable    = NULL;
  stream->pool_used             = 0;
  stream->struct_pool_used      = 0;
  stream->struct_pool_available = 0;
  
  /* pre-allocate chunk structures for pooling */
  for (i = 0; i < UNZ_CHUNK_STRUCT_POOL_SIZE; i++) {
    stream->chunk_struct_pool[i] = calloc(1, sizeof(unz_chunk_t));
    if (!stream->chunk_struct_pool[i]) {
      /* cleanup on failure */
      while (--i >= 0) {
        free(stream->chunk_struct_pool[i]);
      }
      return false;
    }
    stream->struct_pool_available++;
  }
  
  /* pre-allocate appendable chunk structures and buffers */
  for (i = 0; i < UNZ_CHUNK_POOL_SIZE; i++) {
    stream->chunk_pool[i] = calloc(1, sizeof(unz_chunk_t));
    if (!stream->chunk_pool[i]) {
      /* cleanup on failure */
      while (--i >= 0) {
        free(stream->chunk_buffers[i]);
        free(stream->chunk_pool[i]);
      }
      /* cleanup struct pool too */
      for (int j = 0; j < UNZ_CHUNK_STRUCT_POOL_SIZE; j++) {
        free(stream->chunk_struct_pool[j]);
      }
      return false;
    }
    
    /* allocate aligned buffer for better performance */
    if (posix_memalign((void**)&stream->chunk_buffers[i], 64, UNZ_CHUNK_PAGE_SIZE) != 0) {
      stream->chunk_buffers[i] = malloc(UNZ_CHUNK_PAGE_SIZE);
      if (!stream->chunk_buffers[i]) {
        /* cleanup on failure */
        free(stream->chunk_pool[i]);
        while (--i >= 0) {
          free(stream->chunk_buffers[i]);
          free(stream->chunk_pool[i]);
        }
        /* cleanup struct pool too */
        for (int j = 0; j < UNZ_CHUNK_STRUCT_POOL_SIZE; j++) {
          free(stream->chunk_struct_pool[j]);
        }
        return false;
      }
    }
    
    /* initialize chunk */
    stream->chunk_pool[i]->buffer        = stream->chunk_buffers[i];
    stream->chunk_pool[i]->buffer_size   = UNZ_CHUNK_PAGE_SIZE;
    stream->chunk_pool[i]->used          = 0;
    stream->chunk_pool[i]->is_pooled     = true;
    stream->chunk_pool[i]->is_appendable = true;
    stream->chunk_pool[i]->next          = NULL;
  }
  
  return true;
}

/* get a chunk from pool or create new one */
static unz_chunk_t *
get_pooled_chunk(infl_stream_t * __restrict stream) {
  unz_chunk_t *chk;
  if (stream->pool_used < UNZ_CHUNK_POOL_SIZE) {
    chk       = stream->chunk_pool[stream->pool_used++];
    chk->used = 0;
    chk->p    = chk->buffer;
    chk->end  = chk->buffer;
    return chk;
  }
  return NULL;
}

/* get a chunk structure from the struct pool */
static unz_chunk_t *
get_pooled_chunk_struct(infl_stream_t * __restrict stream) {
  unz_chunk_t *chk;
  if (stream->struct_pool_available > 0) {
    chk = stream->chunk_struct_pool[stream->struct_pool_used++];
    stream->struct_pool_available--;

    /* reset the chunk structure */
    memset(chk, 0, sizeof(*chk));
    chk->is_pooled     = true;
    chk->is_appendable = false;
    
    return chk;
  }
  return NULL;
}

/* prefetch data for better performance */
static inline void
prefetch_data(const void *addr, size_t len) {
#if defined(__builtin_prefetch)
  const char *p = (const char *)addr;
  for (size_t i = 0; i < len; i += 64) {
    __builtin_prefetch(p + i, 0, 1);
  }
#endif
}

/* optimized memory copy with SIMD when available */
static inline void
fast_memcpy(uint8_t * __restrict dst, const uint8_t * __restrict src, size_t len) {
  size_t i = 0;
  
#if defined(__AVX2__)
  /* AVX2 copy for aligned data */
  if (((uintptr_t)dst & 31) == 0 && ((uintptr_t)src & 31) == 0) {
    for (; i + 63 < len; i += 64) {
      _mm256_store_si256((__m256i*)(dst + i), 
                         _mm256_load_si256((const __m256i*)(src + i)));
      _mm256_store_si256((__m256i*)(dst + i + 32), 
                         _mm256_load_si256((const __m256i*)(src + i + 32)));
    }
  } else {
    /* Unaligned AVX2 copy */
    for (; i + 63 < len; i += 64) {
      _mm256_storeu_si256((__m256i*)(dst + i), 
                          _mm256_loadu_si256((const __m256i*)(src + i)));
      _mm256_storeu_si256((__m256i*)(dst + i + 32), 
                          _mm256_loadu_si256((const __m256i*)(src + i + 32)));
    }
  }
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
  /* NEON copy */
  for (; i + 63 < len; i += 64) {
    vst1q_u8(dst + i,      vld1q_u8(src + i));
    vst1q_u8(dst + i + 16, vld1q_u8(src + i + 16));
    vst1q_u8(dst + i + 32, vld1q_u8(src + i + 32));
    vst1q_u8(dst + i + 48, vld1q_u8(src + i + 48));
  }
#endif

  /* handle remaining bytes with unrolled loops */
  for (; i + 7 < len; i += 8) {
    *(uint64_t*)(dst + i) = *(uint64_t*)(src + i);
  }

  /* handle final bytes */
  switch (len - i) {
    case 7: dst[i+6]              = src[i+6];
    case 6: dst[i+5]              = src[i+5];
    case 5: dst[i+4]              = src[i+4];
    case 4: *(uint32_t*)(dst + i) = *(uint32_t*)(src + i); break;
    case 3: dst[i+2]              = src[i+2];
    case 2: *(uint16_t*)(dst + i) = *(uint16_t*)(src + i); break;
    case 1: dst[i]                = src[i]; break;
  }
}

UNZ_EXPORT
void
infl_include(infl_stream_t * __restrict stream,
             const void    * __restrict ptr,
             uint32_t                   len) {
  unz_chunk_t *chk;
  
  /* initialize pool on first use */
  if (stream->pool_used == 0 && !stream->chunk_pool[0]) {
    if (!infl_init_chunk_pool(stream)) {
      goto fallback_alloc;
    }
  }
  
  /* prefetch source data for better performance */
  if (len >= 256) {
    prefetch_data(ptr, len);
  }
  
  /* strategy: append chunks up to threshold, allocate large ones directly */
  if (len <= UNZ_CHUNK_APPEND_THRESHOLD) {
    /* try to append to current appendable chunk */
    if (stream->current_appendable && 
        stream->current_appendable->is_appendable &&
        (stream->current_appendable->used + len) <= stream->current_appendable->buffer_size) {
      
      /* append to existing chunk with fast copy */
      fast_memcpy(stream->current_appendable->buffer 
                  + stream->current_appendable->used, 
                  ptr, 
                  len);
      stream->current_appendable->used += len;
      stream->current_appendable->end   = stream->current_appendable->buffer + stream->current_appendable->used;
      stream->srclen                   += len;
      return;
    }
    
    /* need new chunk - check if we should flush current one first */
    if (stream->current_appendable && 
        stream->current_appendable->used > 0) {
      /* current chunk has data but not enough space, mark it as complete */
      stream->current_appendable->is_appendable = false;
    }
    
    /* get new pooled chunk for small data */
    chk = get_pooled_chunk(stream);
    if (chk) {
      fast_memcpy(chk->buffer, ptr, len);
      chk->used                  = len;
      chk->p                     = chk->buffer;
      chk->end                   = chk->buffer + len;
      chk->is_appendable         = true;
      stream->current_appendable = chk;
    } else {
      /* pool exhausted, fall back to direct allocation */
      goto fallback_alloc;
    }
  } else {
    /* large chunk: try to use pooled chunk structure first */
    chk = get_pooled_chunk_struct(stream);
    if (!chk) {
      /* structure pool exhausted, fall back to calloc */
fallback_alloc:
      chk = calloc(1, sizeof(*chk));
      if (!chk) return;
      chk->is_pooled = false;
    }
    
    /* set up direct pointer (no copying for large chunks) */
    chk->p             = ptr;
    chk->end           = ptr + len;
    chk->buffer        = NULL;
    chk->buffer_size   = 0;
    chk->used          = len;
    chk->is_appendable = false;
    
    /* mark current appendable as complete if it has data */
    if (stream->current_appendable && stream->current_appendable->used > 0) {
      stream->current_appendable->is_appendable = false;
    }
    stream->current_appendable = NULL;
  }

  /* link into chain */
  if (!stream->start) { stream->start     = chk; } 
  else                { stream->end->next = chk; }

  stream->end     = chk;
  stream->srclen += len;
}

/* Reset pool for reuse - call this after processing to reuse chunks */
UNZ_EXPORT
void
infl_reset_pool(infl_stream_t * __restrict stream) {
  int i;
  
  /* reset appendable chunks */
  for (i = 0; i < stream->pool_used; i++) {
    stream->chunk_pool[i]->used          = 0;
    stream->chunk_pool[i]->next          = NULL;
    stream->chunk_pool[i]->p             = stream->chunk_pool[i]->buffer;
    stream->chunk_pool[i]->end           = stream->chunk_pool[i]->buffer;
    stream->chunk_pool[i]->is_appendable = true;
  }
  stream->pool_used = 0;
  
  /* reset structure pool */
  stream->struct_pool_used = 0;
  stream->struct_pool_available = UNZ_CHUNK_STRUCT_POOL_SIZE;
  
  /* Reset stream state */
  stream->current_appendable = NULL;
  stream->start              = NULL;
  stream->end                = NULL;
  stream->srclen             = 0;
}

UNZ_EXPORT
void
infl_destroy(defl_stream_t * __restrict stream) {
  unz_chunk_t *chk, *tofree;
  int i;

  if (!stream) return;

  if ((chk = stream->start)) {
    do {
      tofree = chk;
      chk    = chk->next;
      
      /* only free non-pooled chunks */
      if (!tofree->is_pooled) { free(tofree); }
    } while (chk);
  }

  /* free appendable chunk pool */
  for (i = 0; i < UNZ_CHUNK_POOL_SIZE; i++) {
    if (stream->chunk_buffers[i]) free(stream->chunk_buffers[i]);
    if (stream->chunk_pool[i])    free(stream->chunk_pool[i]);
  }

  /* free chunk structure pool */
  for (i = 0; i < UNZ_CHUNK_STRUCT_POOL_SIZE; i++) {
    if (stream->chunk_struct_pool[i]) free(stream->chunk_struct_pool[i]);
  }

  free(stream);
}

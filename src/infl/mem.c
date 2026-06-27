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

 /* Platform-specific aligned allocation helpers */
#ifdef _WIN32
#  include <malloc.h>
#  define ALIGNED_ALLOC(ptr, alignment, size) \
      ((*(ptr) = _aligned_malloc((size), (alignment))) != NULL ? 0 : -1)
#  define ALIGNED_FREE(ptr) _aligned_free(ptr)
#else
#  include <unistd.h>
  /* POSIX systems (Linux, macOS, etc.) */
#  define ALIGNED_ALLOC(ptr, alignment, size) \
      posix_memalign((void**)(ptr), (alignment), (size))
#  define ALIGNED_FREE(ptr) free(ptr)
#endif

/* get a chunk from pool or create new one */
static unz_chunk_t *
get_pooled_chunk(infl_stream_t * __restrict stream) {
  unz_chunk_t *chk;
  int          idx;

  if (stream->pool_used >= UNZ_CHUNK_POOL_SIZE)
    return NULL;

  idx = stream->pool_used;
  chk = stream->chunk_pool[idx];

  if (!chk) {
    chk = calloc(1, sizeof(*chk));
    if (!chk)
      return NULL;

    if (ALIGNED_ALLOC(&stream->chunk_buffers[idx], 64, UNZ_CHUNK_PAGE_SIZE) != 0) {
#ifdef _WIN32
      free(chk);
      return NULL;
#else
      stream->chunk_buffers[idx] = malloc(UNZ_CHUNK_PAGE_SIZE);
      if (!stream->chunk_buffers[idx]) {
        free(chk);
        return NULL;
      }
#endif
    }

    chk->buffer        = stream->chunk_buffers[idx];
    chk->buffer_size   = UNZ_CHUNK_PAGE_SIZE;
    chk->is_pooled     = true;
    chk->is_appendable = true;
    stream->chunk_pool[idx] = chk;
  }

  stream->pool_used = idx + 1;
  chk->used          = 0;
  chk->p             = chk->buffer;
  chk->end           = chk->buffer;
  chk->next          = NULL;
  chk->is_appendable = true;
  return chk;
}

/* get a chunk structure from the struct pool */
static unz_chunk_t *
get_pooled_chunk_struct(infl_stream_t * __restrict stream) {
  unz_chunk_t *chk;
  int          idx;

  if (stream->struct_pool_used >= UNZ_CHUNK_STRUCT_POOL_SIZE)
    return NULL;

  idx = stream->struct_pool_used;
  chk = stream->chunk_struct_pool[idx];
  if (!chk) {
    chk = calloc(1, sizeof(*chk));
    if (!chk)
      return NULL;
    stream->chunk_struct_pool[idx] = chk;
  }

  stream->struct_pool_used = idx + 1;

  chk->next          = NULL;
  chk->is_pooled     = true;
  chk->is_appendable = false;

  return chk;
}

/* Let the platform memcpy handle size/alignment-specific dispatch. */
static inline void
fast_memcpy(uint8_t * __restrict dst, const uint8_t * __restrict src, size_t len) {
  if (len)
    memcpy(dst, src, len);
}

UNZ_EXPORT
void
infl_include(infl_stream_t * __restrict stream,
             const void    * __restrict ptr,
             uint32_t                   len) {
  unz_chunk_t *chk;
  
  if (stream->current_appendable &&
      stream->current_appendable->is_appendable &&
      len <= stream->current_appendable->buffer_size - stream->current_appendable->used) {
    fast_memcpy(stream->current_appendable->buffer + stream->current_appendable->used,
                ptr,
                len);
    stream->current_appendable->used += len;
    stream->current_appendable->end   = stream->current_appendable->buffer
                                      + stream->current_appendable->used;
    stream->srclen                   += len;
    return;
  }
  
  /* strategy: append chunks up to threshold, allocate large ones directly */
  if (len <= UNZ_CHUNK_APPEND_THRESHOLD) {
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
    if (len <= UNZ_CHUNK_PAGE_SIZE && stream->start && stream->start == stream->end &&
        !stream->current_appendable && stream->srclen <= UNZ_CHUNK_PAGE_SIZE - len &&
        stream->start->is_pooled && stream->start->p && stream->start->end) {
      unz_chunk_t *first;
      size_t       first_len;

      first     = stream->start;
      first_len = (size_t)(first->end - first->p);
      if (unlikely(first_len > UNZ_CHUNK_PAGE_SIZE - len))
        goto direct_chunk;

      chk       = get_pooled_chunk(stream);
      if (chk) {
        fast_memcpy(chk->buffer, first->p, first_len);
        fast_memcpy(chk->buffer + first_len, ptr, len);
        chk->used                  = first_len + len;
        chk->p                     = chk->buffer;
        chk->end                   = chk->buffer + chk->used;
        chk->is_appendable         = chk->used < chk->buffer_size;
        stream->start              = chk;
        stream->end                = chk;
        stream->current_appendable = chk->is_appendable ? chk : NULL;
        stream->srclen            += len;
        return;
      }
    }

    /* large chunk: try to use pooled chunk structure first */
direct_chunk:
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
    chk->end           = (const uint8_t *)ptr + len;
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
  stream->pool_used = 0;
  
  /* reset structure pool */
  stream->struct_pool_used = 0;
  
  /* Reset stream state */
  stream->current_appendable = NULL;
  stream->start              = NULL;
  stream->end                = NULL;
  stream->srclen             = 0;
}

static inline void
infl_reset_state(infl_stream_t * __restrict stream) {
  memset(&stream->bs, 0, sizeof(stream->bs));
  memset(&stream->ss.raw, 0, sizeof(stream->ss.raw));
  memset(&stream->ss.blk, 0, sizeof(stream->ss.blk));

  stream->ss.state = INFL_STATE_NONE;
  stream->ss.bfinal = 0;
  stream->ss.btype  = 0;
  stream->ss.gothdr = false;

  stream->ss.dyn.hlit         = 0;
  stream->ss.dyn.hdist        = 0;
  stream->ss.dyn.hclen        = 0;
  stream->ss.dyn.n            = 0;
  stream->ss.dyn.i            = 0;
  stream->ss.dyn.repeat       = 0;
  stream->ss.dyn.prev         = 0;
  stream->ss.dyn.tlit_valid   = 0;
  stream->ss.dyn.tdist_valid  = 0;
  stream->ss.dyn.codelen_done = 0;
}

UNZ_EXPORT
void
infl_reset(infl_stream_t * __restrict stream,
           void          * __restrict dst,
           uint32_t                   dstlen,
           int                        flags) {
  if (!stream)
    return;

  infl_reset_pool(stream);
  stream->header = NULL;
  stream->bitpos = 0;
  stream->dst    = (uint8_t *)dst;
  stream->dstlen = dstlen;
  stream->dstpos = 0;
  stream->flags  = flags;

  infl_reset_state(stream);
}

UNZ_EXPORT
int
infl_resize_output(infl_stream_t * __restrict stream,
                   void          * __restrict dst,
                   uint32_t                   dstlen) {
  if (!stream || !dst || dstlen < stream->dstpos)
    return UNZ_ERR;

  stream->dst    = (uint8_t *)dst;
  stream->dstlen = dstlen;
  return UNZ_OK;
}

UNZ_EXPORT
uint32_t
infl_output_pos(const infl_stream_t * __restrict stream) {
  return stream ? (uint32_t)stream->dstpos : 0u;
}

UNZ_EXPORT
uint32_t
infl_input_pos(const infl_stream_t * __restrict stream) {
  const unz_chunk_t *chunk;
  size_t pos;
  unsigned unread;

  if (!stream || !stream->bs.chunk)
    return 0u;

  pos = 0;
  for (chunk = stream->start; chunk && chunk != stream->bs.chunk; chunk = chunk->next)
    pos += (size_t)(chunk->end - chunk->p);

  if (chunk) {
    const uint8_t *p = stream->bs.p;

    if (p < chunk->p)
      p = chunk->p;
    else if (p > chunk->end)
      p = chunk->end;
    pos += (size_t)(p - chunk->p);
  } else {
    pos = stream->srclen;
  }

  unread = stream->bs.nbits + stream->bs.npbits;
  if ((size_t)(unread >> 3) < pos)
    pos -= (size_t)(unread >> 3);
  else
    pos = 0;

  return pos > UINT32_MAX ? UINT32_MAX : (uint32_t)pos;
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
    if (stream->chunk_buffers[i]) ALIGNED_FREE(stream->chunk_buffers[i]);
    if (stream->chunk_pool[i])    free(stream->chunk_pool[i]);
  }

  /* free chunk structure pool */
  for (i = 0; i < UNZ_CHUNK_STRUCT_POOL_SIZE; i++) {
    if (stream->chunk_struct_pool[i]) free(stream->chunk_struct_pool[i]);
  }

  free(stream);
}

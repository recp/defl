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
#include "apicommon.h"

#define UNFINISHED()                         DONATE();return UNZ_UNFINISHED;
#define UNFINISHED_BLK() stream->dstpos=dpos;DONATE();return UNZ_UNFINISHED;
#define SOFTBITS(XXX)    if(bs.nbits){break;}else{XXX;}
#define REQBITS(XXX,req) if(bs.nbits>=req){break;}else{XXX;}

#define REFILL_STREAM_X(req,REQQ)                                         \
  if (unlikely(bs.nbits < (req))) {                                           \
    int take;                                                                 \
    do {                                                                      \
      if ((take = min(64 - bs.nbits, bs.npbits)) != 64) {                     \
        bs.bits   |= (bs.pbits&(((bitstream_t)1<<take)-1)) << bs.nbits;       \
        bs.pbits >>= take;                                                    \
        bs.nbits  += take;                                                    \
        bs.npbits -= take;                                                    \
      } else {                                                                \
        bs.bits    = bs.pbits;                                                \
        bs.nbits   = bs.npbits;                                               \
        bs.pbits   = 0;                                                       \
        bs.npbits  = 0;                                                       \
      }                                                                       \
      if (unlikely(!bs.npbits)) {                                             \
        if (unlikely(bs.p >= bs.end)) {                                       \
          if (bs.chunk && bs.chunk->next) {                                   \
            bs.chunk = bs.chunk->next;                                        \
            bs.p     = bs.chunk->p;                                           \
            bs.end   = bs.chunk->end;                                         \
            if (!bs.p || !bs.end) { REQQ; }                                   \
          } else { REQQ; }                                                    \
        }                                                                     \
        bs.npbits=huff_read(&bs.p,&bs.pbits,bs.end);                          \
      }                                                                       \
    } while (unlikely(bs.nbits < (req) && bs.npbits));                        \
  }

#define REFILL_STREAM_BLK(req)      REFILL_STREAM_X(req,SOFTBITS(UNFINISHED_BLK()))
#define REFILL_STREAM(req)          REFILL_STREAM_X(req,SOFTBITS(UNFINISHED()))
#define REFILL_STREAM_REQ(req)      REFILL_STREAM_X(req,REQBITS(UNFINISHED(),req))

static UNZ_HOT
UnzResult
infl_strm_raw(defl_stream_t * __restrict stream) {
  uint8_t        *dst;
  const uint8_t  *p;
  unz__bitstate_t bs;
  unsigned        dpos, dlen, nbytes, chkrem, n, remlen, i, simdlen;
  uint32_t        header, val;
  uint16_t        len, nlen;

  /* check if we're resuming from saved state */
  if (stream->ss.raw.resuming) {
    /* restore saved state */
    len    = stream->ss.raw.len;
    remlen = stream->ss.raw.remlen;
    dpos   = (unsigned)stream->dstpos;
    dlen   = stream->dstlen;
    dst    = stream->dst + dpos;
    RESTORE();
    goto resume_copy;
  }

  dpos = (unsigned)stream->dstpos;
  dlen = stream->dstlen;
  (void)simdlen; /* suppress unused warn */

  RESTORE();

  /* align to byte boundary */
  if (bs.nbits & 7) {
    int shift = bs.nbits & 7;
    bs.bits >>= shift;
    bs.nbits -= shift;
  }

  /* need 32 bits for header */
  REFILL_STREAM(32);
  if (bs.nbits < 32) {
    DONATE();
    return UNZ_UNFINISHED;
  }

  header = (uint32_t)bs.bits;
  CONSUME(32);
  
  len  = header & 0xFFFF;
  nlen = header >> 16;

  if (unlikely((len^(uint16_t)~nlen))) return UNZ_ERR;
  if (unlikely(dpos+len > dlen))       return UNZ_EFULL;

  remlen = len;
  dst    = stream->dst + dpos;

  /* save state for potential resume */
  stream->ss.raw.len    = len;
  stream->ss.raw.remlen = remlen;

resume_copy:
  /* flush bs.bits */
  nbytes = bs.nbits >> 3;
  if (nbytes > 0 && remlen > 0) {
    nbytes = (nbytes < remlen) ? nbytes : remlen;
    if (nbytes <= 4) {
      val = (uint32_t)bs.bits;
      switch (nbytes) {
        case 4: dst[3] = (uint8_t)(val >> 24); /* fall through */
        case 3: dst[2] = (uint8_t)(val >> 16); /* fall through */
        case 2: dst[1] = (uint8_t)(val >> 8);  /* fall through */
        case 1: dst[0] = (uint8_t)val;
      }
    } else {
      for (i = 0; i < nbytes; i++) dst[i] = (uint8_t)(bs.bits >> (i << 3));
    }
    bs.bits >>= (nbytes << 3);
    bs.nbits -= (int)(nbytes << 3);
    dst      += nbytes;
    remlen   -= nbytes;
  }

  /* flush bs.pbits */
  nbytes = bs.npbits >> 3;
  if (nbytes > 0 && remlen > 0) {
    nbytes     = (nbytes < remlen) ? nbytes : remlen;
    for (i = 0; i < nbytes; i++) dst[i] = (uint8_t)(bs.pbits >> (i << 3));
    bs.pbits >>= (nbytes << 3);
    bs.npbits -= (int)(nbytes << 3);
    dst       += nbytes;
    remlen    -= nbytes;
  }

  while (remlen > 0) {
    if (bs.p >= bs.end) {
      /* first check if current chunk was extended */
      if (bs.chunk == stream->end && stream->end->end > bs.end) {
        bs.end = stream->end->end;
      } else if (!bs.chunk || !bs.chunk->next || !bs.chunk->next->p || !bs.chunk->next->end) {
        /* no more data available - save state and return */
        stream->ss.raw.resuming = 1;
        stream->ss.raw.remlen   = remlen;
        stream->dstpos             = (size_t)(dst - stream->dst);
        DONATE();
        return UNZ_UNFINISHED;
      } else {
        /* advance to next chunk */
        bs.chunk = bs.chunk->next;
        bs.p     = bs.chunk->p;
        bs.end   = bs.chunk->end;
      }
    }

    p      = bs.p;
    chkrem = (unsigned)(size_t)(bs.end - p);

    if (likely(chkrem > 0)) {
      n = (chkrem < remlen) ? chkrem : remlen;

#if defined(__builtin_prefetch)
      if (n >= 256) {
        __builtin_prefetch(p + 64, 0, 0);
      }
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
      if (n >= 64) {
        simdlen = n & ~15UL;
        for (i = 0; i < simdlen; i += 16) {
          vst1q_u8(dst + i, vld1q_u8(p + i));
        }
        p      += simdlen;
        dst    += simdlen;
        remlen -= simdlen;
        n      -= simdlen;
      }
#elif defined(__AVX2__)
      if (n >= 64) {
        simdlen = n & ~31UL;
        for (i = 0; i < simdlen; i += 32) {
          _mm256_storeu_si256((__m256i*)(dst + i),
                              _mm256_loadu_si256((__m256i*)(p + i)));
        }
        p      += simdlen;
        dst    += simdlen;
        remlen -= simdlen;
        n      -= simdlen;
      }
#endif
      /* copy remaining bytes */
      if (n > 0) {
        i = 0;

        /* check if both pointers are 8-byte aligned for fast copy */
        if (((uintptr_t)p&7) == 0 && ((uintptr_t)dst&7) == 0) {
          for (; i + 31 < n; i += 32) {
            *(uint64_t*)(dst+i)    = *(uint64_t*)(p+i);
            *(uint64_t*)(dst+i+8)  = *(uint64_t*)(p+i+8);
            *(uint64_t*)(dst+i+16) = *(uint64_t*)(p+i+16);
            *(uint64_t*)(dst+i+24) = *(uint64_t*)(p+i+24);
          }

          for (; i + 7 < n; i += 8)
            *(uint64_t*)(dst + i) = *(uint64_t*)(p + i);
        }
        
        /* remaining bytes or unaligned case */
        for (; i < n; i++) dst[i] = p[i];
        
        p      += n;
        dst    += n;
        remlen -= n;
      }
      bs.p = p;
    }
  }

  /* successfully completed */
  stream->dstpos             = dpos + len;
  stream->ss.raw.resuming = 0;  /* clear resume flag */

  DONATE();
  return UNZ_OK;
}

static UNZ_HOT
UnzResult
infl_strm_blk(defl_stream_t          * __restrict stream,
              const huff_table_ext_t * __restrict tlit,
              const huff_table_ext_t * __restrict tdist) {
  uint8_t * __restrict dst;
  size_t  * __restrict dst_pos;
  unz__bitstate_t      bs;
  size_t               dst_cap, dpos;
  unsigned             len, dist, src;
  uint_fast16_t        lsym;
  uint8_t              used;
  block_decode_state_t state;
  unsigned             saved_len, saved_dist, saved_src, copy_remaining;

  dst     = stream->dst;
  dst_cap = stream->dstlen;
  dst_pos = &stream->dstpos;
  dpos    = *dst_pos;

  RESTORE();

  /* restore block state if resuming */
  state          = stream->ss.blk.state;
  saved_len      = stream->ss.blk.len;
  saved_dist     = stream->ss.blk.dist;
  saved_src      = stream->ss.blk.src;
  copy_remaining = stream->ss.blk.copy_remaining;

  switch (state) {
    case BLOCK_STATE_NONE:
    case BLOCK_STATE_LITERAL:
      break;
    case BLOCK_STATE_LENGTH:
      len = saved_len;
      goto state_distance;
    case BLOCK_STATE_BACKREF:
      len  = copy_remaining;
      dist = saved_dist;
      src  = saved_src;
      goto resume_backref;
  }

  while (true) {
    REFILL_STREAM_BLK(21);

    lsym = huff_decode_lsb_extof(tlit, bs.bits, &used, &len, 257);
    if (!used || used > bs.nbits) {
      UNFINISHED_BLK();
    }

    if (lsym > 285) {
      *dst_pos = dpos;
      DONATE();
      return UNZ_ERR; /* invalid symbol */
    }

    CONSUME(used);

    if (lsym < 256) {
      /* literal byte */
      if (unlikely(dpos >= dst_cap)) {
        *dst_pos = dpos;
        DONATE();
        return UNZ_EFULL;
      }
      dst[dpos++] = (uint8_t)lsym;

      /* clear state after successful literal */
      stream->ss.blk.state = BLOCK_STATE_NONE;
      continue;
    } else if (unlikely(lsym == 256)) {
      /* eof */
      break;
    }

    stream->ss.blk.state = BLOCK_STATE_LENGTH;
    stream->ss.blk.len   = len;

  state_distance:
    REFILL_STREAM_BLK(29);

    dist = huff_decode_lsb_ext(tdist, bs.bits, &used);
    if (!used || used > bs.nbits) {
      UNFINISHED_BLK();
    }

    if (unlikely(dist > dpos)) {
      *dst_pos = dpos;
      DONATE();
      return UNZ_ERR; /* validate distance */
    }

    CONSUME(used);

    if (unlikely((dpos + len) > dst_cap)) {
      *dst_pos = dpos;
      DONATE();
      return UNZ_EFULL;
    }

    src = (unsigned)(dpos - dist);

  state_backref:
    /* save state for potential pause */
    stream->ss.blk.state          = BLOCK_STATE_BACKREF;
    stream->ss.blk.len            = len;
    stream->ss.blk.dist           = dist;
    stream->ss.blk.src            = src;
    stream->ss.blk.copy_remaining = len;

  resume_backref:
    /* output back-reference */
    if (dist == 1) {
      used = dst[dpos - 1];
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
      if (len >= 16) {
        uint8x16_t value = vdupq_n_u8(used);
        do {
          vst1q_u8(&dst[dpos], value);
          len-=16;dpos+=16;
        } while (len >= 16);
      }
#endif
      while (len >= 8) {
        dst[dpos]   = used;
        dst[dpos+1] = used;
        dst[dpos+2] = used;
        dst[dpos+3] = used;
        dst[dpos+4] = used;
        dst[dpos+5] = used;
        dst[dpos+6] = used;
        dst[dpos+7] = used;
        len-=8;dpos+=8;
      }
      while (len >= 4) {
        dst[dpos]   = used;
        dst[dpos+1] = used;
        dst[dpos+2] = used;
        dst[dpos+3] = used;
        len-=4;dpos+=4;
      }
      switch (len) {
        case 3: dst[dpos+2] = used; /* fall through */
        case 2: dst[dpos+1] = used; /* fall through */
        case 1: dst[dpos]   = used; break;
        case 0:                     break;
      }
      dpos += len;
    } else {
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
      if (len >= 16 && dist >= 16) {
        do {
          vst1q_u8(&dst[dpos], vld1q_u8(&dst[src]));
          len-=16;dpos+=16;src+=16;
        } while (len >= 16);
      }
#endif
      while (len >= 4) {
        dst[dpos]   = dst[src];
        dst[dpos+1] = dst[src+1];
        dst[dpos+2] = dst[src+2];
        dst[dpos+3] = dst[src+3];
        len-=4;dpos+=4;src+=4;
      }
      switch (len) {
        case 3: dst[dpos+2] = dst[src+2]; /* fall through */
        case 2: dst[dpos+1] = dst[src+1]; /* fall through */
        case 1: dst[dpos]   = dst[src]; break;
        case 0:                         break;
      }
      dpos += len;
    }

    /* clear state after successful backref copy */
    stream->ss.blk.state          = BLOCK_STATE_NONE;
    stream->ss.blk.len            = 0;
    stream->ss.blk.dist           = 0;
    stream->ss.blk.src            = 0;
    stream->ss.blk.copy_remaining = 0;
  }

  *dst_pos = dpos;

  stream->ss.blk.state          = BLOCK_STATE_NONE;
  stream->ss.blk.len            = 0;
  stream->ss.blk.dist           = 0;
  stream->ss.blk.src            = 0;
  stream->ss.blk.copy_remaining = 0;

  DONATE();
  return UNZ_OK;
}

UNZ_EXPORT
int
infl_stream(infl_stream_t * __restrict stream,
            const void    * __restrict src,
            uint32_t                   srclen) {
  static huff_table_ext_t _tlitl_s={0},_tdist_s={0};
  static bool             _init_s=false;

  unz__bitstate_t bs;
  uint_fast8_t    btype, bfinal=0;

  /* add new data */
  if (src && srclen > 0) {
    infl_include(stream, src, srclen);
    /* current chunk is extended */
    if (stream->bs.end == stream->end->end - srclen) {
      stream->bs.end = stream->end->end;
    }
  }

  /* Check if already done */
  if (stream->ss.state == INFL_STATE_DONE)
    return UNZ_OK;

  /* initial setup */
  if (!stream->bs.chunk && !(stream->bs.chunk = stream->start))
    return UNZ_NOOP;

  /* if no data and not in middle of processing, return NOOP */
  if (!src && srclen == 0 && stream->ss.state == INFL_STATE_NONE)
    return UNZ_NOOP;

  /* resume from saved state */
  if (stream->ss.state != INFL_STATE_NONE) {
    bfinal = stream->ss.bfinal;
    RESTORE();

    /* jump to appropriate state */
    switch (stream->ss.state) {
      case INFL_STATE_HEADER:                 goto state_header;
      case INFL_STATE_BLOCK_HEADER:           goto state_blk_head;
      case INFL_STATE_RAW:                    goto state_raw;
      case INFL_STATE_FIXED:                  goto state_fixed;
      case INFL_STATE_DYNAMIC_HEADER:
      case INFL_STATE_DYNAMIC_CODELEN:
      /* for dynamic states, we need to enter through case 2 to load state */
      case INFL_STATE_DYNAMIC_BLOCK: btype=2; goto state_blk_head_with_type;
      case INFL_STATE_DONE:                   return UNZ_OK;  /* already done */
      default:                                break;
    }
  }

  /* initialize static tables for streaming */
  if (!_init_s) {
    if (!huff_init_lsb_extof(&_tlitl_s,fxd,NULL,lvals,257,288) ||
        !huff_init_lsb_ext(&_tdist_s,fxd+288,NULL,dvals,32)) {
      return UNZ_ERR;
    }
    _init_s = true;
  }

  /* initialize bit reader if needed */
  if (stream->ss.state == INFL_STATE_NONE) {
    stream->bs.p      = stream->start->p;
    stream->bs.end    = stream->start->end;
    stream->bs.chunk  = stream->start;
    stream->bs.nbits  = 0;
    stream->bs.bits   = 0;
    stream->bs.npbits = 0;
    stream->bs.pbits  = 0;
  }
  RESTORE();

state_header:
  if (stream->flags == 1 && unlikely(!stream->header)) {
    unz_chunk_t *tmp;
    size_t       avail;

    stream->ss.state = INFL_STATE_HEADER;

    /* count available bytes */
    avail = 0;
    tmp   = stream->bs.chunk;
    while (tmp && avail < 6) {
      avail += (tmp->end - tmp->p);
      tmp    = tmp->next;
    }

    /* wait for at least 6 bytes to handle worst case (header + dict) */
    if (avail < 6) {
      DONATE();
      return UNZ_UNFINISHED;
    }

    /* process header - guaranteed to have enough data */
    zlib_header(stream, &stream->bs.chunk, true);
  }

  bfinal = stream->ss.bfinal;
  
state_blk_head:
  while (!bfinal && bs.chunk) {
    stream->ss.state = INFL_STATE_BLOCK_HEADER;

    REFILL_STREAM(3);
    bfinal = bs.bits & 0x1;
    btype  = (bs.bits >> 1) & 0x3;
    CONSUME(3);

  state_blk_head_with_type:
    /* save block state */
    stream->ss.bfinal = bfinal;
    stream->ss.btype  = btype;

    switch (btype) {
      case 0:
state_raw:
        stream->ss.state = INFL_STATE_RAW;
        DONATE();
        {
          int res = infl_strm_raw(stream);
          if (res == UNZ_UNFINISHED) return UNZ_UNFINISHED;
          if (res < UNZ_OK)          goto   err;
        }
        RESTORE();
        break;
        
      case 1:
state_fixed:
        stream->ss.state = INFL_STATE_FIXED;
        DONATE();
        {
          int res = infl_strm_blk(stream, &_tlitl_s, &_tdist_s);
          if (res == UNZ_UNFINISHED) return UNZ_UNFINISHED;
          if (res < UNZ_OK)          goto   err;
        }
        RESTORE();
        break;
      case 2: {
        huff_fast_entry_t tcodelen[HUFF_FAST_TABLE_SIZE] = {0};
        huff_fast_entry_t fe;
        int               i, n, hclen, hlit, hdist, repeat, prev;

        /* check if we're resuming */
        if (stream->ss.state >= INFL_STATE_DYNAMIC_HEADER) {
          /* Restore saved state */
          hlit   = stream->ss.dyn.hlit;
          hdist  = stream->ss.dyn.hdist;
          hclen  = stream->ss.dyn.hclen;
          n      = stream->ss.dyn.n;
          i      = stream->ss.dyn.i;
          repeat = stream->ss.dyn.repeat;
          prev   = stream->ss.dyn.prev;

          /* jump to appropriate resume point */
          switch (stream->ss.state) {
            case INFL_STATE_DYNAMIC_HEADER:
              /* jump to resume if we've already read the header */
              if (stream->ss.dyn.hlit > 0) { goto state_dyn_header_resume; }
              /* otherwise fall through to read header */
              break;
            case INFL_STATE_DYNAMIC_CODELEN:
              /* rebuild tcodelen table from saved codelens */
              if (!huff_init_fast_lsb(tcodelen,stream->ss.dyn.codelens,NULL,MAX_CODELEN_CODES))
                goto err;
              goto state_dyn_codelen;
            case INFL_STATE_DYNAMIC_BLOCK:
              goto state_dyn_block;
            default:
              break;
          }
        }

        /* fresh start - read header */
        stream->ss.state = INFL_STATE_DYNAMIC_HEADER;

        REFILL_STREAM_REQ(14);
        hlit  = (bs.bits & 0x1F) + 257;
        hdist = ((bs.bits >> 5) & 0x1F) + 1;
        hclen = ((bs.bits >> 10) & 0xF) + 4;
        n     = hlit + hdist;
        CONSUME(14);

        if (n > MAX_LITLEN_CODES + MAX_DIST_CODES) goto err;

        /* save state */
        stream->ss.dyn.hlit  = hlit;
        stream->ss.dyn.hdist = hdist;
        stream->ss.dyn.hclen = hclen;
        stream->ss.dyn.n     = n;
        stream->ss.dyn.i     = 0;
        stream->ss.dyn.codelen_complete = 0;

        /* Clear arrays */
        memset(stream->ss.dyn.codelens,0,sizeof(stream->ss.dyn.codelens));
        memset(stream->ss.dyn.lens,    0,sizeof(stream->ss.dyn.lens));

      state_dyn_header_resume:
        for (i = stream->ss.dyn.i; i < hclen; i++) {
          REFILL_STREAM_REQ(3);
          stream->ss.dyn.codelens[ord[i]] = bs.bits & 0x7;
          CONSUME(3);
          stream->ss.dyn.i = i + 1;
        }

        if (!huff_init_fast_lsb(tcodelen,stream->ss.dyn.codelens,NULL,MAX_CODELEN_CODES))
          goto err;

        stream->ss.dyn.codelen_complete = 1;
        stream->ss.dyn.i                = 0;  /* reset i for next phase */

      state_dyn_codelen:
        stream->ss.state = INFL_STATE_DYNAMIC_CODELEN;
        i                    = stream->ss.dyn.i;

        while (i < n) {
          REFILL_STREAM_REQ(21);

          fe = tcodelen[(uint8_t)bs.bits];
          if (unlikely(!fe.len || fe.sym > 18)) goto err;
          CONSUME(fe.len);

          switch (fe.sym) {
            default:
              stream->ss.dyn.lens[i++] = (uint_fast8_t)fe.sym;
              stream->ss.dyn.i = i;
              break;

            case 16: {
              if (stream->ss.dyn.repeat > 0) {
                /* resuming mid-repeat */
                repeat = stream->ss.dyn.repeat;
                prev   = stream->ss.dyn.prev;
              } else {
                /* new repeat sequence */
                repeat = 3 + (bs.bits & 0x3);
                CONSUME(2);
                if (i == 0 || i + repeat > n) goto err;
                prev = stream->ss.dyn.lens[i - 1];
                stream->ss.dyn.prev = prev;
              }

              /* process repeat, saving state in case we need to pause */
              while (repeat > 0 && i < n) {
                stream->ss.dyn.lens[i++] = prev;
                repeat--;
                stream->ss.dyn.i = i;
                stream->ss.dyn.repeat = repeat;
              }

              if (repeat == 0) {
                /* clear repeat state */
                stream->ss.dyn.repeat = 0;
              }
            } break;
            case 17:
              repeat = 3 + (bs.bits & 0x7);
              CONSUME(3);
              if (i + repeat > n) goto err;
              i += repeat;
              stream->ss.dyn.i = i;
              break;
            case 18:
              repeat = 11 + (bs.bits & 0x7F);
              CONSUME(7);
              if (i + repeat > n) goto err;
              i += repeat;
              stream->ss.dyn.i = i;
              break;
          }
        }
        /* build literal/length and distance tables */
        if (!huff_init_lsb_extof(&stream->ss.dyn.tlit,stream->ss.dyn.lens,NULL,lvals,257,hlit) ||
            !huff_init_lsb_ext(&stream->ss.dyn.tdist, stream->ss.dyn.lens+hlit,NULL,dvals,hdist))
          goto err;

        stream->ss.dyn.tlit_valid  = 1;
        stream->ss.dyn.tdist_valid = 1;

      state_dyn_block:
        stream->ss.state = INFL_STATE_DYNAMIC_BLOCK;
        DONATE();
        {
        int res = infl_strm_blk(stream,
                                    &stream->ss.dyn.tlit,
                                    &stream->ss.dyn.tdist);
        if (res == UNZ_UNFINISHED) return UNZ_UNFINISHED;
        if (res < UNZ_OK)          goto   err;
        }
        RESTORE();

        /* clear dynamic state for next block */
        stream->ss.dyn.tlit_valid       = 0;
        stream->ss.dyn.tdist_valid      = 0;
        stream->ss.dyn.codelen_complete = 0;
      } break;

      default:
        goto err;
    }
  }

  stream->ss.state = INFL_STATE_DONE;
  DONATE();
  return UNZ_OK;
  
err:
  stream->ss.state = INFL_STATE_NONE;  /* reset on error */
  return UNZ_ERR;
}

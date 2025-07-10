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

#define REFILL_STREAM(req)                                                    \
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
        if (unlikely(bs.p >= bs.end)                                          \
            && (!bs.chunk || !(bs.chunk = bs.chunk->next)                     \
                || !(bs.p = bs.chunk->p) || !(bs.end = bs.chunk->end))) {     \
          if(bs.nbits >= (req)) break;                                        \
          DONATE();                                                           \
          return UNZ_UNFINISHED;                                              \
        }                                                                     \
        bs.npbits=huff_read(&bs.p,&bs.pbits,bs.end);                          \
      }                                                                       \
    } while (unlikely(bs.nbits < (req) && bs.npbits));                        \
  }


static UNZ_HOT
UnzResult
infl_raw_stream(defl_stream_t * __restrict stream) {
  uint8_t        *dst;
  const uint8_t  *p;
  unz__bitstate_t bs;
  unsigned        dpos, dlen, nbytes, chkrem, n, remlen, i, simdlen;
  uint32_t        header, val;
  uint16_t        len, nlen;
  
  /* check if we're resuming from saved state */
  if (stream->raw_state.resuming) {
    /* restore saved state */
    len    = stream->raw_state.len;
    remlen = stream->raw_state.remlen;
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
  if (bs.nbits < 32) {
    REFILL_STREAM(32);
  }
  
  header = (uint32_t)bs.bits;
  CONSUME(32);
  
  len  = header & 0xFFFF;
  nlen = header >> 16;

  if (unlikely((len^(uint16_t)~nlen)|(dpos+len > dlen)))
    return UNZ_ERR;

  remlen = len;
  dst    = stream->dst + dpos;

  /* save state for potential resume */
  stream->raw_state.len    = len;
  stream->raw_state.remlen = remlen;

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
      for (i = 0; i < nbytes; i++)
        dst[i] = (uint8_t)(bs.bits >> (i << 3));
    }
    bs.bits >>= (nbytes << 3);
    bs.nbits -= (int)(nbytes << 3);
    dst      += nbytes;
    remlen   -= nbytes;
  }

  /* flush bs.pbits */
  nbytes = bs.npbits >> 3;
  if (nbytes > 0 && remlen > 0) {
    nbytes = (nbytes < remlen) ? nbytes : remlen;
    for (i = 0; i < nbytes; i++) {
      dst[i] = (uint8_t)(bs.pbits >> (i << 3));
    }
    bs.pbits >>= (nbytes << 3);
    bs.npbits -= (int)(nbytes << 3);
    dst       += nbytes;
    remlen    -= nbytes;
  }

  while (remlen > 0) {
    if (bs.p >= bs.end) {
      if (!(bs.chunk = bs.chunk->next) || !bs.chunk->p || !bs.chunk->end) {
        /* no more data available - save state and return */
        stream->raw_state.resuming = 1;
        stream->raw_state.remlen   = remlen;
        stream->dstpos = dpos + (len - remlen);
        DONATE();
        return UNZ_UNFINISHED;
      }
      bs.p   = bs.chunk->p;
      bs.end = bs.chunk->end;
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
  stream->raw_state.resuming = 0;  /* clear resume flag */

  DONATE();
  return UNZ_OK;
}

static UNZ_HOT
UnzResult
infl_block_stream(defl_stream_t          * __restrict stream,
                  const huff_table_ext_t * __restrict tlit,
                  const huff_table_ext_t * __restrict tdist) {
  uint8_t * __restrict dst;
  size_t  * __restrict dst_pos;
  unz__bitstate_t bs;
  size_t          dst_cap, dpos, src;
  unsigned        len,  dist;
  uint_fast16_t   lsym;
  uint8_t         used;

  dst     = stream->dst;
  dst_cap = stream->dstlen;
  dst_pos = &stream->dstpos;
  dpos    = *dst_pos;

  RESTORE();

  while (true) {
    /* decode literal/length symbol - need up to 21 bits */
    REFILL_STREAM(21);
    
    lsym = huff_decode_lsb_extof(tlit, bs.bits, &used, &len, 257);
    if (!used || lsym > 285)
      return UNZ_ERR; /* invalid symbol */

    CONSUME(used);

    if (lsym < 256) {
      /* literal byte */
      if (unlikely(dpos >= dst_cap))
        return UNZ_EFULL;
      dst[dpos++] = (uint8_t)lsym;
      continue;
    } else if (unlikely(lsym == 256)) {
      /* eof */
      break;
    }

    /* Distance code - need up to 29 bits */
    REFILL_STREAM(29);
    
    dist = huff_decode_lsb_ext(tdist, bs.bits, &used);

    /* validate distance */
    if (unlikely(!used || dist > dpos))
      return UNZ_ERR;
    CONSUME(used);

    if (unlikely((dpos + len) > dst_cap))
      return UNZ_EFULL;

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
      src = dpos - dist;
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
  }

  *dst_pos = dpos;

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
  uint_fast8_t    btype, bfinal;

  /* add new data */
  if (src && srclen > 0) {
    infl_include(stream, src, srclen);
  }

  /* resume from saved state */
  if (stream->stream_state != INFL_STATE_NONE) {
    bfinal = stream->stream_bfinal;
    RESTORE();

    /* jump to appropriate state */
    switch (stream->stream_state) {
      case INFL_STATE_HEADER:                 goto state_header;
      case INFL_STATE_BLOCK_HEADER:           goto state_blk_head;
      case INFL_STATE_RAW:                    goto state_raw;
      case INFL_STATE_FIXED:                  goto state_fixed;
      case INFL_STATE_DYNAMIC_HEADER:
      case INFL_STATE_DYNAMIC_CODELEN:
      /* for dynamic states, we need to enter through case 2 to load state */
      case INFL_STATE_DYNAMIC_BLOCK: btype=2; goto state_blk_head_with_type;
      default:                                break;
    }
  }

  /* initial setup */
  if (!stream->bs.chunk && !(stream->bs.chunk = stream->start))
    return UNZ_NOOP;

  /* initialize static tables for streaming */
  if (!_init_s) {
    if (!huff_init_lsb_extof(&_tlitl_s,fxd,NULL,lvals,257,288) ||
        !huff_init_lsb_ext(&_tdist_s,fxd+288,NULL,dvals,32)) {
      return UNZ_ERR;
    }
    _init_s = true;
  }

  /* initialize bit reader if needed */
  if (stream->stream_state == INFL_STATE_NONE) {
    stream->bs.p   = stream->start->p;
    stream->bs.end = stream->start->end;
  }
  RESTORE();

state_header:
  if (stream->flags == 1 && unlikely(!stream->header)) {
    unz_chunk_t *tmp;
    size_t       avail;

    stream->stream_state = INFL_STATE_HEADER;

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

  bfinal = stream->stream_bfinal;
  
state_blk_head:
  while (!bfinal && bs.chunk) {
    stream->stream_state = INFL_STATE_BLOCK_HEADER;

    REFILL_STREAM(3);
    bfinal = bs.bits & 0x1;
    btype  = (bs.bits >> 1) & 0x3;
    CONSUME(3);

  state_blk_head_with_type:
    /* save block state */
    stream->stream_bfinal = bfinal;
    stream->stream_btype  = btype;

    switch (btype) {
      case 0:
state_raw:
        stream->stream_state = INFL_STATE_RAW;
        DONATE();
        {
          int res = infl_raw_stream(stream);
          if (res == UNZ_UNFINISHED) return UNZ_UNFINISHED;
          if (res < UNZ_OK)          goto   err;
        }
        RESTORE();
        break;
        
      case 1:
state_fixed:
        stream->stream_state = INFL_STATE_FIXED;
        DONATE();
        {
          int res = infl_block_stream(stream, &_tlitl_s, &_tdist_s);
          if (res == UNZ_UNFINISHED) return UNZ_UNFINISHED;
          if (res < UNZ_OK)          goto   err;
        }
        RESTORE();
        break;
      case 2: {
        union {
          uint_fast8_t codelens[MAX_CODELEN_CODES];
          uint_fast8_t lens[MAX_LITLEN_CODES + MAX_DIST_CODES];
        } lens={0};
        huff_fast_entry_t tcodelen[HUFF_FAST_TABLE_SIZE];
        huff_fast_entry_t fe;
        int               i=0,n=0,hclen=0,hlit=0,hdist=0,repeat=0,prev=0;

        stream->stream_state = INFL_STATE_DYNAMIC_HEADER;

        /* load saved dynamic state if resuming */
        if (stream->dyn_state.hlit > 0) {
          hlit  = stream->dyn_state.hlit;
          hdist = stream->dyn_state.hdist;
          hclen = stream->dyn_state.hclen;
          n     = stream->dyn_state.n;
          i     = stream->dyn_state.i;
          memcpy(lens.lens, stream->dyn_state.lens, sizeof(lens.lens));

          if (stream->stream_state == INFL_STATE_DYNAMIC_CODELEN) {
            /* rebuild tcodelen table when resuming */
            if (!huff_init_fast_lsb(tcodelen, lens.codelens, NULL, MAX_CODELEN_CODES))
              goto err;
            goto state_dyn_codelen;
          } else if (stream->stream_state == INFL_STATE_DYNAMIC_BLOCK) {
            /* ensure tables are valid when resuming */
            if (stream->dyn_state.tlit_valid
                && stream->dyn_state.tdist_valid) { goto state_dyn_block; }
            else {
              goto err; /* tables not initialized */
            }
          }
        }

        REFILL_STREAM(14);
        hlit  = (bs.bits & 0x1F) + 257;
        hdist = ((bs.bits >> 5) & 0x1F) + 1;
        hclen = ((bs.bits >> 10) & 0xF) + 4;
        n     = hlit + hdist;
        CONSUME(14);

        if (n > MAX_LITLEN_CODES + MAX_DIST_CODES)
          goto err;

        /* save state */
        stream->dyn_state.hlit  = hlit;
        stream->dyn_state.hdist = hdist;
        stream->dyn_state.hclen = hclen;
        stream->dyn_state.n     = n;

        for (i = 0; i < hclen; i++) {
          REFILL_STREAM(3);
          lens.codelens[ord[i]] = bs.bits & 0x7;
          CONSUME(3);
        }

        if (!huff_init_fast_lsb(tcodelen,lens.codelens,NULL,MAX_CODELEN_CODES))
          goto err;

        for (i = MAX_CODELEN_CODES; i;) lens.codelens[--i] = 0;
        stream->dyn_state.i = 0; /* reset i before starting */

      state_dyn_codelen:
        stream->stream_state = INFL_STATE_DYNAMIC_CODELEN;
        i                    = stream->dyn_state.i;

        while (i < n) {
          REFILL_STREAM(14);

          fe = tcodelen[(uint8_t)bs.bits];
          if (unlikely(!fe.len || fe.sym > 18)) goto err;
          CONSUME(fe.len);

          switch (fe.sym) {
            default: lens.lens[i++] = (uint_fast8_t)fe.sym; break;
            case 16: {
              repeat = 3 + (bs.bits & 0x3); CONSUME(2);
              if (i == 0 || i + repeat > n) goto err;
              prev = lens.lens[i - 1];
              while (repeat--) lens.lens[i++] = prev;
            } break;
            case 17: i+=(3  + (bs.bits & 0x7));  CONSUME(3); break;
            case 18: i+=(11 + (bs.bits & 0x7F)); CONSUME(7); break;
          }

          /* save progress */
          stream->dyn_state.i = i;
          memcpy(stream->dyn_state.lens, lens.lens, sizeof(lens.lens));
        }

        if (!huff_init_lsb_extof(&stream->dyn_state.tlit,lens.lens,NULL,lvals,257,hlit) ||
            !huff_init_lsb_ext(&stream->dyn_state.tdist,lens.lens+hlit,NULL,dvals,hdist))
          goto err;

        stream->dyn_state.tlit_valid  = 1;
        stream->dyn_state.tdist_valid = 1;

      state_dyn_block:
        stream->stream_state = INFL_STATE_DYNAMIC_BLOCK;
        DONATE();
        {
          int res = infl_block_stream(stream,
                                      &stream->dyn_state.tlit,
                                      &stream->dyn_state.tdist);
          if (res == UNZ_UNFINISHED) return UNZ_UNFINISHED;
          if (res < UNZ_OK)          goto   err;
        }
        RESTORE();
      } break;

      default:
        goto err;
    }
  }

  stream->stream_state = INFL_STATE_DONE;
  DONATE();
  return UNZ_OK;
  
err:
  return UNZ_ERR;
}

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

  (void)simdlen; /* suppress unused warn */

  RESTORE();

  dpos   = (unsigned)stream->dstpos;
  dlen   = stream->dstlen;
  len    = stream->ss.raw.len;
  remlen = stream->ss.raw.remlen;
  dst    = stream->dst + dpos + (len - remlen);

  if (!stream->ss.raw.resuming) {
    if (!stream->ss.raw.header_read) {
      if (!stream->ss.raw.align_done) {
        /* align to byte boundary */
        if (bs.nbits & 7) {
          int shift = bs.nbits & 7;
          bs.bits >>= shift;
          bs.nbits -= shift;
        }
        stream->ss.raw.align_done = true;
      }

      /* need 32 bits for header: 16 + 16 */
      REFILL_STREAM_REQ(32);
      header = (uint32_t)bs.bits;
      CONSUME(32);

      len  = remlen = header & 0xFFFF;
      nlen = header >> 16;

      /* save state for potential resume */
      stream->ss.raw.len         = len;
      stream->ss.raw.remlen      = len;
      stream->ss.raw.header_read = true;

      if (unlikely((len^(uint16_t)~nlen))) {
        DONATE();
        return UNZ_ERR;
      }

      if (unlikely(dpos+len > dlen)) {
        DONATE();
        return UNZ_EFULL;
      }
    }
  }

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
  stream->ss.raw.resuming    = 0;
  stream->ss.raw.align_done  = 0;
  stream->ss.raw.header_read = 0;
  stream->ss.raw.resuming    = 0;
  stream->ss.raw.len         = 0;
  stream->ss.raw.remlen      = 0;

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
      goto distance;
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

  distance:
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

  /* backref: */
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
      if (len >= 1) {
        dst[dpos] = used;
        switch (len - 1) {
          case 2: dst[dpos+2] = used; /* fall through */
          case 1: dst[dpos+1] = used; break;
          case 0:                     break;
        }
        dpos += len;
      }
    } else {
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
      if (len >= 16 && dist >= 16) {
        do {
          vst1q_u8(&dst[dpos], vld1q_u8(&dst[src]));
          len-=16;dpos+=16;src+=16;
        } while (len >= 16);
      }
#endif
      while (len >= 8) {
        dst[dpos]   = dst[src];
        dst[dpos+1] = dst[src+1];
        dst[dpos+2] = dst[src+2];
        dst[dpos+3] = dst[src+3];
        dst[dpos+4] = dst[src+4];
        dst[dpos+5] = dst[src+5];
        dst[dpos+6] = dst[src+6];
        dst[dpos+7] = dst[src+7];
        len -= 8; dpos += 8; src += 8;
      }
      while (len >= 4) {
        dst[dpos]   = dst[src];
        dst[dpos+1] = dst[src+1];
        dst[dpos+2] = dst[src+2];
        dst[dpos+3] = dst[src+3];
        len-=4;dpos+=4;src+=4;
      }
      if (len >= 1) {
        dst[dpos] = dst[src];
        switch (len - 1) {
          case 2: dst[dpos+2] = dst[src+2]; /* fall through */
          case 1: dst[dpos+1] = dst[src+1]; break;
          case 0:                           break;
        }
        dpos += len;
      }
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
  UnzResult       res;

  /* add new data */
  if (src && srclen > 0) {
    infl_include(stream, src, srclen);
    /* current chunk is extended */
    if (stream->bs.chunk
        && stream->bs.chunk == stream->end
        && stream->bs.end   == stream->end->end - srclen) {
      stream->bs.end = stream->end->end;
    }
  } else if (!stream->start || !stream->start->p) {
    /* empty data */
    RESTORE();
    goto ok;
  }

  /* check if already done */
  if (stream->ss.state == INFL_STATE_DONE) {
    RESTORE();
    goto ok;
  }

  /* initial setup */
  if (!stream->bs.chunk && !(stream->bs.chunk = stream->start))
    goto noop;

  /* if no data and not in middle of processing, return NOOP */
  if (!src && srclen == 0 && stream->ss.state == INFL_STATE_NONE)
    goto noop;

  /* initialize static tables for streaming */
  if (!_init_s) {
    if (!huff_init_lsb_extof(&_tlitl_s,fxd,NULL,lvals,257,288) ||
        !huff_init_lsb_ext(&_tdist_s,fxd+288,NULL,dvals,32)) {
      goto err;
    }
    _init_s = true;
  }

  /* resume from saved state */
  if (stream->ss.state != INFL_STATE_NONE) {
    bfinal = stream->ss.bfinal;
    RESTORE();

    /* jump to appropriate state */
    switch (stream->ss.state) {
      case INFL_STATE_HEADER:                 goto hdr;
      case INFL_STATE_BLOCK_HEADER:           goto blk_head;
      case INFL_STATE_RAW:                    goto raw;
      case INFL_STATE_FIXED:                  goto fixed;
      case INFL_STATE_DYNAMIC_HEADER:
      case INFL_STATE_DYNAMIC_CODELEN:
      case INFL_STATE_DYNAMIC_BLOCK: btype=2; goto blk_head_resume;
      case INFL_STATE_DONE:                   goto ok;  /* already done */
      default:                                break;
    }
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

hdr:
  if (stream->flags == 1 && unlikely(!stream->header) && !stream->ss.gothdr) {
    unz_chunk_t *tmp;
    size_t       avail;

    stream->ss.state = INFL_STATE_HEADER;

    /* ensure we have a chunk before proceeding */
    if (!stream->bs.chunk || !stream->bs.chunk->p) {
      DONATE();
      return UNZ_UNFINISHED;
    }

    /* count available bytes */
    avail = 0;
    tmp   = stream->bs.chunk;
    while (tmp && avail < 2) {
      avail += (tmp->end - tmp->p);
      tmp    = tmp->next;
    }

    /* wait for at least 6 bytes to handle worst case (header + dict) */
    if (avail < 2) {
      DONATE();
      return UNZ_UNFINISHED;
    }

    /* process header - guaranteed to have enough data */
    zlib_header(stream, &stream->bs.chunk, true);

    stream->bs.p      = stream->bs.chunk->p;
    stream->bs.end    = stream->bs.chunk->end;
    stream->ss.gothdr = true;

    RESTORE();

    if (bs.p == bs.end) {
      DONATE();
      return UNZ_UNFINISHED;
    }
  }

  bfinal = stream->ss.bfinal;
  
blk_head:
  while (!bfinal && bs.chunk) {
    stream->ss.state = INFL_STATE_BLOCK_HEADER;

    REFILL_STREAM_REQ(3);
    bfinal = bs.bits & 0x1;
    btype  = (bs.bits >> 1) & 0x3;
    CONSUME(3);

  blk_head_resume:
    /* save block state */
    stream->ss.bfinal = bfinal;
    stream->ss.btype  = btype;

    switch (btype) {
      case 0:
raw:
        stream->ss.state = INFL_STATE_RAW;
        DONATE();
        {
          res = infl_strm_raw(stream);
          if (res == UNZ_UNFINISHED) return UNZ_UNFINISHED;
          if (res < UNZ_OK)          goto   err;
          RESTORE();
        }
        break;
        
      case 1:
fixed:
        stream->ss.state = INFL_STATE_FIXED;
        DONATE();
        {
          res = infl_strm_blk(stream, &_tlitl_s, &_tdist_s);
          if (res == UNZ_UNFINISHED) return UNZ_UNFINISHED;
          if (res < UNZ_OK)          goto   err;
        }
        RESTORE();
        break;
      case 2: {
        huff_fast_entry_t tcodelen[HUFF_FAST_TABLE_SIZE] = {0};
        huff_fast_entry_t fe;
        int               i, n, hclen, hlit, hdist, repeat, prev;

        if (stream->ss.state == INFL_STATE_DYNAMIC_BLOCK) {
          goto dyn_blk;
        } else if (stream->ss.state == INFL_STATE_DYNAMIC_CODELEN) {
          hlit  = stream->ss.dyn.hlit;
          hdist = stream->ss.dyn.hdist;
          n     = hlit + hdist;

          if (!huff_init_fast_lsb(tcodelen,stream->ss.dyn.codelens,NULL,MAX_CODELEN_CODES))
            goto err;

          i = stream->ss.dyn.i;
          goto dyn_codelen_resume;
        } else if (stream->ss.state == INFL_STATE_DYNAMIC_HEADER && stream->ss.dyn.i > 0) {
          hlit  = stream->ss.dyn.hlit;
          hdist = stream->ss.dyn.hdist;
          hclen = stream->ss.dyn.hclen;
          n     = hlit + hdist;
          i     = stream->ss.dyn.i;
          goto dyn_hdr_resume;
        }

        /* fresh start - read header */
        stream->ss.state = INFL_STATE_DYNAMIC_HEADER;

        REFILL_STREAM_REQ(14);
        hlit  = (bs.bits & 0x1F) + 257;
        hdist = ((bs.bits >> 5) & 0x1F) + 1;
        hclen = ((bs.bits >> 10) & 0xF) + 4;
        n     = hlit + hdist;
        CONSUME(14);

        if (hlit > 286 || hdist > 30 || n > MAX_LITLEN_CODES + MAX_DIST_CODES)
          goto err;

        /* initialize state */
        stream->ss.dyn.hlit   = hlit;
        stream->ss.dyn.hdist  = hdist;
        stream->ss.dyn.hclen  = hclen;
        stream->ss.dyn.n      = n;
        stream->ss.dyn.i      = 0;
        stream->ss.dyn.repeat = 0;
        stream->ss.dyn.prev   = 0;

        /* clear arrays */
        memset(stream->ss.dyn.codelens,0,sizeof(stream->ss.dyn.codelens));
        memset(stream->ss.dyn.lens,    0,sizeof(stream->ss.dyn.lens));
        i = 0;

      dyn_hdr_resume:
        for (; i < hclen; i++) {
          stream->ss.dyn.i = i;
          REFILL_STREAM_REQ(3);
          stream->ss.dyn.codelens[ord[i]] = bs.bits & 0x7;
          CONSUME(3);
        }
        stream->ss.dyn.i = hclen;

        if (!huff_init_fast_lsb(tcodelen,stream->ss.dyn.codelens,NULL,MAX_CODELEN_CODES))
          goto err;

        /* reset for next phase */
        stream->ss.dyn.i = 0;
        i                = 0;

        stream->ss.state = INFL_STATE_DYNAMIC_CODELEN;

      dyn_codelen_resume:
        while (i < n) {
          REFILL_STREAM_REQ(21);

          fe = tcodelen[(uint8_t)bs.bits];
          if (unlikely(!fe.len || fe.sym > 18)) goto err;
          CONSUME(fe.len);

          switch (fe.sym) {
            default:  /* 0-15: literal code length */
              if (fe.sym > 15) goto err;
              stream->ss.dyn.lens[i++] = (uint_fast8_t)fe.sym;
              break;
            case 16: {  /* repeat previous */
              if (stream->ss.dyn.repeat > 0) {
                /* resuming a repeat operation */
                repeat = stream->ss.dyn.repeat;
                prev   = stream->ss.dyn.prev;
              } else {
                /* new repeat */
                if (i == 0) goto err;  /* no previous code length */
                repeat = 3 + (bs.bits & 0x3);
                CONSUME(2);
                prev = stream->ss.dyn.lens[i - 1];
                if (i + repeat > n) goto err;
                /* save state for potential resume */
                stream->ss.dyn.repeat = repeat;
                stream->ss.dyn.prev   = prev;
              }

              /* fill repeat values */
              while (repeat > 0 && i < n) {
                stream->ss.dyn.lens[i++] = prev;
                repeat--;
              }

              /* update or clear repeat state */
              if (repeat > 0) {
                stream->ss.dyn.repeat = repeat;  /* still have more to do */
              } else {
                stream->ss.dyn.repeat = 0;  /* done with this repeat */
                stream->ss.dyn.prev   = 0;
              }
            } break;

            case 17:  /* repeat zero 3-10 times */
              repeat = 3 + (bs.bits & 0x7);
              CONSUME(3);
              if (i + repeat > n) goto err;
              /* lens array already zero-initialized */
              i += repeat;
              break;

            case 18:  /* repeat zero 11-138 times */
              repeat = 11 + (bs.bits & 0x7F);
              CONSUME(7);
              if (i + repeat > n) goto err;
              /* lens array already zero-initialized */
              i += repeat;
              break;
          }

          /* Save progress */
          stream->ss.dyn.i = i;
        }

        /* build literal/length and distance tables */
        if (!huff_init_lsb_extof(&stream->ss.dyn.tlit,stream->ss.dyn.lens,NULL,lvals,257,hlit) ||
            !huff_init_lsb_ext(&stream->ss.dyn.tdist, stream->ss.dyn.lens+hlit,NULL,dvals,hdist))
          goto err;

      dyn_blk:
        stream->ss.state = INFL_STATE_DYNAMIC_BLOCK;

        /* decode the compressed block */
        DONATE();
        res = infl_strm_blk(stream, &stream->ss.dyn.tlit, &stream->ss.dyn.tdist);
        RESTORE();

        if (res == UNZ_UNFINISHED) return UNZ_UNFINISHED;
        if (res < UNZ_OK)          goto err;

        /* success - reset ALL dynamic state for next block */
        memset(&stream->ss.dyn, 0, sizeof(stream->ss.dyn));
      } break;

      default:
        goto err;
    }
  }

  stream->ss.state = INFL_STATE_DONE;

ok:
  DONATE();
  return UNZ_OK;
noop:
  return UNZ_NOOP;
err:
  stream->ss.state = INFL_STATE_NONE;  /* reset on error */
  return UNZ_ERR;
}

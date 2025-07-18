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

#include "apicommon.h"

#define REFILL(req)                                                           \
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
          if(bs.nbits)break;else return UNZ_ERR;                              \
        }                                                                     \
        bs.npbits=huff_read(&bs.p,&bs.pbits,bs.end);                          \
      }                                                                       \
    } while (unlikely(bs.nbits < (req) && bs.npbits));                        \
  }

#if 0
#define REFILL(req)                                                           \
  if (bs.nbits < (req)) {                                                     \
    int take;                                                                 \
    do {                                                                      \
      if (!bs.nbits) {                                                        \
        bs.bits    = bs.pbits;                                                \
        bs.nbits   = bs.npbits;                                               \
        bs.pbits   = 0;                                                       \
        bs.npbits  = 0;                                                       \
      } else if ((take = min(64 - bs.nbits, bs.npbits))) {                    \
        bs.bits   |= (bs.pbits&(((bitstream_t)1<<take)-1)) << bs.nbits;       \
        bs.pbits >>= take;                                                    \
        bs.nbits  += take;                                                    \
        bs.npbits -= take;                                                    \
      }                                                                       \
      if (!bs.npbits) {                                                       \
        if (bs.p > bs.end                                                     \
            && (!(bs.chunk = bs.chunk->next)                                  \
                || !(bs.p = bs.chunk->p) || !(bs.end = bs.chunk->end))) {     \
          if(bs.nbits)break;else return UNZ_ERR;                              \
        }                                                                     \
        bs.npbits=huff_read(&bs.p,&bs.pbits,bs.end);                          \
      }                                                                       \
    } while (bs.nbits < (req) && bs.npbits);                                  \
  }
#endif

#if 0
#define REFILL(req)                                                           \
  if (bs.nbits < (req)) {                                                     \
    int take;                                                                 \
    do {                                                                      \
      take       = min(56-bs.nbits, bs.npbits);                               \
      bs.bits   |= (bs.pbits&(((bitstream_t)1<<take)-1)) << bs.nbits;         \
      bs.pbits >>= take;                                                      \
      bs.nbits  += take;                                                      \
      bs.npbits -= take;                                                      \
      if (!bs.npbits) {                                                       \
        if (bs.p > bs.end                                                     \
            && (!(bs.chunk = bs.chunk->next)                                  \
                || !(bs.p = bs.chunk->p) || !(bs.end = bs.chunk->end))) {     \
          if(bs.nbits)break;else return UNZ_ERR;                              \
        }                                                                     \
        bs.npbits=huff_read(&bs.p,&bs.pbits,bs.end);                          \
      }                                                                       \
    } while (bs.nbits < (req) && bs.npbits);                                  \
  }
#endif

#if 0
/**
 * dont require more than 56bits to reduce 64bit shift and partial bits
 */
#define REFILL(req) do {                                                      \
  int take, s, i;                                                             \
  if (bs.nbits < (req)) {                                                     \
    s          = bs.nbits;                                                    \
    take       = min(56-s,bs.npbits);                                         \
    bs.bits   |= (bs.pbits & (((bitstream_t)1<<take)-1)) << s;                \
    bs.pbits >>= take;                                                        \
    s         += take;                                                        \
    bs.npbits -= take;                                                        \
                                                                              \
    /* we still need bits, fill both buffers. we dont allow a chunk to be */  \
    /* less than 2x word size, so two iter would be enough to fill a      */  \
    /* buff e.g. we are end of one chunk then switch to next              */  \
    if (unlikely((req) > s)) {                                                \
      /* front buff: first try */                                             \
      take = min((int)(bs.end - bs.p),(56-s)/8);                              \
      for (i=0;i<take;i++,s+=8) bs.bits |= ((BITS_TYPE)bs.p[i]) << s;         \
      bs.p += take;                                                           \
                                                                              \
      if (bs.p > bs.end                                                       \
          && (!(bs.chunk = bs.chunk->next)                                    \
               || !(bs.p = bs.chunk->p) || !(bs.end = bs.chunk->end))) {      \
        bs.nbits = s;                                                         \
        break;                                                                \
      }                                                                       \
                                                                              \
      /* front buff: second try */                                            \
      if ((req) > s) {                                                        \
        take = min((int)(bs.end - bs.p),(56-s)/8);                            \
        for (i=0;i<take;i++,s+=8) bs.bits |= ((BITS_TYPE)bs.p[i])<<s;         \
        bs.p += take;                                                         \
      }                                                                       \
                                                                              \
      /* back buff: full read */                                              \
      if (unlikely(!(bs.npbits=huff_read(&bs.p,&bs.pbits,bs.end))))           \
        break;                                                                \
    }                                                                         \
    bs.nbits = s;                                                             \
  }                                                                           \
} while(0)
#endif

static UNZ_HOT
UnzResult
infl_block(defl_stream_t          * __restrict stream,
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
    /* decode literal/length symbol */
    REFILL(21);
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

    REFILL(29);
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
          len -= 16; dpos += 16;
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
        len -= 8; dpos += 8;
      }
      while (len >= 4) {
        dst[dpos]   = used;
        dst[dpos+1] = used;
        dst[dpos+2] = used;
        dst[dpos+3] = used;
        len -= 4; dpos += 4;
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
      src = dpos - dist;
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
      if (len >= 16 && dist >= 16) {
        do {
          vst1q_u8(&dst[dpos], vld1q_u8(&dst[src]));
          len -= 16; dpos += 16; src += 16;
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
        len -= 4; dpos += 4; src += 4;
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
  }

  *dst_pos = dpos;

  DONATE();

  return UNZ_OK;
}

static UNZ_HOT
UnzResult
infl_raw(defl_stream_t * __restrict stream) {
  uint8_t        *dst;
  const uint8_t  *p;
  unz__bitstate_t bs;
  unsigned        dpos, dlen, nbytes, chkrem, n, remlen, i, simdlen;
  uint32_t        header, val;
  uint16_t        len, nlen;

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

  REFILL(32);
  header = (uint32_t)bs.bits;
  CONSUME(32);
  
  len  = header & 0xFFFF;
  nlen = header >> 16;

  if (unlikely((len^(uint16_t)~nlen)|(dpos+len > dlen)))
    return UNZ_ERR;

  remlen = len;
  dst    = stream->dst + dpos;

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
      if (!(bs.chunk = bs.chunk->next) || !bs.chunk->p || !bs.chunk->end)
        return UNZ_ERR;
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

  stream->dstpos = dpos + len;

  DONATE();

  return UNZ_OK;
}

UNZ_EXPORT
int
infl(defl_stream_t * __restrict stream) {
  static huff_table_ext_t _tlitl={0},_tdist={0};
  static bool             _init=false;

  unz__bitstate_t bs;
  uint_fast8_t    btype, bfinal = 0;

  if (!stream->bs.chunk && !(stream->bs.chunk = stream->start))
    goto noop;

  if (!stream->start->p || stream->start->p == stream->start->end) {
    RESTORE();
    goto ok;
  }

  /* initilize static tables */
  if (!_init) {
    if (!huff_init_lsb_extof(&_tlitl,fxd,NULL,lvals,257,288) ||
        !huff_init_lsb_ext(&_tdist,fxd+288,NULL,dvals,32)) {
      goto err;
    }
    _init = true;
  }

  if (stream->flags == 1 && unlikely(!stream->header)) {
    zlib_header(stream, &stream->bs.chunk, true);

    stream->bs.p   = stream->bs.chunk->p;
    stream->bs.end = stream->bs.chunk->end;
    bs.p           = stream->bs.p;
    bs.end         = stream->bs.end;
  }

  stream->bs.p   = stream->start->p;
  stream->bs.end = stream->start->end;
  RESTORE();

  while (!bfinal && bs.chunk) {
    REFILL(3);
    bfinal = bs.bits & 0x1;
    btype  = (bs.bits >> 1) & 0x3;
    CONSUME(3);

    switch (btype) {
      case 0:
        DONATE();
        if (infl_raw(stream) < UNZ_OK) goto err;
        RESTORE();
        break;
      case 1:
        DONATE();
        if (infl_block(stream, &_tlitl, &_tdist) < UNZ_OK) goto err;
        RESTORE();
        break;
      case 2: {
        union {
          uint_fast8_t codelens[MAX_CODELEN_CODES];
          uint_fast8_t lens[MAX_LITLEN_CODES + MAX_DIST_CODES];
        } lens={0};
        huff_fast_entry_t tcodelen[HUFF_FAST_TABLE_SIZE];
        huff_table_ext_t  dyn_tlen, dyn_tdist;
        huff_fast_entry_t fe;
        int               i, n, hclen, hlit, hdist, repeat, prev;

        REFILL(14);
        hlit  = (bs.bits & 0x1F) + 257;
        hdist = ((bs.bits >> 5) & 0x1F) + 1;
        hclen = ((bs.bits >> 10) & 0xF) + 4;
        n     = hlit + hdist;
        CONSUME(14);

        if (n > MAX_LITLEN_CODES + MAX_DIST_CODES)
          goto err;

        for (i = 0; i < hclen; i++) {
          REFILL(3);
          lens.codelens[ord[i]] = bs.bits & 0x7;
          CONSUME(3);
        }

        if (!huff_init_fast_lsb(tcodelen, lens.codelens, NULL, MAX_CODELEN_CODES))
          goto err;

        /* clean used union prefix then ensure i=0 after loop exit */
        for (i = MAX_CODELEN_CODES; i;) lens.codelens[--i] = 0;

        while (i < n) {
          REFILL(14);

          fe = tcodelen[(uint8_t)bs.bits];
          if (unlikely(!fe.len || fe.sym > 18)) goto err;
          CONSUME(fe.len);

          switch (fe.sym) {
            default: lens.lens[i++] = (uint_fast8_t)fe.sym; break; /* sym is <= 15 */
            case 16: {
              repeat = 3 + (bs.bits & 0x3); CONSUME(2);
              if (i == 0 || i + repeat > n) goto err;
              prev = lens.lens[i - 1];
              while (repeat--) lens.lens[i++] = prev;
            } break;
            case 17: i+=(3  + (bs.bits & 0x7));  CONSUME(3); break;
            case 18: i+=(11 + (bs.bits & 0x7F)); CONSUME(7); break;
          }
        }

        if (!huff_init_lsb_extof(&dyn_tlen,lens.lens,NULL,lvals,257,hlit) ||
            !huff_init_lsb_ext(&dyn_tdist,lens.lens+hlit,NULL,dvals,hdist))
          goto err;

        DONATE();
        if (infl_block(stream, &dyn_tlen, &dyn_tdist) < UNZ_OK) goto err;
        RESTORE();
      } break;
      default:
        goto err;
    }
  }

  /* stream->it = bs.chunk; */
ok:
  DONATE();
  return UNZ_OK;
noop:
  return UNZ_NOOP;
err:
  return UNZ_ERR;
}

UNZ_EXPORT
defl_stream_t *
infl_init(const void * __restrict dst, uint32_t dstlen, int flags) {
  infl_stream_t *st;

  st          = calloc(1, sizeof(*st));
  st->dst     = (uint8_t *)dst;
  st->dstlen  = dstlen;

  st->malloc  = malloc;
  st->realloc = realloc;
  st->free    = free;
  st->flags   = flags;

  return st;
}

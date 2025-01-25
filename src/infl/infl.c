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
#include "../zlib/zlib.h"
#include <math.h>
#include <huff/huff.h>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#  include <arm_neon.h>
#endif

#define MAX_CODELEN_CODES 19
#define MAX_LITLEN_CODES  288
#define MAX_DIST_CODES    32

static const huff_ext_t lvals[] = {
  {3,0,0},{4,0,0},{5,0,0},{6,0,0},{7,0,0},{8,0,0},{9,0,0},{10,0,0},{11,1,1},
  {13,1,1},{15,1,1},{17,1,1},{19,2,3},{23,2,3},{27,2,3},{31,2,3},{35,3,7},
  {43,3,7},{51,3,7},{59,3,7},{67,4,15},{83,4,15},{99,4,15},{115,4,15},
  {131,5,31},{163,5,31},{195,5,31},{227,5,31},{258,0,0}
};

static const huff_ext_t dvals[] = {
  {1,0,0},{2,0,0},{3,0,0},{4,0,0},{5,1,1},{7,1,1},{9,2,3},{13,2,3},{17,3,7},
  {25,3,7},{33,4,15},{49,4,15},{65,5,31},{97,5,31},{129,6,63},{193,6,63},
  {257,7,127},{385,7,127},{513,8,255},{769,8,255},{1025,9,511},{1537,9,511},
  {2049,10,1023},{3073,10,1023},{4097,11,2047},{6145,11,2047},
  {8193,12,4095},{12289,12,4095},{16385,13,8191},{24577,13,8191}
};

static const uint_fast8_t
  l_orders[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15},
  f_llitl[288] = {[0 ...143]=8,[144 ...255]=9,[256 ...279]=7,[280 ...287]=8},
  f_ldist[32]  = {[0 ...31]=5}
;

UNZ_INLINE uint_fast16_t min16(uint_fast16_t a, uint_fast16_t b) { return a < b ? a : b; }
UNZ_INLINE int min(int a, int b) { return a < b ? a : b; }

#define EXTRACT(B,C)  ((B) & (((bitstream_t)1 << (C)) - 1))
#define CONSUME(N)    bs.bits >>= (N);bs.nbits -= (N);
#define RESTORE()     bs=stream->bs;
#define DONATE()      stream->bs=bs;bs.bits=0;bs.nbits=0;bs.npbits=0;bs.pbits=0;bs.chunk=NULL;

#define BITS_MSBI     (BITS_SZF-1)
#define BITS_MASKMSBI ((1ULL << BITS_MSBI) - 1)

#define REFILL(req)                                                           \
  while (bs.nbits < (req)) {                                                  \
    int shr;                                                                  \
    if (!bs.npbits) {                                                         \
      if (unlikely((bs.chunk->p >= bs.chunk->end)                             \
          && (!(bs.chunk = bs.chunk->next) || !bs.chunk->p)))                 \
        return UNZ_ERR;                                                       \
      bs.pbits = huff_read(&bs.chunk->p, &bs.chunk->bitpos,                   \
                           &bs.npbits, bs.chunk->end);                        \
      if (unlikely(!bs.npbits)) return UNZ_ERR;                               \
    }                                                                         \
                                                                              \
    bs.bits   |= (bs.pbits&BITS_MASKMSBI) << bs.nbits;                        \
    shr        = min(min(BITS_SZF-bs.nbits,bs.npbits),BITS_MSBI);             \
                                                                              \
    bs.pbits >>= shr;                                                         \
    bs.nbits  += shr;                                                         \
    bs.npbits -= shr;                                                         \
  }

static inline
UnzResult
infl_block(defl_stream_t        * __restrict stream,
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
    REFILL(32);
    lsym = huff_decode_lsb_extof(tlit, bs.bits, &used, &len, 257);
    if (!used || lsym > 285)
      return UNZ_ERR; /* invalid symbol */

    CONSUME(used);

    if (lsym < 256) {
      /* literal byte */
      if (dpos >= dst_cap)
        return UNZ_EFULL;
      dst[dpos++] = (uint8_t)lsym;
      continue;
    } else if (lsym == 256) {
      /* eof */
      break;
    }

    /* validate distance */
    dist = huff_decode_lsb_ext(tdist, bs.bits, &used);
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
      dst[dpos] = used;
      switch (len) {
        case 3: dst[dpos+2] = used;
        case 2: dst[dpos+1] = used; break;
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
      dst[dpos] = dst[src];
      switch (len) {
        case 3: dst[dpos+2] = dst[src+2];
        case 2: dst[dpos+1] = dst[src+1]; break;
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
infl(defl_stream_t * __restrict stream) {
  static huff_table_ext_t _tlitl={0}, _tdist={0};
  static bool         _init=false;

  unz__bitstate_t bs;
  uint_fast8_t    used, btype, bfinal = 0;

  if (!stream->bs.chunk && !(stream->bs.chunk = stream->start)) {
    return UNZ_NOOP;
  }

  /* initilize static tables */
  if (!_init) {
    if (!huff_init_lsb_extof(&_tlitl, f_llitl, NULL, lvals, 257, 29) ||
        !huff_init_lsb_ext(&_tdist, f_ldist, NULL, dvals, 30)) {
      return UNZ_ERR;
    }
    _init = true;
  }

  if (unlikely(!stream->header)) {
    zlib_header(stream, &stream->bs.chunk, true);
  }

  RESTORE();

  while (!bfinal && bs.chunk) {
    REFILL(3);
    bfinal = bs.bits & 0x1;
    btype  = (bs.bits >> 1) & 0x3;
    CONSUME(3);

    switch (btype) {
      case 0: {
        size_t        remlen, chunkrem, dpos, dlen;
        uint_fast16_t len, nlen, padbits, n;
        uint_fast8_t  cached;

        dpos    = stream->dstpos;
        dlen    = stream->dstlen;
        padbits = bs.nbits % 8;
        if (padbits > 0)
          CONSUME(padbits);

        REFILL(32);
        len  = EXTRACT(bs.bits, 16); CONSUME(16);
        nlen = EXTRACT(bs.bits, 16); CONSUME(16);

        if (unlikely(len != (uint16_t)~nlen)) { goto err; } /* invalid block */

        /* flush cached bits (bst.bits) to the output buffer */
        while (bs.nbits >= 8) {
          cached = EXTRACT(bs.bits, 8);
          /* output buffer overflow */
          if (dpos >= dlen) { goto err; }
          stream->dst[dpos++] = cached;
          CONSUME(8);
        }

        /* flush remaining bits in bst.pbits to the output buffer */
        while (bs.npbits >= 8) {
          cached = EXTRACT(bs.pbits, 8);
          /* output buffer overflow */
          if (dpos >= dlen) { goto err; }
          stream->dst[dpos++] = cached;
          bs.pbits >>= 8;
          bs.npbits -= 8;
        }

        /* copy LEN bytes of literal data, handling multiple chunks */
        remlen = len;
        while (remlen > 0) {
          if ((chunkrem = bs.chunk->end - bs.chunk->p) == 0) {
            /* invalid stream or insufficient data */
            if (!(bs.chunk = bs.chunk->next)|| !bs.chunk->p) { goto err; }
            continue;
          }

          /* validate output buffer overflow */
          n = min16(chunkrem, remlen);
          if (dpos + n > dlen) { goto err; }

          /* copy data */
          memcpy(stream->dst + dpos, bs.chunk->p, n);
          dpos        += n;
          bs.chunk->p += n;
          remlen      -= n;
        }

        stream->dstpos = dpos;
        // DONATE() /* TODO: reduce donate / restore */
      } continue;
      case 1:
        DONATE();
        if (infl_block(stream, &_tlitl, &_tdist) != UNZ_OK) {
          goto err;
        }
        RESTORE();
        break;
      case 2: {
        union {
          uint_fast8_t codelens[MAX_CODELEN_CODES];
          uint_fast8_t lens[MAX_LITLEN_CODES + MAX_DIST_CODES];
        } lens={0};
        huff_table_t     tcodelen;
        huff_table_ext_t dyn_tlen, dyn_tdist;
        size_t        i;
        uint_fast32_t n;
        uint_fast16_t sym, hclen, hlit, hdist;
        uint_fast8_t  repeat, prev;

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
          lens.codelens[l_orders[i]] = bs.bits & 0x7;
          CONSUME(3);
        }

        if (!huff_init_lsb(&tcodelen, lens.codelens, NULL, MAX_CODELEN_CODES))
          goto err;

        /* clean used union prefix then ensure i=0 after loop exit */
        for (i = MAX_CODELEN_CODES-1; i; i--) lens.codelens[i] = 0;

        while (i < n) {
          REFILL(15);
          sym = huff_decode_lsb(&tcodelen, bs.bits, 15, &used);
          if (!used || sym > 18) goto err;
          CONSUME(used);

          switch (sym) {
            default: {
              lens.lens[i++] = sym; /* sym <= 15 */
            } break;
            case 16: {
              REFILL(2);
              repeat = 3 + (bs.bits & 0x3);
              CONSUME(2);

              if (i == 0 || i + repeat > n)
                goto err;

              prev = lens.lens[i - 1];
              while (repeat--) lens.lens[i++] = prev;
            } break;
            case  17: {
              REFILL(3);
              repeat = 3 + (bs.bits & 0x7);
              CONSUME(3);

              if (i + repeat > n)
                goto err;

              while (repeat--) lens.lens[i++] = 0;
            } break;
            case 18: {
              REFILL(7);
              repeat = 11 + (bs.bits & 0x7F);
              CONSUME(7);

              if (i + repeat > n)
                goto err;

              while (repeat--) lens.lens[i++] = 0;
            } break;
          }
        }

        if (!huff_init_lsb_extof(&dyn_tlen,lens.lens,NULL,lvals,257,hlit) ||
            !huff_init_lsb_ext(&dyn_tdist,lens.lens+hlit,NULL,dvals,hdist))
          goto err;

        DONATE();
        if (infl_block(stream, &dyn_tlen, &dyn_tdist) != UNZ_OK) {
          goto err;
        }
        RESTORE();
      } break;
      default:
        goto err;
    }
  }

  /* stream->it = bs.chunk; */
  DONATE();
  return UNZ_OK;
err:
  return UNZ_ERR;
}

UNZ_EXPORT
int
infl_stream(infl_stream_t * __restrict stream,
            const void    * __restrict src,
            uint32_t                   srclen) {
  return infl(stream);
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

  return st;
}

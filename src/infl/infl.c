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
#include "../zlib/zlib.h"
#include <math.h>
#include <huff/huff.h>

#define MAX_CODELEN_CODES 19
#define MAX_LITLEN_CODES  288
#define MAX_DIST_CODES    32

typedef struct {int base:16,bits:8;} hval_t;

static const hval_t lvals[] = {
  {3,0},{4,0},{5, 0},{6,0},{7,0},{8,0},{9,0},{10,0},{11,1},{13,1},{15,1},
  {17,1},{19,2},{23,2},{27,2},{31,2},{35,3},{43,3},{51,3},{59,3},{67, 4},
  {83, 4},{99, 4},{115, 4},{131, 5},{163, 5},{195, 5},{227, 5},{258,  0}
};

static const hval_t dvals[] = {
  {1,0},{2,0},{3,0},{4,0},{5,1},{7,1},{9, 2},{13,  2},{17,    3},
  {25,3},{33,4},{49,4},{65,5},{97,5},{129,6},{193, 6},{257,   7},
  {385,7},{513,8},{769,8},{1025,9},{1537,9},{2049,10},{3073, 10},
  {4097,11},{6145,11},{8193,12},{12289,12},{16385,13},{24577,13}
};

static const uint_fast8_t
  l_orders[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15},
  f_llitl[288] = {[0 ...143]=8,[144 ...255]=9,[256 ...279]=7,[280 ...287]=8},
  f_ldist[32]  = {[0 ...31]=5}
;

static inline uint_fast16_t min16(uint_fast16_t a, uint_fast16_t b) { return a < b ? a : b; }

#define EXTRACT(B,C) ((B) & (((bitstream_t)1 << (C)) - 1))
#define CONSUME(N)   bs.bits >>= (N);bs.nbits -= (N);
#define RESTORE()    bs=stream->bs;
#define DONATE()     stream->bs=bs;bs.bits=0;bs.nbits=0;bs.npbits=0;bs.pbits=0;bs.chunk=NULL;

#define REFILL(req)                                                           \
  while (bs.nbits < (req)) {                                                  \
    if (!bs.npbits) {                                                         \
      if ((bs.chunk->p >= bs.chunk->end)                                      \
          && (!(bs.chunk = bs.chunk->next) || !bs.chunk->p)) {                \
        return UNZ_ERR;                                                       \
      }                                                                       \
      bs.pbits = huff_read(&bs.chunk->p, &bs.chunk->bitpos,                   \
                           &bs.npbits, bs.chunk->end);                        \
      if (!bs.npbits) { return UNZ_ERR;  }                                    \
    }                                                                         \
                                                                              \
    if (!bs.nbits) {                                                          \
      bs.bits    = bs.pbits;                                                  \
      bs.nbits   = min16(BITS_SZF,bs.npbits);                                 \
      bs.pbits   = bs.nbits<bs.npbits?bs.pbits>>bs.nbits:0;                   \
      bs.npbits  = bs.nbits<bs.npbits?bs.npbits-bs.nbits:0;                   \
   } else {                                                                   \
      int nt     = min16(BITS_SZF-bs.nbits,bs.npbits);                        \
      bs.bits   |= EXTRACT(bs.pbits,nt) << bs.nbits;                          \
      bs.pbits >>= nt; bs.nbits += nt; bs.npbits -= nt;                       \
    }                                                                         \
  }                                                                           \

static 
UnzResult
infl_block(defl_stream_t      * __restrict stream,
           const huff_table_t * __restrict tlit,
           const huff_table_t * __restrict tdist) {
  uint8_t * __restrict dst;
  size_t  * __restrict dst_pos;
  unz__bitstate_t bs;
  size_t          dst_cap, dpos, src;
  uint_fast32_t   len,  dist;
  uint_fast16_t   lsym, dsym;
  hval_t          val;
  uint_fast8_t    used;

  dst     = stream->dst;
  dst_cap = stream->dstlen;
  dst_pos = &stream->dstpos;
  dpos    = *dst_pos;

  RESTORE()

  while (true) {
    /* decode literal/length symbol */
    REFILL(32);
    lsym = huff_decode_lsb(tlit, bs.bits, 15, &used);
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

    /* back-reference length */
    val = lvals[lsym - 257];
    len = val.base;

    if (val.bits) {
      REFILL(val.bits);
      len += EXTRACT(bs.bits, val.bits);
      CONSUME(val.bits);
    }

    /* decode distance symbol */
    REFILL(15);
    dsym = huff_decode_lsb(tdist, bs.bits, 15, &used);
    if (unlikely(!used))
      return UNZ_ERR; /* invalid symbol */
    CONSUME(used);

    val  = dvals[dsym];
    dist = val.base;
    if (val.bits) {
      REFILL(val.bits);
      dist += EXTRACT(bs.bits, val.bits);
      CONSUME(val.bits);
    }

    /* validate distance */
    if (unlikely(dist > dpos))
      return UNZ_ERR; /* invalid distance */

    if (unlikely((dpos + len) > dst_cap))
      return UNZ_EFULL;

    /* output back-reference */
    if (dist == 1) {
      used = dst[dpos - 1];
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
        default: break;
      }
      dpos += len;
    } else {
      src = dpos - dist;
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
        default: break;
      }
      src  += len;
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
  static huff_table_t _tlitl={0}, _tdist={0};
  static bool         _init=false;

  unz__bitstate_t bs;
  uint_fast8_t    used, btype, bfinal = 0;

  if (!stream->bs.chunk && !(stream->bs.chunk = stream->start)) {
    return UNZ_NOOP;
  }

  /* initilize static tables */
  if (!_init) {
    huff_init_lsb(&_tlitl, f_llitl, NULL, ARRAY_LEN(f_llitl));
    huff_init_lsb(&_tdist, f_ldist, NULL, ARRAY_LEN(f_ldist));
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

        huff_table_t  dyn_tlen, dyn_tdist;
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

        if (!huff_init_lsb(&dyn_tlen, lens.codelens, NULL, MAX_CODELEN_CODES))
          goto err;

        /* clean used union prefix then ensure i=0 after loop exit */
        for (i = MAX_CODELEN_CODES-1; i; i--) lens.codelens[i] = 0;

        while (i < n) {
          REFILL(15);
          sym = huff_decode_lsb(&dyn_tlen, bs.bits, 15, &used);
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

        /* re-use dyn_tlen to reduce mem */
        if (!huff_init_lsb(&dyn_tlen,  lens.lens,      NULL, hlit))  goto err;
        if (!huff_init_lsb(&dyn_tdist, lens.lens+hlit, NULL, hdist)) goto err;

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

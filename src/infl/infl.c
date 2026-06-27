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

UNZ_INLINE uint64_t
infl_load64(const uint8_t * __restrict p) {
  uint64_t v;
  memcpy(&v, p, sizeof(v));
  return v;
}

UNZ_INLINE void
infl_store64(uint8_t * __restrict p, uint64_t v) {
  memcpy(p, &v, sizeof(v));
}

UNZ_INLINE void
infl_copy_stored_direct(uint8_t       * __restrict dst,
                        const uint8_t * __restrict src,
                        size_t                     len) {
  if (len)
    memcpy(dst, src, len);
}

typedef struct infl_stored_bits_t {
  const uint8_t *p;
  const uint8_t *end;
  bitstream_t    bits;
  unsigned       nbits;
} infl_stored_bits_t;

UNZ_INLINE bitstream_t
infl_load_partial_le(const uint8_t * __restrict p, size_t n) {
  bitstream_t v;

  if (likely(n == sizeof(uint64_t)))
    return infl_load64(p);

  v = 0;
  switch (n) {
    case 7: v |= (bitstream_t)p[6] << 48; /* fall through */
    case 6: v |= (bitstream_t)p[5] << 40; /* fall through */
    case 5: v |= (bitstream_t)p[4] << 32; /* fall through */
    case 4: v |= (bitstream_t)p[3] << 24; /* fall through */
    case 3: v |= (bitstream_t)p[2] << 16; /* fall through */
    case 2: v |= (bitstream_t)p[1] << 8;  /* fall through */
    case 1: v |= (bitstream_t)p[0];       /* fall through */
    default: break;
  }
  return v;
}

UNZ_INLINE void
infl_stored_refill(infl_stored_bits_t * __restrict br, unsigned need) {
  while (br->nbits < need && br->p < br->end) {
    size_t n, avail;

    n     = (64u - br->nbits) >> 3;
    avail = (size_t)(br->end - br->p);
    if (n > avail)
      n = avail;
    if (!n)
      break;

    br->bits  |= infl_load_partial_le(br->p, n) << br->nbits;
    br->p     += n;
    br->nbits += (unsigned)(n << 3);
  }
}

UNZ_INLINE void
infl_stored_consume(infl_stored_bits_t * __restrict br, unsigned n) {
  if (n == 64)
    br->bits = 0;
  else
    br->bits >>= n;
  br->nbits -= n;
}

static UnzResult
infl_stored_block(infl_stored_bits_t * __restrict br,
                  uint8_t            * __restrict dst,
                  size_t                          dst_cap,
                  size_t             * __restrict dpos,
                  uint_fast8_t       * __restrict bfinal) {
  uint32_t header;
  uint16_t len, nlen;
  unsigned nbytes, shift;
  size_t   rem;

  if (likely(br->nbits == 0 && br->p < br->end)) {
    const uint8_t *p;

    p       = br->p;
    *bfinal = (uint_fast8_t)(p[0] & 1u);
    if (unlikely(((p[0] >> 1) & 3u) != 0))
      return UNZ_NOOP;

    if (unlikely((size_t)(br->end - p) < 5))
      return UNZ_ERR;

    len  = (uint16_t)((uint16_t)p[1] | ((uint16_t)p[2] << 8));
    nlen = (uint16_t)((uint16_t)p[3] | ((uint16_t)p[4] << 8));
    if (unlikely((uint16_t)(len ^ (uint16_t)~nlen) || len > dst_cap - *dpos))
      return UNZ_ERR;

    p += 5;
    if (unlikely((size_t)(br->end - p) < len))
      return UNZ_ERR;

    infl_copy_stored_direct(dst + *dpos, p, len);

    br->p = p + len;
    *dpos += len;

    return UNZ_OK;
  }

  infl_stored_refill(br, 3);
  if (unlikely(br->nbits < 3))
    return UNZ_ERR;

  *bfinal = (uint_fast8_t)(br->bits & 1u);
  if (unlikely(((br->bits >> 1) & 3u) != 0))
    return UNZ_NOOP;
  infl_stored_consume(br, 3);

  shift = br->nbits & 7u;
  if (shift)
    infl_stored_consume(br, shift);

  infl_stored_refill(br, 32);
  if (unlikely(br->nbits < 32))
    return UNZ_ERR;

  header = (uint32_t)br->bits;
  infl_stored_consume(br, 32);
  len  = (uint16_t)header;
  nlen = (uint16_t)(header >> 16);
  if (unlikely((uint16_t)(len ^ (uint16_t)~nlen) || len > dst_cap - *dpos))
    return UNZ_ERR;

  rem    = len;
  nbytes = br->nbits >> 3;
  if (nbytes > rem)
    nbytes = (unsigned)rem;

  if (nbytes) {
    switch (nbytes) {
      case 7: dst[*dpos + 6] = (uint8_t)(br->bits >> 48); /* fall through */
      case 6: dst[*dpos + 5] = (uint8_t)(br->bits >> 40); /* fall through */
      case 5: dst[*dpos + 4] = (uint8_t)(br->bits >> 32); /* fall through */
      case 4: dst[*dpos + 3] = (uint8_t)(br->bits >> 24); /* fall through */
      case 3: dst[*dpos + 2] = (uint8_t)(br->bits >> 16); /* fall through */
      case 2: dst[*dpos + 1] = (uint8_t)(br->bits >> 8);  /* fall through */
      case 1: dst[*dpos]     = (uint8_t)br->bits;         /* fall through */
      default: break;
    }
    infl_stored_consume(br, nbytes << 3);
    *dpos += nbytes;
    rem   -= nbytes;
  }

  if (unlikely((size_t)(br->end - br->p) < rem))
    return UNZ_ERR;

  if (rem)
    memcpy(dst + *dpos, br->p, rem);
  br->p += rem;
  *dpos += rem;

  return UNZ_OK;
}

UNZ_INLINE void
infl_stored_donate(defl_stream_t      * __restrict stream,
                   infl_stored_bits_t * __restrict br,
                   size_t                          dpos,
                   bool                            zlib) {
  stream->dstpos    = dpos;
  stream->bs.chunk  = stream->start;
  stream->bs.p      = br->p;
  stream->bs.end    = br->end;
  stream->bs.bits   = br->bits;
  stream->bs.nbits  = (int)br->nbits;
  stream->bs.pbits  = 0;
  stream->bs.npbits = 0;
  if (zlib)
    stream->header = stream;
}

static UnzResult
infl_stored_direct(defl_stream_t * __restrict stream) {
  infl_stored_bits_t br;
  const uint8_t     *p, *end;
  uint8_t           *dst;
  size_t             dpos, dst_cap;
  uint_fast8_t       bfinal;
  bool               zlib;

  if (!stream->start || stream->start != stream->end ||
      stream->dstpos != 0 || stream->bs.chunk || stream->header)
    return UNZ_NOOP;

  p   = stream->start->p;
  end = stream->start->end;
  if (!p || p >= end)
    return UNZ_NOOP;

  zlib = stream->flags == INFL_ZLIB;
  if (zlib) {
    uint8_t cmf, flg;

    if (unlikely((size_t)(end - p) < 2))
      return UNZ_ERR;
    if ((size_t)(end - p) < 3 || (((p[2] >> 1) & 3u) != 0))
      return UNZ_NOOP;

    cmf = p[0];
    flg = p[1];
    if (unlikely((cmf & 0x0f) != 8 || (cmf >> 4) > 7 ||
                 ((((uint16_t)cmf << 8) + flg) % 31) != 0 ||
                 (flg & 0x20)))
      return UNZ_ERR;
    p += 2;
  } else if (stream->flags != 0) {
    return UNZ_NOOP;
  } else if (((p[0] >> 1) & 3u) != 0) {
    return UNZ_NOOP;
  }

  dst     = stream->dst;
  dst_cap = stream->dstlen;
  dpos    = 0;
  br.p     = p;
  br.end   = end;
  br.bits  = 0;
  br.nbits = 0;

  do {
    UnzResult res = infl_stored_block(&br, dst, dst_cap, &dpos, &bfinal);
    if (res == UNZ_NOOP) {
      infl_stored_donate(stream, &br, dpos, zlib);
      return UNZ_UNFINISHED;
    } else if (res != UNZ_OK) {
      return res;
    }
  } while (!bfinal);

  infl_stored_donate(stream, &br, dpos, zlib);

  return UNZ_OK;
}

UNZ_INLINE void
infl_copy_rle(uint8_t * __restrict dst, size_t * __restrict dpos, unsigned len) {
  uint8_t  byte;
  uint64_t word;
  size_t   pos, end;

  pos  = *dpos;
  byte = dst[pos - 1];
  word = UINT64_C(0x0101010101010101) * byte;
  end  = pos + len;

  do {
    infl_store64(dst + pos, word);
    pos += 8;
  } while (pos < end);

  *dpos = end;
}

UNZ_INLINE void
infl_copy_rle_overrun(uint8_t * __restrict dst, size_t * __restrict dpos, unsigned len) {
  uint8_t  byte;
  uint64_t word;
  size_t   pos, end;

  pos  = *dpos;
  byte = dst[pos - 1];
  word = UINT64_C(0x0101010101010101) * byte;
  end  = pos + len;

  do {
    infl_store64(dst + pos,      word);
    infl_store64(dst + pos + 8,  word);
    infl_store64(dst + pos + 16, word);
    infl_store64(dst + pos + 24, word);
    infl_store64(dst + pos + 32, word);
    pos += 40;
  } while (pos < end);

  *dpos = end;
}

UNZ_INLINE void
infl_copy_match_word(uint8_t * __restrict dst,
                     size_t  * __restrict dpos,
                     unsigned             dist,
                     unsigned             len) {
  const uint8_t *src;
  size_t         pos, end;

  pos = *dpos;
  src = dst + pos - dist;
  end = pos + len;

  do {
    infl_store64(dst + pos, infl_load64(src));
    src += 8;
    pos += 8;
  } while (pos < end);

  *dpos = end;
}

UNZ_INLINE void
infl_copy_match_small_overrun(uint8_t * __restrict dst,
                              size_t  * __restrict dpos,
                              unsigned             dist,
                              unsigned             len) {
  size_t pos, src, end;

  pos = *dpos;
  src = pos - dist;
  end = pos + len;

  /* dist < 8: overlapping word stores quickly propagate the repeated pattern. */
  do {
    infl_store64(dst + pos, infl_load64(dst + src));
    pos += dist;
    src += dist;
  } while (pos < end);

  *dpos = end;
}

UNZ_INLINE void
infl_copy_match_overrun(uint8_t * __restrict dst,
                        size_t  * __restrict dpos,
                        unsigned             dist,
                        unsigned             len) {
  const uint8_t *src;
  size_t         pos, end;

  pos = *dpos;
  src = dst + pos - dist;
  end = pos + len;

  do {
    infl_store64(dst + pos,      infl_load64(src));
    infl_store64(dst + pos + 8,  infl_load64(src + 8));
    infl_store64(dst + pos + 16, infl_load64(src + 16));
    infl_store64(dst + pos + 24, infl_load64(src + 24));
    infl_store64(dst + pos + 32, infl_load64(src + 32));
    src += 40;
    pos += 40;
  } while (pos < end);

  *dpos = end;
}

#define INFL_FT_LIT_BITS    10u
#define INFL_FT_DIST_BITS   8u
#define INFL_FT_LIT_MAIN    (1u << INFL_FT_LIT_BITS)
#define INFL_FT_DIST_MAIN   (1u << INFL_FT_DIST_BITS)
#define INFL_FT_LIT_CAP     2048u
#define INFL_FT_DIST_CAP    512u

#define INFL_FT_TOTAL(E)    ((unsigned)((E) & 31u))
#define INFL_FT_CODELEN(E)  ((unsigned)(((E) >> 5) & 15u))
#define INFL_FT_XBITS(E)    ((unsigned)(((E) >> 9) & 15u))
#define INFL_FT_LITERAL     (1u << 13)
#define INFL_FT_END         (1u << 14)
#define INFL_FT_SUBTABLE    (1u << 15)
#define INFL_FT_BASE(E)     ((unsigned)((E) >> 16))
#define INFL_FT_ENTRY(BASE, XBITS, CODELEN, FLAGS) \
  (((uint32_t)(BASE) << 16) | (uint32_t)(FLAGS) | ((uint32_t)(XBITS) << 9) | \
   ((uint32_t)(CODELEN) << 5) | (uint32_t)((CODELEN) + (XBITS)))
#define INFL_FT_SUBENTRY(BASE, SUBBITS, MAINBITS) \
  (((uint32_t)(BASE) << 16) | INFL_FT_SUBTABLE | ((uint32_t)(SUBBITS) << 5) | \
   (uint32_t)(MAINBITS))

typedef struct infl_ft_bits_t {
  const uint8_t *p;
  const uint8_t *end;
  bitstream_t    bits;
  unsigned       nbits;
} infl_ft_bits_t;

typedef struct infl_ft_table_t {
  UNZ_ALIGN(64) uint32_t table[INFL_FT_LIT_CAP];
  uint16_t used;
} infl_ft_table_t;

typedef struct infl_ft_dist_table_t {
  UNZ_ALIGN(64) uint32_t table[INFL_FT_DIST_CAP];
  uint16_t used;
} infl_ft_dist_table_t;

UNZ_INLINE uint16_t
infl_ft_rev16(uint16_t v, unsigned len) {
  v = (uint16_t)(((v & 0x5555u) << 1) | ((v >> 1) & 0x5555u));
  v = (uint16_t)(((v & 0x3333u) << 2) | ((v >> 2) & 0x3333u));
  v = (uint16_t)(((v & 0x0f0fu) << 4) | ((v >> 4) & 0x0f0fu));
  v = (uint16_t)((v << 8) | (v >> 8));
  return (uint16_t)(v >> (16u - len));
}

UNZ_INLINE uint32_t
infl_ft_lit_entry(unsigned sym, unsigned len) {
  if (sym < 256)
    return INFL_FT_ENTRY(sym, 0, len, INFL_FT_LITERAL);

  if (sym == 256)
    return INFL_FT_ENTRY(0, 0, len, INFL_FT_END);

  if (sym <= 285) {
    huff_ext_t ext = lvals[sym - 257];
    return INFL_FT_ENTRY(ext.base, ext.bits, len, 0);
  }

  return 0;
}

UNZ_INLINE uint32_t
infl_ft_dist_entry(unsigned sym, unsigned len) {
  huff_ext_t ext;

  if (unlikely(sym > 29))
    return 0;

  ext = dvals[sym];
  return INFL_FT_ENTRY(ext.base, ext.bits, len, 0);
}

static bool
infl_ft_build(uint32_t       * __restrict table,
              uint16_t      * __restrict used_out,
              const uint8_t * __restrict lens,
              uint16_t                   nsyms,
              unsigned                   tablebits,
              unsigned                   cap,
              bool                       litlen) {
  uint_fast16_t count[HUFF_MAX_CODE_LENGTH + 1] = {0};
  uint_fast16_t code[HUFF_MAX_CODE_LENGTH + 1];
  uint_fast16_t next_code[HUFF_MAX_CODE_LENGTH + 1];
  uint16_t      subbase[1u << INFL_FT_LIT_BITS];
  uint8_t       subbits[1u << INFL_FT_LIT_BITS];
  uint_fast16_t l, sym, prev_code;
  unsigned      main_size, used, maxbits;
  int           left;

  maxbits   = 15;
  main_size = 1u << tablebits;
  used      = main_size;

  if (unlikely(tablebits > INFL_FT_LIT_BITS || main_size > cap))
    return false;

  memset(subbits, 0, main_size * sizeof(subbits[0]));

  for (sym = 0; sym < nsyms; sym++) {
    l = lens[sym];
    if (unlikely(l > maxbits))
      return false;
    count[l]++;
  }

  left = 1;
  for (l = 1; l <= maxbits; l++) {
    left = (left << 1) - (int)count[l];
    if (unlikely(left < 0))
      return false;
  }

  prev_code = 0;
  code[0] = next_code[0] = 0;
  for (l = 1; l <= maxbits; l++) {
    code[l] = (prev_code + count[l - 1]) << 1;
    next_code[l] = code[l];
    prev_code = code[l];
  }

  for (sym = 0; sym < nsyms; sym++) {
    unsigned len, rev, prefix, need;

    len = lens[sym];
    if (!len)
      continue;

    rev = infl_ft_rev16((uint16_t)next_code[len]++, len);
    if (len <= tablebits)
      continue;

    prefix = rev & (main_size - 1u);
    need   = len - tablebits;
    if (subbits[prefix] < need)
      subbits[prefix] = (uint8_t)need;
  }

  for (unsigned prefix = 0; prefix < main_size; prefix++) {
    unsigned size;

    if (!subbits[prefix])
      continue;

    size = 1u << subbits[prefix];
    if (unlikely(used + size > cap))
      return false;

    subbase[prefix] = (uint16_t)used;
    used += size;
  }

  memset(table, 0, used * sizeof(table[0]));

  for (unsigned prefix = 0; prefix < main_size; prefix++) {
    if (subbits[prefix])
      table[prefix] = INFL_FT_SUBENTRY(subbase[prefix], subbits[prefix], tablebits);
  }

  for (l = 1; l <= maxbits; l++)
    next_code[l] = code[l];

  for (sym = 0; sym < nsyms; sym++) {
    uint32_t entry;
    unsigned len, rev;

    len = lens[sym];
    if (!len)
      continue;

    rev   = infl_ft_rev16((uint16_t)next_code[len]++, len);
    entry = litlen ? infl_ft_lit_entry(sym, len) : infl_ft_dist_entry(sym, len);
    if (unlikely(!entry))
      continue;

    if (len <= tablebits) {
      unsigned step, end;

      step = 1u << len;
      end  = 1u << tablebits;
      for (unsigned idx = rev; idx < end; idx += step)
        table[idx] = entry;
    } else {
      unsigned prefix, base, bits, suffix, step, end;

      prefix = rev & (main_size - 1u);
      base   = subbase[prefix];
      bits   = subbits[prefix];
      suffix = rev >> tablebits;
      step   = 1u << (len - tablebits);
      end    = 1u << bits;

      for (unsigned idx = suffix; idx < end; idx += step)
        table[base + idx] = entry;
    }
  }

  *used_out = (uint16_t)used;
  return true;
}

UNZ_INLINE void
infl_ft_refill(infl_ft_bits_t * __restrict br, unsigned need) {
  while (br->nbits < need && br->p < br->end) {
    size_t n, avail;

    n     = (64u - br->nbits) >> 3;
    avail = (size_t)(br->end - br->p);
    if (n > avail)
      n = avail;
    if (!n)
      break;

    br->bits  |= infl_load_partial_le(br->p, n) << br->nbits;
    br->p     += n;
    br->nbits += (unsigned)(n << 3);
  }
}

UNZ_INLINE void
infl_ft_refill_fast(infl_ft_bits_t * __restrict br, unsigned need) {
  infl_ft_refill(br, need);
}

UNZ_INLINE void
infl_ft_consume(infl_ft_bits_t * __restrict br, unsigned n) {
  br->bits >>= n;
  br->nbits -= n;
}

UNZ_INLINE uint32_t
infl_ft_lookup_lit(const infl_ft_table_t * __restrict tab, bitstream_t bits) {
  uint32_t entry;

  entry = tab->table[bits & ((1u << INFL_FT_LIT_BITS) - 1u)];
  if (likely(!(entry & INFL_FT_SUBTABLE)))
    return entry;

  return tab->table[INFL_FT_BASE(entry) +
                    ((bits >> INFL_FT_LIT_BITS) & ((1u << INFL_FT_CODELEN(entry)) - 1u))];
}

UNZ_INLINE uint32_t
infl_ft_lookup_dist(const infl_ft_dist_table_t * __restrict tab, bitstream_t bits) {
  uint32_t entry;

  entry = tab->table[bits & ((1u << INFL_FT_DIST_BITS) - 1u)];
  if (likely(!(entry & INFL_FT_SUBTABLE)))
    return entry;

  return tab->table[INFL_FT_BASE(entry) +
                    ((bits >> INFL_FT_DIST_BITS) & ((1u << INFL_FT_CODELEN(entry)) - 1u))];
}

static UnzResult
infl_ft_stored(infl_ft_bits_t * __restrict br,
               uint8_t        * __restrict dst,
               size_t         * __restrict dpos,
               size_t                      dst_cap) {
  uint32_t header;
  uint16_t len, nlen;
  unsigned nbytes, shift;
  size_t   rem;

  shift = br->nbits & 7u;
  if (shift)
    infl_ft_consume(br, shift);

  infl_ft_refill(br, 32);
  if (unlikely(br->nbits < 32))
    return UNZ_ERR;

  header = (uint32_t)br->bits;
  infl_ft_consume(br, 32);

  len  = (uint16_t)header;
  nlen = (uint16_t)(header >> 16);
  if (unlikely((uint16_t)(len ^ (uint16_t)~nlen) || len > dst_cap - *dpos))
    return UNZ_ERR;

  rem    = len;
  nbytes = br->nbits >> 3;
  if (nbytes > rem)
    nbytes = (unsigned)rem;

  if (nbytes) {
    switch (nbytes) {
      case 7: dst[*dpos + 6] = (uint8_t)(br->bits >> 48); /* fall through */
      case 6: dst[*dpos + 5] = (uint8_t)(br->bits >> 40); /* fall through */
      case 5: dst[*dpos + 4] = (uint8_t)(br->bits >> 32); /* fall through */
      case 4: dst[*dpos + 3] = (uint8_t)(br->bits >> 24); /* fall through */
      case 3: dst[*dpos + 2] = (uint8_t)(br->bits >> 16); /* fall through */
      case 2: dst[*dpos + 1] = (uint8_t)(br->bits >> 8);  /* fall through */
      case 1: dst[*dpos]     = (uint8_t)br->bits;         /* fall through */
      default: break;
    }
    infl_ft_consume(br, nbytes << 3);
    *dpos += nbytes;
    rem   -= nbytes;
  }

  if (unlikely((size_t)(br->end - br->p) < rem))
    return UNZ_ERR;

  if (rem)
    memcpy(dst + *dpos, br->p, rem);
  br->p += rem;
  *dpos += rem;

  return UNZ_OK;
}

static UNZ_HOT UnzResult
infl_ft_block(infl_ft_bits_t              * __restrict br,
              uint8_t                     * __restrict dst,
              size_t                      * __restrict dpos,
              size_t                                   dst_cap,
              const infl_ft_table_t       * __restrict tlit,
              const infl_ft_dist_table_t  * __restrict tdist) {
  size_t   pos, out_rem, src;
  unsigned len, dist, total, code_len, base;
  uint32_t entry;
  bool     fast_copy;

  pos = *dpos;
  infl_ft_refill_fast(br, 32);
  entry = infl_ft_lookup_lit(tlit, br->bits);

  for (;;) {
    bitstream_t saved;

    if (unlikely(!entry))
      return UNZ_ERR;

    total = INFL_FT_TOTAL(entry);

    if (unlikely(br->nbits < total)) {
      infl_ft_refill(br, total);
      if (unlikely(br->nbits < total))
        return UNZ_ERR;
    }

    if (likely(entry & INFL_FT_LITERAL)) {
      base = INFL_FT_BASE(entry);
      infl_ft_consume(br, total);

      if (unlikely(pos >= dst_cap))
        return UNZ_EFULL;
      dst[pos++] = (uint8_t)base;

      for (unsigned litrun = 2; litrun; litrun--) {
        if (unlikely(br->nbits < 15)) {
          infl_ft_refill_fast(br, 32);
          if (unlikely(br->nbits < 15))
            break;
        }

        entry = infl_ft_lookup_lit(tlit, br->bits);
        if (unlikely((entry & INFL_FT_LITERAL) == 0))
          goto next_symbol_ready;

        total = INFL_FT_TOTAL(entry);
        if (unlikely(br->nbits < total))
          break;

        if (unlikely(pos >= dst_cap))
          return UNZ_EFULL;
        dst[pos++] = (uint8_t)INFL_FT_BASE(entry);
        infl_ft_consume(br, total);
      }

      infl_ft_refill_fast(br, 32);
      entry = infl_ft_lookup_lit(tlit, br->bits);
next_symbol_ready:
      continue;
    }

    if (unlikely(entry & INFL_FT_END)) {
      infl_ft_consume(br, total);
      break;
    }

    saved    = br->bits;
    code_len = INFL_FT_CODELEN(entry);
    base     = INFL_FT_BASE(entry);
    infl_ft_consume(br, total);
    len = base + (unsigned)((saved & (((bitstream_t)1 << total) - 1u)) >> code_len);

    if (unlikely(br->nbits < 15)) {
      infl_ft_refill_fast(br, 32);
    }

    entry = infl_ft_lookup_dist(tdist, br->bits);
    if (unlikely(!entry))
      return UNZ_ERR;

    saved    = br->bits;
    total    = INFL_FT_TOTAL(entry);
    code_len = INFL_FT_CODELEN(entry);
    dist     = INFL_FT_BASE(entry);

    if (unlikely(br->nbits < total)) {
      infl_ft_refill(br, total);
      if (unlikely(br->nbits < total))
        return UNZ_ERR;
      saved = br->bits;
    }

    infl_ft_consume(br, total);
    dist += (unsigned)((saved & (((bitstream_t)1 << total) - 1u)) >> code_len);

    if (unlikely(!dist || (size_t)dist > pos))
      return UNZ_ERR;

    out_rem = dst_cap - pos;
    if (unlikely(len > out_rem))
      return UNZ_EFULL;
    fast_copy = likely(out_rem >= 258u + 39u);

    infl_ft_refill_fast(br, 32);
    entry = infl_ft_lookup_lit(tlit, br->bits);

    if (dist >= 8 && likely(fast_copy || (len >= 16 && len + 39 <= out_rem))) {
      infl_copy_match_overrun(dst, &pos, dist, len);
    } else if (dist >= 8 && likely(len + 7 <= out_rem)) {
      infl_copy_match_word(dst, &pos, dist, len);
    } else if (dist == 1 && likely(fast_copy || (len >= 32 && len + 39 <= out_rem))) {
      infl_copy_rle_overrun(dst, &pos, len);
    } else if (dist == 1 && likely(len + 7 <= out_rem)) {
      infl_copy_rle(dst, &pos, len);
    } else if (dist == 1) {
      unsigned byte = dst[pos - 1];

      while (len >= 8) {
        dst[pos]   = (uint8_t)byte;
        dst[pos+1] = (uint8_t)byte;
        dst[pos+2] = (uint8_t)byte;
        dst[pos+3] = (uint8_t)byte;
        dst[pos+4] = (uint8_t)byte;
        dst[pos+5] = (uint8_t)byte;
        dst[pos+6] = (uint8_t)byte;
        dst[pos+7] = (uint8_t)byte;
        len -= 8; pos += 8;
      }
      while (len >= 4) {
        dst[pos]   = (uint8_t)byte;
        dst[pos+1] = (uint8_t)byte;
        dst[pos+2] = (uint8_t)byte;
        dst[pos+3] = (uint8_t)byte;
        len -= 4; pos += 4;
      }
      if (len >= 1) {
        dst[pos] = (uint8_t)byte;
        switch (len - 1) {
          case 2: dst[pos+2] = (uint8_t)byte; /* fall through */
          case 1: dst[pos+1] = (uint8_t)byte; break;
          case 0:                         break;
        }
        pos += len;
      }
    } else if (likely(len + 7 <= out_rem)) {
      infl_copy_match_small_overrun(dst, &pos, dist, len);
    } else {
      src = pos - dist;
      while (len >= 8) {
        dst[pos]   = dst[src];
        dst[pos+1] = dst[src+1];
        dst[pos+2] = dst[src+2];
        dst[pos+3] = dst[src+3];
        dst[pos+4] = dst[src+4];
        dst[pos+5] = dst[src+5];
        dst[pos+6] = dst[src+6];
        dst[pos+7] = dst[src+7];
        len -= 8; pos += 8; src += 8;
      }
      while (len >= 4) {
        dst[pos]   = dst[src];
        dst[pos+1] = dst[src+1];
        dst[pos+2] = dst[src+2];
        dst[pos+3] = dst[src+3];
        len -= 4; pos += 4; src += 4;
      }
      if (len >= 1) {
        dst[pos] = dst[src];
        switch (len - 1) {
          case 2: dst[pos+2] = dst[src+2]; /* fall through */
          case 1: dst[pos+1] = dst[src+1]; break;
          case 0:                         break;
        }
        pos += len;
      }
    }
  }

  *dpos = pos;
  return UNZ_OK;
}

static UnzResult
infl_ft_dynamic(infl_ft_bits_t         * __restrict br,
                infl_ft_table_t        * __restrict tlit,
                infl_ft_dist_table_t   * __restrict tdist) {
  union {
    uint_fast8_t codelens[MAX_CODELEN_CODES];
    uint8_t      lens[MAX_LITLEN_CODES + MAX_DIST_CODES];
  } lens;
  huff_fast_entry_t tcodelen[HUFF_FAST_TABLE_SIZE];
  huff_fast_entry_t fe;
  int               i, n, hclen, hlit, hdist, repeat, prev;

  memset(&lens, 0, sizeof(lens));

  infl_ft_refill(br, 14);
  if (unlikely(br->nbits < 14))
    return UNZ_ERR;

  hlit  = (int)(br->bits & 0x1Fu) + 257;
  hdist = (int)((br->bits >> 5) & 0x1Fu) + 1;
  hclen = (int)((br->bits >> 10) & 0xFu) + 4;
  n     = hlit + hdist;
  infl_ft_consume(br, 14);

  if (unlikely(n > MAX_LITLEN_CODES + MAX_DIST_CODES))
    return UNZ_ERR;

  for (i = 0; i < hclen; i++) {
    infl_ft_refill(br, 3);
    if (unlikely(br->nbits < 3))
      return UNZ_ERR;
    lens.codelens[ord[i]] = (uint_fast8_t)(br->bits & 0x7u);
    infl_ft_consume(br, 3);
  }

  if (unlikely(!huff_init_fast_lsb(tcodelen, (const uint8_t *)lens.codelens,
                                  NULL, MAX_CODELEN_CODES)))
    return UNZ_ERR;

  for (i = MAX_CODELEN_CODES; i;)
    lens.codelens[--i] = 0;

  while (i < n) {
    infl_ft_refill(br, 14);
    if (unlikely(br->nbits < 7))
      return UNZ_ERR;

    fe = tcodelen[(uint8_t)br->bits];
    if (unlikely(!fe.len || fe.sym > 18))
      return UNZ_ERR;
    infl_ft_consume(br, fe.len);

    switch (fe.sym) {
      default:
        lens.lens[i++] = (uint8_t)fe.sym;
        break;
      case 16:
        if (unlikely(br->nbits < 2 || i == 0))
          return UNZ_ERR;
        repeat = 3 + (int)(br->bits & 0x3u);
        infl_ft_consume(br, 2);
        if (unlikely(i + repeat > n))
          return UNZ_ERR;
        prev = lens.lens[i - 1];
        while (repeat--)
          lens.lens[i++] = (uint8_t)prev;
        break;
      case 17:
        if (unlikely(br->nbits < 3))
          return UNZ_ERR;
        repeat = 3 + (int)(br->bits & 0x7u);
        infl_ft_consume(br, 3);
        if (unlikely(i + repeat > n))
          return UNZ_ERR;
        i += repeat;
        break;
      case 18:
        if (unlikely(br->nbits < 7))
          return UNZ_ERR;
        repeat = 11 + (int)(br->bits & 0x7Fu);
        infl_ft_consume(br, 7);
        if (unlikely(i + repeat > n))
          return UNZ_ERR;
        i += repeat;
        break;
    }
  }

  if (unlikely(!infl_ft_build(tlit->table, &tlit->used, lens.lens,
                              (uint16_t)hlit, INFL_FT_LIT_BITS,
                              INFL_FT_LIT_CAP, true) ||
               !infl_ft_build(tdist->table, &tdist->used, lens.lens + hlit,
                              (uint16_t)hdist, INFL_FT_DIST_BITS,
                              INFL_FT_DIST_CAP, false)))
    return UNZ_ERR;

  return UNZ_OK;
}

static UnzResult
infl_ft_full(defl_stream_t * __restrict stream) {
  static infl_ft_table_t      fixed_lit;
  static infl_ft_dist_table_t fixed_dist;
  static bool                 fixed_init;

  infl_ft_table_t      dyn_lit;
  infl_ft_dist_table_t dyn_dist;
  infl_ft_bits_t       br;
  const uint8_t       *p, *end;
  uint8_t             *dst;
  size_t               dpos, dst_cap;
  uint_fast8_t         bfinal, btype;
  bool                 zlib;

  if (!stream->start || stream->start != stream->end ||
      stream->dstpos != 0 || stream->bs.chunk || stream->header)
    return UNZ_NOOP;

  p   = stream->start->p;
  end = stream->start->end;
  if (!p || p >= end)
    return UNZ_NOOP;

  zlib = stream->flags == INFL_ZLIB;
  if (zlib) {
    uint8_t cmf, flg;

    if (unlikely((size_t)(end - p) < 2))
      return UNZ_ERR;

    cmf = p[0];
    flg = p[1];
    if (unlikely((cmf & 0x0f) != 8 || (cmf >> 4) > 7 ||
                 ((((uint16_t)cmf << 8) + flg) % 31) != 0 ||
                 (flg & 0x20)))
      return UNZ_ERR;
    p += 2;
  } else if (stream->flags != 0) {
    return UNZ_NOOP;
  }

  if (!fixed_init) {
    if (unlikely(!infl_ft_build(fixed_lit.table, &fixed_lit.used, fxd, 288,
                                INFL_FT_LIT_BITS, INFL_FT_LIT_CAP, true) ||
                 !infl_ft_build(fixed_dist.table, &fixed_dist.used, fxd + 288,
                                32, INFL_FT_DIST_BITS, INFL_FT_DIST_CAP,
                                false)))
      return UNZ_ERR;
    fixed_init = true;
  }

  br.p     = p;
  br.end   = end;
  br.bits  = 0;
  br.nbits = 0;
  dst      = stream->dst;
  dst_cap  = stream->dstlen;
  dpos     = 0;
  bfinal   = 0;

  while (!bfinal) {
    infl_ft_refill(&br, 3);
    if (unlikely(br.nbits < 3))
      return UNZ_ERR;

    bfinal = (uint_fast8_t)(br.bits & 1u);
    btype  = (uint_fast8_t)((br.bits >> 1) & 3u);
    infl_ft_consume(&br, 3);

    switch (btype) {
      case 0:
        if (unlikely(infl_ft_stored(&br, dst, &dpos, dst_cap) != UNZ_OK))
          return UNZ_ERR;
        break;
      case 1:
        if (unlikely(infl_ft_block(&br, dst, &dpos, dst_cap,
                                   &fixed_lit, &fixed_dist) < UNZ_OK))
          return UNZ_ERR;
        break;
      case 2:
        if (unlikely(infl_ft_dynamic(&br, &dyn_lit, &dyn_dist) != UNZ_OK))
          return UNZ_ERR;
        if (unlikely(infl_ft_block(&br, dst, &dpos, dst_cap,
                                   &dyn_lit, &dyn_dist) < UNZ_OK))
          return UNZ_ERR;
        break;
      default:
        return UNZ_ERR;
    }
  }

  stream->dstpos    = dpos;
  stream->bs.chunk  = stream->start;
  stream->bs.p      = br.p;
  stream->bs.end    = br.end;
  stream->bs.bits   = br.bits;
  stream->bs.nbits  = br.nbits;
  if (zlib)
    stream->header = stream;

  return UNZ_OK;
}

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
           unz__bitstate_t        * __restrict state,
           size_t                 * __restrict dst_pos,
           const huff_table_ext_t * __restrict tlit,
           const huff_table_ext_t * __restrict tdist) {
  uint8_t * __restrict dst;
  unz__bitstate_t bs;
  size_t          dst_cap, dpos, src, out_rem;
  unsigned        len,  dist;
  uint_fast16_t   lsym;
  uint8_t         used;

  dst     = stream->dst;
  dst_cap = stream->dstlen;
  dpos    = *dst_pos;

  bs = *state;

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

      for (unsigned litrun = 4; litrun && likely(dpos < dst_cap); litrun--) {
        const huff_fast_entry_ext_t *fe;

        if (unlikely(bs.nbits < HUFF_FAST_TABLE_BITS))
          break;

        fe = &tlit->fast[(uint8_t)bs.bits];
        if (unlikely(!fe->len || fe->sym >= 256))
          break;

        dst[dpos++] = (uint8_t)fe->sym;
        CONSUME(fe->len);
      }
      continue;
    } else if (unlikely(lsym == 256)) {
      /* eof */
      break;
    }

    REFILL(29);
    dist = huff_decode_lsb_ext(tdist, bs.bits, &used);

    /* validate distance */
    if (unlikely(!used || (size_t)(dist - 1) >= dpos))
      return UNZ_ERR;
    CONSUME(used);

    out_rem = dst_cap - dpos;
    if (unlikely(len > out_rem))
      return UNZ_EFULL;

    /* Fast match copies may over-write a few future bytes; keep it in bounds. */
    if (dist >= 8 && len >= 16 && likely(len + 39 <= out_rem)) {
      infl_copy_match_overrun(dst, &dpos, dist, len);
    } else if (dist >= 8 && likely(len + 7 <= out_rem)) {
      infl_copy_match_word(dst, &dpos, dist, len);
    } else if (dist == 1 && len >= 32 && likely(len + 39 <= out_rem)) {
      infl_copy_rle_overrun(dst, &dpos, len);
    } else if (dist == 1 && likely(len + 7 <= out_rem)) {
      infl_copy_rle(dst, &dpos, len);
    } else if (dist == 1) {
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
    } else if (likely(len + 7 <= out_rem)) {
      infl_copy_match_small_overrun(dst, &dpos, dist, len);
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
  *state   = bs;

  return UNZ_OK;
}

static UNZ_HOT
UnzResult
infl_raw(defl_stream_t   * __restrict stream,
         unz__bitstate_t * __restrict state,
         size_t          * __restrict dst_pos) {
  uint8_t        *dst;
  const uint8_t  *p;
  unz__bitstate_t bs;
  unsigned        dpos, dlen, chkrem, n, remlen, i, simdlen, align;
  uint32_t        header;
  uint16_t        len, nlen;

  dpos = (unsigned)*dst_pos;
  dlen = stream->dstlen;
  (void)simdlen; /* suppress unused warn */

  bs = *state;

  /* align to byte boundary */
  if ((align = infl_byte_align_drop(&bs)))
    infl_drop_bits(&bs, align);

  REFILL(32);
  header = (uint32_t)bs.bits;
  CONSUME(32);
  
  len  = header & 0xFFFF;
  nlen = header >> 16;

  if (unlikely((len^(uint16_t)~nlen)|(dpos+len > dlen)))
    return UNZ_ERR;

  remlen = len;
  dst    = stream->dst + dpos;

  while (remlen > 0 && bs.nbits + bs.npbits >= 8) {
    *dst++ = infl_take_byte(&bs);
    remlen--;
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

  *dst_pos = dpos + len;
  *state   = bs;

  return UNZ_OK;
}

UNZ_EXPORT
int
infl(defl_stream_t * __restrict stream) {
  static huff_table_ext_t _tlitl={0},_tdist={0};
  static bool             _init=false;

  unz__bitstate_t bs;
  size_t          dpos = stream->dstpos;
  uint_fast8_t    btype, bfinal = 0;
  UnzResult       ft_res, stored_res;
  bool            try_stored;

  try_stored = true;
  if (stream->start && stream->start == stream->end &&
      stream->dstpos == 0 && !stream->bs.chunk && !stream->header &&
      stream->start->p && stream->start->p < stream->start->end) {
    const uint8_t *sp = stream->start->p;
    const uint8_t *se = stream->start->end;

    if (stream->flags == 0) {
      try_stored = (((sp[0] >> 1) & 3u) == 0);
    } else if (stream->flags == INFL_ZLIB && (size_t)(se - sp) >= 3) {
      try_stored = (((sp[2] >> 1) & 3u) == 0);
    }
  }

  stored_res = try_stored ? infl_stored_direct(stream) : UNZ_NOOP;
  if (stored_res != UNZ_NOOP && stored_res != UNZ_UNFINISHED)
    return stored_res;

  if (stored_res == UNZ_NOOP) {
    ft_res = infl_ft_full(stream);
    if (ft_res != UNZ_NOOP)
      return ft_res;
  }

  if (stored_res == UNZ_NOOP) {
    if (!stream->bs.chunk && !(stream->bs.chunk = stream->start))
      goto noop;

    if (!stream->start->p || stream->start->p == stream->start->end) {
      RESTORE();
      goto ok;
    }
  }

  /* initilize static tables */
  if (!_init) {
    if (!huff_init_lsb_extof(&_tlitl,fxd,NULL,lvals,257,288) ||
        !huff_init_lsb_ext(&_tdist,fxd+288,NULL,dvals,32)) {
      goto err;
    }
    _init = true;
  }

  if (stored_res == UNZ_NOOP) {
    stream->bs.p   = stream->start->p;
    stream->bs.end = stream->start->end;
    if (stream->flags == INFL_ZLIB && unlikely(!stream->header)) {
      if (zlib_header(stream, &stream->bs.chunk, true) != UNZ_OK)
        goto err;
      stream->header = stream;
      stream->bs.p   = stream->bs.chunk->p;
      stream->bs.end = stream->bs.chunk->end;
    }
  }
  dpos = stream->dstpos;
  RESTORE();

  while (!bfinal && bs.chunk) {
    REFILL(3);
    bfinal = bs.bits & 0x1;
    btype  = (bs.bits >> 1) & 0x3;
    CONSUME(3);

    switch (btype) {
      case 0:
        if (infl_raw(stream, &bs, &dpos) < UNZ_OK) goto err;
        if (bfinal) goto ok;
        break;
      case 1:
        if (infl_block(stream, &bs, &dpos, &_tlitl, &_tdist) < UNZ_OK) goto err;
        if (bfinal) goto ok;
        break;
      case 2: {
        union {
          uint_fast8_t codelens[MAX_CODELEN_CODES];
          uint_fast8_t lens[MAX_LITLEN_CODES + MAX_DIST_CODES];
        } lens={0};
        huff_fast_entry_t tcodelen[HUFF_FAST_TABLE_SIZE];
        huff_table_ext_t  *dyn_tlen, *dyn_tdist;
        huff_fast_entry_t fe;
        int               i, n, hclen, hlit, hdist, repeat, prev;

        dyn_tlen  = &stream->ss.dyn.tlit;
        dyn_tdist = &stream->ss.dyn.tdist;

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
            case 17:
              repeat = 3 + (bs.bits & 0x7); CONSUME(3);
              if (i + repeat > n) goto err;
              i += repeat;
              break;
            case 18:
              repeat = 11 + (bs.bits & 0x7F); CONSUME(7);
              if (i + repeat > n) goto err;
              i += repeat;
              break;
          }
        }

        if (!huff_init_lsb_extof(dyn_tlen,lens.lens,NULL,lvals,257,hlit) ||
            !huff_init_lsb_ext(dyn_tdist,lens.lens+hlit,NULL,dvals,hdist))
          goto err;

        if (infl_block(stream, &bs, &dpos, dyn_tlen, dyn_tdist) < UNZ_OK) goto err;
        if (bfinal) goto ok;
      } break;
      default:
        goto err;
    }
  }

  /* stream->it = bs.chunk; */
ok:
  stream->dstpos = dpos;
  DONATE();
  return UNZ_OK;
noop:
  return UNZ_NOOP;
err:
  return UNZ_ERR;
}

UNZ_EXPORT
defl_stream_t *
infl_init(void * __restrict dst, uint32_t dstlen, int flags) {
  infl_stream_t *st;

  st          = calloc(1, sizeof(*st));
  if (!st)
    return NULL;

  st->dst     = (uint8_t *)dst;
  st->dstlen  = dstlen;

  st->malloc  = malloc;
  st->realloc = realloc;
  st->free    = free;
  st->flags   = flags;

  return st;
}

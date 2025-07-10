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

#ifndef api_common_h
#define api_common_h

#include "../common.h"
#include "../../include/defl/infl.h"
#include "../zlib/zlib.h"
#include <math.h>
#include <huff/huff.h>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#  include <arm_neon.h>
#endif

#if defined(__AVX2__)
#  include <immintrin.h>
#endif

#define MAX_CODELEN_CODES 19
#define MAX_LITLEN_CODES  288
#define MAX_DIST_CODES    32

static const huff_ext_t lvals[] = {
  {3,0,0},{4,0,0},{5,0,0},{6,0,0},{7,0,0},{8,0,0},{9,0,0},{10,0,0},{11,1,1},
  {13,1,1},{15,1,1},{17,1,1},{19,2,3},{23,2,3},{27,2,3},{31,2,3},{35,3,7},
  {43,3,7},{51,3,7},{59,3,7},{67,4,15},{83,4,15},{99,4,15},{115,4,15},
  {131,5,31},{163,5,31},{195,5,31},{227,5,31},{258,0,0},{0},{0}
};

static const huff_ext_t dvals[] = {
  {1,0,0},{2,0,0},{3,0,0},{4,0,0},{5,1,1},{7,1,1},{9,2,3},{13,2,3},{17,3,7},
  {25,3,7},{33,4,15},{49,4,15},{65,5,31},{97,5,31},{129,6,63},{193,6,63},
  {257,7,127},{385,7,127},{513,8,255},{769,8,255},{1025,9,511},{1537,9,511},
  {2049,10,1023},{3073,10,1023},{4097,11,2047},{6145,11,2047},
  {8193,12,4095},{12289,12,4095},{16385,13,8191},{24577,13,8191},{0},{0}
};

static const uint_fast8_t
  l_orders[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15},
  f_ldist[32]  = {5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5},
  f_llitl[288] = {
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
    9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
    9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
    9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,7,7,7,7,7,7,7,7,7,7,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,8,8,8,8,8,8,8,8
  };

#undef min
UNZ_INLINE int min(int a, int b) { return a < b ? a : b; }

#define EXTRACT(B,C)  ((B) & (((bitstream_t)1 << (C)) - 1))
#define CONSUME(N)    bs.bits >>= (N);bs.nbits -= (N);
#define RESTORE()     bs=stream->bs;
#define DONATE()      stream->bs=bs;

#endif /* api_common_h */

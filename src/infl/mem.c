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
#include "../include/defl/infl.h"

UNZ_EXPORT
void
infl_include(infl_stream_t * __restrict stream,
             const void    * __restrict ptr,
             uint32_t                   len) {
  unz_chunk_t *chk;

  chk      = calloc(1, sizeof(*chk));
  chk->p   = ptr;
  chk->end = ptr + len;

  if (!stream->start) { stream->start     = chk; }
  else                { stream->end->next = chk; }

  stream->end     = chk;
  stream->srclen += len;
}

UNZ_EXPORT
void
infl_destroy(defl_stream_t * __restrict stream) {
  unz_chunk_t *chk, *tofree;

  if (!stream) return;

  if ((chk = stream->start)) {
    do {
      tofree = chk;
      chk    = chk->next;
      free(tofree);
    } while (chk);
  }

  free(stream);
}

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

#ifndef defl_common_h
#define defl_common_h
#ifdef __cplusplus
extern "C" {
#endif

#if !defined(IM__in__UNZ) && !defined(unz_common_h)

#ifndef _USE_MATH_DEFINES
#  define _USE_MATH_DEFINES       /* for windows */
#endif

#ifndef _CRT_SECURE_NO_WARNINGS
#  define _CRT_SECURE_NO_WARNINGS /* for windows */
#endif

#ifndef _CRT_NONSTDC_NO_DEPRECATE
#  define _CRT_NONSTDC_NO_DEPRECATE /* for windows */
#endif

/* since C99 or compiler ext */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__MINGW64__)
#  define UNZ_WINAPI
#  pragma warning (disable : 4068) /* disable unknown pragma warnings */
#endif

#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__MINGW64__)
#  ifdef UNZ_STATIC
#    define UNZ_EXPORT
#  elif defined(UNZ_EXPORTS)
#    define UNZ_EXPORT __declspec(dllexport)
#  else
#    define UNZ_EXPORT __declspec(dllimport)
#  endif
#  define UNZ_HIDE
#else
#  define UNZ_EXPORT   __attribute__((visibility("default")))
#  define UNZ_HIDE     __attribute__((visibility("hidden")))
#endif

#if defined(_MSC_VER)
#  define UNZ_INLINE      __forceinline
#  define UNZ_ALIGN(X)    __declspec(align(X))
#  define UNZ_HOT
#  define UNZ_HOT_INLINE  __forceinline
#else
#  define UNZ_INLINE      static inline __attribute__((always_inline))
#  define UNZ_ALIGN(X)    __attribute__((aligned(X)))
#  define UNZ_HOT         __attribute__((hot))
#  define UNZ_HOT_INLINE  static inline __attribute__((hot, always_inline))
#endif

#ifndef __has_builtin
#  define __has_builtin(x) 0
#endif

typedef enum UnzResult {
  UNZ_NOOP     =  1,       /* no operation needed */
  UNZ_OK       =  0,
  UNZ_ERR      = -1,       /* UKNOWN ERR */
  UNZ_EFOUND   = -1000,
  UNZ_ENOMEM   = -ENOMEM,
  UNZ_EPERM    = -EPERM,
  UNZ_EBADF    = -EBADF,   /* file couldn't parsed / loaded */
  UNZ_EFULL    = -ENOBUFS  /* no space ENOBUFS vs ENOSPC    */
} UnzResult;

#endif

typedef struct unz__stream_t defl_stream_t;
typedef struct unz__stream_t infl_stream_t;
typedef struct unz__chunk_t  defl_chunk_t;

#ifdef __cplusplus
}
#endif
#endif /* defl_common_h */

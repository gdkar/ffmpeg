/*
 * Copyright (c) 2012 Ronald S. Bultje <rsbultje@gmail.com>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVUTIL_ATOMIC_GCC_H
#define AVUTIL_ATOMIC_GCC_H

#include <stdint.h>

#include "atomic.h"

#define avpriv_atomic_get(ptr) __atomic_load_n((ptr),__ATOMIC_SEQ_CST)
#define avpriv_atomic_set(ptr,val) __atomic_store_n((ptr),(val),__ATOMIC_SEQ_CST)
#define avpriv_atomic_ptr_set(ptr,val) __atomic_store_n((ptr),(val),__ATOMIC_SEQ_CST)
#if HAVE_ATOMIC_COMPARE_EXCHANGE
#   define avpriv_atomic_fetch_add(ptr,inc) __atomic_fetch_add((ptr),(inc),__ATOMIC_SEQ_CST)
#   define avpriv_atomic_fetch_sub(ptr,inc) __atomic_fetch_sub((ptr),(inc),__ATOMIC_SEQ_CST)
#   define avpriv_atomic_exchange(ptr,inc) __atomic_exchange_n((ptr),(inc),__ATOMIC_SEQ_CST)
#   define avpriv_atomic_cas(p,o,n) \
    __atomic_compare_exchange_n((p), &(o), (n), 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
#else
#   define avpriv_atomic_fetch_add(ptr,inc) __sync_fetch_add((ptr),(inc))
#   define avpriv_atomic_fetch_sub(ptr,inc) __sync_fetch_sub((ptr),(inc))
#   define avpriv_atomic_exchange(ptr,inc) __sync_lock_test_and_set((ptr),(inc))
#   define avpriv_atomic_cas(p,o,n) ({ \
        __typeof__(o) __atomic_cas_o = (o); \
        __atomic_cas_o == ((o) = __sync_val_compare_and_swap((p),__atomic_cas_o,(n))); \
        })
#endif
#define avpriv_atomic_int_get avpriv_atomic_get
#define avpriv_atomic_int_set avpriv_atomic_set
#define avpriv_atomic_int_fetch_add avpriv_atomic_fetch_add
#define avpriv_atomic_int_fetch_sub avpriv_atomic_fetch_sub
#define avpriv_atomic_int_exchange avpriv_atomic_exchange
#define avpriv_atomic_int_cas avpriv_atomic_cas

#define avpriv_atomic_ptr_get avpriv_atomic_get
#define avpriv_atomic_ptr_set avpriv_atomic_set
#define avpriv_atomic_ptr_fetch_add avpriv_atomic_fetch_add
#define avpriv_atomic_ptr_fetch_sub avpriv_atomic_fetch_sub
#define avpriv_atomic_ptr_exchange avpriv_atomic_exchange
#define avpriv_atomic_ptr_cas avpriv_atomic_cas

#endif

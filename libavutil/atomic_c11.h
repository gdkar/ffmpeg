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

#ifndef AVUTIL_ATOMIC_C11_H
#define AVUTIL_ATOMIC_C11_H

#include <stdint.h>
#include <stdatomic.h>
#include "atomic.h"

#define avpriv_atomic_get(p) atomic_load(p)
#define avpriv_atomic_set(p,v) atomic_store((p),(v))
#define avpriv_atomic_fetch_add(p,v) \
    atomic_fetch_add((p),(v))
#define avpriv_atomic_fetch_sub(p,v) \
    atomic_fetch_sub((p),(v))
#define avpriv_atomic_exchange(p,v) \
    atomic_exchange((p),(v))
#define avpriv_atomic_cas(p,o,n) \
    atomic_compare_exchange_strong((p),&(o),(n))

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

#endif /* AVUTIL_ATOMIC_C11_H */

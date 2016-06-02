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
#include <stdatomic.h>
#include "atomic.h"

#define avpriv_atomic_int_get(p) atomic_load(p)
#define avpriv_atomic_int_set(p,v) atomic_store(p,v)
#define avpriv_atomic_int_fetch_add(p,i) atomic_fetch_add((p),(i))
#define avpriv_atomic_int_exchange(p,w) atomic_exchange((p),(w))
#define avpriv_atomic_ptr_get(p) atomic_load(p)
#define avpriv_atomic_ptr_set(p,v) atomic_store(p,v)
#define avpriv_atomic_ptr_exchange(p,w) atomic_exchange((p),(w))
#define avpriv_atomic_ptr_cas(p,o,n) ({ \
            __typeof__(o) __atomic_cas_o = (o); \
            atomic_compare_exchange_strong((p),&__atomic_cas_o,(n)); \
            __atomic_cas_o; \
        })
#endif /* AVUTIL_ATOMIC_GCC_H */

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

#ifndef AVUTIL_ATOMIC_H
#define AVUTIL_ATOMIC_H

#include "config.h"

#if HAVE_ATOMICS_NATIVE
#if HAVE_ATOMICS_C11
#include "atomic_c11.h"
#elif HAVE_ATOMICS_GCC
#include "atomic_gcc.h"
#elif HAVE_ATOMICS_WIN32
#include "atomic_win32.h"
#elif HAVE_ATOMICS_SUNCC
#include "atomic_suncc.h"
#endif

#else
#ifndef AV_ATOMIC
#define AV_ATOMIC(type) type volatile
#endif
/**
 * Load the current value stored in an atomic integer.
 *
 * @param ptr atomic integer
 * @return the current value of the atomic integer
 * @note This acts as a memory barrier.
 */
int avpriv_atomic_int_get(AV_ATOMIC(int) *ptr);

/**
 * Store a new value in an atomic integer.
 *
 * @param ptr atomic integer
 * @param val the value to store in the atomic integer
 * @note This acts as a memory barrier.
 */
void avpriv_atomic_int_set(AV_ATOMIC(int) *ptr, int val);

/**
 * Add a value to an atomic integer.
 *
 * @param ptr atomic integer
 * @param inc the value to add to the atomic integer (may be negative)
 * @return the new value of the atomic integer.
 * @note This does NOT act as a memory barrier. This is primarily
 *       intended for reference counting.
 */
int avpriv_atomic_int_fetch_add(AV_ATOMIC(int) *ptr, int inc);
/**
 * Add a value to an atomic integer.
 *
 * @param ptr atomic integer
 * @param inc the value to add to the atomic integer (may be negative)
 * @return the new value of the atomic integer.
 * @note This does NOT act as a memory barrier. This is primarily
 *       intended for reference counting.
 */
int avpriv_atomic_int_exchange(AV_ATOMIC(int) *ptr, int with);

/*
 * Load the current value stored in an atomic integer.
 *
 * @param ptr atomic integer
 * @return the current value of the atomic integer
 * @note This acts as a memory barrier.
 */
void *avpriv_atomic_ptr_get(AV_ATOMIC(void * )*ptr);

/**
 * Store a new value in an atomic integer.
 *
 * @param ptr atomic integer
 * @param val the value to store in the atomic integer
 * @note This acts as a memory barrier.
 */
void avpriv_atomic_ptr_set(AV_ATOMIC(void *) *ptr, void *val);

/**
 * Add a value to an atomic integer.
 *
 * @param ptr atomic integer
 * @param inc the value to add to the atomic integer (may be negative)
 * @return the new value of the atomic integer.
 * @note This does NOT act as a memory barrier. This is primarily
 *       intended for reference counting.
 */
void *avpriv_atomic_ptr_exchange(AV_ATOMIC(void *) *ptr, void *with);
/**
 * Atomic pointer compare and swap.
 *
 * @param ptr pointer to the pointer to operate on
 * @param oldval do the swap if the current value of *ptr equals to oldval
 * @param newval value to replace *ptr with
 * @return the value of *ptr before comparison
 */
void *avpriv_atomic_ptr_cas(AV_ATOMIC(void *) *ptr, void *oldval, void *newval);

#endif /* HAVE_ATOMICS_NATIVE */

#endif /* AVUTIL_ATOMIC_H */

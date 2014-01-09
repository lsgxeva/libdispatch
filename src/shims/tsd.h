/*
 * Copyright (c) 2008-2011 Apple Inc. All rights reserved.
 *
 * @APPLE_APACHE_LICENSE_HEADER_START@
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * @APPLE_APACHE_LICENSE_HEADER_END@
 */

/*
 * IMPORTANT: This header file describes INTERNAL interfaces to libdispatch
 * which are subject to change in future releases of Mac OS X. Any applications
 * relying on these interfaces WILL break.
 */

#ifndef __DISPATCH_SHIMS_TSD__
#define __DISPATCH_SHIMS_TSD__

#if HAVE_PTHREAD_MACHDEP_H
#include <pthread_machdep.h>
#endif

#define DISPATCH_TSD_INLINE DISPATCH_ALWAYS_INLINE_NDEBUG

#if USE_APPLE_TSD_OPTIMIZATIONS && HAVE_PTHREAD_KEY_INIT_NP && \
		!defined(DISPATCH_USE_DIRECT_TSD)
#define DISPATCH_USE_DIRECT_TSD 1
#endif

#if !TARGET_OS_WIN32
typedef pthread_key_t dispatch_thread_key_t;
#else
typedef struct dispatch_thread_key_s {
	uintptr_t tls_index_plus_one;
	void (*destructor)(void *);
} *dispatch_thread_key_t;

#define DISPATCH_MAX_TLS_SLOTS 6

extern 
dispatch_thread_key_s _dispatch_tls_keys[DISPATCH_MAX_TLS_SLOTS];

#endif

#if DISPATCH_USE_DIRECT_TSD
static const unsigned long dispatch_queue_key	= __PTK_LIBDISPATCH_KEY0;
static const unsigned long dispatch_sema4_key	= __PTK_LIBDISPATCH_KEY1;
static const unsigned long dispatch_cache_key	= __PTK_LIBDISPATCH_KEY2;
static const unsigned long dispatch_io_key		= __PTK_LIBDISPATCH_KEY3;
static const unsigned long dispatch_apply_key	= __PTK_LIBDISPATCH_KEY4;
static const unsigned long dispatch_bcounter_key  = __PTK_LIBDISPATCH_KEY5;

DISPATCH_TSD_INLINE
static inline void
_dispatch_thread_key_create(const unsigned long *k, void (*d)(void *))
{
	dispatch_assert_zero(pthread_key_init_np((int)*k, d));
}
#else
extern dispatch_thread_key_t dispatch_queue_key;
extern dispatch_thread_key_t dispatch_sema4_key;
extern dispatch_thread_key_t dispatch_cache_key;
extern dispatch_thread_key_t dispatch_io_key;
extern dispatch_thread_key_t dispatch_apply_key;
extern dispatch_thread_key_t dispatch_bcounter_key;

DISPATCH_TSD_INLINE
static inline void
_dispatch_thread_key_create(dispatch_thread_key_t *k, void (*d)(void *))
{
#if !TARGET_OS_WIN32
  dispatch_assert_zero(pthread_key_create(k, d));
#else
  // This isn't thread-safe, unlike pthread_key_create, but as we create our tls
  // keys once at library init time, we don't care.
  DWORD tls_index = TlsAlloc();
  dispatch_thread_key_s new_key = {tls_index + 1, d};
  if (tls_index == TLS_OUT_OF_INDEXES) goto ohno;

  for (size_t i = 0; i < DISPATCH_MAX_TLS_SLOTS; i++) {
	dispatch_thread_key_t key = &_dispatch_tls_keys[i];
	if (key->tls_index_plus_one) continue;
	*key = new_key;
	*k = key;
	return;
  }
ohno:
	DISPATCH_CRASH("Out of tsd indexes");
#endif
}
#endif	// DISPATCH_USE_DIRECT_TSD

#if DISPATCH_USE_TSD_BASE && !DISPATCH_DEBUG
#else  // DISPATCH_USE_TSD_BASE
DISPATCH_TSD_INLINE
static inline void
_dispatch_thread_setspecific(dispatch_thread_key_t k, void *v)
{
#if DISPATCH_USE_DIRECT_TSD
  if (_pthread_has_direct_tsd()) {
	(void)_pthread_setspecific_direct(k, v);
	return;
  }
#elif TARGET_OS_WIN32
  dispatch_assert(TlsSetValue(k->tls_index_plus_one - 1, v));
#else
	dispatch_assert_zero(pthread_setspecific(k, v));
#endif
}

DISPATCH_TSD_INLINE
static inline void *
_dispatch_thread_getspecific(dispatch_thread_key_t k)
{
#if DISPATCH_USE_DIRECT_TSD
  if (_pthread_has_direct_tsd()) {
	return _pthread_getspecific_direct(k);
  }
#elif TARGET_OS_WIN32
	return TlsGetValue(k->tls_index_plus_one - 1);
#else
	return pthread_getspecific(k);
#endif
}
#endif	// DISPATCH_USE_TSD_BASE

DISPATCH_TSD_INLINE
static inline void
_dispatch_call_tsd_destructors()
{
#if TARGET_OS_WIN32
  for (size_t i = 0; i < DISPATCH_MAX_TLS_SLOTS; ++i) {
	dispatch_thread_key_t key = &_dispatch_tls_keys[i];
	if (!key->destructor) continue;
	
	void* val = _dispatch_thread_getspecific(key);
	if (!val) continue;
	
	_dispatch_thread_setspecific(key, NULL);
	key->destructor(val);
  }
#endif
}

#if !TARGET_OS_WIN32
#define _dispatch_thread_self() (uintptr_t)pthread_self()
#else
#define _dispatch_thread_self() (uintptr_t)GetCurrentThreadId()
#endif

#undef DISPATCH_TSD_INLINE

#endif

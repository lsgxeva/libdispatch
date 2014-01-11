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

#include "internal.h"

// semaphores are too fundamental to use the dispatch_assume*() macros
#if USE_MACH_SEM
#define DISPATCH_SEMAPHORE_VERIFY_KR(x) do { \
		if (slowpath(x)) { \
			DISPATCH_CRASH("flawed group/semaphore logic"); \
		} \
	} while (0)
#elif USE_POSIX_SEM
#define DISPATCH_SEMAPHORE_VERIFY_RET(x) do { \
		if (slowpath((x) == -1)) { \
			DISPATCH_CRASH("flawed group/semaphore logic"); \
		} \
	} while (0)
#elif USE_WIN32_SEM
#define DISPATCH_SEMAPHORE_VERIFY_RET(x) do { \
		if (slowpath(!(x))) { \
			DISPATCH_CRASH("flawed group/semaphore logic"); \
		} \
	} while (0)
#endif

DISPATCH_WEAK // rdar://problem/8503746
long _dispatch_semaphore_signal_slow(dispatch_semaphore_t dsema);

static long _dispatch_group_wake(dispatch_semaphore_t dsema);


DISPATCH_ALWAYS_INLINE
static dispatch_platform_semaphore_t
_dispatch_platform_semaphore_create();
DISPATCH_ALWAYS_INLINE
static void
_dispatch_platform_semaphore_dispose(dispatch_platform_semaphore_t sema);
DISPATCH_ALWAYS_INLINE
static void
_dispatch_platform_semaphore_signal(dispatch_platform_semaphore_t sema);
DISPATCH_ALWAYS_INLINE
static bool
_dispatch_platform_semaphore_wait(dispatch_platform_semaphore_t sema,
		dispatch_time_t timeout);

#pragma mark -
#pragma mark dispatch_semaphore_t

static void
_dispatch_semaphore_init(long value, dispatch_object_t dou)
{
	dispatch_semaphore_t dsema = dou._dsema;

	dsema->do_next = DISPATCH_OBJECT_LISTLESS;
	dsema->do_targetq = dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
	dsema->dsema_value = value;
	dsema->dsema_orig = value;
}

dispatch_semaphore_t
dispatch_semaphore_create(long value)
{
	dispatch_semaphore_t dsema;

	// If the internal value is negative, then the absolute of the value is
	// equal to the number of waiting threads. Therefore it is bogus to
	// initialize the semaphore with a negative value.
	if (value < 0) {
		return NULL;
	}

	dsema = _dispatch_alloc(DISPATCH_VTABLE(semaphore),
			sizeof(struct dispatch_semaphore_s));
	_dispatch_semaphore_init(value, DOBJ(dsema));
	return dsema;
}

static void
_dispatch_semaphore_create_handle(dispatch_platform_semaphore_t *s4)
{
	if (*s4) {
		return;
	}
	// lazily allocate the semaphore port

	// Someday:
	// 1) Switch to a doubly-linked FIFO in user-space.
	// 2) User-space timers for the timeout.
	// 3) Use the per-thread semaphore port.

	_dispatch_safe_fork = false;

	dispatch_platform_semaphore_t tmp = _dispatch_platform_semaphore_create();

	if (!dispatch_atomic_cmpxchg(s4, 0, tmp)) {
		_dispatch_platform_semaphore_dispose(tmp);
	}
}

DISPATCH_ALWAYS_INLINE
dispatch_platform_semaphore_t
_dispatch_platform_semaphore_create()
{
	dispatch_platform_semaphore_t sema;
#if USE_MACH_SEM
	kern_return_t kr;
	while ((kr = semaphore_create(mach_task_self(), &sema,
			SYNC_POLICY_FIFO, 0))) {
		DISPATCH_VERIFY_MIG(kr);
		sleep(1);
	}
#elif USE_POSIX_SEM
	int ret;

	while (!(sema = malloc(sizeof(*sema))) {
		sleep(1);
	}

	while ((ret = sem_init(sema, 0, 0)) && errno == ENOSPC) {
		sleep(1);
	}
	DISPATCH_SEMAPHORE_VERIFY_RET(ret);
#elif USE_WIN32_SEM
	while (!(sema = CreateSemaphore(NULL, 0, LONG_MAX, NULL))) {
		DISPATCH_SEMAPHORE_VERIFY_RET(sema != NULL);
		sleep(1);
	}
#endif
	return sema;
}

DISPATCH_ALWAYS_INLINE
static void
_dispatch_platform_semaphore_dispose(dispatch_platform_semaphore_t sema)
{
	if (!sema) return;
#if USE_MACH_SEM
	kern_return_t kr = semaphore_destroy(mach_task_self(), sema);
	DISPATCH_SEMAPHORE_VERIFY_KR(kr);
#elif USE_POSIX_SEM
	int ret = sem_destroy(sema);
	DISPATCH_SEMAPHORE_VERIFY_RET(ret);
	free(sema);
#elif USE_WIN32_SEM
	DISPATCH_SEMAPHORE_VERIFY_RET(CloseHandle(sema));
#endif
}

DISPATCH_ALWAYS_INLINE
static void
_dispatch_platform_semaphore_signal(dispatch_platform_semaphore_t sema)
{
#if USE_MACH_SEM
	kern_return_t kr = semaphore_signal(sema);
	DISPATCH_SEMAPHORE_VERIFY_KR(kr);
#elif USE_POSIX_SEM
	int ret = sem_post(sema);
	DISPATCH_SEMAPHORE_VERIFY_RET(ret);
#elif USE_WIN32_SEM
	BOOL ret = ReleaseSemaphore(sema, 1, NULL);
	DISPATCH_SEMAPHORE_VERIFY_RET(ret);
#endif
}

// return true if the semaphore was decremented, false if the operation timed
// out.
DISPATCH_ALWAYS_INLINE
bool
_dispatch_platform_semaphore_wait(dispatch_platform_semaphore_t sema,
		dispatch_time_t timeout)
{
	dispatch_assert(timeout != DISPATCH_TIME_NOW);
#if USE_MACH_SEM
	kern_return_t kr;
	if (timeout == DISPATCH_TIME_FOREVER) {
		do {
			kr = semaphore_wait(sema);
		} while (kr == KERN_ABORTED);
		DISPATCH_SEMAPHORE_VERIFY_KR(kr);
	} else {
		do {
			mach_timespec_t platform_timeout;
			uint64_t nsec = _dispatch_timeout(timeout);
			platform_timeout.tv_sec =
				(typeof(platform_timeout.tv_sec))(nsec / NSEC_PER_SEC);
			platform_timeout.tv_nsec =
				(typeof(platform_timeout.tv_nsec))(nsec % NSEC_PER_SEC);
			kr = slowpath(semaphore_timedwait(sema, platform_timeout));
		} while (kr == KERN_ABORTED);
		if (kr != KERN_OPERATION_TIMED_OUT) {
			DISPATCH_SEMAPHORE_VERIFY_KR(kr);
		}
	}
	return kr == KERN_SUCCESS;

#elif USE_POSIX_SEM
	int ret;
	if (timeout == DISPATCH_TIME_FOREVER)
		do {
			ret = sem_wait(sema);
		} while (ret == -1 && errno == EINTR);
		DISPATCH_SEMAPHORE_VERIFY_RET(ret);
	} else {
		do {
			struct timespec platform_timeout = _dispatch_timeout_ts(timeout);
			ret = slowpath(sem_timedwait(sema, &platform_timeout));
		} while (ret == -1 && errno == EINTR);
		if (!(ret == -1 && errno == ETIMEDOUT))
			DISPATCH_SEMAPHORE_VERIFY_RET(ret);
		}
	}
	return ret == 0;

#elif USE_WIN32_SEM
	DWORD ret;
	if (timeout == DISPATCH_TIME_FOREVER) {
		ret = WaitForSingleObject(sema, INFINITE);
		BOOL success = ret == WAIT_OBJECT_0;
		DISPATCH_SEMAPHORE_VERIFY_RET(success);
	} else {
		uint64_t nsec = _dispatch_timeout(timeout);
		DWORD	msec = (DWORD)(nsec / NSEC_PER_MSEC);
		ret = WaitForSingleObject(sema, msec);
		BOOL success = ret == WAIT_OBJECT_0 || ret == WAIT_TIMEOUT;
		DISPATCH_SEMAPHORE_VERIFY_RET(success);
	}
	return ret == WAIT_OBJECT_0;
#endif
}

void
_dispatch_semaphore_dispose(dispatch_object_t dou)
{
	dispatch_semaphore_t dsema = dou._dsema;

	if (dsema->dsema_value < dsema->dsema_orig) {
		DISPATCH_CLIENT_CRASH(
				"Semaphore/group object deallocated while in use");
	}

	_dispatch_platform_semaphore_dispose(dsema->dsema_handle);
	_dispatch_platform_semaphore_dispose(dsema->dsema_waiter_handle);
}

size_t
_dispatch_semaphore_debug(dispatch_object_t dou, char *buf, size_t bufsiz)
{
	dispatch_semaphore_t dsema = dou._dsema;

	size_t offset = 0;
	offset += snprintf(&buf[offset], bufsiz - offset, "%s[%p] = { ",
			dx_kind(dsema), dsema);
	offset += _dispatch_object_debug_attr(DOBJ(dsema), &buf[offset], bufsiz - offset);
	offset += snprintf(&buf[offset], bufsiz - offset, "handle = 0x%u, ",
			dsema->dsema_handle);
	offset += snprintf(&buf[offset], bufsiz - offset,
			"value = %ld, orig = %ld }", dsema->dsema_value, dsema->dsema_orig);
	return offset;
}

DISPATCH_NOINLINE
long
_dispatch_semaphore_signal_slow(dispatch_semaphore_t dsema)
{
	// Before dsema_sent_ksignals is incremented we can rely on the reference
	// held by the waiter. However, once this value is incremented the waiter
	// may return between the atomic increment and the semaphore_signal(),
	// therefore an explicit reference must be held in order to safely access
	// dsema after the atomic increment.
	_dispatch_retain(DOBJ(dsema));

	(void)dispatch_atomic_inc2o(dsema, dsema_sent_ksignals);

	_dispatch_semaphore_create_handle(&dsema->dsema_handle);
	_dispatch_platform_semaphore_signal(dsema->dsema_handle);

	_dispatch_release(DOBJ(dsema));
	return 1;
}

long
dispatch_semaphore_signal(dispatch_semaphore_t dsema)
{
	dispatch_atomic_release_barrier();
	long value = dispatch_atomic_inc2o(dsema, dsema_value);
	if (fastpath(value > 0)) {
		return 0;
	}
	if (slowpath(value == LONG_MIN)) {
		DISPATCH_CLIENT_CRASH("Unbalanced call to dispatch_semaphore_signal()");
	}
	return _dispatch_semaphore_signal_slow(dsema);
}

DISPATCH_NOINLINE
static long
_dispatch_semaphore_wait_slow(dispatch_semaphore_t dsema,
		dispatch_time_t timeout)
{
	long orig;

again:
	// Mach semaphores appear to sometimes spuriously wake up. Therefore,
	// we keep a parallel count of the number of times a Mach semaphore is
	// signaled (6880961).
	while ((orig = dsema->dsema_sent_ksignals)) {
		if (dispatch_atomic_cmpxchg2o(dsema, dsema_sent_ksignals, orig,
				orig - 1)) {
			return 0;
		}
	}

	_dispatch_semaphore_create_handle(&dsema->dsema_handle);

	// From xnu/osfmk/kern/sync_sema.c:
	// wait_semaphore->count = -1; /* we don't keep an actual count */
	//
	// The code above does not match the documentation, and that fact is
	// not surprising. The documented semantics are clumsy to use in any
	// practical way. The above hack effectively tricks the rest of the
	// Mach semaphore logic to behave like the libdispatch algorithm.

	switch (timeout) {
	default:
		if (_dispatch_platform_semaphore_wait(dsema->dsema_handle, timeout)) {
			break;
		}
		// Fall through and try to undo what the fast path did to
		// dsema->dsema_value
	case DISPATCH_TIME_NOW:
		while ((orig = dsema->dsema_value) < 0) {
			if (dispatch_atomic_cmpxchg2o(dsema, dsema_value, orig, orig + 1)) {
				goto timeout;
			}
		}
		// Another thread called semaphore_signal().
		// Fall through and drain the wakeup.
	case DISPATCH_TIME_FOREVER:
		_dispatch_platform_semaphore_wait(dsema->dsema_handle,
											DISPATCH_TIME_FOREVER);
		break;
	}

	goto again;

timeout:
#if USE_MACH_SEM
	return KERN_OPERATION_TIMED_OUT;
#elif USE_POSIX_SEM
	errno = ETIMEDOUT;
	return -1;
#elif USE_WIN32_SEM
	return WAIT_TIMEOUT;
#endif
}

long
dispatch_semaphore_wait(dispatch_semaphore_t dsema, dispatch_time_t timeout)
{
	long value = dispatch_atomic_dec2o(dsema, dsema_value);
	dispatch_atomic_acquire_barrier();
	if (fastpath(value >= 0)) {
		return 0;
	}
	return _dispatch_semaphore_wait_slow(dsema, timeout);
}

#pragma mark -
#pragma mark dispatch_group_t

dispatch_group_t
dispatch_group_create(void)
{
	dispatch_group_t dg = _dispatch_alloc(DISPATCH_VTABLE(group),
			sizeof(struct dispatch_semaphore_s));
	_dispatch_semaphore_init(LONG_MAX, DOBJ(dg));
	return dg;
}

void
dispatch_group_enter(dispatch_group_t dg)
{
	dispatch_semaphore_t dsema = (dispatch_semaphore_t)dg;

	(void)dispatch_semaphore_wait(dsema, DISPATCH_TIME_FOREVER);
}

DISPATCH_NOINLINE
static long
_dispatch_group_wake(dispatch_semaphore_t dsema)
{
	struct dispatch_sema_notify_s *next, *head, *tail = NULL;
	long rval;

	head = dispatch_atomic_xchg2o(dsema, dsema_notify_head, NULL);
	if (head) {
		// snapshot before anything is notified/woken <rdar://problem/8554546>
		tail = dispatch_atomic_xchg2o(dsema, dsema_notify_tail, NULL);
	}
	rval = dispatch_atomic_xchg2o(dsema, dsema_group_waiters, 0);
	if (rval) {
		// wake group waiters
		_dispatch_semaphore_create_handle(&dsema->dsema_waiter_handle);
		do {
			_dispatch_platform_semaphore_signal(dsema->dsema_waiter_handle);
		} while (--rval);
	}
	if (head) {
		// async group notify blocks
		do {
			dispatch_async_f(head->dsn_queue, head->dsn_ctxt, head->dsn_func);
			_dispatch_release(DOBJ(head->dsn_queue));
			next = fastpath(head->dsn_next);
			if (!next && head != tail) {
				while (!(next = fastpath(head->dsn_next))) {
					_dispatch_hardware_pause();
				}
			}
			free(head);
		} while ((head = next));
		_dispatch_release(DOBJ(dsema));
	}
	return 0;
}

void
dispatch_group_leave(dispatch_group_t dg)
{
	dispatch_semaphore_t dsema = (dispatch_semaphore_t)dg;
	dispatch_atomic_release_barrier();
	long value = dispatch_atomic_inc2o(dsema, dsema_value);
	if (slowpath(value == LONG_MIN)) {
		DISPATCH_CLIENT_CRASH("Unbalanced call to dispatch_group_leave()");
	}
	if (slowpath(value == dsema->dsema_orig)) {
		(void)_dispatch_group_wake(dsema);
	}
}

DISPATCH_NOINLINE
static long
_dispatch_group_wait_slow(dispatch_semaphore_t dsema, dispatch_time_t timeout)
{
	long orig;

again:
	// check before we cause another signal to be sent by incrementing
	// dsema->dsema_group_waiters
	if (dsema->dsema_value == dsema->dsema_orig) {
		return _dispatch_group_wake(dsema);
	}
	// Mach semaphores appear to sometimes spuriously wake up. Therefore,
	// we keep a parallel count of the number of times a Mach semaphore is
	// signaled (6880961).
	(void)dispatch_atomic_inc2o(dsema, dsema_group_waiters);
	// check the values again in case we need to wake any threads
	if (dsema->dsema_value == dsema->dsema_orig) {
		return _dispatch_group_wake(dsema);
	}

	_dispatch_semaphore_create_handle(&dsema->dsema_waiter_handle);

	// From xnu/osfmk/kern/sync_sema.c:
	// wait_semaphore->count = -1; /* we don't keep an actual count */
	//
	// The code above does not match the documentation, and that fact is
	// not surprising. The documented semantics are clumsy to use in any
	// practical way. The above hack effectively tricks the rest of the
	// Mach semaphore logic to behave like the libdispatch algorithm.

	switch (timeout) {
	default:
		if (_dispatch_platform_semaphore_wait(dsema->dsema_waiter_handle,
												timeout)) {
			break;
		}
		// Fall through and try to undo the earlier change to
		// dsema->dsema_group_waiters
	case DISPATCH_TIME_NOW:
		while ((orig = dsema->dsema_group_waiters)) {
			if (dispatch_atomic_cmpxchg2o(dsema, dsema_group_waiters, orig,
					orig - 1)) {
				goto timeout;
			}
		}
		// Another thread called semaphore_signal().
		// Fall through and drain the wakeup.
	case DISPATCH_TIME_FOREVER:
		_dispatch_platform_semaphore_wait(dsema->dsema_waiter_handle,
											DISPATCH_TIME_FOREVER);
		break;
	}

	goto again;

timeout:
#if USE_MACH_SEM
	return KERN_OPERATION_TIMED_OUT;
#elif USE_POSIX_SEM
	errno = ETIMEDOUT;
	return -1;
#elif USE_WIN32_SEM
	return WAIT_TIMEOUT;
#endif
}

long
dispatch_group_wait(dispatch_group_t dg, dispatch_time_t timeout)
{
	dispatch_semaphore_t dsema = (dispatch_semaphore_t)dg;

	if (dsema->dsema_value == dsema->dsema_orig) {
		return 0;
	}
	if (timeout == DISPATCH_TIME_NOW) {
#if USE_MACH_SEM
		return KERN_OPERATION_TIMED_OUT;
#elif USE_POSIX_SEM
		errno = ETIMEDOUT;
		return (-1);
#elif USE_WIN32_SEM
		return WAIT_TIMEOUT;
#endif
	}
	return _dispatch_group_wait_slow(dsema, timeout);
}

DISPATCH_NOINLINE
void
dispatch_group_notify_f(dispatch_group_t dg, dispatch_queue_t dq, void *ctxt,
		void (*func)(void *))
{
	dispatch_semaphore_t dsema = (dispatch_semaphore_t)dg;
	struct dispatch_sema_notify_s *dsn, *prev;

	// FIXME -- this should be updated to use the continuation cache
	while (!(dsn = calloc(1, sizeof(*dsn)))) {
		sleep(1);
	}

	dsn->dsn_queue = dq;
	dsn->dsn_ctxt = ctxt;
	dsn->dsn_func = func;
	_dispatch_retain(DOBJ(dq));
	dispatch_atomic_store_barrier();
	prev = dispatch_atomic_xchg2o(dsema, dsema_notify_tail, dsn);
	if (fastpath(prev)) {
		prev->dsn_next = dsn;
	} else {
		_dispatch_retain(DOBJ(dg));
		(void)dispatch_atomic_xchg2o(dsema, dsema_notify_head, dsn);
		if (dsema->dsema_value == dsema->dsema_orig) {
			_dispatch_group_wake(dsema);
		}
	}
}

#ifdef __BLOCKS__
void
dispatch_group_notify(dispatch_group_t dg, dispatch_queue_t dq,
		dispatch_block_t db)
{
	dispatch_group_notify_f(dg, dq, _dispatch_Block_copy(db),
			_dispatch_call_block_and_release);
}
#endif

#pragma mark -
#pragma mark _dispatch_thread_semaphore_t

DISPATCH_NOINLINE
static _dispatch_thread_semaphore_t
_dispatch_thread_semaphore_create(void)
{
	_dispatch_safe_fork = false;
	return (_dispatch_thread_semaphore_t)_dispatch_platform_semaphore_create();
}

DISPATCH_NOINLINE
void
_dispatch_thread_semaphore_dispose(_dispatch_thread_semaphore_t sema)
{
	_dispatch_platform_semaphore_dispose((dispatch_platform_semaphore_t)sema);
}

void
_dispatch_thread_semaphore_signal(_dispatch_thread_semaphore_t sema)
{
	_dispatch_platform_semaphore_signal((dispatch_platform_semaphore_t)sema);

}

void
_dispatch_thread_semaphore_wait(_dispatch_thread_semaphore_t sema)
{
	_dispatch_platform_semaphore_wait((dispatch_platform_semaphore_t)sema,
									  DISPATCH_TIME_FOREVER);
}

_dispatch_thread_semaphore_t
_dispatch_get_thread_semaphore(void)
{
	_dispatch_thread_semaphore_t sema = (_dispatch_thread_semaphore_t)
			_dispatch_thread_getspecific(dispatch_sema4_key);
	if (slowpath(!sema)) {
		return _dispatch_thread_semaphore_create();
	}
	_dispatch_thread_setspecific(dispatch_sema4_key, NULL);
	return sema;
}

void
_dispatch_put_thread_semaphore(_dispatch_thread_semaphore_t sema)
{
	_dispatch_thread_semaphore_t old_sema = (_dispatch_thread_semaphore_t)
			_dispatch_thread_getspecific(dispatch_sema4_key);
	_dispatch_thread_setspecific(dispatch_sema4_key, (void*)sema);
	if (slowpath(old_sema)) {
		return _dispatch_thread_semaphore_dispose(old_sema);
	}
}

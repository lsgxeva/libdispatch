/*
 * Copyright (c) 2014 Nick Hutchinson <nshutchinson@gmail.com>.
 * All rights reserved.
 *
 * @APPLE_APACHE_LICENSE_HEADER_START@
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *	   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * @APPLE_APACHE_LICENSE_HEADER_END@
 */

#include "pthread_main_np.h"
#include "../internal.h"

#if _WIN32
#include <stddef.h>
#include <stdint.h>
#include <Windows.h>
#include <TlHelp32.h>

static uint64_t _integer_from_filetime(FILETIME ft)
{
	return (uint64_t)ft.dwHighDateTime << 32 | ft.dwLowDateTime;
}

struct _main_thread_info_s {
	DWORD thread_id;
	uint64_t creation_time; /* as Win32 FILETIME */
};

static void _find_main_thread_id(void *context)
{
	DWORD current_process_id = GetCurrentProcessId();
	struct _main_thread_info_s *main_thread_info =
		(struct _main_thread_info_s *)context;
	main_thread_info->creation_time = UINT64_MAX;
	main_thread_info->thread_id = 0;

	HANDLE snapshot =
		dispatch_assume_handle(CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0));
	if (snapshot == INVALID_HANDLE_VALUE) goto out;

	THREADENTRY32 entry;
	entry.dwSize = sizeof(entry);
	if (!Thread32First(snapshot, &entry)) goto out;

	do {
		HANDLE thread_handle = NULL;
		FILETIME thread_times[4] = {0};
		uint64_t creation_time = UINT64_MAX;

		if (entry.dwSize < offsetof(THREADENTRY32, th32OwnerProcessID) +
							   sizeof(entry.th32OwnerProcessID))
			goto next;

		if (entry.th32OwnerProcessID != current_process_id) goto next;

		thread_handle =
			OpenThread(THREAD_QUERY_INFORMATION, FALSE, entry.th32ThreadID);
		if (!thread_handle) goto next;

		if (!dispatch_assume(GetThreadTimes(thread_handle, &thread_times[0],
											&thread_times[1], &thread_times[2],
											&thread_times[3])))
			goto next;

		creation_time = _integer_from_filetime(thread_times[0]);
		if (creation_time && creation_time < main_thread_info->creation_time) {
			main_thread_info->creation_time = creation_time;
			main_thread_info->thread_id = entry.th32ThreadID;
		}

	next:
		dispatch_close_handle(thread_handle);
		entry.dwSize = sizeof(entry);
	} while (Thread32Next(snapshot, &entry));

out:
	dispatch_close_handle(snapshot);
}

int pthread_main_np(void)
{
	static dispatch_once_t once_flag;
	static struct _main_thread_info_s thread_info;
	dispatch_once_f(&once_flag, &thread_info, _find_main_thread_id);

	return thread_info.thread_id == GetCurrentThreadId();
}

static struct _main_thread_info_s {
	HANDLE handle;
	DWORD id;
} _main_thread_info;

void _dispatch_set_main_thread()
{
	if (_main_thread_info.handle) {
		if (GetCurrentThreadId() == _main_thread_info.id) return;
		DISPATCH_CRASH(
			"_dispatch_set_main_thread called multiple times on different "
			"threads");
	}

	DWORD id = GetCurrentThreadId();
	_main_thread_info.handle = OpenThread(THREAD_QUERY_INFORMATION, FALSE, id);
	_main_thread_info.id = id;
}

bool _dispatch_is_main_thread()
{
	return _main_thread_info.handle &&
		   _main_thread_info.id == GetCurrentThreadId();
}


// ----------------------------------------------------------------------------

#elif __linux__

#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>

static inline int pthread_main_np(void)
{
	return syscall(SYS_gettid) == getpid();
}

#else
#error "No supported way to determine if the current thread is the main thread."
#endif

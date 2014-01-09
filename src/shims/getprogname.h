/*
 * Copyright (c) 2009-2010 Mark Heily <mark@heily.com>,
 *				 2014 Nick Hutchinson <nshutchinson@gmail.com>
 * All rights reserved.
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

#ifndef __DISPATCH_SHIMS_GETPROGNAME__
#define __DISPATCH_SHIMS_GETPROGNAME__

#if !HAVE_GETPROGNAME

#if HAVE_DECL_PROGRAM_INVOCATION_SHORT_NAME
#include <errno.h>

static inline char *
getprogname(void)
{
	return program_invocation_short_name;
}

#elif TARGET_OS_WIN32
#include <stdlib.h>

static inline char *
getprogname(void)
{
	char *app_path;
	if (dispatch_assume_zero(_get_pgmptr(&app_path))) {
		return NULL;
	}

	char *last_slash = NULL;
	for (char *p = app_path; *p; ++p) {
		switch (*p) {
		case '/':
		case '\\':
			last_slash = p;
		}
	}

	return last_slash ? last_slash + 1 : app_path;
}
#else
#error getprogname(3) is not available on this platform
#endif

#endif /* HAVE_GETPROGNAME */

#endif /* __DISPATCH_SHIMS_GETPROGNAME__ */

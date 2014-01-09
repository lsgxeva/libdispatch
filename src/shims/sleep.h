/*
 * Copyright (c) 2012 Nick Hutchinson <nshutchinson@gmail.com>.
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

#ifndef DISPATCH_SHIMS_SLEEP_H_
#define DISPATCH_SHIMS_SLEEP_H_

#if TARGET_OS_WIN32
#include <windows.h>

static inline unsigned int
sleep(unsigned int seconds)
{
	Sleep(seconds * 1000);
	return 0;
}

#endif

#endif	// DISPATCH_SHIMS_SLEEP_H_

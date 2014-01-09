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

#ifndef __DISPATCH_SHIMS_PTHREAD_MAIN_NP__
#define __DISPATCH_SHIMS_PTHREAD_MAIN_NP__

#if !HAVE_PTHREAD_MAIN_NP

bool _dispatch_is_main_thread();
bool _dispatch_set_main_thread();

#endif

#endif /* __DISPATCH_SHIMS_PTHREAD_MAIN_NP__ */

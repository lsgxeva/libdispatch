/*
 * Copyright (c) 2009 Apple Inc. All rights reserved.
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

#ifndef __DISPATCH_SHIMS_MALLOC_ZONE__
#define __DISPATCH_SHIMS_MALLOC_ZONE__

#include <sys/types.h>

#include <stdlib.h>

#if TARGET_OS_WIN32
typedef HANDLE malloc_zone_t;

static inline malloc_zone_t *
malloc_create_zone(size_t start_size, unsigned flags)
{
	return HeapCreate(0, start_size, 0);
}

static inline void
malloc_destroy_zone(malloc_zone_t *zone)
{
	HeapDestroy(zone);
}

static inline void *
malloc_zone_malloc(malloc_zone_t *zone, size_t size)
{
	return HeapAlloc(zone, 0, size);
}

static inline void *
malloc_zone_calloc(malloc_zone_t *zone, size_t num_items, size_t size)
{
	if (num_items && SIZE_MAX / num_items < size) return NULL;
	return HeapAlloc(zone, HEAP_ZERO_MEMORY, num_items*size);
}

static inline void *
malloc_zone_realloc(malloc_zone_t *zone, void *ptr, size_t size)
{
	if (!ptr) return malloc_zone_malloc(zone, size);
	return return HeapReAlloc(zone, 0, ptr, size);
}

static inline void
malloc_zone_free(malloc_zone_t *zone, void *ptr)
{
	if (!ptr) return;
	HeapFree(zone, 0, ptr);
}

static inline void
malloc_set_zone_name(malloc_zone_t *zone, const char *name)
{
	/* No-op. */
}

#elif !HAVE_MALLOC_CREATE_ZONE
/*
 * Implement malloc zones as a simple wrapper around malloc(3) on systems
 * that don't support them.
 */
typedef void * malloc_zone_t;

static inline malloc_zone_t *
malloc_create_zone(size_t start_size, unsigned flags)
{

	return ((malloc_zone_t *)(-1));
}

static inline void
malloc_destroy_zone(malloc_zone_t *zone)
{

}

static inline void *
malloc_zone_malloc(malloc_zone_t *zone, size_t size)
{

	return (malloc(size));
}

static inline void *
malloc_zone_calloc(malloc_zone_t *zone, size_t num_items, size_t size)
{

	return (calloc(num_items, size));
}

static inline void *
malloc_zone_realloc(malloc_zone_t *zone, void *ptr, size_t size)
{

	return (realloc(ptr, size));
}

static inline void
malloc_zone_free(malloc_zone_t *zone, void *ptr)
{

	free(ptr);
}

static inline void
malloc_set_zone_name(malloc_zone_t *zone, const char *name)
{
	/* No-op. */
}
#endif

#endif /* __DISPATCH_SHIMS_MALLOC_ZONE__ */

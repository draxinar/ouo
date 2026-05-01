#ifndef VG_POOL_H_
#define VG_POOL_H_

/*
 * Valgrind memory pool annotations for custom allocators.
 *
 * Build with -DVALGRIND to enable. Tells valgrind about pool alloc/free
 * so it can track reachability per-item instead of per-block, converting
 * "possibly lost" to proper tracking.
 */

#ifdef VALGRIND
#include <valgrind/memcheck.h>
#define VG_CREATE_POOL(pool)       VALGRIND_CREATE_MEMPOOL(pool, 0, 0)
#define VG_POOL_ALLOC(pool, p, sz) VALGRIND_MEMPOOL_ALLOC(pool, p, sz)
#define VG_POOL_FREE(pool, p)      VALGRIND_MEMPOOL_FREE(pool, p)
#define VG_DESTROY_POOL(pool)      VALGRIND_DESTROY_MEMPOOL(pool)
#define VG_MAKE_DEFINED(p, sz)     VALGRIND_MAKE_MEM_DEFINED(p, sz)
#else
#define VG_CREATE_POOL(pool)       ((void)0)
#define VG_POOL_ALLOC(pool, p, sz) ((void)0)
#define VG_POOL_FREE(pool, p)      ((void)0)
#define VG_DESTROY_POOL(pool)      ((void)0)
#define VG_MAKE_DEFINED(p, sz)     ((void)0)
#endif

#endif /* VG_POOL_H_ */

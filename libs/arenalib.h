/*
 * Copyright (c) 2026 GeraPro2_0
 *
 * Licence: MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef ARENALIB_H
#define ARENALIB_H

#if (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L) || defined(__cplusplus) || defined(_MSC_VER)
#include <stddef.h>
#include <stdint.h>
typedef uintptr_t arenalib_uintptr_t;
typedef size_t arenalib_size_t;
#else
#if !defined(_STDINT_H) && !defined(_STDINT_H_) && !defined(_GCC_STDINT_H) && !defined(__stdint_h__)
#define _STDINT_H
#define _STDINT_H_
#define _GCC_STDINT_H
#define __stdint_h__

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
#if defined(__GNUC__) || defined(__clang__)
__extension__ typedef unsigned long long uint64_t;
#else
typedef unsigned __int64 uint64_t;
#endif
#endif

#if defined(_M_I86) || defined(__MSDOS__)
typedef unsigned long arenalib_uintptr_t;
#elif defined(__alpha__) || defined(__ia64__) || defined(__x86_64__) || defined(_M_X64)
#if defined(__GNUC__) || defined(__clang__)
__extension__ typedef unsigned long long arenalib_uintptr_t;
#else
typedef unsigned long long arenalib_uintptr_t;
#endif
#else
typedef unsigned long arenalib_uintptr_t;
#endif

#ifndef _SIZE_T_DEFINED
typedef unsigned int arenalib_size_t;
#else
typedef size_t arenalib_size_t;
#endif
#endif

#if defined(_MSC_VER)
#include <intrin.h>
#endif

#ifndef NULL
#ifdef __cplusplus
#define NULL 0
#else
#define NULL ((void *)0)
#endif
#endif

#ifdef __cplusplus
extern "C"
{
#endif

/* Compiler compatibility macros */
#if defined(_MSC_VER)
#define ARENALIB_INLINE static __inline
#define ARENALIB_FORCE_INLINE static __forceinline
#define ARENALIB_ASSUME(expr) __assume(expr)
#elif defined(__GNUC__) || defined(__clang__)
#if defined(__cplusplus) || (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L)
#define ARENALIB_INLINE static inline __attribute__((always_inline))
#define ARENALIB_FORCE_INLINE static inline __attribute__((always_inline))
#else
#define ARENALIB_INLINE static
#define ARENALIB_FORCE_INLINE static
#endif
#define ARENALIB_ASSUME(expr)        \
    do                               \
    {                                \
        if (!(expr))                 \
            __builtin_unreachable(); \
    } while (0)
#else
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define ARENALIB_INLINE static inline
#else
#define ARENALIB_INLINE static
#endif
#define ARENALIB_FORCE_INLINE ARENALIB_INLINE
#define ARENALIB_ASSUME(expr) ((void)0)
#endif

#if defined(ARENALIB_LOCK_TYPE) && defined(ARENALIB_LOCK_INITIALIZER) && \
    defined(ARENALIB_LOCK_INIT) && defined(ARENALIB_LOCK_ACQUIRE) && defined(ARENALIB_LOCK_RELEASE)
typedef ARENALIB_LOCK_TYPE arenalib_lock_t;
#define ARENALIB_HAS_THREAD_LOCKS 1
#elif defined(__GNUC__) || defined(__clang__)
typedef volatile int arenalib_lock_t;
#define ARENALIB_LOCK_INITIALIZER 0
#define ARENALIB_LOCK_INIT(lock) (*(lock) = 0)
#define ARENALIB_LOCK_ACQUIRE(lock)                  \
    do                                                \
    {                                                 \
        while (__atomic_exchange_n((lock), 1, __ATOMIC_ACQUIRE) != 0) \
        {                                             \
        }                                             \
    } while (0)
#define ARENALIB_LOCK_RELEASE(lock) __atomic_store_n((lock), 0, __ATOMIC_RELEASE)
#define ARENALIB_HAS_THREAD_LOCKS 1
#elif defined(_MSC_VER)
typedef volatile long arenalib_lock_t;
#define ARENALIB_LOCK_INITIALIZER 0
#define ARENALIB_LOCK_INIT(lock) (*(lock) = 0)
#define ARENALIB_LOCK_ACQUIRE(lock)                                      \
    do                                                                    \
    {                                                                     \
        while (_InterlockedExchange((volatile long *)(lock), 1) != 0)     \
        {                                                                 \
        }                                                                 \
    } while (0)
#define ARENALIB_LOCK_RELEASE(lock) _InterlockedExchange((volatile long *)(lock), 0)
#define ARENALIB_HAS_THREAD_LOCKS 1
#else
typedef int arenalib_lock_t;
#define ARENALIB_LOCK_INITIALIZER 0
#define ARENALIB_LOCK_INIT(lock) (*(lock) = 0)
#define ARENALIB_LOCK_ACQUIRE(lock) ((void)(lock))
#define ARENALIB_LOCK_RELEASE(lock) ((void)(lock))
#define ARENALIB_HAS_THREAD_LOCKS 0
#endif

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define ARENALIB_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#else
#define ARENALIB_CONCAT2(a, b) a##b
#define ARENALIB_CONCAT(a, b) ARENALIB_CONCAT2(a, b)
#define ARENALIB_STATIC_ASSERT(cond, msg) typedef char ARENALIB_CONCAT(arenalib_static_assert_, __LINE__)[(cond) ? 1 : -1]
#endif

/* Default alignment used by arena allocations. */
#ifndef ARENALIB_DEFAULT_ALIGNMENT
#define ARENALIB_DEFAULT_ALIGNMENT (sizeof(void *))
#endif

    typedef void (*arenalib_oom_callback_t)(void *user_data, arenalib_size_t size_requested);

    typedef struct arenalib_oom_event_t
    {
        arenalib_oom_callback_t callback;
        void *user_data;
        arenalib_size_t size_requested;
    } arenalib_oom_event_t;

    /* Structure for secure IDs (32-bit index, 32-bit generation) */
    typedef struct arenalib_id_t
    {
        uint32_t index;
        uint32_t generation;
    } arenalib_id_t;

    /* Portable invalid ID provider: prefer function over compound-literal macro
     * to avoid C++ pedantic warnings about compound literals. */
    ARENALIB_INLINE arenalib_id_t arenalib_invalid_id(void)
    {
        arenalib_id_t id;
        id.index = 0xFFFFFFFFu;
        id.generation = 0xFFFFFFFFu;
        return id;
    }

/* Function form is valid in C89, C99, C11, and C++. */
#define ARENALIB_INVALID_ID arenalib_invalid_id()

/* Static Pool Configuration */
#ifndef ARENALIB_POOL_BLOCKS
#define ARENALIB_POOL_BLOCKS 64
#endif
#ifndef ARENALIB_POOL_BLOCK_SIZE
#define ARENALIB_POOL_BLOCK_SIZE 1048576
#endif

    typedef struct arenalib_arena_t
    {
        arenalib_size_t capacity;
        arenalib_size_t used;
        arenalib_size_t last_size;
        unsigned char *data;
        void *storage;
        arenalib_oom_callback_t oom_callback;
        void *oom_user_data;

        uint32_t *generations;
        uint32_t *offsets;
        uint32_t *free_next;
        uint32_t *active_prev;
        uint32_t max_items;
        uint32_t item_count;
        uint32_t free_head;
        uint32_t active_head;
        uint32_t active_tail;

        struct arenalib_arena_t *next;
        struct arenalib_arena_t *tail;
        uint32_t pool_head;
        uint32_t pool_tail;
        uint32_t pool_count;
        arenalib_lock_t lock;
        arenalib_size_t block_alloc_size;
        int is_dynamic;
        int pool_index;
    } arenalib_arena_t;

    ARENALIB_INLINE void arenalib_oom_event_record(arenalib_arena_t *arena, arenalib_oom_event_t *event, arenalib_size_t size)
    {
        if (arena->oom_callback != NULL)
        {
            event->callback = arena->oom_callback;
            event->user_data = arena->oom_user_data;
            event->size_requested = size;
        }
    }

    ARENALIB_INLINE void arenalib_oom_event_dispatch(arenalib_oom_event_t *event)
    {
        if (event->callback != NULL)
        {
            event->callback(event->user_data, event->size_requested);
        }
    }

    typedef union
    {
        arenalib_arena_t dummy_alignment;
        unsigned char storage[ARENALIB_POOL_BLOCK_SIZE];
    } arenalib_pool_block_t;

#if defined(ARENALIB_IMPLEMENTATION)
#define ARENALIB_POOL_STORAGE
#define ARENALIB_POOL_INITIALIZER(value) = value
#elif defined(ARENALIB_USE_EXTERNAL_POOL)
#define ARENALIB_POOL_STORAGE extern
#define ARENALIB_POOL_INITIALIZER(value)
#else
#define ARENALIB_POOL_STORAGE static
#define ARENALIB_POOL_INITIALIZER(value) = value
#endif

    ARENALIB_POOL_STORAGE arenalib_pool_block_t g_arena_pool[ARENALIB_POOL_BLOCKS];
    ARENALIB_POOL_STORAGE uint32_t g_pool_next[ARENALIB_POOL_BLOCKS];
    ARENALIB_POOL_STORAGE uint32_t g_pool_free_head ARENALIB_POOL_INITIALIZER(0xFFFFFFFFu);
    ARENALIB_POOL_STORAGE uint32_t g_pool_free_count ARENALIB_POOL_INITIALIZER(0);
    ARENALIB_POOL_STORAGE int g_pool_initialized ARENALIB_POOL_INITIALIZER(0);
    ARENALIB_POOL_STORAGE arenalib_lock_t g_pool_lock ARENALIB_POOL_INITIALIZER(ARENALIB_LOCK_INITIALIZER);

    ARENALIB_STATIC_ASSERT(sizeof(void *) == ARENALIB_DEFAULT_ALIGNMENT, "Arenalib assumes pointer-sized alignment by default");
    ARENALIB_STATIC_ASSERT(ARENALIB_POOL_BLOCKS <= 64, "Arenalib pool supports at most 64 blocks");

    /* Safely align up with overflow detection */
    ARENALIB_INLINE arenalib_size_t arenalib_align_up(arenalib_size_t value, arenalib_size_t alignment)
    {
        arenalib_size_t aligned;
        if (alignment == 0)
        {
            return value;
        }
        /* Detect overflow: if value + (alignment - 1) wraps around */
        if (value > (arenalib_size_t)-1 - (alignment - 1))
        {
            return 0; /* Overflow detected, return 0 as sentinel */
        }
        aligned = ((value + alignment - 1) / alignment) * alignment;
        /* Verify result didn't overflow or wrap */
        if (aligned < value)
        {
            return 0; /* Multiplication overflow */
        }
        return aligned;
    }

    ARENALIB_INLINE void *arenalib_align_ptr(void *ptr, arenalib_size_t alignment)
    {
        arenalib_uintptr_t value = (arenalib_uintptr_t)ptr;
        value = arenalib_align_up(value, alignment);
        return (void *)value;
    }

    ARENALIB_INLINE void arenalib_memcpy(void *dest, const void *src, arenalib_size_t count)
    {
        unsigned char *d = (unsigned char *)dest;
        const unsigned char *s = (const unsigned char *)src;
        while (count--)
        {
            *d++ = *s++;
        }
    }

    ARENALIB_INLINE void arenalib_memset(void *dest, int byte_value, arenalib_size_t count)
    {
        unsigned char *d = (unsigned char *)dest;
        unsigned char value = (unsigned char)byte_value;
        while (count--)
        {
            *d++ = value;
        }
    }

    ARENALIB_INLINE int arenalib_size_mul(arenalib_size_t left, arenalib_size_t right, arenalib_size_t *result)
    {
        if (!result || (right != 0 && left > (arenalib_size_t)-1 / right))
        {
            return 0;
        }
        *result = left * right;
        return 1;
    }

    /*
     * Initialize an arena from a backing buffer.
     *
     * storage: pointer to the buffer that will back the arena.
     * capacity: size of the buffer in bytes.
     * max_ids: maximum number of IDs this arena can keep track of simultaneously. Pass 0 if ID features aren't needed.
     *
     * The caller owns the backing storage; destroy does not free it.
     */
    ARENALIB_INLINE int arenalib_arena_init(arenalib_arena_t *arena, void *storage, arenalib_size_t capacity, uint32_t max_ids)
    {
        void *aligned_data;
        arenalib_uintptr_t adjustment;
        arenalib_size_t per_item;
        arenalib_size_t meta_size;
        arenalib_size_t aligned_meta;
        arenalib_uintptr_t offsets_addr;
        uint32_t i;
        if (!arena || !storage || capacity == 0)
        {
            return 0;
        }

        aligned_data = arenalib_align_ptr(storage, ARENALIB_DEFAULT_ALIGNMENT);
        adjustment = (arenalib_uintptr_t)aligned_data - (arenalib_uintptr_t)storage;
        /* Defensive: ensure adjustment doesn't exceed capacity and capacity is reasonable */
        if (adjustment > capacity)
        {
            return 0;
        }

        arena->capacity = capacity - adjustment;
        arena->used = 0;
        arena->last_size = 0;
        arena->data = (unsigned char *)aligned_data;
        arena->storage = storage;
        arena->oom_callback = NULL;
        arena->oom_user_data = NULL;
        arena->tail = arena;
        ARENALIB_LOCK_INIT(&arena->lock);

        arena->max_items = max_ids;
        arena->item_count = 0;

        if (max_ids > 0)
        {
            /* Detect multiplication overflow in metadata size calculation */
            per_item = sizeof(uint32_t) * 4;
            if (max_ids > (arenalib_size_t)-1 / per_item)
            {
                return 0; /* Overflow in meta_size calculation */
            }
            meta_size = (arenalib_size_t)max_ids * per_item;
            /* Ensure that the metadata at the end of the arena maintains the correct alignment */
            aligned_meta = arenalib_align_up(meta_size, ARENALIB_DEFAULT_ALIGNMENT);
            if (aligned_meta == 0)
            {
                return 0; /* Overflow in alignment calculation */
            }
            meta_size = aligned_meta;

            if (arena->capacity <= meta_size)
            {
                return 0;
            }

            arena->generations = (uint32_t *)(arena->data + arena->capacity - meta_size);
            /* Validate generations pointer is within allocated memory */
            if (arena->generations < (uint32_t *)arena->data)
            {
                return 0; /* Pointer calculation failed */
            }
            offsets_addr = (arenalib_uintptr_t)(arena->generations + arena->max_items);
            arena->offsets = (uint32_t *)arenalib_align_ptr((void *)offsets_addr, sizeof(uint32_t));
            arena->free_next = arena->offsets + arena->max_items;
            arena->active_prev = arena->free_next + arena->max_items;
            /* Validate metadata pointers are within the metadata region */
            if (arena->offsets < (uint32_t *)arena->generations ||
                arena->free_next < arena->offsets ||
                arena->active_prev < arena->free_next ||
                arena->active_prev + arena->max_items > (uint32_t *)(arena->data + arena->capacity))
            {
                return 0; /* Metadata pointers out of bounds */
            }

            arenalib_memset(arena->generations, 0, arena->max_items * sizeof(uint32_t));
            arenalib_memset(arena->offsets, 0xFF, arena->max_items * sizeof(uint32_t));
            arenalib_memset(arena->active_prev, 0xFF, arena->max_items * sizeof(uint32_t));

            {
                for (i = 0; i < arena->max_items - 1; i++)
                {
                    arena->free_next[i] = i + 1;
                }
                arena->free_next[arena->max_items - 1] = 0xFFFFFFFF;
            }
            arena->free_head = 0;
            arena->active_head = 0xFFFFFFFF;
            arena->active_tail = 0xFFFFFFFF;

            arena->capacity -= meta_size;
        }
        else
        {
            arena->generations = NULL;
            arena->offsets = NULL;
            arena->free_next = NULL;
            arena->active_prev = NULL;
            arena->free_head = 0xFFFFFFFF;
            arena->active_head = 0xFFFFFFFF;
            arena->active_tail = 0xFFFFFFFF;
        }

        arena->next = NULL;
        arena->tail = arena;
        arena->pool_head = 0xFFFFFFFF;
        arena->pool_tail = 0xFFFFFFFF;
        arena->pool_count = 0;
        arena->block_alloc_size = capacity;
        arena->is_dynamic = 0;
        arena->pool_index = -1;

        return 1;
    }

    ARENALIB_INLINE void arenalib_pool_release_block(uint32_t block_index)
    {
        if (block_index >= ARENALIB_POOL_BLOCKS)
        {
            return;
        }

        ARENALIB_LOCK_ACQUIRE(&g_pool_lock);
        if (g_pool_free_count < ARENALIB_POOL_BLOCKS)
        {
            g_pool_next[block_index] = g_pool_free_head;
            g_pool_free_head = block_index;
            g_pool_free_count++;
        }
        ARENALIB_LOCK_RELEASE(&g_pool_lock);
    }

    ARENALIB_INLINE void arenalib_pool_release_arena_blocks(arenalib_arena_t *arena)
    {
        if (arena->pool_head == 0xFFFFFFFF)
        {
            return;
        }

        ARENALIB_LOCK_ACQUIRE(&g_pool_lock);
        g_pool_next[arena->pool_tail] = g_pool_free_head;
        g_pool_free_head = arena->pool_head;
        g_pool_free_count += arena->pool_count;
        ARENALIB_LOCK_RELEASE(&g_pool_lock);

        arena->pool_head = 0xFFFFFFFF;
        arena->pool_tail = 0xFFFFFFFF;
        arena->pool_count = 0;
    }

    ARENALIB_INLINE int arenalib_pool_acquire_block(arenalib_arena_t *owner, uint32_t max_ids)
    {
        uint32_t i;
        int free_bit;
        int init_result;
        arenalib_arena_t *allocated_arena;

        ARENALIB_LOCK_ACQUIRE(&g_pool_lock);
        if (!g_pool_initialized)
        {
            for (i = 0; i < ARENALIB_POOL_BLOCKS; ++i)
            {
                g_pool_next[i] = (i + 1 < ARENALIB_POOL_BLOCKS) ? i + 1 : 0xFFFFFFFF;
            }
            g_pool_free_head = 0;
            g_pool_free_count = ARENALIB_POOL_BLOCKS;
            g_pool_initialized = 1;
        }
        if (g_pool_free_count == 0)
        {
            ARENALIB_LOCK_RELEASE(&g_pool_lock);
            return -1;
        }

        free_bit = (int)g_pool_free_head;
        g_pool_free_head = g_pool_next[free_bit];
        g_pool_next[free_bit] = 0xFFFFFFFF;
        g_pool_free_count--;
        ARENALIB_LOCK_RELEASE(&g_pool_lock);

        if (ARENALIB_POOL_BLOCK_SIZE <= sizeof(arenalib_arena_t))
        {
            arenalib_pool_release_block((uint32_t)free_bit);
            return -1;
        }

        init_result = arenalib_arena_init((arenalib_arena_t *)&g_arena_pool[free_bit].storage[0],
                                          (void *)&g_arena_pool[free_bit].storage[sizeof(arenalib_arena_t)],
                                          ARENALIB_POOL_BLOCK_SIZE - sizeof(arenalib_arena_t), max_ids);
        if (!init_result)
        {
            arenalib_pool_release_block((uint32_t)free_bit);
            return -1;
        }

        allocated_arena = (arenalib_arena_t *)&g_arena_pool[free_bit].storage[0];
        allocated_arena->pool_index = free_bit;
        if (owner->pool_tail == 0xFFFFFFFF)
        {
            owner->pool_head = (uint32_t)free_bit;
        }
        else
        {
            g_pool_next[owner->pool_tail] = (uint32_t)free_bit;
        }
        owner->pool_tail = (uint32_t)free_bit;
        owner->pool_count++;
        return free_bit;
    }

    ARENALIB_INLINE void arenalib_id_remove_active_unlocked(arenalib_arena_t *arena, uint32_t slot)
    {
        uint32_t previous = arena->active_prev[slot];
        uint32_t next = arena->free_next[slot];
        if (previous == 0xFFFFFFFF)
        {
            arena->active_head = next;
        }
        else
        {
            arena->free_next[previous] = next;
        }
        if (next == 0xFFFFFFFF)
        {
            arena->active_tail = previous;
        }
        else
        {
            arena->active_prev[next] = previous;
        }
        arena->active_prev[slot] = 0xFFFFFFFF;
    }

    ARENALIB_INLINE void arenalib_id_push_free_unlocked(arenalib_arena_t *arena, uint32_t slot)
    {
        arena->free_next[slot] = arena->free_head;
        arena->free_head = slot;
    }

    ARENALIB_INLINE void arenalib_id_activate_unlocked(arenalib_arena_t *arena, uint32_t slot)
    {
        arena->free_next[slot] = 0xFFFFFFFF;
        arena->active_prev[slot] = arena->active_tail;
        if (arena->active_tail == 0xFFFFFFFF)
        {
            arena->active_head = slot;
        }
        else
        {
            arena->free_next[arena->active_tail] = slot;
        }
        arena->active_tail = slot;
    }

    ARENALIB_INLINE void arenalib_arena_reset(arenalib_arena_t *arena)
    {
        uint32_t slot;
        if (!arena)
        {
            return;
        }

        ARENALIB_LOCK_ACQUIRE(&arena->lock);
        arenalib_pool_release_arena_blocks(arena);

        arena->used = 0;
        arena->last_size = 0;
        arena->item_count = 0;
        arena->next = NULL;
        arena->tail = arena;

        if (arena->active_head != 0xFFFFFFFF)
        {
            slot = arena->active_head;
            while (slot != 0xFFFFFFFF)
            {
                uint32_t next = arena->free_next[slot];
                arena->generations[slot]++;
                arena->offsets[slot] = 0xFFFFFFFF;
                arena->active_prev[slot] = 0xFFFFFFFF;
                slot = next;
            }
            arena->free_next[arena->active_tail] = arena->free_head;
            arena->free_head = arena->active_head;
        }
        arena->active_head = 0xFFFFFFFF;
        arena->active_tail = 0xFFFFFFFF;
        arena->item_count = 0;
        ARENALIB_LOCK_RELEASE(&arena->lock);
    }

    ARENALIB_INLINE arenalib_size_t arenalib_arena_available_unlocked(const arenalib_arena_t *arena)
    {
        if (!arena || arena->capacity < arena->used)
        {
            return 0;
        }
        return arena->capacity - arena->used;
    }

    ARENALIB_INLINE arenalib_size_t arenalib_arena_available(const arenalib_arena_t *arena)
    {
        arenalib_size_t available;
        if (!arena)
        {
            return 0;
        }
        ARENALIB_LOCK_ACQUIRE((arenalib_lock_t *)&arena->lock);
        available = arenalib_arena_available_unlocked(arena);
        ARENALIB_LOCK_RELEASE((arenalib_lock_t *)&arena->lock);
        return available;
    }

    ARENALIB_INLINE void *arenalib_arena_malloc_unlocked(arenalib_arena_t *arena, arenalib_size_t size, arenalib_oom_event_t *event)
    {
        arenalib_size_t aligned_size;
        arenalib_arena_t *current;
        int block_idx;
        unsigned char *ptr;
        if (!arena || size == 0)
        {
            return NULL;
        }

        aligned_size = arenalib_align_up(size, ARENALIB_DEFAULT_ALIGNMENT);
        if (aligned_size == 0 || aligned_size > ARENALIB_POOL_BLOCK_SIZE)
        {
            return NULL; /* Overflow or size too large */
        }

        current = arena->tail ? arena->tail : arena;

        if (aligned_size > arenalib_arena_available_unlocked(current))
        {
            if (aligned_size > (ARENALIB_POOL_BLOCK_SIZE - sizeof(arenalib_arena_t)))
            {
                if (arena->oom_callback)
                {
                    arenalib_oom_event_record(arena, event, aligned_size);
                }
                return NULL;
            }

            block_idx = arenalib_pool_acquire_block(arena, 0);
            if (block_idx == -1 || block_idx >= ARENALIB_POOL_BLOCKS)
            {
                if (arena->oom_callback)
                {
                    arenalib_oom_event_record(arena, event, aligned_size);
                }
                return NULL;
            }

            current->next = (arenalib_arena_t *)&g_arena_pool[block_idx].storage;
            current = current->next;
            arena->tail = current;
        }

        /* Defensive: check for overflow in used + aligned_size */
        if (current->used > current->capacity - aligned_size)
        {
            if (arena->oom_callback)
            {
                arenalib_oom_event_record(arena, event, aligned_size);
            }
            return NULL; /* Overflow detected */
        }

        ptr = current->data + current->used;
        current->used += aligned_size;
        current->last_size = aligned_size;
        return ptr;
    }

    ARENALIB_INLINE void *arenalib_arena_malloc(arenalib_arena_t *arena, arenalib_size_t size)
    {
        arenalib_oom_event_t event = {NULL, NULL, 0};
        void *ptr;
        if (!arena)
        {
            return NULL;
        }
        ARENALIB_LOCK_ACQUIRE(&arena->lock);
        ptr = arenalib_arena_malloc_unlocked(arena, size, &event);
        ARENALIB_LOCK_RELEASE(&arena->lock);
        arenalib_oom_event_dispatch(&event);
        return ptr;
    }

    ARENALIB_INLINE void *arenalib_arena_calloc(arenalib_arena_t *arena, arenalib_size_t count, arenalib_size_t size)
    {
        arenalib_size_t total;
        void *ptr;
        arenalib_oom_event_t event = {NULL, NULL, 0};
        if (!arena || count == 0 || size == 0)
        {
            return NULL;
        }
        /* Detect multiplication overflow */
        if (!arenalib_size_mul(count, size, &total))
        {
            return NULL;
        }
        ARENALIB_LOCK_ACQUIRE(&arena->lock);
        ptr = arenalib_arena_malloc_unlocked(arena, total, &event);
        if (ptr)
        {
            arenalib_memset(ptr, 0, total);
        }
        ARENALIB_LOCK_RELEASE(&arena->lock);
        arenalib_oom_event_dispatch(&event);
        return ptr;
    }

    ARENALIB_INLINE void arenalib_arena_free(arenalib_arena_t *arena, void *ptr, arenalib_size_t size)
    {
        arenalib_size_t aligned_size;
        arenalib_arena_t *current;
        if (!arena || !ptr || size == 0)
        {
            return;
        }

        aligned_size = arenalib_align_up(size, ARENALIB_DEFAULT_ALIGNMENT);
        if (aligned_size == 0)
            return; /* Overflow in alignment */

        ARENALIB_LOCK_ACQUIRE(&arena->lock);
        current = arena->tail ? arena->tail : arena;
        if (current->data && current->capacity > 0 &&
            current->last_size == aligned_size && current->used >= current->last_size)
        {
            unsigned char *expected = current->data + (current->used - current->last_size);
            if ((unsigned char *)ptr == expected)
            {
                current->used -= current->last_size;
                current->last_size = 0;
            }
        }
        ARENALIB_LOCK_RELEASE(&arena->lock);
    }

    ARENALIB_INLINE void *arenalib_arena_realloc(arenalib_arena_t *arena, void *ptr, arenalib_size_t old_size, arenalib_size_t new_size)
    {
        arenalib_size_t aligned_old;
        arenalib_size_t aligned_new;
        arenalib_size_t reduction;
        arenalib_size_t growth;
        arenalib_size_t copy_size;
        arenalib_arena_t *current;
        unsigned char *current_last;
        void *new_ptr;
        arenalib_oom_event_t event = {NULL, NULL, 0};
        if (!arena)
            return NULL;
        if (ptr == NULL)
            return arenalib_arena_malloc(arena, new_size);
        if (new_size == old_size)
            return ptr;

        aligned_old = arenalib_align_up(old_size, ARENALIB_DEFAULT_ALIGNMENT);
        aligned_new = arenalib_align_up(new_size, ARENALIB_DEFAULT_ALIGNMENT);

        /* Detect overflow in alignment calculations */
        if (aligned_old == 0 || aligned_new == 0)
        {
            return NULL;
        }

        ARENALIB_LOCK_ACQUIRE(&arena->lock);
        current = arena->tail ? arena->tail : arena;
        if (current->data && current->capacity > 0 && current->used >= current->last_size)
        {
            current_last = current->data + (current->used - current->last_size);

            if ((unsigned char *)ptr == current_last && aligned_old == current->last_size)
            {
                if (aligned_new <= aligned_old)
                {
                    /* Shrinking: safe subtraction after bounds check */
                    reduction = aligned_old - aligned_new;
                    if (current->used >= reduction)
                    {
                        current->used -= reduction;
                        current->last_size = aligned_new;
                        ARENALIB_LOCK_RELEASE(&arena->lock);
                        return ptr;
                    }
                }
                if (aligned_new > aligned_old)
                {
                    /* Expanding: check for overflow before adding */
                    growth = aligned_new - aligned_old;
                    if (growth <= arenalib_arena_available_unlocked(current))
                    {
                        current->used += growth;
                        current->last_size = aligned_new;
                        ARENALIB_LOCK_RELEASE(&arena->lock);
                        return ptr;
                    }
                }
            }
        }

        new_ptr = arenalib_arena_malloc_unlocked(arena, new_size, &event);
        if (new_ptr)
        {
            copy_size = old_size < new_size ? old_size : new_size;
            arenalib_memcpy(new_ptr, ptr, copy_size);
        }
        ARENALIB_LOCK_RELEASE(&arena->lock);
        arenalib_oom_event_dispatch(&event);
        return new_ptr;
    }

    ARENALIB_INLINE void arenalib_arena_destroy(arenalib_arena_t *arena)
    {
        if (!arena)
            return;

        ARENALIB_LOCK_ACQUIRE(&arena->lock);
        arenalib_pool_release_arena_blocks(arena);

        arena->capacity = 0;
        arena->used = 0;
        arena->last_size = 0;
        arena->data = NULL;
        arena->storage = NULL;
        arena->oom_callback = NULL;
        arena->oom_user_data = NULL;
        arena->generations = NULL;
        arena->offsets = NULL;
        arena->free_next = NULL;
        arena->active_prev = NULL;
        arena->max_items = 0;
        arena->item_count = 0;
        arena->free_head = 0xFFFFFFFF;
        arena->active_head = 0xFFFFFFFF;
        arena->active_tail = 0xFFFFFFFF;
        arena->next = NULL;
        arena->tail = NULL;
        arena->pool_index = -1;
        ARENALIB_LOCK_RELEASE(&arena->lock);
    }

    ARENALIB_INLINE void arenalib_arena_set_oom_callback(arenalib_arena_t *arena, arenalib_oom_callback_t callback, void *user_data)
    {
        if (arena)
        {
            ARENALIB_LOCK_ACQUIRE(&arena->lock);
            arena->oom_callback = callback;
            arena->oom_user_data = user_data;
            ARENALIB_LOCK_RELEASE(&arena->lock);
        }
    }

    ARENALIB_INLINE void *arenalib_arena_malloc_align_unlocked(arenalib_arena_t *arena, arenalib_size_t size, arenalib_size_t alignment, arenalib_oom_event_t *event)
    {
        arenalib_size_t aligned_size;
        arenalib_arena_t *current;
        unsigned char *current_ptr;
        void *aligned_ptr;
        arenalib_size_t adjustment;
        arenalib_size_t total_needed;
        int block_idx;
        if (!arena || size == 0 || alignment == 0)
            return NULL;

        aligned_size = arenalib_align_up(size, ARENALIB_DEFAULT_ALIGNMENT);
        if (aligned_size == 0)
            return NULL;

        current = arena->tail ? arena->tail : arena;

        if (!current->data || current->used > current->capacity)
            return NULL;

        current_ptr = current->data + current->used;
        aligned_ptr = arenalib_align_ptr(current_ptr, alignment);
        if (!aligned_ptr)
            return NULL;

        adjustment = (arenalib_size_t)((unsigned char *)aligned_ptr - current_ptr);
        if (adjustment > (arenalib_size_t)-1 - aligned_size)
            return NULL;
        total_needed = adjustment + aligned_size;

        if (total_needed > arenalib_arena_available_unlocked(current))
        {
            if (total_needed > (ARENALIB_POOL_BLOCK_SIZE - sizeof(arenalib_arena_t)))
            {
                if (arena->oom_callback)
                {
                    arenalib_oom_event_record(arena, event, total_needed);
                }
                return NULL;
            }

            block_idx = arenalib_pool_acquire_block(arena, 0);
            if (block_idx < 0 || block_idx >= ARENALIB_POOL_BLOCKS)
            {
                if (arena->oom_callback)
                {
                    arenalib_oom_event_record(arena, event, total_needed);
                }
                return NULL;
            }

            current->next = (arenalib_arena_t *)&g_arena_pool[block_idx].storage;
            current = current->next;
            arena->tail = current;
            current_ptr = current->data + current->used;
            aligned_ptr = arenalib_align_ptr(current_ptr, alignment);
            if (!aligned_ptr)
                return NULL;
            adjustment = (arenalib_size_t)((unsigned char *)aligned_ptr - current_ptr);
            if (adjustment > (arenalib_size_t)-1 - aligned_size)
                return NULL;
            total_needed = adjustment + aligned_size;
        }

        if (total_needed > arenalib_arena_available_unlocked(current))
        {
            if (arena->oom_callback)
            {
                arenalib_oom_event_record(arena, event, total_needed);
            }
            return NULL;
        }

        current->used += total_needed;
        current->last_size = total_needed;
        return aligned_ptr;
    }

    ARENALIB_INLINE void *arenalib_arena_malloc_align(arenalib_arena_t *arena, arenalib_size_t size, arenalib_size_t alignment)
    {
        arenalib_oom_event_t event = {NULL, NULL, 0};
        void *ptr;
        if (!arena)
        {
            return NULL;
        }
        ARENALIB_LOCK_ACQUIRE(&arena->lock);
        ptr = arenalib_arena_malloc_align_unlocked(arena, size, alignment, &event);
        ARENALIB_LOCK_RELEASE(&arena->lock);
        arenalib_oom_event_dispatch(&event);
        return ptr;
    }

    typedef struct arenalib_marker_t
    {
        arenalib_arena_t *block;
        arenalib_size_t used;
    } arenalib_marker_t;

    ARENALIB_INLINE arenalib_marker_t arenalib_arena_get_marker_unlocked(const arenalib_arena_t *arena)
    {
        arenalib_marker_t marker;
        marker.block = NULL;
        marker.used = 0;
        if (arena)
        {
            const arenalib_arena_t *current = arena->tail ? arena->tail : arena;
            marker.block = (arenalib_arena_t *)current;
            marker.used = current->used;
        }
        return marker;
    }

    ARENALIB_INLINE arenalib_marker_t arenalib_arena_get_marker(const arenalib_arena_t *arena)
    {
        arenalib_marker_t marker;
        if (!arena)
        {
            marker.block = NULL;
            marker.used = 0;
            return marker;
        }
        ARENALIB_LOCK_ACQUIRE((arenalib_lock_t *)&arena->lock);
        marker = arenalib_arena_get_marker_unlocked(arena);
        ARENALIB_LOCK_RELEASE((arenalib_lock_t *)&arena->lock);
        return marker;
    }

    ARENALIB_INLINE void arenalib_arena_release_marker(arenalib_arena_t *arena, arenalib_marker_t marker)
    {
        if (!arena || !marker.block)
        {
            return;
        }

        ARENALIB_LOCK_ACQUIRE(&arena->lock);
        if (marker.block == arena->tail && marker.used <= marker.block->used)
        {
            arenalib_arena_t *marked_block = marker.block;
            marked_block->used = marker.used;
            marked_block->last_size = 0;

            if (marked_block == arena && arena->offsets && arena->max_items > 0)
            {
                while (arena->active_tail != 0xFFFFFFFF &&
                       arena->offsets[arena->active_tail] >= marker.used)
                {
                    uint32_t slot = arena->active_tail;
                    arenalib_id_remove_active_unlocked(arena, slot);
                    arena->generations[slot]++;
                    arena->offsets[slot] = 0xFFFFFFFF;
                    arenalib_id_push_free_unlocked(arena, slot);
                    arena->item_count--;
                }
            }
        }
        ARENALIB_LOCK_RELEASE(&arena->lock);
    }

    /* Allocates memory and returns a unique ID instead of a raw pointer */
    ARENALIB_INLINE arenalib_id_t arenalib_arena_alloc_id_unlocked(arenalib_arena_t *arena, arenalib_size_t size, arenalib_oom_event_t *event)
    {
        arenalib_size_t base_available;
        void *ptr;
        uint32_t slot;
        arenalib_id_t id;
        if (!arena || !arena->offsets || !arena->free_next ||
            arena->free_head == 0xFFFFFFFF || arena->free_head >= arena->max_items ||
            arena->next != NULL)
        {
            return arenalib_invalid_id();
        }

        /* ID offsets are relative to the first arena block. */
        base_available = arenalib_arena_available_unlocked(arena);
        if (size == 0 || base_available < (ARENALIB_DEFAULT_ALIGNMENT - 1) ||
            size > base_available - (ARENALIB_DEFAULT_ALIGNMENT - 1))
        {
            return arenalib_invalid_id();
        }

        ptr = arenalib_arena_malloc_align_unlocked(arena, size, ARENALIB_DEFAULT_ALIGNMENT, event);
        if (!ptr)
            return arenalib_invalid_id();

        slot = arena->free_head;
        arena->free_head = arena->free_next[slot];
        arenalib_id_activate_unlocked(arena, slot);
        arena->offsets[slot] = (uint32_t)((unsigned char *)ptr - arena->data);
        arena->item_count++;

        id.index = slot;
        id.generation = arena->generations[slot];

        return id;
    }

    ARENALIB_INLINE arenalib_id_t arenalib_arena_alloc_id(arenalib_arena_t *arena, arenalib_size_t size)
    {
        arenalib_oom_event_t event = {NULL, NULL, 0};
        arenalib_id_t id;
        if (!arena)
        {
            return arenalib_invalid_id();
        }
        ARENALIB_LOCK_ACQUIRE(&arena->lock);
        id = arenalib_arena_alloc_id_unlocked(arena, size, &event);
        ARENALIB_LOCK_RELEASE(&arena->lock);
        arenalib_oom_event_dispatch(&event);
        return id;
    }

    /* Translates a secure ID to an actual memory pointer */
    ARENALIB_INLINE void *arenalib_arena_get_ptr_unlocked(arenalib_arena_t *arena, arenalib_id_t id)
    {
        uint32_t offset;
        if (!arena || !arena->offsets || !arena->generations || id.index >= arena->max_items)
            return NULL;

        /* Defensive: verify the offset doesn't exceed capacity */
        offset = arena->offsets[id.index];
        if (offset == 0xFFFFFFFF || offset >= arena->capacity)
        {
            return NULL; /* Invalid offset or ID is free */
        }

        if (arena->generations[id.index] != id.generation)
            return NULL;

        return (void *)(arena->data + offset);
    }

    ARENALIB_INLINE void *arenalib_arena_get_ptr(arenalib_arena_t *arena, arenalib_id_t id)
    {
        void *ptr;
        if (!arena)
        {
            return NULL;
        }
        ARENALIB_LOCK_ACQUIRE(&arena->lock);
        ptr = arenalib_arena_get_ptr_unlocked(arena, id);
        ARENALIB_LOCK_RELEASE(&arena->lock);
        return ptr;
    }

    /* Invalidates an ID by incrementing its generation and recycles its slot */
    ARENALIB_INLINE void arenalib_arena_free_id_unlocked(arenalib_arena_t *arena, arenalib_id_t id)
    {
        if (arena && arena->generations && arena->offsets && id.index < arena->max_items)
        {
            if (arena->generations[id.index] == id.generation &&
                arena->offsets[id.index] != 0xFFFFFFFF)
            {
                arenalib_id_remove_active_unlocked(arena, id.index);
                arena->generations[id.index]++;

                arena->offsets[id.index] = 0xFFFFFFFF;
                arenalib_id_push_free_unlocked(arena, id.index);

                if (arena->item_count > 0)
                {
                    arena->item_count--;
                }
            }
        }
    }

    ARENALIB_INLINE void arenalib_arena_free_id(arenalib_arena_t *arena, arenalib_id_t id)
    {
        if (arena)
        {
            ARENALIB_LOCK_ACQUIRE(&arena->lock);
            arenalib_arena_free_id_unlocked(arena, id);
            ARENALIB_LOCK_RELEASE(&arena->lock);
        }
    }

#ifdef __cplusplus
}

namespace arenalib
{
    typedef ::arenalib_arena_t arena_t;
    typedef ::arenalib_oom_callback_t oom_callback_t;
    typedef ::arenalib_marker_t marker_t;
    typedef ::arenalib_id_t id_t;

#if defined(__cpp_constexpr) || (defined(__cplusplus) && __cplusplus >= 201103L) || (defined(_MSC_VER) && _MSC_VER >= 1900)
    static constexpr arenalib_size_t default_alignment = ARENALIB_DEFAULT_ALIGNMENT;
#else
    static const arenalib_size_t default_alignment = ARENALIB_DEFAULT_ALIGNMENT;
#endif

#if defined(__GNUC__) || defined(_MSC_VER) || (defined(__cplusplus) && __cplusplus >= 199711L)
    typedef bool arena_bool_t;
#else
    typedef int arena_bool_t;
#endif

    inline arena_bool_t init(arena_t *arena, void *storage, arenalib_size_t capacity, uint32_t max_ids = 0)
    {
        return ::arenalib_arena_init(arena, storage, capacity, max_ids) != 0;
    }

    inline void reset(arena_t *arena)
    {
        ::arenalib_arena_reset(arena);
    }

    inline arenalib_size_t available(const arena_t *arena)
    {
        return ::arenalib_arena_available(arena);
    }

    inline void *malloc(arena_t *arena, arenalib_size_t size)
    {
        return ::arenalib_arena_malloc(arena, size);
    }

    inline void *calloc(arena_t *arena, arenalib_size_t count, arenalib_size_t size)
    {
        return ::arenalib_arena_calloc(arena, count, size);
    }

    inline void free(arena_t *arena, void *ptr, arenalib_size_t size)
    {
        ::arenalib_arena_free(arena, ptr, size);
    }

    inline void *realloc(arena_t *arena, void *ptr, arenalib_size_t old_size, arenalib_size_t new_size)
    {
        return ::arenalib_arena_realloc(arena, ptr, old_size, new_size);
    }

    template <typename T>
    inline T *alloc(arena_t *arena, arenalib_size_t count = 1)
    {
        arenalib_size_t total;
        if (!::arenalib_size_mul(count, sizeof(T), &total))
            return NULL;
        return reinterpret_cast<T *>(::arenalib_arena_malloc(arena, total));
    }

    template <typename T>
    inline T *alloc_zeroed(arena_t *arena, arenalib_size_t count = 1)
    {
        return reinterpret_cast<T *>(::arenalib_arena_calloc(arena, count, sizeof(T)));
    }

    template <typename T>
    inline T *resize(arena_t *arena, T *ptr, arenalib_size_t old_count, arenalib_size_t new_count)
    {
        arenalib_size_t old_size;
        arenalib_size_t new_size;
        if (!::arenalib_size_mul(old_count, sizeof(T), &old_size) ||
            !::arenalib_size_mul(new_count, sizeof(T), &new_size))
            return NULL;
        return reinterpret_cast<T *>(::arenalib_arena_realloc(arena, ptr, old_size, new_size));
    }

    template <typename T>
    inline T *alloc_aligned(arena_t *arena, arenalib_size_t count = 1, arenalib_size_t alignment = default_alignment)
    {
        arenalib_size_t total;
        if (!::arenalib_size_mul(count, sizeof(T), &total))
            return NULL;
        return reinterpret_cast<T *>(::arenalib_arena_malloc_align(arena, total, alignment));
    }

    inline void destroy(arena_t *arena)
    {
        ::arenalib_arena_destroy(arena);
    }

    inline void set_oom_callback(arena_t *arena, arenalib_oom_callback_t callback, void *user_data)
    {
        ::arenalib_arena_set_oom_callback(arena, callback, user_data);
    }

    inline void *malloc_align(arena_t *arena, arenalib_size_t size, arenalib_size_t alignment)
    {
        return ::arenalib_arena_malloc_align(arena, size, alignment);
    }

    inline marker_t get_marker(const arena_t *arena)
    {
        return ::arenalib_arena_get_marker(arena);
    }

    inline void release_marker(arena_t *arena, marker_t marker)
    {
        ::arenalib_arena_release_marker(arena, marker);
    }

#if defined(__cpp_constexpr) || (defined(__cplusplus) && __cplusplus >= 201103L)
    static constexpr id_t invalid_id = {0xFFFFFFFF, 0xFFFFFFFF};
#else
    static const id_t invalid_id = {0xFFFFFFFF, 0xFFFFFFFF};
#endif

    inline id_t alloc_id(arena_t *arena, arenalib_size_t size)
    {
        return ::arenalib_arena_alloc_id(arena, size);
    }

    inline void *get_ptr(arena_t *arena, id_t id)
    {
        return ::arenalib_arena_get_ptr(arena, id);
    }

    inline void free_id(arena_t *arena, id_t id)
    {
        ::arenalib_arena_free_id(arena, id);
    }
}
#endif

#endif /* ARENALIB_H */

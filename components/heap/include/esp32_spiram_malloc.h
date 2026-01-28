/**
 * @file esp32_spiram_malloc.h
 * @brief Optimized SPIRAM allocation helpers for ESP32-C5
 *
 * Provides macros and inline functions for aggressive SPIRAM usage.
 * Objects >= 16 bytes are automatically placed in SPIRAM.
 *
 * Usage in ESP-IDF components:
 *   #include "esp32_spiram_malloc.h"
 *
 * Static allocation:
 *   EXT_RAM_BSS_ATTR uint8_t my_buffer[256];
 *   EXT_RAM_DATA_ATTR const uint8_t lookup_table[] = {...};
 *
 * Dynamic allocation:
 *   void* ptr = spiram_malloc(size);  // Auto-selects SPIRAM if size >= 16
 *   void* ptr = spiram_calloc(n, size);
 *   spiram_free(ptr);
 */

#ifndef ESP32_SPIRAM_MALLOC_H
#define ESP32_SPIRAM_MALLOC_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#if defined(ESP32) || defined(ESP32C5) || defined(ESP32S3)
#include <esp_heap_caps.h>
#include <esp_attr.h>

// ============================================================================
// Configuration
// ============================================================================

/** Threshold in bytes: allocations >= this go to SPIRAM */
#ifndef SPIRAM_MALLOC_THRESHOLD
  #define SPIRAM_MALLOC_THRESHOLD 16
#endif

/** Reserve this much internal DRAM for critical allocations */
#ifndef SPIRAM_RESERVE_INTERNAL_DRAM
  #define SPIRAM_RESERVE_INTERNAL_DRAM 32768  // 32 KB
#endif

// ============================================================================
// Static Allocation Attributes
// ============================================================================

/** Place uninitialized variable in external SPIRAM */
#ifndef EXT_RAM_BSS_ATTR
  #define EXT_RAM_BSS_ATTR __attribute__((section(".ext_ram.bss")))
#endif

/** Place initialized variable/const in external SPIRAM */
#ifndef EXT_RAM_DATA_ATTR
  #define EXT_RAM_DATA_ATTR __attribute__((section(".ext_ram.data")))
#endif

/** Place non-initialized variable in external SPIRAM (no zero-init) */
#ifndef EXT_RAM_NOINIT_ATTR
  #define EXT_RAM_NOINIT_ATTR __attribute__((section(".ext_ram_noinit")))
#endif

// ============================================================================
// Dynamic Allocation Helpers
// ============================================================================

/**
 * @brief Allocate memory with automatic SPIRAM selection
 *
 * @param size Size in bytes to allocate
 * @return void* Pointer to allocated memory, or NULL on failure
 *
 * Behavior:
 * - size >= SPIRAM_MALLOC_THRESHOLD: Try SPIRAM first, fall back to DRAM
 * - size < threshold: Use internal DRAM only
 */
static inline void* spiram_malloc(size_t size) {
#if CONFIG_SPIRAM
    if (size >= SPIRAM_MALLOC_THRESHOLD) {
        // Try SPIRAM first for large allocations
        void* ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (ptr) return ptr;

        // SPIRAM full or not available - try DRAM if we have enough
        size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        if (free_internal > (size + SPIRAM_RESERVE_INTERNAL_DRAM)) {
            return heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
        return NULL;  // Not enough memory anywhere
    }
#endif
    // Small allocations: use internal DRAM only
    return malloc(size);
}

/**
 * @brief Allocate zeroed memory with automatic SPIRAM selection
 *
 * @param nmemb Number of elements
 * @param size Size of each element
 * @return void* Pointer to zeroed memory, or NULL on failure
 */
static inline void* spiram_calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
#if CONFIG_SPIRAM
    if (total >= SPIRAM_MALLOC_THRESHOLD) {
        void* ptr = heap_caps_calloc(nmemb, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (ptr) return ptr;

        size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        if (free_internal > (total + SPIRAM_RESERVE_INTERNAL_DRAM)) {
            return heap_caps_calloc(nmemb, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
        return NULL;
    }
#endif
    return calloc(nmemb, size);
}

/**
 * @brief Reallocate memory with SPIRAM awareness
 *
 * @param ptr Existing pointer (may be NULL)
 * @param size New size
 * @return void* Pointer to reallocated memory, or NULL on failure
 */
static inline void* spiram_realloc(void* ptr, size_t size) {
#if CONFIG_SPIRAM
    if (size >= SPIRAM_MALLOC_THRESHOLD) {
        // If ptr is in SPIRAM, try to realloc in SPIRAM
        if (ptr && heap_caps_get_allocated_size(ptr) &&
            (heap_caps_get_info(ptr, MALLOC_CAP_SPIRAM).total_allocated_bytes > 0)) {
            void* new_ptr = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (new_ptr) return new_ptr;
        }

        // Try SPIRAM for new allocation
        void* new_ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (new_ptr) {
            if (ptr) {
                size_t old_size = heap_caps_get_allocated_size(ptr);
                memcpy(new_ptr, ptr, old_size < size ? old_size : size);
                heap_caps_free(ptr);
            }
            return new_ptr;
        }
    }
#endif
    return realloc(ptr, size);
}

/**
 * @brief Free memory allocated by spiram_* functions
 *
 * @param ptr Pointer to free (works for both SPIRAM and DRAM)
 */
static inline void spiram_free(void* ptr) {
    if (ptr) {
        heap_caps_free(ptr);  // Works for both SPIRAM and DRAM
    }
}

/**
 * @brief Check if pointer is in SPIRAM
 *
 * @param ptr Pointer to check
 * @return true if in SPIRAM, false otherwise
 */
static inline bool spiram_ptr_is_spiram(const void* ptr) {
#if CONFIG_SPIRAM
    if (!ptr) return false;
    return heap_caps_get_info((void*)ptr, MALLOC_CAP_SPIRAM).total_allocated_bytes > 0;
#else
    return false;
#endif
}

/**
 * @brief Get free SPIRAM size
 *
 * @return size_t Free SPIRAM in bytes
 */
static inline size_t spiram_get_free_size(void) {
#if CONFIG_SPIRAM
    return heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
#else
    return 0;
#endif
}

/**
 * @brief Get total SPIRAM size
 *
 * @return size_t Total SPIRAM in bytes
 */
static inline size_t spiram_get_total_size(void) {
#if CONFIG_SPIRAM
    return heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
#else
    return 0;
#endif
}

// ============================================================================
// C++ Operators (optional, enable with ESP32_SPIRAM_CPP_OPERATORS)
// ============================================================================

#if defined(__cplusplus) && defined(ESP32_SPIRAM_CPP_OPERATORS)

inline void* operator new(size_t size) {
    return spiram_malloc(size);
}

inline void* operator new[](size_t size) {
    return spiram_malloc(size);
}

inline void operator delete(void* ptr) noexcept {
    spiram_free(ptr);
}

inline void operator delete[](void* ptr) noexcept {
    spiram_free(ptr);
}

#endif // __cplusplus && ESP32_SPIRAM_CPP_OPERATORS

// ============================================================================
// Debugging Helpers
// ============================================================================

#ifdef SPIRAM_DEBUG_ALLOCATIONS
#include <stdio.h>

#define SPIRAM_MALLOC(size) ({ \
    void* _ptr = spiram_malloc(size); \
    printf("SPIRAM_MALLOC(%zu) = %p [%s]\n", (size_t)(size), _ptr, \
           spiram_ptr_is_spiram(_ptr) ? "SPIRAM" : "DRAM"); \
    _ptr; \
})

#define SPIRAM_FREE(ptr) ({ \
    printf("SPIRAM_FREE(%p) [%s]\n", (ptr), \
           spiram_ptr_is_spiram(ptr) ? "SPIRAM" : "DRAM"); \
    spiram_free(ptr); \
})

#else
#define SPIRAM_MALLOC(size) spiram_malloc(size)
#define SPIRAM_FREE(ptr) spiram_free(ptr)
#endif // SPIRAM_DEBUG_ALLOCATIONS

#else  // !ESP32
// Fallback for non-ESP32 platforms
#define EXT_RAM_BSS_ATTR
#define EXT_RAM_DATA_ATTR
#define EXT_RAM_NOINIT_ATTR
#define spiram_malloc(size) malloc(size)
#define spiram_calloc(n, size) calloc(n, size)
#define spiram_realloc(ptr, size) realloc(ptr, size)
#define spiram_free(ptr) free(ptr)
#define spiram_ptr_is_spiram(ptr) false
#define spiram_get_free_size() 0
#define spiram_get_total_size() 0
#define SPIRAM_MALLOC(size) malloc(size)
#define SPIRAM_FREE(ptr) free(ptr)
#endif // ESP32

#endif // ESP32_SPIRAM_MALLOC_H

#pragma once

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Grow *arr (typed as T*) so its capacity is at least `needed` elements.
 * Storage is allocated in PSRAM via heap_caps_realloc. The pointer array
 * is kept across collections — caller only needs to reset its element
 * count, not free the buffer. Returns false if `needed` exceeds
 * `hard_cap` or if PSRAM allocation failed. */
bool psram_dynarr_ensure(void **arr, int *cap, int needed,
                         size_t elem_size, int hard_cap);

/* Free underlying buffer. Sets *arr=NULL and *cap=0. */
void psram_dynarr_free(void **arr, int *cap);

#ifdef __cplusplus
}
#endif

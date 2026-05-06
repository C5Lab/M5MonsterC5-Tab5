#include "psram_dynarr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <stdlib.h>

static const char *TAG = "psram_dynarr";

bool psram_dynarr_ensure(void **arr, int *cap, int needed,
                         size_t elem_size, int hard_cap)
{
    if (!arr || !cap || elem_size == 0 || hard_cap <= 0) return false;
    if (needed <= *cap) return true;
    if (needed > hard_cap) {
        ESP_LOGW(TAG, "needed=%d exceeds hard_cap=%d, refusing", needed, hard_cap);
        return false;
    }

    int new_cap = *cap ? *cap : 16;
    while (new_cap < needed && new_cap < hard_cap) new_cap *= 2;
    if (new_cap > hard_cap) new_cap = hard_cap;
    if (new_cap <= *cap) return false;

    void *np = heap_caps_realloc(*arr, (size_t)new_cap * elem_size,
                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!np) {
        ESP_LOGE(TAG, "PSRAM realloc failed (%d -> %d, elem=%u)",
                 *cap, new_cap, (unsigned)elem_size);
        return false;
    }
    *arr = np;
    *cap = new_cap;
    return true;
}

void psram_dynarr_free(void **arr, int *cap)
{
    if (!arr) return;
    if (*arr) {
        free(*arr);
        *arr = NULL;
    }
    if (cap) *cap = 0;
}

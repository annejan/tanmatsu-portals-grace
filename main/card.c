#include "card.h"

#ifdef ESP_PLATFORM
#include <stdint.h>
#include "esp_heap_caps.h"

#define HELD_MAX 24

FILE* card_fopen(char const* path, char const* mode) {
    // Every free block of the RAM the card cannot DMA, held while FatFs
    // takes its buffers, then let go: theirs go where the card can reach.
    static uint32_t const caps[] = {MALLOC_CAP_SPM, MALLOC_CAP_RTCRAM};
    void*                 held[HELD_MAX];
    int                   n = 0;
    for (size_t c = 0; c < sizeof(caps) / sizeof(caps[0]); c++)
        while (n < HELD_MAX) {
            size_t b = heap_caps_get_largest_free_block(caps[c]);
            if (b < 64) break;
            void* p = NULL;
            while (b >= 64 && (p = heap_caps_malloc(b, caps[c])) == NULL) b /= 2;
            if (p == NULL) break;
            held[n++] = p;
        }
    FILE* const f = fopen(path, mode);
    while (n > 0) heap_caps_free(held[--n]);
    return f;
}
#else
FILE* card_fopen(char const* path, char const* mode) {
    return fopen(path, mode);
}
#endif

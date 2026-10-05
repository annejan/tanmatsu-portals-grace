// Host tests for main/speech.c: lines split into pieces SAM can say, and
// SAM (third_party/sam) saying every built-in chamber's story. `make check`.

#include <stdio.h>
#include <string.h>
#include "../third_party/sam/sam.h"
#include "chamber.h"
#include "speech.h"

static int s_fail;

#define CHECK(cond, ...)                                \
    do {                                                \
        if (!(cond)) {                                  \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                        \
            printf("\\n");                              \
            s_fail++;                                   \
        }                                               \
    } while (0)

// `host_speech wav FILE TEXT`: say TEXT into a WAV file, to listen to.
static int write_wav(char const* path, char const* text) {
    static uint8_t all[SAM_BUFFER_BYTES * 4];
    char           p[8][SPEECH_MAX];
    int            total = 0;
    int const      n     = speech_split(text, p, 8);
    for (int i = 0; i < n; i++) {
        int                  len = 0;
        uint8_t const* const pcm = speech_render(p[i], &len);
        if (pcm == NULL || total + len > (int)sizeof(all)) continue;
        memcpy(all + total, pcm, (size_t)len);
        total += len;
    }
    FILE* f = fopen(path, "wb");
    if (f == NULL) return 1;
    uint32_t const rate = SPEECH_RATE, size = (uint32_t)total;
    uint32_t const h[] = {0x46464952u, 36 + size, 0x45564157u, 0x20746d66u, 16,  0x00010001u,
                          rate,        rate,      0x00080001u, 0x61746164u, size};
    fwrite(h, sizeof(h), 1, f);
    fwrite(all, 1, (size_t)total, f);
    fclose(f);
    printf("%s: %.1f s\n", path, (double)total / SPEECH_RATE);
    return 0;
}

int main(int argc, char** argv) {
    if (argc == 4 && strcmp(argv[1], "wav") == 0) return write_wav(argv[2], argv[3]);
    char p[8][SPEECH_MAX];
    int  n = speech_split("Hello there. How are you? Fine!", p, 8);
    CHECK(
        n == 3 && strcmp(p[0], "Hello there.") == 0 && strcmp(p[1], "How are you?") == 0 && strcmp(p[2], "Fine!") == 0,
        "three sentences, three pieces (%d)", n);
    // A long sentence breaks at a comma, and never past SPEECH_MAX.
    char const* long_one =
        "This sentence goes on and on, far longer than any one piece can hold, and still it goes on, past every "
        "limit there is";
    n         = speech_split(long_one, p, 8);
    bool fits = n >= 2;
    for (int i = 0; i < n; i++) fits = fits && strlen(p[i]) < SPEECH_MAX;
    CHECK(fits && p[0][strlen(p[0]) - 1] == ',', "a long sentence in %d pieces, the first ending at a comma: %s", n,
          p[0]);
    // Every built-in story, every piece: SAM says something, and it fits
    // SAM's buffer with room to spare.
    for (int c = 0; c < chamber_builtin_count; c++) {
        static level_t lv;
        char           err[96];
        if (!chamber_parse(chamber_builtins[c].text, &lv, NULL, NULL, err, sizeof(err)) || !lv.story[0]) continue;
        n = speech_split(lv.story, p, 8);
        for (int i = 0; i < n; i++) {
            int                  len = 0;
            uint8_t const* const pcm = speech_render(p[i], &len);
            CHECK(pcm != NULL && len > SPEECH_RATE / 10 && len < SAM_BUFFER_BYTES * 8 / 10,
                  "%s, piece %d: %d samples (\"%s\")", chamber_builtins[c].id, i, len, p[i]);
        }
    }
    speech_free();
    if (s_fail) {
        printf("speech tests: %d failed\n", s_fail);
        return 1;
    }
    printf("speech tests: all passed\n");
    return 0;
}

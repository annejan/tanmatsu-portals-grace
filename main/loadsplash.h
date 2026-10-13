#pragma once
// Graceloader's splash, between the engine's and the game's own: the
// loader Portals runs under, at work. A loader's log types out -- its
// version, the app it loaded, the app's code, data and zeroed memory, the
// symbols it resolved -- over a memory map filling in, then its name.
// The numbers are real: read from the app's own app.so (its ELF section
// table) and from Graceloader's metadata on the card. Graceloader has no
// look of its own yet; this is one.

#include <stdbool.h>
#include <stdint.h>
#include "pax_gfx.h"

typedef struct {
    char     version[16];  // Graceloader's, or "" if its metadata is not on the card
    char     app[48];      // what was loaded: <slug>/app.so
    uint32_t text, data, bss;  // bytes: code, data (read-only too), zeroed
    int      symbols;          // resolved from the firmware: the app's imports
    bool     ok;               // the app.so read
} loadsplash_info_t;

// `app_so` read for its sections and imports, `meta_json` (may be missing)
// for Graceloader's version, `app` the name to show. Pure C: the host
// films read the same.
void  loadsplash_read(char const* app_so, char const* meta_json, char const* app, loadsplash_info_t* out);

void  loadsplash_start(loadsplash_info_t const* info);
// A frame: false once it is over.
bool  loadsplash_update(float dt);
// A key: on to the end.
void  loadsplash_skip(void);
// The whole frame (it draws its own black).
void  loadsplash_draw(pax_buf_t* fb);

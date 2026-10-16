#include "text.h"
#include "se_text.h"

static bool s_large;

void text_set_large(bool on) {
    s_large = on;
}

bool text_large(void) {
    return s_large;
}

// Small text grows most: 12 -> 18, 16 -> 22, 24 -> 30.
float text_px(float size) {
    if (!s_large) return size;
    return size * 1.25f > size + 6.0f ? size * 1.25f : size + 6.0f;
}

pax_vec2f text_size(float size, char const* s) {
    pax_vec2f v = rendertext_size(NULL, text_px(size), s);
    if (s_large) v.x += 2.0f;
    return v;
}

// Each stroke twice, a pixel apart, so it is two pixels wide, over a dark
// shadow a pixel down and right -- grey on a white wall reads too.
static pax_vec2f firm(pax_buf_t* fb, uint32_t col, float px, float x, float y, char const* s) {
    rendertext_draw(fb, 0xFF000000u, NULL, px, x + 2.0f, y + 1.0f, s);
    rendertext_draw(fb, 0xFF000000u, NULL, px, x + 1.0f, y + 1.0f, s);
    rendertext_draw(fb, col, NULL, px, x + 1.0f, y, s);
    pax_vec2f v = rendertext_draw(fb, col, NULL, px, x, y, s);
    v.x += 2.0f;
    return v;
}

pax_vec2f text_draw(pax_buf_t* fb, uint32_t col, float size, float x, float y, char const* s) {
    if (!s_large) return rendertext_draw(fb, col, NULL, size, x, y, s);
    return firm(fb, col, text_px(size), x, y, s);
}

pax_vec2f text_draw_firm(pax_buf_t* fb, uint32_t col, float size, float x, float y, char const* s) {
    if (!s_large) return rendertext_draw(fb, col, NULL, size, x, y, s);
    return firm(fb, col, size, x, y, s);
}

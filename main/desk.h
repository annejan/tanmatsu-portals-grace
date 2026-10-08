#pragma once
// The desk a desk story is told from (pack.h `frame: desk`): an Aperture
// terminal, 80 by 25, a DOS prompt and its programs. Pure C -- the host
// tests type at it; deskview.c draws it and feeds it keys.
//
// A pack's desk/ folder:
//
//   day.txt          user: DRATTMANN       who is logged in
//                    host: RLHF-07         ... where
//                    prompt: C:\RLHF>
//                    start: 09:00          the clock as the day begins
//                    step: 0:40            ... and on, each round played
//                    (a blank line, then what the terminal says as it starts)
//   files/NAME.txt   a file DIR lists and TYPE shows; `after: <when>` on
//                    its first line keeps it hidden until then
//   mail/NN-id.txt   from: / subject: / after: <when>, a blank line, the mail
//   calendar.txt     event: / at: HH:MM / moves: <when> HH:MM|never, per event
//   outro.txt        card: a line, typed on black, one card each
//                    reveal: the last card, big; coda: lines under it
//
// <when> is `round N` (N rounds played), `done N` (N chambers approved),
// `mail ID` (that mail read) or `time HH:MM` (the clock that far on).

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "story.h"

#define DESK_COLS   80
#define DESK_ROWS   25
#define DESK_FILES  16
#define DESK_MAILS  24
#define DESK_EVENTS 8
#define DESK_MOVES  6
#define DESK_LINES  160  // the prompt's scrollback
#define DESK_ARENA  (64 * 1024)
#define DESK_CARDS  12

typedef enum {
    WHEN_ALWAYS,
    WHEN_ROUND,
    WHEN_DONE,
    WHEN_MAIL,
    WHEN_TIME
} when_kind_t;

typedef struct {
    uint8_t kind;  // when_kind_t
    int16_t n;     // rounds, chambers, minutes
    char    mail[16];
} desk_when_t;

typedef struct {
    char        name[16];  // as DIR shows it: ASSIGN.TXT
    desk_when_t after;
    char const* text;
} desk_file_t;

typedef struct {
    char        id[16];
    char        from[40];
    char        subject[64];
    desk_when_t after;
    char const* text;
} desk_mail_t;

typedef struct {
    char        what[64];
    int16_t     at;  // minutes since midnight
    int         n_moves;
    desk_when_t when[DESK_MOVES];
    int16_t     to[DESK_MOVES];  // -1: never
} desk_event_t;

typedef struct {
    char         user[16], host[24], prompt[24];
    int16_t      start, step;  // minutes
    char const*  boot;         // what the terminal says as it starts
    desk_file_t  files[DESK_FILES];
    int          n_files;
    desk_mail_t  mails[DESK_MAILS];
    int          n_mails;
    desk_event_t events[DESK_EVENTS];
    int          n_events;
    char const*  cards[DESK_CARDS];  // the outro's (outro.txt)
    int          n_cards;
    char const*  reveal;  // its last card, or NULL
    char const*  coda[4];
    int          n_coda;
    char         arena[DESK_ARENA];  // the texts
    size_t       used;
} desk_data_t;

// A pack folder's desk/: false, with the reason in `err`, if it does not
// read (a pack without one gets a plain desk).
bool desk_load(desk_data_t* d, char const* pack_dir, char* err, size_t err_n);

typedef enum {
    DV_PROMPT,
    DV_PAGE,
    DV_MAIL
} desk_view_t;

typedef enum {
    DESK_NONE,
    DESK_PLAY,   // GLADOS: the next round
    DESK_LEAVE,  // the day done, and out of the door
    DESK_TITLE,  // Esc at an empty prompt: back to the title, saved
} desk_action_t;

// Keys, as deskview.c turns the badge's into them.
enum {
    DK_CHAR,
    DK_ENTER,
    DK_BACK,
    DK_ESC,
    DK_UP,
    DK_DOWN,
    DK_LEFT,
    DK_RIGHT,
    DK_PGUP,
    DK_PGDN,
    DK_TAB
};

typedef struct {
    desk_data_t const* d;
    story_t*           s;
    int                n_rounds;  // the pack's round files
    uint32_t           read;      // mails read (bit i: mails[i])
    uint32_t           told;      // mails announced
    char               lines[DESK_LINES][DESK_COLS + 1];
    int                n_lines;  // lines so far (the ring's last is n_lines - 1)
    char               in[DESK_COLS + 1];
    int                in_len, in_cur;
    char               hist[8][DESK_COLS + 1];
    int                n_hist, at_hist;
    uint8_t            view;                  // desk_view_t
    char               page[DESK_ARENA / 4];  // a page's text, wrapped: TYPE, a mail
    int                page_lines, page_top;
    uint8_t            page_back;  // the view it goes back to
    int                mail_cur;
} desk_t;

// At the desk, for story `s` of `n_rounds` round files: `fresh` for the
// day's start (the boot text), else back from a round.
void          desk_begin(desk_t* k, desk_data_t const* d, story_t* s, int n_rounds, bool fresh);
desk_action_t desk_key(desk_t* k, int key, char ch);
// Back from a round's exit: its verdict in the log, and new mail told.
void          desk_after_round(desk_t* k, story_result_t const* r);
// The clock, minutes since midnight.
int           desk_clock(desk_t const* k);
// Whether the story's rounds are all approved.
bool          desk_done(desk_t const* k);
// The screen as it stands: 25 rows of text, where the cursor is (-1: none)
// and which row is highlighted (-1: none).
void          desk_screen(desk_t const* k, char rows[DESK_ROWS][DESK_COLS + 1], int* cur_row, int* cur_col, int* hl);

// A save: the story where it is, and the mail read. Its length, or -1.
int  desk_save_text(desk_t const* k, char* out, size_t n);
// Read one back into `s` and the mail read: false if it does not read.
bool desk_load_save(char const* text, story_t* s, desk_data_t const* d, uint32_t* read);

// Lines into the terminal, as the desk would print them.
void desk_print(desk_t* k, char const* text);

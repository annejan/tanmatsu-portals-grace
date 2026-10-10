#pragma once
// Files on the card, opened so the card can read and write them.
//
// On the badge FatFs mallocs each open file's sector buffer, which the SD
// card's DMA reads from and writes into. A small malloc can land in TCM
// (SPM) or LP RAM, which that DMA cannot reach: every read of such a file
// then fails, and every write fails at fclose (EIO) -- a ghost not saved,
// a recording that "cannot be read". card_fopen() holds that RAM while the
// file opens, so the buffer goes where the card can reach it. On the host,
// fopen().

#include <stdio.h>

FILE* card_fopen(char const* path, char const* mode);

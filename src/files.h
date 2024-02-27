#ifndef FILES_H
#define FILES_H
#include "common.h"
#include "string.h"
#include <stdbool.h>
#include <stdio.h>
#include "arena.h"

struct Image {
  i32 width, height;
  i32 bpp;
  u8 *data;
};

bool ah_ReadFile(Arena * arena, struct String *string, const char *fileName);

#endif

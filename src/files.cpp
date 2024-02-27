#include "files.h"
#include "arena.h"
#include "common.h"
#include <cstdio>

bool ah_ReadFile(Arena *arena, struct String *string, const char *fileName) {
  FILE *filePtr;
  long fileSize, count;
  int error;

  filePtr = fopen(fileName, "r");
  if (!filePtr) {
    printf("Failed to open '%s'\n", fileName);
    return false;
  }

  fseek(filePtr, 0, SEEK_END);
  fileSize = ftell(filePtr);

  string->len = fileSize;
  string->buffer = (u8 *)ArenaPushArray(arena, u8, fileSize + 1);
  printf("Pushed %ld\n", fileSize);
  string->buffer[fileSize] = '\0';

  fseek(filePtr, 0, SEEK_SET);
  count = fread(string->buffer, 1, fileSize, filePtr);
  if (count != fileSize) {
    printf("read %ld not %ld \n", count, fileSize);
    free(string->buffer);
    return false;
  }

  error = fclose(filePtr);
  if (error != 0) {
    printf("Failed to close '%s'\n", fileName);
    free(string->buffer);
    return false;
  }

  return true;
}

#include "string.h"

void ah_strcpyString(struct String* s1, struct String* s2)
{
  s1->len    = s2->len;
  s1->buffer = (u8*)malloc(sizeof(char) * s1->len);
  memcpy(s1->buffer, s2->buffer, s2->len);
}

void allocateString(String* string, u64 size)
{
  string->buffer = (u8*)malloc(sizeof(u8) * size);
}

char* ah_strcpy(char* buffer, struct String* s2)
{
  memcpy(buffer, s2->buffer, s2->len);
  return buffer;
}

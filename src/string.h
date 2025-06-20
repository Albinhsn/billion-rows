#ifndef STA_STRING_H
#define STA_STRING_H


struct String
{
  u64 len;
  const u8* buffer;
};
typedef struct String String;

void                  allocateString(String* string, u64 size);

#endif

#include <sys/thread.h>
#include <propeller.h>
#include "test29.h"

#define Yield__() (__napuntil(_CNT + 256))
int32_t test29::tx(int32_t val)
{
  int32_t result = 0;
  ((uint8_t *)28672)[idx] = 0;
  return result;
}

int32_t test29::str(int32_t stringptr)
{
  int32_t result = 0;
  while (lockset(strlock)) {
    Yield__();
  }
  for (int32_t _tmp__0000 = strlen((char *) stringptr); _tmp__0000 != 0; _tmp__0000 += -1) {
    tx(((uint8_t *)(stringptr++))[0]);
  }
  lockclr(strlock);
  return result;
}

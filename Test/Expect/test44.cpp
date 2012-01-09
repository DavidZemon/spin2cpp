#include <propeller.h>
#include "test44.h"

int32_t test44::fun(int32_t x, int32_t y)
{
  int32_t result = 0;
  switch (x) {
    case 10:
      switch (y) {
        case 0:
          _OUTA ^= 0x1;
          break;
        case 1:
          _OUTA ^= 0x2;
          break;
      }
      break;
    case 20:
      _OUTA ^= 0x4;
      break;
    default:
        _OUTA ^= 0x8;
      break;
  }
  return result;
}

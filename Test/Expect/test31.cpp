#include <propeller.h>
#include "test31.h"

void test31::Fun(int32_t Y)
{
  int32_t	_tmp__0001;
  _tmp__0001 = X + Y;
  if (_tmp__0001 == 10) {
  OUTA ^= 0x1;
  } else if (_tmp__0001 == (A * 2)) {
  OUTA ^= 0x2;
  OUTA ^= 0x4;
  } else if (1) {
  OUTA ^= 0x8;
  }
  X = X + 5;
}


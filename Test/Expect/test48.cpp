#include <propeller.h>
#include "test48.h"

int32_t test48::setcolors(int32_t colorptr)
{
  int32_t result = 0;
  int32_t	i, fore, back;
  for (i = 0; i != 8; i += 1) {
    fore = (((uint8_t *)colorptr)[(i << 1)] << 2);
    _OUTA = (_OUTA & 0xffffff00) | ((fore << 0) & 0x000000ff);
  }
  return result;
}

int32_t test48::stop(void)
{
  int32_t result = 0;
  _OUTA = (_OUTA & 0xffffff00) | ((0 << 0) & 0x000000ff);
  return result;
}

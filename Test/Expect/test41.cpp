#include <propeller.h>
#include "test41.h"

#define Lookup__(x, a) __extension__({ int32_t i = (x); (i < 0 || i >= sizeof(a)/sizeof(a[0])) ? 0 : a[i]; })
int32_t _lookup__0000[] = {
  48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 65, 66, 67, 68, 69, 70, 
};

int32_t test41::hexdigit(int32_t x)
{
  int32_t result = 0;
  return Lookup__((x & 15), _lookup__0000);
}

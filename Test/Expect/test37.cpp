#include <propeller.h>
#include "test37.h"

#ifdef __GNUC__
#define INLINE__ static inline
#define PostEffect__(X, Y) __extension__({ int32_t tmp__ = (X); (X) = (Y); tmp__; })
#else
#define INLINE__ static
static int32_t tmp__;
#define PostEffect__(X, Y) (tmp__ = (X), (X) = (Y), tmp__)
#endif

uint8_t test37::dat[] = {
  0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 
};
int32_t test37::Zero(int32_t N)
{
  { int32_t _fill__0000; int32_t *_ptr__0002 = (int32_t *)((int32_t *)&dat[0]); int32_t _val__0001 = 0; for (_fill__0000 = N; _fill__0000 > 0; --_fill__0000) {  *_ptr__0002++ = _val__0001; } };
  return 0;
}


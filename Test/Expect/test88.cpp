#include <propeller.h>
#include "test88.h"

#ifdef __GNUC__
#define INLINE__ static inline
#define PostEffect__(X, Y) __extension__({ int32_t tmp__ = (X); (X) = (Y); tmp__; })
#else
#define INLINE__ static
static int32_t tmp__;
#define PostEffect__(X, Y) (tmp__ = (X), (X) = (Y), tmp__)
#endif

int32_t test88::Sum(void)
{
  int32_t	R, X;
  R = 0;
  X = 0;
  do {
    R = (R + X);
    X = (X + 1);
  } while (X <= 4);
  return R;
}


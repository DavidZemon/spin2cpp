#include <propeller.h>
#include "test37.h"

uint8_t test37::dat[] = {
  0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 
};
void test37::Zero(int32_t N)
{
  memset( (void *)((int32_t *)&dat[0]), 0, sizeof(int32_t)*N);
}


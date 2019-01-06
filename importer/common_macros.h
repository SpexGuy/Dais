#ifndef COMMON_MACROS_H_
#define COMMON_MACROS_H_

#include <stdio.h> // printf
#include <stdlib.h> // exit

#define Terabytes(x) ((u64)(x) * 1024 * 1024 * 1024 * 1024)
#define Gigabytes(x) ((u64)(x) * 1024 * 1024 * 1024)
#define Megabytes(x) ((u64)(x) * 1024 * 1024)
#define Kilobytes(x) ((u64)(x) * 1024)

#define Assert(x) do { if (!(x)) { printf("Assert failed! on line %d of %s, '%s'\n", __LINE__, __FILE__, #x); exit(42); } } while(0)

#define AlignRoundUp(x, sz) (((x) + (sz) - 1) & ~((sz) - 1))
#define IsPowerOfTwo(x) (((x) & ((x)-1)) == 0)

#define ElementCount(x) (sizeof(x) / sizeof(x[0]))

#define Min(x, y) ((x) < (y) ? (x) : (y))
#define Max(x, y) ((x) < (y) ? (y) : (x))

#define SetMin(x, y) do { if ((y) < (x)) (x) = (y); } while (0)
#define SetMax(x, y) do { if ((x) < (y)) (x) = (y); } while (0)

#endif

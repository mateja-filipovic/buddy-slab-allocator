#ifndef _CHECK_H_
#define _CHECK_H_

#include <stdio.h>
#include <stdlib.h>

#define CHECK(cond) do { \
	if (!(cond)) { \
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		return 1; \
	} \
} while (0)

#endif

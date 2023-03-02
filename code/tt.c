#include <stdio.h>

int main()
{
 	printf(":0x%lx\n", (unsigned long)(192L << 40 | 5L << 32));
	return 0;
}

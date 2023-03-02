#include <stdio.h>

int foo1(int a, int b)
{
	return a + b;;
}

void foo(int c, int d)
{
	int x =10, y = 20, z = 0;

	z = foo1(c , d);
	printf("z=%d,\n", z);
	return;
}

int main()
{
	int a =3, b = 7;

	//foo(a, b);
	printf("z=%d,\n", sizeof(unsigned long int));
	return 0;
}

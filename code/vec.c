#include<stdio.h>
#define M 10
#define N 10

#if 0
int a[M][N];

void foo (int x) {
	int i,j;

	/* feature: support for multidimensional arrays  */
	for (i=0; i<M; i++) {
		for (j=0; j<N; j++) {
			a[i][j] = x;
		}
	}
}
#endif

//double  a[10] = {1.33, 2.56, 3.54, 4.854, 5.564, 6.56955, 7.5884, 8.12547, 9.1554, 10.5448};
//double b[10] = {10.8855, 9.45785, 8.45726, 7.4758, 6.4586 ,5.4578, 4.12568, 3.1458, 2.12458, 1.2458};
double  a[10] = {1.33, 2.56, 3.54, 4.854};
double b[10] = {10.8855, 9.45785, 8.4576, 7.475};

int foo1(double b)
{
 return b + b;
}

void foo () {
	int i;

	/* feature: support for multidimensional arrays  */
	for (i=0; i<10; i++) {

		a[i] = a[i] + foo1(b[i]);
	}
}

int main()
{
	foo();
	int i;

	for (i=0; i<10; i++) {
			printf("a[%d] = %f\n", i, a[i]);
	}
	return 0;
}

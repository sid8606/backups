#include<stdio.h>

int main()
{
	double max = 0;
	double array[6] = {1.6,3.33,4.87,2.23,66.421,77.765};
	int i = 0;

	max = array[0];
	for (i = 1; i < 6; i++) {
		if (max < array[i]) max = array[i];
	}
	printf("max:%d", max);

}

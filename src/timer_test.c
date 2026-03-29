#include <stdio.h>
#include <stdlib.h>
#include "timer.h"

int main() {
	monotonic_timer_t timer;
	timer_start(&timer);
	for (int i = 0;i < 1000;i++) {
		printf("%d\n", i);
	}
	double elapsed_ms = timer_elapsed_ms(&timer);
	printf("Elapsed time: %.3f ms\n", elapsed_ms);
	scanf("%*s"); // Wait for user input before exiting
	return 0;
}
#include "util.h"
#include "libcoro.h"

int count_numbers_in_file(FILE* file){
	int numbers_count = 0;

	int tmp;
	while (!feof(file)) {
		fscanf(file, "%d", &tmp);
		++numbers_count;
	}

	fseek(file, 0, SEEK_SET);
	return numbers_count;
}

void initialize_array(FILE* file, int* array){
	int i = 0;
	while (!feof(file)) {
		fscanf(file, "%d", &array[i]);
		++i;
	}

	fseek(file, 0, SEEK_SET);
}

double get_time_difference(struct timespec monotime_start, struct timespec monotime_end){
	long long elapsed_ns = ((monotime_end.tv_sec - monotime_start.tv_sec) * 1000000000 + (monotime_end.tv_nsec - monotime_start.tv_nsec)) / 1000;
	return (double) elapsed_ns/ 1000000;
}

void yield_on_time(struct timespec* start_time, double coroutine_latency, double* yield_delay_time){
	struct timespec current_time, yield_start_time, yield_end_time;
	clock_gettime(CLOCK_MONOTONIC, &current_time);

	double time_difference = get_time_difference(*start_time, current_time);

	if (time_difference >= coroutine_latency){
		clock_gettime(CLOCK_MONOTONIC, &yield_start_time);
		coro_yield();
		clock_gettime(CLOCK_MONOTONIC, &yield_end_time);
		*start_time = yield_end_time;
		*yield_delay_time += get_time_difference(yield_start_time, yield_end_time);
	}
}
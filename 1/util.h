#pragma once

#include <time.h>
#include <stdio.h>

int count_numbers_in_file(FILE* file);
void initialize_array(FILE* file, int* array);
double get_time_difference(struct timespec monotime_start, struct timespec monotime_end);
void yield_on_time(struct timespec* monotime_start, double latency, double* yield_delay_time);
#pragma once

#include <stdlib.h>
#include <string.h>
#include <time.h>

int merge(
	void* array, size_t m,
	size_t r, size_t element_size, 
	int (*comparator)(const void *, const void *));

int custom_mergesort(
	void *array,
	size_t elements, size_t element_size,
	int (*comparator)(const void *, const void *),
	struct timespec* start_time,
	float latency,
	double* yield_delay_time);

int int_lt_cmp(const void* p1, const void* p2);
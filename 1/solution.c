#include <stdio.h>
#include <time.h>
#include <limits.h>
#include "mergesort.h"
#include "containers.h"
#include "util.h"
#include "libcoro.h"

/**
 * You can compile and run this code using the commands:
 *
 * $> gcc solution.c libcoro.c
 * $> ./a.out
 */

struct my_context {
	int i;
	double coroutine_latency;
	char* name;
	int* total_numbers_count;
	struct filename_container* filename_container;
	struct array_container** array_containers;
};

static struct my_context *
my_context_new(int i, double coroutine_latency, char* name, int* total_numbers_count, struct filename_container* filename_container, struct array_container** array_containers)
{
	struct my_context *ctx = malloc(sizeof(*ctx));
	ctx->i = i;
	ctx->coroutine_latency = coroutine_latency;
	ctx->name = strdup(name);
	ctx->filename_container = filename_container;
	ctx->total_numbers_count = total_numbers_count;
	ctx->array_containers = array_containers;
	return ctx;
}

static void
my_context_delete(struct my_context *ctx)
{
	free(ctx->name);
	free(ctx);
}

/**
 * Coroutine body. This code is executed by all the coroutines. Here you
 * implement your solution, sort each individual file.
 */
static int
coroutine_mergesort_single_file(void *context)
{	
	double yield_delay_time;
	struct timespec coroutine_start_time, coroutine_end_time;
	clock_gettime(CLOCK_MONOTONIC, &coroutine_start_time);

	struct coro *this = coro_this();
	struct my_context *ctx = context;
	
	printf("Started coroutine %s\n", ctx->name);

	while (ctx->filename_container->current_file_index < ctx->filename_container->count) {
		int file_index = ctx->filename_container->current_file_index;
		char* filename = ctx->filename_container->filenames[file_index];

		printf("Coroutine %s sorting file %s...\n", ctx->name, filename);

		FILE* file = fopen(filename, "r");
		if (!file) {
			my_context_delete(ctx);
			return 0;
		}

		ctx->filename_container->current_file_index += 1;

		int numbers_count = count_numbers_in_file(file);
		*ctx->total_numbers_count += numbers_count;

		ctx->array_containers[file_index] = malloc(sizeof(struct array_container));
		ctx->array_containers[file_index]->array = malloc(numbers_count * sizeof(int));
		ctx->array_containers[file_index]->size = numbers_count;
		initialize_array(file, ctx->array_containers[file_index]->array);
		fclose(file);

		struct timespec start_time;
		clock_gettime(CLOCK_MONOTONIC, &start_time);

		mergesort(ctx->array_containers[file_index]->array, numbers_count, sizeof(int), int_lt_cmp, &start_time, ctx->coroutine_latency, &yield_delay_time);
	}

	printf("%s: switch count after other function %lld\n", ctx->name,
			coro_switch_count(this));

	clock_gettime(CLOCK_MONOTONIC, &coroutine_end_time);

	printf("%s: execution time = %f s\n", ctx->name, get_time_difference(coroutine_start_time, coroutine_end_time) - yield_delay_time);
	
	my_context_delete(ctx);
	/* This will be returned from coro_status(). */
	return 0;
}


int
main(int argc, char **argv)
{	
	/* Setting up initial time */
	struct timespec monotime_start;
	clock_gettime(CLOCK_MONOTONIC, &monotime_start);

	/* Startup arguments initialization */
	int file_count = argc - 3;
	int corountine_count = atoi(argv[1]);
	double target_latency = atof(argv[2]);
	double coroutine_latency = (target_latency / corountine_count) / 1000000;

	int total_numbers_count = 0;
	struct array_container** array_containers = malloc(file_count * sizeof(struct array_container*));
	struct filename_container filename_container = {file_count, 0, &argv[3]};

	/* Initialize our coroutine global cooperative scheduler. */
	coro_sched_init();
	
	/* Start several coroutines. */

	for (int i = 0; i < corountine_count; ++i) {
		/*
		 * The coroutines can take any 'void *' interpretation of which
		 * depends on what you want. Here as an example I give them
		 * some names.
		 */
		char name[16];
		sprintf(name, "coro_%d", i + 1);

		coro_new(
			coroutine_mergesort_single_file, 
			my_context_new(i, coroutine_latency, name, &total_numbers_count, &filename_container, array_containers));
	}

	/* Wait for all the coroutines to end. */
	struct coro* c;
	while ((c = coro_sched_wait()) != NULL) {
		if (c != NULL){
			printf("Finished with status %d\n", coro_status(c));
			coro_delete(c);
		}
	}
	/* All coroutines have finished. */

	/* IMPLEMENT MERGING OF THE SORTED ARRAYS HERE. */

	FILE *output_file = fopen("out.txt", "w");
	if (output_file == NULL) {
		exit(EXIT_FAILURE);
	}

	int *pos = (int *)calloc(file_count, sizeof(int));

	int min = INT_MAX;
	int array_index_with_min = 0;
	int count_index = 0; 

	while (1){
		min = INT_MAX;

		for (int i = 0; i < file_count; ++i) {
			if (pos[i] < array_containers[i]->size){
				int current_element = array_containers[i]->array[pos[i]];
				if (current_element < min){
					min = current_element;
					array_index_with_min = i;
				}
			}
		}
		fprintf(output_file, "%d ", min);

		pos[array_index_with_min] += 1;

		count_index++;
		if (count_index == total_numbers_count){
			break;
		}
	}

	for (int i = 0; i < file_count; ++i){
		free(array_containers[i]->array);
		free(array_containers[i]);
	}

	free(pos);
	free(array_containers);
	fclose(output_file);


	/* Setting up end time */
	struct timespec monotime_end;
	clock_gettime(CLOCK_MONOTONIC, &monotime_end);
	printf("Execution time = %f s\n", get_time_difference(monotime_start, monotime_end));

	return 0;
}

#include <stdio.h>
#include "mergesort.h"
#include "util.h"
#include "libcoro.h"


int merge(
    void* array, size_t m,
    size_t r, size_t element_size, 
    int (*comparator)(const void *, const void *)){
        void* left_array = malloc(m * element_size);
        void* right_array = malloc((r - m) * element_size);

        if (left_array == NULL || right_array == NULL) {
            return -1;
        }

        for (size_t i = 0; i < m; i++){
			memcpy((left_array + i * element_size), (array + i * element_size), element_size);
        }
        
        for (size_t i = 0; i < r - m; i++){
			memcpy((right_array + i * element_size), (array + (i + m) * element_size), element_size);
        }

        size_t left_index = 0;
        size_t right_index = 0;
        size_t merged_index = 0;

        while (left_index < m && right_index < r - m){
            void* array_element = array + merged_index * element_size;
            void* left_element = left_array + left_index * element_size;
            void* right_element = right_array + right_index * element_size;

            int cmp = comparator(left_element, right_element);
            
            if (cmp > 0){
				memcpy(array_element, right_element, element_size);
                right_index++;

            }
            else {
				memcpy(array_element, left_element, element_size);
                left_index++;
            }

            merged_index++;
        }

        while (left_index < m) {
			memcpy((array + merged_index * element_size), (left_array + left_index * element_size), element_size); 
            left_index++; 
            merged_index++;
        }

        while (right_index < r - m) {
			memcpy((array + merged_index * element_size), (right_array + right_index * element_size), element_size);
            right_index++; 
            merged_index++;
        }

        free(left_array);
        free(right_array);

        return 0;
    }


int mergesort(
    void *array,
    size_t elements, size_t element_size,
    int (*comparator)(const void *, const void *),
    struct timespec* start_time,
    float latency
){  
    int status = 0;

    if (elements > 1){
        size_t m = elements / 2;
        size_t r = elements;

        void* left_array = array;
        size_t left_array_size = m;

        void* right_array = array + m * element_size;
        size_t right_array_size = r - m; 

        mergesort(left_array, left_array_size, element_size, comparator, start_time, latency);
        mergesort(right_array, right_array_size, element_size, comparator, start_time, latency);
 
        status = merge(array, m, r, element_size, comparator);

        yield_on_time(start_time, latency);
    }

    return status;
}

int int_lt_cmp(const void* p1, const void* p2){
    return (*(int*)p1 - *(int*)p2);
}
GCC_FLAGS = -Wextra -Werror -Wall -Wno-gnu-folding-constant -ldl -rdynamic

all: libcoro.c util.c mergesort.c solution.c ../utils/heap_help/heap_help.c
	gcc $(GCC_FLAGS) libcoro.c util.c mergesort.c solution.c ../utils/heap_help/heap_help.c

clean:
	rm a.out

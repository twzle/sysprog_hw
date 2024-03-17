GCC_FLAGS = -Wextra -Werror -Wall -Wno-gnu-folding-constant -ldl -rdynamic

all: test.o userfs.o heap_help.o
	gcc $(GCC_FLAGS) test.o userfs.o heap_help.o

heap_help.o: ../utils/heap_help/heap_help.c
	gcc $(GCC_FLAGS) -c ../utils/heap_help/heap_help.c -o heap_help.o

test.o: test.c
	gcc $(GCC_FLAGS) -c test.c -o test.o -I ../utils

userfs.o: userfs.c
	gcc $(GCC_FLAGS) -c userfs.c -o userfs.o

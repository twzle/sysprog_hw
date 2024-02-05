#pragma once

struct array_container {
	int size;
	int* array;
};

struct filename_container {
	int count;
	int current_file_index;
	char** filenames;
};
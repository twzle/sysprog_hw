#include "userfs.h"
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

enum
{
    BLOCK_SIZE = 512,
    MAX_FILE_SIZE = 1024 * 1024 * 100,
};

/** Global error code. Set from any function on any error. */
static enum ufs_error_code ufs_error_code = UFS_ERR_NO_ERR;

struct block
{
    /** Block memory. */
    char *memory;
    /** How many bytes are occupied. */
    int occupied;
    /** Next block in the file. */
    struct block *next;
    /** Previous block in the file. */
    struct block *prev;
};

struct file
{
    /** Double-linked list of file new_blocks. */
    struct block *block_list_head;
    /**
     * Last block in the list above for fast access to the end
     * of file.
     */
    struct block *block_list_tail;
    /** How many file descriptors are opened on the file. */
    int refs;
    /** File name. */
    char *name;
    /** Files are stored in a double-linked list. */
    struct file *next;
    struct file *prev;

    int is_deleted;
    int blocks_count;
    size_t size;
};

/** List of all files. */
static struct file *file_list = NULL;

struct filedesc
{
    struct file *file;
    int position;
    int permission;
};

/**
 * An array of file descriptors. When a file descriptor is
 * created, its pointer drops here. When a file descriptor is
 * closed, its place in this array is set to NULL and can be
 * taken by next ufs_open() call.
 */
static struct filedesc **file_descriptors = NULL;
static int file_descriptor_count = 0;
static int file_descriptor_capacity = 0;

enum ufs_error_code
ufs_errno()
{
    return ufs_error_code;
}

int ufs_open(const char *filename, int flags)
{
    if (flags == UFS_CREATE)
    {
        int file_descriptor_index = -1;
        struct file *existing_file = NULL;
        struct file *last_file_in_list = NULL;
        struct file *file_list_copy = file_list;

        while (file_list_copy != NULL)
        {
            if (strcmp(file_list_copy->name, filename) == 0 && file_list_copy->is_deleted == 0)
            {
                existing_file = file_list_copy;
                break;
            }
            last_file_in_list = file_list_copy;
            file_list_copy = file_list_copy->next;
        }

        struct file *new_file;
        if (existing_file == NULL)
        {
            new_file = malloc(sizeof(struct file));
            new_file->refs = 1;
            new_file->blocks_count = 0;
            new_file->size = 0;
            new_file->name = malloc(sizeof(char) * (strlen(filename) + 1));
            new_file->is_deleted = 0;
            new_file->block_list_head = NULL;
            new_file->block_list_tail = NULL;
            strcpy(new_file->name, filename);

            if (last_file_in_list == NULL)
            {
                new_file->prev = NULL;
                new_file->next = NULL;
                file_list = new_file;
            }
            else
            {
                new_file->prev = last_file_in_list;
                new_file->next = NULL;
                last_file_in_list->next = new_file;
            }
        }
        else
        {
            new_file = existing_file;
            ++(new_file->refs);
        }

        struct filedesc *new_filedesc = malloc(sizeof(struct filedesc));
        new_filedesc->file = new_file;
        new_filedesc->position = 0;
        new_filedesc->permission = UFS_READ_WRITE;

        if (file_descriptor_capacity == file_descriptor_count)
        {
            if (file_descriptor_capacity == 0)
            {
                file_descriptors = malloc(sizeof(struct filedesc *));
            }
            else
            {
                file_descriptors = realloc(file_descriptors, sizeof(struct filedesc *) * (file_descriptor_capacity + 1));
            }
            ++file_descriptor_capacity;
            file_descriptor_index = file_descriptor_count;
            file_descriptors[file_descriptor_index] = new_filedesc;
            ++file_descriptor_count;
        }
        else
        {
            for (int i = 0; i < file_descriptor_capacity; ++i)
            {
                if (file_descriptors[i] == NULL)
                {
                    file_descriptor_index = i;
                    file_descriptors[file_descriptor_index] = new_filedesc;
                    ++file_descriptor_count;
                    break;
                }
            }
        }

        return file_descriptor_index;
    }
    else
    {
        int file_descriptor_index = -1;
        struct file *existing_file = NULL;
        struct file *file_list_copy = file_list;

        while (file_list_copy != NULL)
        {
            if (strcmp(file_list_copy->name, filename) == 0 && file_list_copy->is_deleted == 0)
            {
                existing_file = file_list_copy;
                break;
            }
            file_list_copy = file_list_copy->next;
        }

        if (existing_file == NULL)
        {
            ufs_error_code = UFS_ERR_NO_FILE;
            return -1;
        }
        else
        {
            ++(existing_file->refs);

            struct filedesc *new_filedesc = malloc(sizeof(struct filedesc));
            new_filedesc->file = existing_file;
            new_filedesc->position = 0;
            new_filedesc->permission = flags == 0 ? UFS_READ_WRITE : flags;

            if (file_descriptor_capacity == file_descriptor_count)
            {
                if (file_descriptor_capacity == 0)
                {
                    file_descriptors = malloc(sizeof(struct filedesc *));
                }
                else
                {
                    file_descriptors = realloc(file_descriptors, sizeof(struct filedesc *) * (file_descriptor_capacity + 1));
                }
                ++file_descriptor_capacity;
                file_descriptor_index = file_descriptor_count;
                file_descriptors[file_descriptor_index] = new_filedesc;
                ++file_descriptor_count;
            }
            else
            {
                for (int i = 0; i < file_descriptor_capacity; i++)
                {
                    if (file_descriptors[i] == NULL)
                    {
                        file_descriptor_index = i;
                        file_descriptors[file_descriptor_index] = new_filedesc;
                        ++file_descriptor_count;
                        break;
                    }
                }
            }

            ufs_error_code = UFS_ERR_NO_ERR;
            return file_descriptor_index;
        }
    }
}

ssize_t
ufs_write(int fd, const char *buf, size_t size)
{
    if (fd > file_descriptor_capacity || fd < 0)
    {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    struct filedesc *descriptor = file_descriptors[fd];
    if (descriptor == NULL){
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    if (descriptor->permission != UFS_WRITE_ONLY && descriptor->permission != UFS_READ_WRITE){
        ufs_error_code = UFS_ERR_NO_PERMISSION;
        return -1;
    }

    if (size == 0)
    {
        ufs_error_code = UFS_ERR_NO_ERR;
        return 0;
    }

    struct file *current_file = descriptor->file;

    if (descriptor->position > (int) current_file->size){
        descriptor->position = current_file->size;
    }

    if (descriptor->position + size > MAX_FILE_SIZE)
    {
        ufs_error_code = UFS_ERR_NO_MEM;
        return -1;
    }

    int new_blocks_count = 0;
    int required_blocks_count = ((descriptor->position + size) / BLOCK_SIZE) + 1;
    if ((descriptor->position + size) % BLOCK_SIZE == 0){
        --required_blocks_count;
    }
    if (required_blocks_count > current_file->blocks_count)
    {
        new_blocks_count = required_blocks_count - current_file->blocks_count;
    }

    current_file->blocks_count += new_blocks_count;
    if (new_blocks_count > 0)
    {
        struct block **new_blocks = malloc(sizeof(struct block*) * new_blocks_count);
        for (int i = 0; i < new_blocks_count; i++){
            struct block *new_block = malloc(sizeof(struct block));
            new_blocks[i] = new_block;
        }

        for (int i = 0; i < new_blocks_count; i++)
        {
            new_blocks[i]->memory = malloc(sizeof(char) * BLOCK_SIZE);
            new_blocks[i]->occupied = 0;

            if (i == 0)
            {
                new_blocks[i]->prev = NULL;
            }
            else if (i > 0)
            {
                new_blocks[i]->prev = new_blocks[i - 1];
            }

            if (i + 1 < new_blocks_count)
            {
                new_blocks[i]->next = new_blocks[i + 1];
            }
            else
            {
                new_blocks[i]->next = NULL;
            }
        }

        if (current_file->block_list_head == NULL)
        {
            current_file->block_list_head = new_blocks[0];
        }
        else
        {
            current_file->block_list_tail->next = new_blocks[0];
            new_blocks[0]->prev = current_file->block_list_tail;
        }

        current_file->block_list_tail = new_blocks[new_blocks_count - 1];
        free(new_blocks);
    }

    struct block *current_block = current_file->block_list_head;
    int current_block_number = descriptor->position / BLOCK_SIZE;
    for (int i = 0; i < current_block_number; ++i)
    {
        current_block = current_block->next;
    }

    int result_position = descriptor->position + size;
    int position_in_buffer = 0;
    while (descriptor->position < result_position)
    {
        int position_in_block = (descriptor->position % BLOCK_SIZE);
        int available_memory_size_in_block = BLOCK_SIZE - position_in_block;

        int remaining_size_to_write = result_position - descriptor->position;
        int size_to_write = 0;
        if (remaining_size_to_write < available_memory_size_in_block)
        {
            size_to_write = remaining_size_to_write;
        }
        else if (remaining_size_to_write >= available_memory_size_in_block)
        {
            size_to_write = available_memory_size_in_block;
        }

        memcpy(current_block->memory + position_in_block, buf + position_in_buffer, size_to_write);
        position_in_buffer += size_to_write;
        descriptor->position += size_to_write;

        int occupied = 0;
        if (descriptor->position % BLOCK_SIZE == 0)
        {
            occupied = BLOCK_SIZE;
        }
        else
        {
            occupied = descriptor->position % BLOCK_SIZE;
        }

        current_block->occupied = (occupied > current_block->occupied) ? occupied : current_block->occupied;
        current_file->size += size_to_write;

        if (descriptor->position < result_position)
        {
        }
        if (descriptor->position % BLOCK_SIZE == 0)
        {
            if (descriptor->position < result_position)
            {
                current_block = current_block->next;
            }
            else
            {
                break;
            }
        }
    }

    ufs_error_code = UFS_ERR_NO_ERR;
    return size;
}

ssize_t
ufs_read(int fd, char *buf, size_t size)
{
    if (fd > file_descriptor_capacity || fd < 0)
    {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    struct filedesc *descriptor = file_descriptors[fd];
    if (descriptor == NULL){
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    if (descriptor->permission != UFS_READ_ONLY && descriptor->permission != UFS_READ_WRITE){
        ufs_error_code = UFS_ERR_NO_PERMISSION;
        return -1;
    }

    if (size == 0)
    {
        ufs_error_code = UFS_ERR_NO_ERR;
        return 0;
    }

    struct file* current_file = descriptor->file;

    if (descriptor->position > (int) current_file->size){
        descriptor->position = current_file->size;
    }

    int current_block_number = descriptor->position / BLOCK_SIZE;
    if (current_block_number > current_file->blocks_count){
        ufs_error_code = UFS_ERR_NO_ERR;
        return 0;
    }

    struct block* current_block = descriptor->file->block_list_head;

    for (int i = 0; i < current_block_number; ++i){
        current_block = current_block->next;
    }

    int result_position = descriptor->position + size;
    int position_in_buffer = 0;

    while (descriptor->position < result_position && current_block != NULL){
        int position_in_block = (descriptor->position % BLOCK_SIZE);
        int available_memory_size_in_block = current_block->occupied - position_in_block;

        int remaining_size_to_read = result_position - descriptor->position;
        int size_to_read = remaining_size_to_read < available_memory_size_in_block ? remaining_size_to_read : available_memory_size_in_block;
        
        memcpy(buf + position_in_buffer, current_block->memory + position_in_block, size_to_read);

        position_in_block += size_to_read;
        position_in_buffer += size_to_read;
        descriptor->position += size_to_read;

        current_block = current_block->next;
    }
    return position_in_buffer;
}

int ufs_close(int fd)
{
    if (fd > file_descriptor_capacity || fd < 0)
    {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }
    if (file_descriptors[fd] == NULL)
    {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }
    else
    {
        struct file *closing_file = file_descriptors[fd]->file;
        --(closing_file->refs);
        if (closing_file->is_deleted == 1 && closing_file->refs == 0)
        {
            free(closing_file->name);

            while (closing_file->block_list_tail != NULL)
            {
                if (closing_file->block_list_tail->prev != NULL)
                {
                    closing_file->block_list_tail = closing_file->block_list_tail->prev;
                    free(closing_file->block_list_tail->next->memory);
                    free(closing_file->block_list_tail->next);
                }
                else
                {
                    free(closing_file->block_list_tail->memory);
                    free(closing_file->block_list_tail);
                    closing_file->block_list_tail = NULL;
                }
            }

            if (closing_file->prev != NULL)
            {
                closing_file->prev->next = closing_file->next;
            }
            else
            {
                file_list = closing_file->next;
            }
            if (closing_file->next != NULL)
            {
                closing_file->next->prev = closing_file->prev;
            }

            free(closing_file);
        }

        free(file_descriptors[fd]);
        file_descriptors[fd] = NULL;
        --file_descriptor_count;

        ufs_error_code = UFS_ERR_NO_ERR;
        return 0;
    }
}

int ufs_delete(const char *filename)
{
    struct file *existing_file = NULL;
    struct file *file_list_copy = file_list;
    while (file_list_copy != NULL)
    {
        if (strcmp(file_list_copy->name, filename) == 0 && file_list_copy->is_deleted == 0)
        {
            existing_file = file_list_copy;
            break;
        }
        file_list_copy = file_list_copy->next;
    }

    if (existing_file == NULL)
    {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }
    else
    {
        existing_file->is_deleted = 1;

        if (existing_file->refs == 0)
        {
            struct file *closing_file = existing_file;

            free(closing_file->name);

            while (closing_file->block_list_tail != NULL)
            {
                if (closing_file->block_list_tail->prev != NULL)
                {
                    closing_file->block_list_tail = closing_file->block_list_tail->prev;
                    free(closing_file->block_list_tail->next->memory);
                    free(closing_file->block_list_tail->next);
                }
                else
                {
                    free(closing_file->block_list_tail->memory);
                    free(closing_file->block_list_tail);
                    closing_file->block_list_tail = NULL;
                }
            }

            if (closing_file->prev != NULL)
            {
                closing_file->prev->next = closing_file->next;
            }
            else
            {
                file_list = closing_file->next;
            }
            if (closing_file->next != NULL)
            {
                closing_file->next->prev = closing_file->prev;
            }

            free(closing_file);
        }


        ufs_error_code = UFS_ERR_NO_ERR;
        return 0;
    }
}

void ufs_destroy(void)
{
    struct file *file_list_copy = file_list;

    while (file_list_copy != NULL)
    {
        struct file *closing_file = file_list_copy;
        file_list_copy = file_list_copy->next;

        free(closing_file->name);

        while (closing_file->block_list_tail != NULL)
        {
            if (closing_file->block_list_tail->prev != NULL)
            {
                closing_file->block_list_tail = closing_file->block_list_tail->prev;
                free(closing_file->block_list_tail->next->memory);
                free(closing_file->block_list_tail->next);
            }
            else
            {
                free(closing_file->block_list_tail->memory);
                free(closing_file->block_list_tail);
                closing_file->block_list_tail = NULL;
            }
        }

        if (closing_file->prev != NULL)
        {
            closing_file->prev->next = closing_file->next;
        }
        if (closing_file->next != NULL)
        {
            closing_file->next->prev = closing_file->prev;
        }

        free(closing_file);
    }

    for (int i = 0; i < file_descriptor_capacity; i++)
    {
        if (file_descriptors[i] != NULL)
        {
            free(file_descriptors[i]);
        }
    }

    free(file_descriptors);
}

int
ufs_resize(int fd, size_t new_size)
{
    if (fd > file_descriptor_capacity || fd < 0)
    {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    struct filedesc *descriptor = file_descriptors[fd];
    if (descriptor == NULL){
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    if (descriptor->permission != UFS_WRITE_ONLY && descriptor->permission != UFS_READ_WRITE){
        ufs_error_code = UFS_ERR_NO_PERMISSION;
        return -1;
    }

    if (new_size > MAX_FILE_SIZE){
        ufs_error_code = UFS_ERR_NO_MEM;
        return -1;
    }

    struct file *current_file = descriptor->file;

    if (new_size > current_file->size){
        while (new_size > current_file->size){
            struct block* current_block = current_file->block_list_tail;

            if (current_block == NULL){
                current_block = malloc(sizeof(struct block));
                current_block->memory = malloc(sizeof(char) * BLOCK_SIZE);
                current_block->occupied = 0;
                current_block->next = NULL;
                current_block->prev = NULL;

                current_file->block_list_head = current_block;
                current_file->block_list_tail = current_block;
                current_file->blocks_count++;
            }

            int size_difference = new_size - current_file->size;
            int available_memory_size_in_block = BLOCK_SIZE - current_block->occupied;

            int size_to_write = size_difference < available_memory_size_in_block ? size_difference : available_memory_size_in_block;
            current_file->size += size_to_write;
            current_block->occupied += size_to_write;

            if (new_size > current_file->size){
                struct block* new_block = malloc(sizeof(struct block));
                new_block->memory = malloc(sizeof(char) * BLOCK_SIZE);
                new_block->occupied = 0;
                new_block->next = NULL;
                new_block->prev = current_block;

                current_block->next = new_block;
                current_file->block_list_tail = new_block;
                ++current_file->blocks_count;
            }
        }
    } else if (new_size < current_file->size){
        while (new_size < current_file->size){
            struct block* current_block = current_file->block_list_tail;
            
            int size_difference = current_file->size - new_size;
            int available_memory_size_to_truncate = current_block->occupied;
            int size_to_truncate = size_difference < available_memory_size_to_truncate ? size_difference : available_memory_size_to_truncate; 
            current_file->size -= size_to_truncate;
            current_block->occupied -= size_to_truncate;

            if (new_size < current_file->size){
                if (current_block->prev != NULL){
                    current_block = current_block->prev;
                    current_file->block_list_tail = current_block;
                    --current_file->blocks_count;
                    free(current_block->next->memory);
                    free(current_block->next);
                    current_block->next = NULL;
                } else {
                    current_file->block_list_head = NULL;
                    current_file->block_list_tail = NULL;
                    --current_file->blocks_count;
                    free(current_block->memory);
                    free(current_block);
                }
            }
        }
    }

    return 0;
}
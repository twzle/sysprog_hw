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

    /* PUT HERE OTHER MEMBERS */
};

struct file
{
    /** Double-linked list of file new_blocks. */
    struct block *block_list;
    /**
     * Last block in the list above for fast access to the end
     * of file.
     */
    struct block *last_block;
    /** How many file descriptors are opened on the file. */
    int refs;
    /** File name. */
    char *name;
    /** Files are stored in a double-linked list. */
    struct file *next;
    struct file *prev;

    int is_deleted;
    int blocks_count;
    /* PUT HERE OTHER MEMBERS */
};

/** List of all files. */
static struct file *file_list = NULL;

struct filedesc
{
    struct file *file;

    /* PUT HERE OTHER MEMBERS */
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
            new_file->name = malloc(sizeof(char) * (strlen(filename) + 1));
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
                new_file->next = last_file_in_list->next;
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

        return file_descriptor_index;
    }
    else
    {
        int file_descriptor_index = -1;
        struct file *existing_file = NULL;
        struct file *file_list_copy = file_list;
        while (file_list_copy != NULL)
        {
            if (strcmp(file_list_copy->name, filename) == 0 || file_list_copy->is_deleted == 0)
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
    if (fd > file_descriptor_capacity || fd < 0){
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    if (size == 0){
        ufs_error_code = UFS_ERR_NO_ERR;
        return 0;
    }
    
    struct file* current_file = file_descriptors[fd]->file;
    size_t unused_memory_in_last_block = 0;
    int new_blocks_count = 0;

    if (current_file->last_block != NULL){
        unused_memory_in_last_block = BLOCK_SIZE - current_file->last_block->occupied;
    }

    if (size > unused_memory_in_last_block){
        new_blocks_count = ((size - unused_memory_in_last_block) / BLOCK_SIZE) + 1;
    }

    if ((current_file->blocks_count + new_blocks_count) * BLOCK_SIZE > MAX_FILE_SIZE){
        ufs_error_code = UFS_ERR_NO_MEM;
        return -1;
    }

    current_file->blocks_count += new_blocks_count;
    struct block* new_blocks = malloc(sizeof(struct block) * new_blocks_count);

    for (int i = 0; i < new_blocks_count; i++){
        new_blocks[i].memory = malloc(BLOCK_SIZE);
        new_blocks[i].occupied = 0;

        if (i == 0){
            new_blocks[i].prev = NULL;
        } else if (i > 0){
            new_blocks[i].prev = &new_blocks[i - 1];
        }

        if (i + 1 < new_blocks_count){
            new_blocks[i].next = &new_blocks[i + 1];    
        } else {
            new_blocks[i].next = NULL;
        }
    }

    if (current_file->block_list == NULL){
        current_file->block_list = &new_blocks[0];
    } else {
        current_file->last_block->next = &new_blocks[0];
        new_blocks[0].prev = current_file->last_block;
    }

    current_file->last_block = &new_blocks[new_blocks_count - 1];

    size_t position = 0;
    while (position < size && current_file->block_list){
        char* memory = current_file->block_list->memory;
        int occupied = current_file->block_list->occupied;
        size_t unused_memory_in_block = BLOCK_SIZE - occupied;
        if (unused_memory_in_block == 0){
            current_file->block_list = current_file->block_list->next;
            continue;
        }

        int char_sequence_size_to_write = 0;
        if (size - position < unused_memory_in_block){
            char_sequence_size_to_write = size - position;
        } else {
            char_sequence_size_to_write = unused_memory_in_block;
        }

        strncpy(memory + occupied, buf + position, char_sequence_size_to_write);
        position += char_sequence_size_to_write; 
    }
    
    ufs_error_code = UFS_ERR_NO_ERR;
    return 0;
}

ssize_t
ufs_read(int fd, char *buf, size_t size)
{
    /* IMPLEMENT THIS FUNCTION */
    (void)fd;
    (void)buf;
    (void)size;
    ufs_error_code = UFS_ERR_NOT_IMPLEMENTED;
    return -1;
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

            while (closing_file->block_list != NULL)
            {
                if (closing_file->block_list->next != NULL)
                {
                    closing_file->block_list = closing_file->block_list->next;
                    free(closing_file->block_list->prev);
                }
                else
                {
                    free(closing_file->block_list);
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

            while (closing_file->block_list != NULL)
            {
                if (closing_file->block_list->next != NULL)
                {
                    closing_file->block_list = closing_file->block_list->next;
                    free(closing_file->block_list->prev);
                }
                else
                {
                    free(closing_file->block_list);
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

        while (closing_file->block_list != NULL)
        {
            if (closing_file->block_list->next != NULL)
            {
                closing_file->block_list = closing_file->block_list->next;
                free(closing_file->block_list->prev);
            }
            else
            {
                free(closing_file->block_list);
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

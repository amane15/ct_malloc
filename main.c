#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

typedef struct block_meta {
    size_t size;
    struct block_meta *next;
    bool free;
    bool is_mmap;
} BlockMeta;

const size_t META_SIZE = sizeof(BlockMeta);
const size_t MMAP_THRESHOLD = 128 * 1024;
BlockMeta *free_list = NULL;

BlockMeta *request_space(BlockMeta *last, size_t size) {
    void *request = sbrk(size + META_SIZE);

    if (request == (void *)-1) {
        return NULL;
    }

    BlockMeta *block = (BlockMeta *)request;
    block->size = size;
    block->next = NULL;
    block->free = false;
    block->is_mmap = false;

    if (last) {
        last->next = block;
    }

    return block;
}

BlockMeta *find_free_space(BlockMeta **last, size_t size) {
    BlockMeta *current = free_list;

    while (current != NULL && !(current->free && current->size >= size)) {
        *last = current;
        current = current->next;
    }

    return current;
}

void split_block(BlockMeta *block, size_t size) {
    if (block->size >= size + META_SIZE + sizeof(void *)) {
        BlockMeta *new_block = (BlockMeta *)((char *)(block + 1) + size);
        new_block->size = block->size - size - META_SIZE;
        new_block->free = true;
        new_block->is_mmap = false;

        block->size = size;
        block->next = new_block;
    }
}

void coalesce_block(BlockMeta *block) {
    if (block->next && block->next->free) {
        block->size = META_SIZE + block->next->size;
        block->next = block->next->next;
    }

    BlockMeta *current = free_list;

    while (current && current->next != block) {
        current = current->next;
    }

    if (current && current->free) {
        current->size = META_SIZE + block->size;
        current->next = block->next;
    }
}

void free(void *ptr) {
    if (!ptr)
        return;

    BlockMeta *block = (BlockMeta *)ptr - 1;

    if (block->is_mmap) {
        munmap(block, block->size + META_SIZE);
    } else {
        block->free = true;
        coalesce_block(block);
    }
}

void *malloc(size_t size) {
    BlockMeta *block;

    if (size == 0) {
        return NULL;
    }

    size = (size + sizeof(void *) - 1) & ~(sizeof(void *) - 1);

    if (size >= MMAP_THRESHOLD) {
        void *ptr = mmap(NULL, size + META_SIZE, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

        if (ptr == MAP_FAILED) {
            return NULL;
        }

        block = (BlockMeta *)ptr;
        block->size = size;
        block->next = NULL;
        block->free = false;
        block->is_mmap = true;
    } else {
        BlockMeta *last = free_list;
        block = find_free_space(&last, size);

        if (!block) {
            block = request_space(last, size);
            if (!block) {
                return NULL;
            }
        } else {
            split_block(block, size);
            block->free = false;
        }
    }

    return block + 1;
}

void *calloc(size_t n, size_t size) {
    if (n != 0 && size > SIZE_MAX / n) {
        return NULL;
    }

    void *ptr = malloc(n * size);
    if (ptr == NULL)
        return NULL;

    memset(ptr, 0, n * size);

    return ptr;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr)
        return malloc(size);

    if (size == 0) {
        free(ptr);
        return NULL;
    }

    BlockMeta *block = (BlockMeta *)ptr - 1;

    size = (size + sizeof(void *) - 1) & ~(sizeof(void *) - 1);

    if (block->size >= size) {
        split_block(block, size);
        return ptr;
    }

    if (!block->is_mmap && block->next && block->next->free &&
        block->size + META_SIZE + block->next->size >= size) {
        block->size += META_SIZE + block->next->size;
        block->next = block->next->next;
        split_block(block, size);

        return ptr;
    }

    void *new_ptr = malloc(size);
    if (!new_ptr)
        return NULL;

    memcpy(new_ptr, ptr, block->size);
    free(ptr);
    return new_ptr;
}

int main() {
    int *ptr = malloc(sizeof(int));
    if (ptr == NULL) {
        printf("Memory alloc error\n");
        return 1;
    }

    *ptr = 4;

    printf("Value at memory location %p: %d\n", ptr, *ptr);

    return 0;
}

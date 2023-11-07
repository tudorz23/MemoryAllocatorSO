#pragma once

#include <sys/mman.h>
#include <unistd.h>
#include <string.h>

#include "osmem.h"
#include "block_meta.h"

#define HEAP_PREALLOC_SIZE (128 * 1024)
#define MMAP_THRESHOLD (128 * 1024)

typedef struct block_meta block_meta_t;

// Taken from "Resources" -> "Implementing malloc"
#define ALIGNMENT 8
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1))

#define META_BLOCK_SIZE ALIGN(sizeof(struct block_meta))

void head_init(void);
void list_add_last(block_meta_t *block);
void list_remove_block(block_meta_t *block);

block_meta_t *map_block_in_mem(size_t size);
int prealloc_heap_attempt(void);
block_meta_t *find_best_block(size_t size);
void split_block_attempt(block_meta_t *block, size_t size);
block_meta_t *expand_last_block(size_t size);
void coalesce_blocks(block_meta_t *block1, block_meta_t *block2);
void coalesce_attempt(void);
block_meta_t *search_block_in_list(void *ptr);
block_meta_t *get_free_heap_block(size_t size);
int check_last_on_heap(block_meta_t *block);

void delete_mapped_block(block_meta_t *block);
void copy_block(block_meta_t *dest, block_meta_t *src, size_t size);
void *shrink_realloc(block_meta_t *block, size_t size);
void block_coalesce_to_size(block_meta_t *block, size_t size);
void *extend_realloc(block_meta_t *block, size_t size);

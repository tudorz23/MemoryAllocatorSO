/* SPDX-License-Identifier: BSD-3-Clause */

#pragma once

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include "printf.h"
#include "block_meta.h"
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>


#define HEAP_PREALLOC_SIZE (128 * 1024)
#define MMAP_THRESHOLD (128 * 1024)

void *os_malloc(size_t size);
void os_free(void *ptr);
void *os_calloc(size_t nmemb, size_t size);
void *os_realloc(void *ptr, size_t size);

void head_init(void);
void list_add_last(block_meta_t *block);
void list_remove_block(block_meta_t *block);
block_meta_t *search_block_in_list(void *ptr);

block_meta_t *map_block_in_mem(size_t size);

int prealloc_heap_attempt();
block_meta_t *find_best_block(size_t size);
void split_block_attempt(block_meta_t *block, size_t size);
block_meta_t *expand_last_block(size_t size);

block_meta_t *get_free_heap_block(size_t size);

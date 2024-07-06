/* SPDX-License-Identifier: BSD-3-Clause */

#include "block_meta.h"

// define reusable constants
#define MMAP_THRESHOLD (128 * 1024)
#define META_SIZE sizeof(struct block_meta)
#define META_PADDING ((8 - (META_SIZE % 8)) % 8)
#define PROTS (PROT_READ | PROT_WRITE)
#define MAPS (MAP_PRIVATE | MAP_ANONYMOUS)
#define PAGE_SIZE getpagesize()

// function that determines the last block of the list
struct block_meta *find_last_block(struct block_meta *start_block);
// function that returns the best fitting block with a size at least of size
struct block_meta *find_available_block(struct block_meta *start, size_t size);
// function the finds the block whose useful address starts from ptr
struct block_meta *find_block(struct block_meta *start_block, void *ptr);
// function that returns the last heap-allocated block
struct block_meta *find_last_heap_block(struct block_meta *start_block);
// function that sorts the list of blocks so that
// all the heap-allocated blocks are at the beggining of the list
// and the mapped blocks are at the end
void sort_blocks(struct block_meta *start_block);
// function that iterates through the list of blocks and
// coalesces all the free blocks that are one after another
void coalesce_blocks(struct block_meta *start_block);

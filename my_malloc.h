#ifndef __MY_MALLOC_H__
#define __MY_MALLOC_H__

#include <stdlib.h>

struct block_t {
   size_t dataSize;
   struct block_t* prev;
   struct block_t* next;
};
typedef struct block_t block;

void* ts_malloc_lock(size_t size);
void ts_free_lock(void* ptr);
void* ts_malloc_nolock(size_t size);
void ts_free_nolock(void* ptr);

void* bf_malloc(size_t size, int lockMode, block** head, block** tail); // 0: nolock 1:lock
void bf_free(void* ptr, block** head, block** tail);

block* findBestFit(block* head, size_t size);
void* addBlock(size_t size, int lockMode);
void* reuseBlock(block* block_, size_t size, block** head, block** tail);

void splitBlock(block* block_, size_t size, block** head, block** tail);
void removeFreeBlock(block* block_, block** head, block** tail);
void removeAndLink(block* block_, block* remainFree, block** head, block** tail);
void putToFreeList(block* block_, block** head, block** tail);
void mergeFreeSpace(block* block_1, block* block_2, block** tail);

#endif

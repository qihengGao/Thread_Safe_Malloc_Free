#include "my_malloc.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

block* freeBlockHead = NULL;
block* freeBlockTail = NULL;

__thread block* freeBlockHeadNoLock = NULL;  // thread local storage
__thread block* freeBlockTailNoLock = NULL;  // thread local storage

void* ts_malloc_lock(size_t size) {
  pthread_mutex_lock(&lock);
  void* result = bf_malloc(size, 1, &freeBlockHead, &freeBlockTail);
  pthread_mutex_unlock(&lock);
  return result;
}

void ts_free_lock(void* ptr) {
  pthread_mutex_lock(&lock);
  bf_free(ptr, &freeBlockHead, &freeBlockTail);
  pthread_mutex_unlock(&lock);
}

void* ts_malloc_nolock(size_t size) {
  return bf_malloc(size, 0, &freeBlockHeadNoLock, &freeBlockTailNoLock);
}

void ts_free_nolock(void* ptr) {
  bf_free(ptr, &freeBlockHeadNoLock, &freeBlockTailNoLock);
}

void* bf_malloc(size_t size, int lockMode, block** head, block** tail) {
  block* freeBlock = findBestFit(*head, size);
  // If suitable block is found.
  if (freeBlock != NULL) {
    return reuseBlock(freeBlock, size, head, tail);
  }
  return addBlock(size, lockMode);
}

void bf_free(void* ptr, block** head, block** tail) {
  // for large_size valgrind clean
  if (ptr == NULL) {
    return;
  }
  block* block_ = (block*)ptr - 1;  // Back to the start of the block
  putToFreeList(block_, head, tail);
  if (block_->next != NULL) mergeFreeSpace(block_, block_->next, tail);
  if (block_->prev != NULL) mergeFreeSpace(block_->prev, block_, tail);
}

block* findBestFit(block* curr, size_t size) {
  block* candidate = NULL;
  while (curr != NULL) {
    if (curr->dataSize == size) {
      candidate = curr;
      break;
    }
    if (curr->dataSize > size) {
      if (candidate == NULL) {
        candidate = curr;
      } else if (candidate->dataSize > curr->dataSize) {
        candidate = curr;
      }
    }
    curr = curr->next;
  }
  return candidate;
}

void* addBlock(size_t size, int lockMode) {
  size_t currSize = sizeof(block) + size;
  void* spacePtr = NULL;
  if (lockMode == 1) {
    spacePtr = sbrk(currSize);
  } else {
    pthread_mutex_lock(&lock);
    spacePtr = sbrk(currSize);
    pthread_mutex_unlock(&lock);
  }
  if (spacePtr == (void*)-1) {
    return NULL;
  }
  block* block_ = (block*)spacePtr;
  block_->dataSize = size;
  block_->prev = NULL;
  block_->next = NULL;
  return block_ + 1;
}

void* reuseBlock(block* block_, size_t size, block** head, block** tail) {
  /* If previous size is greater than curr required size + metadata (for the
   * remain parts), we need to split the free block.
   */
  if (block_->dataSize >= sizeof(block) + size) {
    splitBlock(block_, size, head, tail);
  } else {  // otherwise, simply remove from the list of free blocks, set the
            // dataSize to the malloced size
    removeFreeBlock(block_, head, tail);
  }
  return block_ + 1;  // return the pointer to the start of the data
}

void splitBlock(block* block_, size_t size, block** head, block** tail) {
  size_t currSize = sizeof(block) + size;  // metadata + dataSize
  block* remainFree =
      (block*)((char*)(block_) + currSize);  // start of remainFree
  remainFree->dataSize = block_->dataSize - currSize;
  block_->dataSize = size;
  removeAndLink(block_, remainFree, head, tail);
}

void removeFreeBlock(block* block_, block** head, block** tail) {
  if (block_ == (*head) || block_ == (*tail)) {
    if (block_ == (*head)) {  // head
      (*head) = block_->next;
      if ((*head) != NULL) {
        (*head)->prev = NULL;
      }
    }
    if (block_ == (*tail)) {  // tail
      (*tail) = block_->prev;
      if ((*tail) != NULL) {
        (*tail)->next = NULL;
      }
    }
  } else {  // middle
    block_->prev->next = block_->next;
    block_->next->prev = block_->prev;
  }
  block_->prev = NULL;
  block_->next = NULL;
}

void removeAndLink(block* block_, block* remainFree, block** head,
                   block** tail) {
  if (block_ == (*head) || block_ == (*tail)) {
    if (block_ == (*head)) {  // head
      (*head) = remainFree;
      (*head)->prev = NULL;
      remainFree->next = block_->next;
      if (remainFree->next != NULL) {
        remainFree->next->prev = remainFree;
      }
    }
    if (block_ == (*tail)) {  // tail
      (*tail) = remainFree;
      (*tail)->next = NULL;
      (*tail)->prev = block_->prev;
      if ((*tail)->prev != NULL) {
        (*tail)->prev->next = (*tail);
      }
    }
  } else {  // in the middle
    remainFree->prev = block_->prev;
    remainFree->next = block_->next;
    remainFree->prev->next = remainFree;
    remainFree->next->prev = remainFree;
  }
}

// Put according to the order of physical address.
void putToFreeList(block* block_, block** head, block** tail) {
  block_->prev = NULL;
  block_->next = NULL;
  if ((*head) == NULL && (*tail) == NULL) {
    (*head) = (*tail) = block_;
  } else if (block_ < (*head)) {
    block_->next = (*head);
    (*head)->prev = block_;
    (*head) = block_;
  } else if (block_ > (*tail)) {
    block_->prev = (*tail);
    (*tail)->next = block_;
    (*tail) = block_;
  } else {
    block* curr = (*head);
    while (curr != NULL && block_ > curr->next) {
      curr = curr->next;
    }
    block_->next = curr->next;
    block_->prev = curr;
    curr->next = block_;
    block_->next->prev = block_;
  }
}

// Merge two blocks if they are ajacent.
void mergeFreeSpace(block* block_1, block* block_2, block** tail) {
  char* segment = (char*)block_1 + sizeof(block) + block_1->dataSize;
  if (segment == (char*)block_2) {
    block_1->dataSize += sizeof(block) + block_2->dataSize;
    block_1->next = block_2->next;
    if (block_1->next == NULL) {
      (*tail) = block_1;
    } else {
      block_1->next->prev = block_1;
    }
    block_2->prev = NULL;
    block_2->next = NULL;
  }
}
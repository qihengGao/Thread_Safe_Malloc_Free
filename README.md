# Thread_Safe_Malloc_Free
This repo is for self-implemented `malloc` and `free` function that are similar to those in C library.  

We utilized two different techniques to ensure the thread-safe property: 1. mutex on critical sections, and 2. thread local storage of the head and tail pointer of the doubly linked list that tracks heap memory resources.
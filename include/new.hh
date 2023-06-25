#ifndef NEW_HH__
#define NEW_HH__

#include "common.h"

void* operator new(size_t size,ptr_t ptr);
void* operator new(size_t size);
void* operator new[](size_t size);
void operator delete(void* ptr);
void operator delete(void* ptr,xlen_t unknown);
void operator delete[](void* ptr);

#endif
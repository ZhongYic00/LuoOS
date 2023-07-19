#include "vm.hh"
#include "alloc.hh"
#include "kernel.hh"
#include "rvcsr.hh"

// #define moduleLevel LogLevel::info

using namespace vm;
PageBuf::PageBuf(const PageKey key_):key(key),ppn(kGlobObjs->pageMgr->alloc(1)){}
PageBuf::~PageBuf(){kGlobObjs->pageMgr->free(ppn,1);}
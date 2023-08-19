#ifndef SYSCALL_HH__
#define SYSCALL_HH__

#include "sys.hh"
#include <thirdparty/expected.hpp>

namespace syscall
{
    using namespace nonstd;
    // typedef xlen_t sysrt_t;
    typedef expected<xlen_t,xlen_t> sysrt_t;
    typedef sysrt_t (*syscall_t)(xlen_t,xlen_t,xlen_t,xlen_t,xlen_t,xlen_t);
    void init();
    extern syscall_t syscallPtrs[];
    extern const char* syscallHelper[];
    inline sysrt_t Err(int err){return make_unexpected(err);}
} // namespace syscall

#endif
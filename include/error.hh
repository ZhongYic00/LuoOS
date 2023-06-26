#ifndef ERROR_HH__
#define ERROR_HH__
#pragma once

#include "thirdparty/expected.hpp"
#include "EASTL/string.h"
using nonstd::expected;
using nonstd::make_unexpected;
typedef eastl::string Err;

#endif
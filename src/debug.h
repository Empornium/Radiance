#ifndef DEBUG__H
#define DEBUG__H
#include <string>

#if defined(__DEBUG_BUILD__)
std::string backtrace(int skip);
#endif

#endif

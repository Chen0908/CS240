/**
 * Author: Haihong Li
 *
 * File: debug-utils.h
 * ---------------------------
 * Debugging toolbox. You can pass in macro SUPPRESS_DEBUG to temporarily suppress the output.
 * Obviously you could use gdb (or lldb), but sometimes printing is handier.
 * NOTE not thread-safe.
 */

#ifndef DEBUG_UTILS_H_
#define DEBUG_UTILS_H_

#include <cstdlib>
#include <cstdio>
#include <cstdarg>
#include <iostream>

static inline void __WRITE_FORMATTED(const char *format, ...) {
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args); 
}

#ifndef SUPPRESS_DEBUG /* don't rely on NDEBUG here - that is the standard way to disable assert() */

#define debugCL1 "\033[38;5;167m" /* red */
#define debugCL2 "\033[38;5;221m" /* yellow */
#define debugCL3 "\033[38;5;153m" /* blue */
#define debugCL0 "\033[0;m"

#define PIN std::cerr << "[DEBUG] PIN " << debugCL1 << __FILE__ << ":" << __LINE__ << debugCL0 << debugCL1 << " <>" << debugCL0 << std::endl;

#define LOGC(CONTENT) std::cerr << "[DEBUG] LOG " << debugCL2 << __FILE__ << ":" << __LINE__ << debugCL0 \
                               << " " << (CONTENT) << debugCL2 << " <>" << debugCL0 << std::endl;

#define PRINTF(FORMAT, ...) std::cerr << "[DEBUG] PRINTF " << debugCL3 << __FILE__ << ":" << __LINE__ << debugCL0 << " "; \
										__WRITE_FORMATTED(FORMAT, __VA_ARGS__); \
										std::cerr << debugCL3 << " <>" << debugCL0 << std::endl;

#define NOT_REACHED() std::cerr << "[DEBUG] PANIC: " << __FILE__ << ":" << __LINE__ \
								<< " executed an unreachable statement, abort. <>" \
								<< std::endl; abort();

#define BREAK_POINT asm("int $3" ::); /* Trace/breakpoint trap on x86, x86-64 */

#endif

#ifdef SUPPRESS_DEBUG

#define PIN ;
#define LOGC(STRING) ;
#define PRINTF(FORMAT, ...) ;
#define NOT_REACHED() ;
#define BREAK_POINT ;

#endif

#endif
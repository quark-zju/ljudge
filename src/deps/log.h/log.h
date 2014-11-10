#ifndef __LOG_h__
#define __LOG_h__

#include <stdio.h>
#include <errno.h>
#include <string.h>

#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

extern int debug_level;
#ifdef _OPENMP
# define LOG_H_PREFIX_FORMAT "%3d "
# define LOG_H_PREFIX_VALUE omp_get_thread_num()
#else
# define LOG_H_PREFIX_FORMAT "%s"
# define LOG_H_PREFIX_VALUE ""
#endif

#ifdef NDEBUG
/* compile with all debug messages removed */
#define log_debug(M, ...)
#else
#define log_debug(M, ...) if (debug_level > 0) fprintf(stderr, LOG_H_PREFIX_FORMAT "\33[34mDEBUG\33[39m " M "  \33[90m at %s (%s:%d) \33[39m\n", LOG_H_PREFIX_VALUE, ##__VA_ARGS__, __func__, __FILE__, __LINE__)
#endif

/* safe readable version of errno */
#define clean_errno() (errno == 0 ? "None" : strerror(errno))

#define log_error(M, ...) fprintf(stderr, LOG_H_PREFIX_FORMAT "\33[31mERR\33[39m   " M "  \33[90m at %s (%s:%d) \33[94merrno: %s\33[39m\n", LOG_H_PREFIX_VALUE, ##__VA_ARGS__, __func__, __FILE__, __LINE__, clean_errno())

#define log_warn(M, ...) if (debug_level > 2) fprintf(stderr, LOG_H_PREFIX_FORMAT "\33[91mWARN\33[39m  " M "  \33[90m at %s (%s:%d) \33[94merrno: %s\33[39m\n", LOG_H_PREFIX_VALUE, ##__VA_ARGS__, __func__, __FILE__, __LINE__, clean_errno())

#define log_info(M, ...) if (debug_level > 1) fprintf(stderr, LOG_H_PREFIX_FORMAT "\33[32mINFO\33[39m  " M "  \33[90m at %s (%s:%d) \33[39m\n", LOG_H_PREFIX_VALUE, ##__VA_ARGS__, __func__, __FILENAME__, __LINE__)

#endif


#ifndef ERR_H
#define ERR_H

#if 1
#define perror(x) { perror(x); printf("    Function: %s, Line: %d\n\n", __func__, __LINE__); }
#define log(...) printf("log: " __VA_ARGS__)
#else
#define log(...)
#endif

#endif

#ifndef ERR_H
#define ERR_H

#include <errno.h>
#include <stddef.h>
#include <stdio.h>

#if 1

# define perror(x) \
({ \
	perror(x); \
	fprintf(stderr, "    Function: %s @ %s:%d\n\n", \
		__func__, __FILE__, __LINE__); \
})
# define log(...) fprintf(stderr, "log: " __VA_ARGS__)

#else

# define log(...)

#endif

#define fail_fn(err, fn) \
({ \
	if (errno == 0) \
	{ \
		errno = (err); \
	} \
	if (errno != 0) \
	{ \
		perror("error: " #fn); \
	} \
	else \
	{ \
		fprintf(stderr, "error: " #fn " failed\n"); \
		fprintf(stderr, "    Function: %s @ %s:%d\n\n", \
			__func__, __FILE__, __LINE__); \
	} \
	ret = -1; \
	goto exit; \
})

#define try_fn(err, fn, ...) \
({ \
	errno = 0; \
	int try_fn_ret_ = fn(__VA_ARGS__); \
	if (try_fn_ret_ != 0) \
	{ \
		fail_fn(err, fn); \
	} \
	try_fn_ret_; \
})

#define try_fd(err, fn, ...) \
({ \
	errno = 0; \
	int try_fd_ret_ = fn(__VA_ARGS__); \
	if (try_fd_ret_ == -1) \
	{ \
		fail_fn(err, fn); \
	} \
	try_fd_ret_; \
})

#define try_ptr(err, fn, ...) \
({ \
	errno = 0; \
	void *try_ptr_ret_ = fn(__VA_ARGS__); \
	if (try_ptr_ret_ == NULL) \
	{ \
		fail_fn(err, fn); \
	} \
	try_ptr_ret_; \
})

#define try_io(err, fn, fd, ptr, size, ...) \
({ \
	errno = 0; \
	size_t try_io_size_ = (size); \
	size_t try_io_ret_ = fn(fd, ptr, try_io_size_ \
				__VA_OPT__(,) __VA_ARGS__); \
	if (try_io_ret_ != try_io_size_) \
	{ \
		fail_fn(err, fn); \
	} \
	try_io_ret_; \
})

#endif

#include <sys/types.h>
#include <sys/stat.h>

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <unistd.h>

#include "trace-marker.h"

#ifdef ENABLE_TRACING

static int tracefd = -1;

int trace_marker_init(void)
{
	if (tracefd == -1) {
		tracefd = open("/sys/kernel/debug/tracing/trace_marker",
				O_WRONLY);
		if (tracefd == -1) {
			return errno;
		}
	}
	
	return 0;
}

int trace_printk(const char *fmt, ...)
{
	va_list argp;
	int len;
	char buffer[1024];

	/*
	 * Since we do not allow this to ever be closed once it was successfully
	 * opened, we do not need to worry about synchronization here.
	 */
	if (tracefd == -1) {
		return -EINVAL;
	}

	va_start(argp, fmt);
	len = vsnprintf(buffer, sizeof(buffer), fmt, argp);
	va_end(argp);

	if (len > 0) {
		int written;
		written = write(tracefd, buffer, len - 1);
		if (written != (len - 1)) {
			return -EIO;
		}
	} else {
		return -EIO;
	}

	return 0;
}

#endif /* ENABLE_TRACING */

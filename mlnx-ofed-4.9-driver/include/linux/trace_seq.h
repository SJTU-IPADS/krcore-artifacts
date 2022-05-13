#ifndef _MLNX_LINUX_TRACE_SEQ_H
#define _MLNX_LINUX_TRACE_SEQ_H

#include "../../compat/config.h"

#include_next <linux/trace_seq.h>

#ifndef HAVE_TRACE_SEQ_BUFFER_PTR
/**
 * trace_seq_buffer_ptr - return pointer to next location in buffer
 * @s: trace sequence descriptor
 *
 * Returns the pointer to the buffer where the next write to
 * the buffer will happen. This is useful to save the location
 * that is about to be written to and then return the result
 * of that write.
 */
static inline unsigned char *
trace_seq_buffer_ptr(struct trace_seq *s)
{
	return s->buffer + s->len;
}
#endif

#endif /* _MLNX_LINUX_TRACE_SEQ_H */

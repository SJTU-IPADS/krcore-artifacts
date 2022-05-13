#ifndef _COMPAT_LINUX_PRINTK_H
#define _COMPAT_LINUX_PRINTK_H 1

#include <linux/version.h>
#include "../../compat/config.h"

#ifdef HAVE_LINUX_PRINTK_H
#include_next <linux/printk.h>
#else /* HAVE_LINUX_PRINTK_H */
#include <linux/kernel.h>

#define pr_emerg_once(fmt, ...)					\
	printk_once(KERN_EMERG pr_fmt(fmt), ##__VA_ARGS__)
#define pr_alert_once(fmt, ...)					\
	printk_once(KERN_ALERT pr_fmt(fmt), ##__VA_ARGS__)
#define pr_crit_once(fmt, ...)					\
	printk_once(KERN_CRIT pr_fmt(fmt), ##__VA_ARGS__)
#define pr_err_once(fmt, ...)					\
	printk_once(KERN_ERR pr_fmt(fmt), ##__VA_ARGS__)
#define pr_warn_once(fmt, ...)					\
	printk_once(KERN_WARNING pr_fmt(fmt), ##__VA_ARGS__)
#define pr_warn pr_warning
#define pr_notice_once(fmt, ...)				\
	printk_once(KERN_NOTICE pr_fmt(fmt), ##__VA_ARGS__)
#define pr_info_once(fmt, ...)					\
	printk_once(KERN_INFO pr_fmt(fmt), ##__VA_ARGS__)
#define pr_cont_once(fmt, ...)					\
	printk_once(KERN_CONT pr_fmt(fmt), ##__VA_ARGS__)
#if defined(DEBUG)
#define pr_debug_once(fmt, ...)					\
	printk_once(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)
#else
#define pr_debug_once(fmt, ...)					\
	no_printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)
#endif

/*
 * ratelimited messages with local ratelimit_state,
 * no local ratelimit_state used in the !PRINTK case
 */
#ifndef printk_ratelimited
#ifdef CONFIG_PRINTK
#define printk_ratelimited(fmt, ...)                                    \
({                                                                      \
        static DEFINE_RATELIMIT_STATE(_rs,                              \
                                      DEFAULT_RATELIMIT_INTERVAL,       \
                                      DEFAULT_RATELIMIT_BURST);         \
                                                                        \
        if (__ratelimit(&_rs))                                          \
                printk(fmt, ##__VA_ARGS__);                             \
})
#else
#define printk_ratelimited(fmt, ...)                                    \
        no_printk(fmt, ##__VA_ARGS__)
#endif
#endif /* ifndef printk_ratelimited */

#ifndef printk_once
#define printk_once(fmt, ...)                   \
({                                              \
        static bool __print_once;               \
                                                \
        if (!__print_once) {                    \
                __print_once = true;            \
                printk(fmt, ##__VA_ARGS__);     \
        }                                       \
})
#endif

#endif /* HAVE_LINUX_PRINTK_H */

#ifndef HAVE_PRINT_HEX_DUMP_DEBUG

#if  defined(CONFIG_PRINTK) && defined(DEBUG)
#define print_hex_dump_debug(prefix_str, prefix_type, rowsize,         \
			     groupsize, buf, len, ascii)               \
	print_hex_dump(KERN_DEBUG, prefix_str, prefix_type, rowsize,   \
		       groupsize, buf, len, ascii)
#else
static inline void print_hex_dump_debug(const char *prefix_str, int prefix_type,
					int rowsize, int groupsize,
					const void *buf, size_t len, bool ascii)
{
}
#endif

#endif /* HAVE_PRINT_HEX_DUMP_DEBUG */

#endif	/* _COMPAT_LINUX_PRINTK_H */

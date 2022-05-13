#ifndef _COMPAT_LINUX_SOCKET_H
#define _COMPAT_LINUX_SOCKET_H 1

#include_next <linux/socket.h>

#ifndef for_each_cmsghdr
#define for_each_cmsghdr(cmsg, msg) \
	for (cmsg = CMSG_FIRSTHDR(msg); \
	     cmsg; \
	     cmsg = CMSG_NXTHDR(msg, cmsg))
#endif

#endif	/* _COMPAT_LINUX_SOCKET_H */

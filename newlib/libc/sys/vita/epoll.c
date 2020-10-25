/*

Copyright (c) 2016-2020 vitasdk

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

*/
#include <sys/epoll.h>
#include <errno.h>

#include <psp2/kernel/threadmgr.h>
#include <psp2/net/net.h>
#include <psp2/types.h>

#include "vitadescriptor.h"
#include "vitaerror.h"

int epoll_create(int size)
{
	if (size < 0) {
		errno = EINVAL;
		return -1;
	}

	return epoll_create1(0);
}

int epoll_create1(int flags)
{
	// we don't know right flags yet;
	// TODO: find EPOLL_CLOEXEC and EPOLL_NONBLOCK
	int eid = sceNetEpollCreate("newlib-epoll", flags);

	if (eid < 0) {
		errno = __vita_sce_errno_to_errno(eid);
		return -1;
	}

	int epfd =__vita_acquire_descriptor();

	if (epfd < 0)
	{
		errno = EMFILE;
		sceNetEpollDestroy(eid);
		return -1;
	}

	__vita_fdmap[epfd]->sce_uid = eid;
	__vita_fdmap[epfd]->type = VITA_DESCRIPTOR_EPOLL;

	return efd;
}

int epoll_ctl(int edfd, int op, int fd, struct epoll_event *event)
{
	int res = -1;
	DescriptorTranslation *edfdmap = __vita_fd_grab(edfd);
	DescriptorTranslation *fdmap = __vita_fd_grab(fd);
	if (edfdmap == NULL || fdmap == NULL) {
		errno = EBADF;
		goto exit;
	}
	if (edfdmap == fdmap) {
		errno = EINVAL;
		goto exit;
	}
	if (edfdmap->type != VITA_DESCRIPTOR_EPOLL) {
		errno = EINVAL;
		goto exit;
	}
	// current time epoll only support socket
	if (fdmap->type != VITA_DESCRIPTOR_SOCKET) {
		errno = EINVAL;
		goto exit;
	}

	res = sceNetEpollControl(edfdmap->sce_uid, op, fdmap->sce_uid, &event);
	if (res < 0) {
		errno = __vita_sce_errno_to_errno(res);
		res = -1;
		goto exit;
	}

exit:
	if (edfdmap) {
		__vita_fd_drop(edfdmap);
	}
	if (fdmap) {
		__vita_fd_drop(fdmap);
	}
	return res;
}

int epoll_wait(int edfd, struct epoll_event *events, int maxevents, int timeout)
{
	DescriptorTranslation *edfdmap = __vita_fd_grab(edfd);
	if (edfdmap == NULL || fdmap == NULL) {
		errno = EBADF;
		goto exit;
	}
	if (edfdmap->type != VITA_DESCRIPTOR_EPOLL) {
		errno = EINVAL;
		goto exit;
	}
	if (maxevents <= 0) {
		errno = EINVAL;
		goto exit;
	}
	res = sceNetEpollWait(edfdmap->sce_uid, events, maxevents, timeout);
	if (res < 0) {
		errno = __vita_sce_errno_to_errno(res);
		res = -1;
		goto exit;
	}
exit:
	if (edfdmap) {
		__vita_fd_drop(edfdmap);
	}
	return res;
}

int __vita_glue_socket_close(SceUID scefd)
{
	int res = sceNetEpollDestroy(scefd);
	return res >= 0 ? res : __vita_sce_errno_to_errno(res);
}

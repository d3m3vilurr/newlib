/*

Copyright (C) 2017, David "Davee" Morgan
Copyright (C) 2020, vitasdk

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
#include <errno.h>
#include <poll.h>
#include <string.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/net/net.h>
#include <psp2/types.h>

#include "vitadescriptor.h"
#include "vitaerror.h"

static int poll_peek_socket(DescriptorTranslation *f, unsigned int evt)
{
	if (evt == 0)
		return 0;

	int eid = sceNetEpollCreate("poll", 0);
	if (eid < 0) {
		errno = __vita_sce_errno_to_errno(eid);
		return -1;
	}

	SceNetEpollEvent event = { 0 };

	event.events = evt;
	int ret = sceNetEpollControl(eid, SCE_NET_EPOLL_CTL_ADD, f->sce_uid, &event);
	if (ret < 0) {
		errno = __vita_sce_errno_to_errno(eid);
		goto exit;
	}

	memset(&event, 0, sizeof(SceNetEpollEvent));
	ret = sceNetEpollWait(eid, &event, 1, 0);
	if (ret < 0) {
		errno = __vita_sce_errno_to_errno(eid);
		goto exit;
	}

exit:
	sceNetEpollDestroy(eid);
	if (ret < 0)
		return -1;
	ret = 0;

	if (event.events & SCE_NET_EPOLLIN)
		ret |= POLLIN;
	if (event.events & SCE_NET_EPOLLOUT)
		ret |= POLLOUT;
	if (event.events & SCE_NET_EPOLLERR)
		ret |= POLLERR;
	return ret;
}

#define POLL_PEEK_IN_SOCKET(f) poll_peek_socket(f, SCE_NET_EPOLLIN)
#define POLL_PEEK_OUT_SOCKET(f) poll_peek_socket(f, SCE_NET_EPOLLOUT)

static int is_pollin_ready(DescriptorTranslation *f)
{
	switch (f->type) {
		case VITA_DESCRIPTOR_SOCKET:
			return POLL_PEEK_IN_SOCKET(f);
		//case VITA_DESCRIPTOR_PIPE:
		//	return poll_peek_pipe(f);
		case VITA_DESCRIPTOR_TTY:
		case VITA_DESCRIPTOR_FILE:
			return 0;
		default:
			// TODO: error?
			return 0;
	}
}

static int is_pollout_ready(DescriptorTranslation *f)
{
	switch (f->type) {
		case VITA_DESCRIPTOR_SOCKET:
			return POLL_PEEK_OUT_SOCKET(f);
		//case VITA_DESCRIPTOR_PIPE:
		//	return 0;
		case VITA_DESCRIPTOR_TTY:
		case VITA_DESCRIPTOR_FILE:
			return 0;
		default:
			// TODO: error?
			return 0;
	}
}

int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
	// Number of selected fd's
	int selected = 0;
	int elapsed_time = 0;

	while (elapsed_time < timeout || timeout == -1) {
		for (int i = 0; i < nfds; ++i) {
			struct pollfd *fd = &fds[i];

			fd->revents = 0;

			// The field fd contains a file descriptor for an open file. If this
			// field is negative, then the corresponding events field is ignored and
			// the revents field returns zero.
			if (fd->fd < 0) {
				continue;
			}

			DescriptorTranslation *f = __vita_fd_grab(fd->fd);

			if (!f) {
				// we have a bad file descriptor
				errno = EBADF;
				return -1;
			}

			int ret = 0;

			// There is data to be read.
			if (fd->events & POLLIN) {
				ret = is_pollin_ready(f);
				if (ret < 0)
					goto finally;
				fd->revents |= ret;
			}

			// Writing is now possible, though a write larger than available
			// space in a socket or pipe will still block (unless O_NONBLOCK
			// is set).
			if (fd->events & POLLOUT) {
				ret = is_pollout_ready(f);
				if (ret < 0)
					goto finally;
				fd->revents |= ret;
			}

finally:
			__vita_fd_drop(f);

			if (ret < 0)
				return ret;

			if (fd->revents)
				++selected;
		}

		if (selected)
			break;

		// TODO: improve - this is very crude
		sceKernelDelayThread(100000);
		elapsed_time += 100;
	}

	return selected;
}
/*
 * Copyright (c) 2019 - 2020 Andri Yngvason
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include "config.h"

// Linux with glibc < 2.27 has no wrapper
#if defined(HAVE_MEMFD) && !defined(HAVE_MEMFD_CREATE)
#include <sys/syscall.h>

static inline int memfd_create(const char *name, unsigned int flags) {
	return syscall(SYS_memfd_create, name, flags);
}
#endif

#if !defined(HAVE_MEMFD) && !defined(__FreeBSD__)
static void randname(char *buf)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	long r = ts.tv_nsec;

	for (int i = 0; i < 6; ++i) {
		buf[i] = 'A'+(r&15)+(r&16)*2;
		r >>= 5;
	}
}
#endif

static int create_shm_file(void)
{
/* NetBSD is checked FIRST, ahead of HAVE_MEMFD, and that ordering IS the fix.
 *
 * NetBSD 10 added memfd_create(2), so meson's feature test finds it and
 * defines HAVE_MEMFD -- but a descriptor from NetBSD's memfd cannot be
 * mmapped by the process it is PASSED TO over SCM_RIGHTS. wayvnc hands
 * exactly such a descriptor to the compositor in
 * zwp_virtual_keyboard_v1.keymap; wlroots mmaps it, gets MAP_FAILED and
 * calls wl_client_post_no_memory(), which appears on the wire as
 *     wl_display#1: error 2: no memory
 * and tears the connection down the instant a VNC client connects. Remote
 * input never works and the session drops immediately.
 *
 * Everything else was ruled out by measurement rather than assumption:
 * shm_open, shm_unlink, ftruncate, write and mmap (MAP_PRIVATE and
 * MAP_SHARED) all succeed on NetBSD, and the 31140-byte keymap round-trips
 * cleanly through xkb_keymap_get_as_string() -> xkb_keymap_new_from_string().
 * The descriptor is the only remaining variable.
 *
 * A plain file, unlinked immediately, is as anonymous as a memfd and has no
 * such restriction when passed across a socket.
 */
#if defined(__NetBSD__)
	const char *dir = getenv("XDG_RUNTIME_DIR");
	if (!dir)
		dir = "/tmp";

	char path[256];
	snprintf(path, sizeof(path), "%s/wayvnc-shm-XXXXXX", dir);

	int fd = mkstemp(path);
	if (fd < 0)
		return -1;

	unlink(path);
	return fd;
#elif defined(HAVE_MEMFD)
	return memfd_create("wayvnc-shm", 0);
#elif defined(__FreeBSD__)
	// memfd_create added in FreeBSD 13, but SHM_ANON has been supported for ages
	return shm_open(SHM_ANON, O_RDWR | O_CREAT | O_EXCL, 0600);
#else
	int retries = 100;

	do {
		char name[] = "/wl_shm-XXXXXX";
		randname(name + sizeof(name) - 7);
		--retries;

		int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
		if (fd >= 0) {
			shm_unlink(name);
			return fd;
		}
	} while (retries > 0 && errno == EEXIST);

	return -1;
#endif
}

int shm_alloc_fd(size_t size)
{
	int fd = create_shm_file();
	if (fd < 0)
		return -1;

	int ret;
	do {
		ret = ftruncate(fd, size);
	} while (ret < 0 && errno == EINTR);

	if (ret < 0) {
		close(fd);
		return -1;
	}

	return fd;
}

/*
 * Copyright (c) 1996 by Internet Software Consortium.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND INTERNET SOFTWARE CONSORTIUM DISCLAIMS
 * ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL INTERNET SOFTWARE
 * CONSORTIUM BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 */

#if !defined(__rtems__)
#if !defined(LINT) && !defined(CODECENTER)
static char rcsid[] = "$Id: ns_netint.c,v 1.2 2001-09-27 12:01:53 chris Exp $";
#endif /* not lint */
#endif /* not rtems */

/* Import. */

#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <arpa/nameser.h>

u_int
ns_get16(const u_char *src) {
	u_int dst;

	NS_GET16(dst, src);
	return (dst);
}

u_long
ns_get32(const u_char *src) {
	u_long dst;

	NS_GET32(dst, src);
	return (dst);
}

void
ns_put16(u_int src, u_char *dst) {
	NS_PUT16(src, dst);
}

void
ns_put32(u_long src, u_char *dst) {
	NS_PUT32(src, dst);
}

/* SPDX-License-Identifier: BSD-2-Clause */
/* Exercise the actual xdr_sizeof implementation through its public callback. */
#include <rpc/types.h>
#include <rpc/xdr.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) return (FALSE); } while (0)

static bool_t
measure(XDR *xdrs, const void *data)
{
	const char bytes[] = "abc";
	char output[] = "xyz";
	long value = 17, result = 23;
	int32_t *inline_data;

	CHECK(data != NULL && xdrs->x_op == XDR_ENCODE);
	CHECK(xdrs->x_ops->x_control == NULL);
	CHECK(XDR_PUTLONG(xdrs, &value));
	CHECK(XDR_GETPOS(xdrs) == BYTES_PER_XDR_UNIT);
	CHECK(!XDR_GETLONG(xdrs, &result) && result == 23);
	CHECK(!XDR_GETBYTES(xdrs, output, 0));
	CHECK(!XDR_GETBYTES(xdrs, output, 3));
	CHECK(memcmp(output, "xyz", sizeof(output)) == 0);
	CHECK(XDR_GETPOS(xdrs) == BYTES_PER_XDR_UNIT);
	CHECK(XDR_PUTBYTES(xdrs, bytes, 3));
	CHECK(!XDR_SETPOS(xdrs, 0));
	CHECK(XDR_GETPOS(xdrs) == BYTES_PER_XDR_UNIT + 3);
	CHECK(XDR_INLINE(xdrs, 0) == NULL);
	inline_data = XDR_INLINE(xdrs, 8);
	CHECK(inline_data != NULL);
	inline_data[0] = 0;
	CHECK(XDR_INLINE(xdrs, 4) != NULL);
	return (TRUE);
}

static bool_t
reject(XDR *xdrs, const void *data)
{
	long value = 17;

	(void)data;
	(void)XDR_PUTLONG(xdrs, &value);
	return (FALSE);
}

int
main(void)
{
	char data[] = "input";

	if (xdr_sizeof(measure, data) != BYTES_PER_XDR_UNIT + 15)
		return (1);
	if (xdr_sizeof(reject, NULL) != 0)
		return (2);
	return (0);
}

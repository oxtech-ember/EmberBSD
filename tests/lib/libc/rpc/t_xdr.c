/* $NetBSD: t_xdr.c,v 1.1 2011/01/08 06:59:37 pgoyette Exp $ */

/*
 * Copyright (c) 2008 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Ben Harris.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*-
 * Copyright (c) 2001 Ben Harris
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__COPYRIGHT("@(#) Copyright (c) 2008\
 The NetBSD Foundation, inc. All rights reserved.");
__RCSID("$NetBSD: t_xdr.c,v 1.1 2011/01/08 06:59:37 pgoyette Exp $");

#include <rpc/types.h>
#include <rpc/xdr.h>

#include <string.h>

#include <atf-c.h>

#include "h_testbits.h"

char xdrdata[] = {
	0x3f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* double 1.0 */
	0x00, 0x00, 0x00, 0x01, /* enum smallenum SE_ONE */
	0xff, 0xff, 0xfb, 0x2e, /* enum medenum ME_NEG */
	0x00, 0x12, 0xd6, 0x87, /* enum bigenum BE_LOTS */
};

ATF_TC(xdr);
ATF_TC_HEAD(xdr, tc)
{
	atf_tc_set_md_var(tc, "descr",
		"Checks encoding/decoding of doubles and enumerations");
}
ATF_TC_BODY(xdr, tc)
{
	XDR x;
	double d;
	smallenum s;
	medenum m;
	bigenum b;
	char newdata[sizeof(xdrdata)];

	xdrmem_create(&x, xdrdata, sizeof(xdrdata), XDR_DECODE);

	ATF_REQUIRE_MSG(xdr_double(&x, &d), "xdr_double DECODE failed");
	ATF_REQUIRE_EQ_MSG(d, 1.0, "double 1.0 decoded as %g", d);

	ATF_REQUIRE_MSG(xdr_smallenum(&x, &s), "xdr_smallenum DECODE failed");
	ATF_REQUIRE_EQ_MSG(s, SE_ONE, "SE_ONE decoded as %d", s);

	ATF_REQUIRE_MSG(xdr_medenum(&x, &m), "xdr_medenum DECODE failed");
	ATF_REQUIRE_EQ_MSG(m, ME_NEG, "ME_NEG decoded as %d", m);

	ATF_REQUIRE_MSG(xdr_bigenum(&x, &b), "xdr_bigenum DECODE failed");
	ATF_REQUIRE_EQ_MSG(b, BE_LOTS, "BE_LOTS decoded as %d", b);

	xdr_destroy(&x);


	xdrmem_create(&x, newdata, sizeof(newdata), XDR_ENCODE);

	ATF_REQUIRE_MSG(xdr_double(&x, &d), "xdr_double ENCODE failed");
	ATF_REQUIRE_MSG(xdr_smallenum(&x, &s), "xdr_smallenum ENCODE failed");
	ATF_REQUIRE_MSG(xdr_medenum(&x, &m), "xdr_medenum ENCODE failed");
	ATF_REQUIRE_MSG(xdr_bigenum(&x, &b), "xdr_bigenum ENCODE failed");
	ATF_REQUIRE_MSG(memcmp(newdata, xdrdata, sizeof(xdrdata)) == 0,
		"xdr ENCODE result differs");

	xdr_destroy(&x);
}

ATF_TC(xdr_void_callback);
ATF_TC_HEAD(xdr_void_callback, tc)
{
	atf_tc_set_md_var(tc, "descr",
		"Checks xdr_void has the generic XDR callback signature");
}
ATF_TC_BODY(xdr_void_callback, tc)
{
	xdrproc_t proc = xdr_void;
	XDR x;
	char value = 0;

	x.x_op = XDR_ENCODE;
	ATF_REQUIRE_MSG(proc(&x, &value), "xdr_void ENCODE failed");
	x.x_op = XDR_DECODE;
	ATF_REQUIRE_MSG(proc(&x, &value), "xdr_void DECODE failed");
	x.x_op = XDR_FREE;
	ATF_REQUIRE_MSG(proc(&x, &value), "xdr_void FREE failed");
	ATF_REQUIRE_MSG(xdr_void(NULL, NULL), "xdr_void NULL call failed");
}

static bool_t
sizeof_probe(XDR *x, const void *data)
{
	long value = 123;
	char bytes[] = "abc";

	(void)data;
	ATF_REQUIRE(!XDR_CONTROL(x, 0, NULL));
	ATF_REQUIRE(!XDR_GETLONG(x, &value));
	ATF_REQUIRE_EQ(value, 123);
	ATF_REQUIRE(!XDR_GETBYTES(x, bytes, 3));
	ATF_REQUIRE(memcmp(bytes, "abc", sizeof(bytes)) == 0);
	ATF_REQUIRE(XDR_PUTLONG(x, &value));
	ATF_REQUIRE(XDR_PUTBYTES(x, bytes, 3));
	return TRUE;
}

ATF_TC(xdr_sizeof_ops);
ATF_TC_HEAD(xdr_sizeof_ops, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks size stream operations");
}
ATF_TC_BODY(xdr_sizeof_ops, tc)
{
	ATF_REQUIRE_EQ(xdr_sizeof(sizeof_probe, NULL), BYTES_PER_XDR_UNIT + 3);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, xdr);
	ATF_TP_ADD_TC(tp, xdr_void_callback);
	ATF_TP_ADD_TC(tp, xdr_sizeof_ops);

	return atf_no_error();
}

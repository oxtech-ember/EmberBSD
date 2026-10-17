/* SPDX-License-Identifier: BSD-2-Clause */
/* Original native RTL8822CE net ingress contracts, AI-assisted. */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "rtw88_net_policy.h"

static unsigned checks, failures;
#define CHECK(e) do { checks++; if (!(e)) { failures++; \
 fprintf(stderr,"line %u: %s\n",__LINE__,#e); } } while (0)
int
main(void)
{
 struct rtw88_net_policy p = {0};
 unsigned char frame[64] = {0x08,0x02};
 uint64_t a,b;
 /* Removing epoch/acceptance checks admits stale DMA after a rescan. */
 CHECK(rtw88_net_rx_admit(&p,0,frame,24,0,0)==ENXIO);
 a=rtw88_net_request(&p);
 CHECK(a != 0);
 CHECK(rtw88_net_commit(&p,a,0,1)==0);
 CHECK(rtw88_net_rx_admit(&p,a,frame,24,0,0)==0);
 CHECK(p.queued==1);
 rtw88_net_rx_release(&p);
 CHECK(p.queued==0);
 rtw88_net_rx_release(&p);
 CHECK(p.queued==0);
 b=rtw88_net_request(&p);
 CHECK(b!=a);
 CHECK(rtw88_net_commit(&p,a,0,1)==ECANCELED);
 CHECK(rtw88_net_rx_admit(&p,b,frame,24,0,0)==ENXIO);
 CHECK(rtw88_net_commit(&p,b,EIO,1)==EIO);
 CHECK(!p.accepting);
 CHECK(rtw88_net_commit(&p,b,0,1)==0);
 CHECK(rtw88_net_rx_admit(&p,a,frame,24,0,0)==ECANCELED);
 /* Hardware-decrypted input must not masquerade as software ciphertext. */
 frame[1]=0x42;
 CHECK(rtw88_net_rx_admit(&p,b,frame,40,0,3)==EACCES);
 CHECK(rtw88_net_rx_admit(&p,b,frame,40,8,3)==0);
 rtw88_net_rx_release(&p);
 /* Bounds, version, errors and four-address frames never reach the stack. */
 CHECK(rtw88_net_rx_admit(&p,b,NULL,40,8,3)==EINVAL);
 CHECK(rtw88_net_rx_admit(&p,b,frame,23,8,3)==EMSGSIZE);
 CHECK(rtw88_net_rx_admit(&p,b,frame,4097,8,3)==EMSGSIZE);
 CHECK(rtw88_net_rx_admit(&p,b,frame,40,1,3)==EBADMSG);
 CHECK(rtw88_net_rx_admit(&p,b,frame,40,2,3)==EBADMSG);
 CHECK(rtw88_net_rx_admit(&p,b,frame,40,16,0)==EBADMSG);
 frame[0]=0x09;
 CHECK(rtw88_net_rx_admit(&p,b,frame,40,8,3)==EPROTONOSUPPORT);
 frame[0]=0x08; frame[1]=3;
 CHECK(rtw88_net_rx_admit(&p,b,frame,40,0,0)==EPROTONOSUPPORT);
 frame[1]=2;
 /* The 65th queued mbuf must not grow the ingress backlog. */
 for(unsigned i=0;i<64;i++) CHECK(rtw88_net_rx_admit(&p,b,frame,24,0,0)==0);
 CHECK(rtw88_net_rx_admit(&p,b,frame,24,0,0)==ENOBUFS);
 CHECK(p.queued==64);
 rtw88_net_close(&p);
 CHECK(!p.accepting && p.dying);
 CHECK(rtw88_net_commit(&p,b,0,1)==ECANCELED);
 CHECK(rtw88_net_rx_admit(&p,p.epoch,frame,24,0,0)==ENXIO);
 printf("rtw88 net policy: %u assertions, %u failures\n",checks,failures);
 return failures!=0;
}

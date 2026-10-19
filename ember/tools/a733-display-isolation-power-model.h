/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD, AI-assisted. PCK calls have separate actual-source tests. */
static const void *isolation_owner;
static bool isolation_retained;
static unsigned isolation_reads, isolation_fail_read;
static int isolation_reserve_error, isolation_retain_error, isolation_release_error;
static void isolation_model_reset(void) {
 isolation_owner=NULL;isolation_retained=false;isolation_reads=isolation_fail_read=0;
 isolation_reserve_error=isolation_retain_error=isolation_release_error=0;
}
int sun60i_a733_pck_display_isolation_reserve(int node,const void *owner) {
 assert(node==17&&owner==display_power_owner&&!display_power_retained);
 if(isolation_reserve_error)return isolation_reserve_error;
 if(isolation_owner)return EBUSY;isolation_owner=owner;return 0;
}
int sun60i_a733_pck_display_isolation_release(int node,const void *owner) {
 assert(node==17&&owner==isolation_owner&&!display_power_retained&&!isolation_retained);
 if(isolation_release_error)return isolation_release_error;
 isolation_owner=NULL;return 0;
}
int sun60i_a733_pck_display_isolation_retain(int node,const void *owner) {
 assert(node==17&&owner==isolation_owner&&display_power_retained&&!isolation_retained);
 if(isolation_retain_error)return isolation_retain_error;
 isolation_retained=true;return 0;
}
int sun60i_a733_pck_display_isolation_snapshot(int node,const void *owner,uint32_t out[2][3]) {
 assert(node==17&&owner==isolation_owner);
 if(++isolation_reads==isolation_fail_read)return EFAULT;
 memset(out,0,6*sizeof(uint32_t));out[0][0]=out[0][2]=out[1][0]=out[1][2]=8;
 return 0;
}

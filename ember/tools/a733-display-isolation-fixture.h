/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD, AI-assisted. Fixed inactive-peer admission controls. */

static void isolation_setup(void) {
 display_memory_fixture();
 CHECK(sun60i_a733_ccu_display_reserve(CLOCK(A733_CLK_DE0),17,&display_cookie)==0);
 CHECK(sun60i_a733_ccu_display_memory_reserve(CLOCK(A733_CLK_DE0),&display_cookie)==0);
 reads=0;
}
static void test_display_isolation(void) {
 static const bus_size_t regs[]={0xa20,0xa24,0xa40,0xa44,0xa60,0xa64,0xa6c,0xa88,0xa8c,0x5e4};
 static const uint32_t masks[]={0x80000000,0x10001,0x80000000,0x10001,0x80000000,0x80000000,0x10001,0x80000000,0x40004,0x40000};
 uint32_t out[2][A733_DISPLAY_NISOLATION_REGS],saved[2][A733_DISPLAY_NISOLATION_REGS];
 const int wrong=71;bool retained,handled;
 struct sunxi_ccu_reset reset_desc={0};
 display_memory_fixture();memset(saved,0xa5,sizeof(saved));memcpy(out,saved,sizeof(out));
 CHECK(sun60i_a733_ccu_display_isolation_reserve(CLOCK(A733_CLK_DE0),&display_cookie)==EINVAL);
 CHECK(sun60i_a733_ccu_display_isolation_state(CLOCK(A733_CLK_DE0),&display_cookie,out)==EINVAL);
 CHECK(reads==0&&writes==0&&memcmp(out,saved,sizeof(out))==0);
 isolation_setup();
 CHECK(sun60i_a733_ccu_display_isolation_reserve(CLOCK(A733_CLK_DE0),&wrong)==EINVAL&&reads==0);
 handled=false;CHECK(sun60i_a733_gpu_guard_enter(&state,CLOCK(A733_CLK_BUS_PWM0),NULL,SUNXI_CCU_ENABLE,&handled)==0&&!handled);
 CHECK(sun60i_a733_ccu_display_isolation_reserve(CLOCK(A733_CLK_DE0),&display_cookie)==EBUSY&&reads==0);
 sun60i_a733_gpu_guard_exit(&state);
 CHECK(sun60i_a733_ccu_display_isolation_reserve(CLOCK(A733_CLK_DE0),&display_cookie)==0);
 CHECK(reads==20&&writes==0&&isolation_owner==&display_cookie);
 CHECK(sun60i_a733_ccu_display_isolation_reserve(CLOCK(A733_CLK_DE0),&display_cookie)==EBUSY);
 for(unsigned i=0;i<10;i++) {
  CHECK(sun60i_a733_display_isolation_regs[i].reg==regs[i]);
  CHECK(sun60i_a733_display_isolation_regs[i].mask==masks[i]);
  for(unsigned b=0;b<32;b++)if(masks[i]&__BIT(b)) {
   CHECK(sunxi_ccu_update_bits(&state,regs[i],__BIT(b),__BIT(b))==EBUSY);
   reset_desc.reg=regs[i];reset_desc.mask=__BIT(b);handled=false;
   CHECK(sun60i_a733_gpu_guard_enter(&state,NULL,&reset_desc,SUNXI_CCU_DEASSERT,&handled)==EBUSY&&handled);
  }
 }
 CHECK(sunxi_ccu_update_bits(&state,0xa8c,__BIT(16)|__BIT(0),__BIT(16)|__BIT(0))==0);
 CHECK(sunxi_ccu_update_bits(&state,0x5e4,__BIT(11),__BIT(11))==0);
 CHECK(sun60i_a733_ccu_display_isolation_state(CLOCK(A733_CLK_DE0),&display_cookie,out)==0);
 CHECK(out[0][8]==0x10001&&out[1][9]==0x800);
 CHECK(sun60i_a733_ccu_display_release(CLOCK(A733_CLK_DE0),&display_cookie)==0);
 CHECK(isolation_owner==NULL&&!sun60i_display_lease.isolation_claimed);
 for(unsigned f=1;f<=20;f++) {
  isolation_setup();fail_peek=f;
  CHECK(sun60i_a733_ccu_display_isolation_reserve(CLOCK(A733_CLK_DE0),&display_cookie)==EFAULT);
  CHECK(reads==f&&writes==0&&!sun60i_display_lease.isolation_claimed&&isolation_owner==NULL);
 }
 for(unsigned i=0;i<10;i++)for(unsigned b=0;b<32;b++)if(masks[i]&__BIT(b)) {
  isolation_setup();REG(regs[i])=__BIT(b);
  CHECK(sun60i_a733_ccu_display_isolation_reserve(CLOCK(A733_CLK_DE0),&display_cookie)==EBUSY);
  CHECK(writes==0&&!sun60i_display_lease.isolation_claimed&&isolation_owner==NULL);
 }
 for(unsigned i=0;i<10;i++) {
  isolation_setup();unstable_reg=regs[i];unstable_read=11+i;unstable_mask=masks[i]&-masks[i];
  CHECK(sun60i_a733_ccu_display_isolation_reserve(CLOCK(A733_CLK_DE0),&display_cookie)==EBUSY);
  CHECK(!sun60i_display_lease.isolation_claimed&&isolation_owner==NULL&&writes==0);
 }
 for(unsigned f=1;f<=2;f++) {
  isolation_setup();isolation_fail_read=f;
  CHECK(sun60i_a733_ccu_display_isolation_reserve(CLOCK(A733_CLK_DE0),&display_cookie)==EFAULT);
  CHECK(!sun60i_display_lease.isolation_claimed&&isolation_owner==NULL&&writes==0);
 }
 isolation_setup();isolation_reserve_error=EBUSY;
 CHECK(sun60i_a733_ccu_display_isolation_reserve(CLOCK(A733_CLK_DE0),&display_cookie)==EBUSY&&reads==0);
 isolation_setup();fail_peek=1;isolation_release_error=EIO;
 CHECK(sun60i_a733_ccu_display_isolation_reserve(CLOCK(A733_CLK_DE0),&display_cookie)==EFAULT);
 CHECK(sun60i_display_lease.isolation_claimed&&isolation_owner==&display_cookie&&sun60i_display_lease.fault==EFAULT);
 CHECK(sun60i_a733_ccu_display_release(CLOCK(A733_CLK_DE0),&display_cookie)==EBUSY&&writes==0);
 isolation_setup();CHECK(sun60i_a733_ccu_display_isolation_reserve(CLOCK(A733_CLK_DE0),&display_cookie)==0);
 isolation_retain_error=EIO;retained=false;
 CHECK(sun60i_a733_ccu_display_begin_de0(CLOCK(A733_CLK_DE0),&display_cookie,&retained)==EIO);
 CHECK(retained&&writes==0&&sun60i_display_lease.fault==EIO&&isolation_owner==&display_cookie);
 CHECK(sun60i_a733_ccu_display_release(CLOCK(A733_CLK_DE0),&display_cookie)==EBUSY);
 for(unsigned i=0;i<10;i++) {
  isolation_setup();CHECK(sun60i_a733_ccu_display_isolation_reserve(CLOCK(A733_CLK_DE0),&display_cookie)==0);
  REG(regs[i])|=masks[i]&-masks[i];retained=false;
  CHECK(sun60i_a733_ccu_display_begin_de0(CLOCK(A733_CLK_DE0),&display_cookie,&retained)==EBUSY);
  CHECK(retained&&isolation_retained&&writes==0&&sun60i_display_lease.fault==EBUSY);
 }
 for(unsigned i=0;i<10;i++) {
  isolation_setup();CHECK(sun60i_a733_ccu_display_isolation_reserve(CLOCK(A733_CLK_DE0),&display_cookie)==0);
  retained=false;CHECK(sun60i_a733_ccu_display_begin_de0(CLOCK(A733_CLK_DE0),&display_cookie,&retained)==0);
  CHECK(retained&&isolation_retained&&writes==4);
  CHECK(sun60i_a733_ccu_display_ready(CLOCK(A733_CLK_DE0),&display_cookie)==0);
  REG(regs[i])|=masks[i]&-masks[i];memcpy(out,saved,sizeof(out));
  CHECK(sun60i_a733_ccu_display_isolation_state(CLOCK(A733_CLK_DE0),&display_cookie,out)==EBUSY);
  CHECK(memcmp(out,saved,sizeof(out))==0&&sun60i_display_lease.fault==EBUSY&&writes==4);
  CHECK(sun60i_a733_ccu_display_release(CLOCK(A733_CLK_DE0),&display_cookie)==EBUSY);
 }

 /* Only the retained same-cookie peer claim can request ON. */
 isolation_setup();
 CHECK(sun60i_a733_ccu_display_begin_peer_access(CLOCK(A733_CLK_DE0),&display_cookie)==EINVAL&&peer_on_calls==0);
 CHECK(sun60i_a733_ccu_display_isolation_reserve(CLOCK(A733_CLK_DE0),&display_cookie)==0);
 CHECK(sun60i_a733_ccu_display_begin_peer_access(CLOCK(A733_CLK_DE0),&display_cookie)==EBUSY&&peer_on_calls==0);
 retained=false;CHECK(sun60i_a733_ccu_display_begin_de0(CLOCK(A733_CLK_DE0),&display_cookie,&retained)==0);
 CHECK(sun60i_a733_ccu_display_begin_peer_access(CLOCK(A733_CLK_DE0),&wrong)==EINVAL&&peer_on_calls==0);
 CHECK(sun60i_display_lease.fault==0);
 CHECK(sun60i_a733_ccu_display_begin_peer_access(CLOCK(A733_CLK_DE0),&display_cookie)==0&&peer_on_calls==1&&writes==4);
 for(unsigned test=0;test<3;test++) {
  isolation_setup();CHECK(sun60i_a733_ccu_display_isolation_reserve(CLOCK(A733_CLK_DE0),&display_cookie)==0);
  retained=false;CHECK(sun60i_a733_ccu_display_begin_de0(CLOCK(A733_CLK_DE0),&display_cookie,&retained)==0);
  if(test==0)REG(0xa88)=0x80000000;
  if(test==1)peer_on_error=ETIMEDOUT;
  if(test==2)peer_on_post_fault=true;
  CHECK(sun60i_a733_ccu_display_begin_peer_access(CLOCK(A733_CLK_DE0),&display_cookie)!=0);
  CHECK(peer_on_calls==(test==0?0U:1U)&&sun60i_display_lease.fault!=0);
  CHECK(sun60i_display_lease.pck_retained&&isolation_retained&&writes==4);
  unsigned saved_calls=peer_on_calls;
  CHECK(sun60i_a733_ccu_display_begin_peer_access(CLOCK(A733_CLK_DE0),&display_cookie)==EBUSY&&peer_on_calls==saved_calls);
  CHECK(sun60i_a733_ccu_display_release(CLOCK(A733_CLK_DE0),&display_cookie)==EBUSY);
 }

}

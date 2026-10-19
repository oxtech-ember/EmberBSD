/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD, AI-assisted. Actual PCK domain2 lease controls. */

static void isolation_power_setup(const void *owner,unsigned mode) {
 borrow_fixture();
 registers[0x2000/4]=registers[0x2008/4]=mode;registers[0x2004/4]=0;
 CHECK(sun60i_a733_pck_display_reserve(1,owner)==0);peeks=0;
}
static void test_display_isolation_power(void) {
 const int owner=73,other=79;unsigned cases=0;
 uint32_t out[2][3],saved[2][3],ids[2]={0,swap32(2)};
 for(unsigned mode=0;mode<=8;mode+=8) {
  isolation_power_setup(&owner,mode);memset(saved,0xa5,sizeof(saved));memcpy(out,saved,sizeof(out));
  CHECK(sun60i_a733_pck_display_isolation_reserve(1,&other)==EINVAL&&peeks==0);
  CHECK(sun60i_a733_pck_display_isolation_snapshot(1,&owner,out)==EINVAL&&peeks==0);
  CHECK(memcmp(saved,out,sizeof(out))==0);
  CHECK(sun60i_a733_pck_display_isolation_reserve(1,&owner)==0&&peeks==6);
  CHECK(sun60i_a733_pck_display_isolation_snapshot(1,&owner,out)==0);
  CHECK(out[0][0]==mode&&out[0][1]==0&&out[1][2]==mode);
  CHECK(sun60i_pck600_set(&sc,ids,true)==EBUSY&&writes==0);
  CHECK(sun60i_pck600_set(&sc,ids,false)==EBUSY&&writes==0);
  CHECK(sun60i_a733_pck_display_isolation_retain(1,&owner)==EBUSY);
  CHECK(sun60i_a733_pck_display_isolation_release(1,&owner)==0);
  CHECK(sc.sc_lease[2].owner==NULL);
  CHECK(sun60i_a733_pck_display_isolation_reserve(1,&owner)==0);
  CHECK(sun60i_a733_pck_display_retain(1,&owner)==0);
  CHECK(sun60i_a733_pck_display_isolation_release(1,&owner)==EBUSY);
  CHECK(sun60i_a733_pck_display_isolation_retain(1,&owner)==0);
  CHECK(sc.sc_lease[2].retained&&writes==0);
  CHECK(sun60i_a733_pck_display_isolation_release(1,&owner)==EBUSY);
  CHECK(sun60i_a733_pck_display_isolation_snapshot(1,&owner,out)==0);
  registers[0x2000/4]=registers[0x2008/4]=mode==0?8:0;
  memcpy(out,saved,sizeof(out));
  CHECK(sun60i_a733_pck_display_isolation_snapshot(1,&owner,out)==EBUSY);
  CHECK(memcmp(saved,out,sizeof(out))==0&&sc.sc_lease[2].retained&&writes==0);cases++;
 }
 for(unsigned f=1;f<=6;f++) {
  isolation_power_setup(&owner,8);fail_peek=f;
  CHECK(sun60i_a733_pck_display_isolation_reserve(1,&owner)==EFAULT);
  CHECK(peeks==f&&writes==0&&sc.sc_lease[2].owner==NULL);cases++;
 }
 for(unsigned test=0;test<7;test++) {
  isolation_power_setup(&owner,8);
  switch(test) {
  case 0:registers[0x2004/4]=1;break;
  case 1:registers[0x2000/4]|=PCK600_DYNAMIC;break;
  case 2:registers[0x2008/4]=0;break;
  case 3:registers[0x2000/4]=registers[0x2008/4]=4;break;
  case 4:sc.sc_failed[2]=true;break;
  case 5:change_peek=6;break;
  case 6:sc.sc_lease[2].owner=&other;break;
  }
  CHECK(sun60i_a733_pck_display_isolation_reserve(1,&owner)!=0);
  CHECK(writes==0&&sc.sc_lease[2].owner!=(const void *)&owner);cases++;
 }
 for(unsigned f=1;f<=6;f++) {
  isolation_power_setup(&owner,8);
  CHECK(sun60i_a733_pck_display_isolation_reserve(1,&owner)==0);
  CHECK(sun60i_a733_pck_display_retain(1,&owner)==0);
  peeks=0;fail_peek=f;
  CHECK(sun60i_a733_pck_display_isolation_retain(1,&owner)==EFAULT);
  CHECK(!sc.sc_lease[2].retained&&sc.sc_lease[2].owner==&owner);
  CHECK(sun60i_a733_pck_display_isolation_release(1,&owner)==EBUSY&&writes==0);cases++;
 }
 printf("Actual PCK peer isolation: %u always-active scenarios passed\n",cases);
}

static void peer_power_setup(const void *owner,unsigned mode) {
 isolation_power_setup(owner,mode);
 CHECK(sun60i_a733_pck_display_isolation_reserve(1,owner)==0);
 CHECK(sun60i_a733_pck_display_retain(1,owner)==0);
 CHECK(sun60i_a733_pck_display_isolation_retain(1,owner)==0);
 peer_on_test=lease_test=true;peeks=0;complete_after=3;
}
static void test_display_peer_power(void) {
 const int owner=101,other=103;unsigned scenarios=0,total_peeks;
 uint32_t before[2][3],after[2][3];
 for(unsigned mode=0;mode<=8;mode+=8) {
  peer_power_setup(&owner,mode);
  CHECK(sun60i_a733_pck_display_peer_on(1,&other)==EBUSY&&peeks==0&&pokes==0);
  CHECK(sun60i_a733_pck_display_peer_on(1,NULL)==EINVAL&&peeks==0);
  CHECK(sun60i_a733_pck_display_peer_on(7,&owner)==ENXIO&&peeks==0);
  CHECK(sun60i_a733_pck_display_peer_on(1,&owner)==0);
  CHECK(pokes==(mode==0?6U:0U)&&writes==pokes&&!sc.sc_failed[2]);
  CHECK(sc.sc_lease[2].before[0][0]==8&&sc.sc_lease[2].before[1][2]==8);
  CHECK(sun60i_a733_pck_display_isolation_snapshot(1,&owner,after)==0);
  CHECK(sun60i_a733_pck_display_peer_on(1,&owner)==0);
  CHECK(pokes==(mode==0?6U:0U));
  CHECK(sun60i_a733_pck_display_isolation_release(1,&owner)==EBUSY);
  scenarios++;
 }
 for(unsigned slot=1;slot<=10;slot++)if(slot==1||slot==2||slot==10) {
  peer_power_setup(&owner,0);sc.sc_lease[slot].retained=false;
  CHECK(sun60i_a733_pck_display_peer_on(1,&owner)==EBUSY&&pokes==0);scenarios++;
  peer_power_setup(&owner,0);sc.sc_lease[slot].owner=&other;
  CHECK(sun60i_a733_pck_display_peer_on(1,&owner)==EBUSY&&pokes==0);scenarios++;
 }
 /* Every OFF policy/status bit outside MODE must refuse a write. */
 for(unsigned bit=4;bit<32;bit++) {
  peer_power_setup(&owner,0);
  registers[0x2000/4]=registers[0x2008/4]=1U<<bit;
  for(unsigned sample=0;sample<2;sample++)
   sc.sc_lease[2].before[sample][0]=sc.sc_lease[2].before[sample][2]=1U<<bit;
  CHECK(sun60i_a733_pck_display_peer_on(1,&owner)!=0&&pokes==0);scenarios++;
 }
 for(unsigned test=0;test<5;test++) {
  peer_power_setup(&owner,0);
  switch(test) {
  case 0:registers[0x2004/4]=1;break;
  case 1:registers[0x1008/4]=0;break;
  case 2:registers[0xa004/4]=1;break;
  case 3:registers[0x2000/4]=8;break;
  case 4:sc.sc_failed[2]=true;break;
  }
  CHECK(sun60i_a733_pck_display_peer_on(1,&owner)!=0&&pokes==0);scenarios++;
 }
 peer_power_setup(&owner,0);
 CHECK(sun60i_a733_pck_display_peer_on(1,&owner)==0);total_peeks=peeks;
 /* Cover every actual read, including the final complete ON capture. */
 for(unsigned fault=1;fault<=total_peeks;fault++) {
  peer_power_setup(&owner,0);memcpy(before,sc.sc_lease[2].before,sizeof(before));fail_peek=fault;
  CHECK(sun60i_a733_pck_display_peer_on(1,&owner)==EFAULT);
  CHECK(memcmp(before,sc.sc_lease[2].before,sizeof(before))==0);
  CHECK(sc.sc_lease[2].retained&&sc.sc_failed[2]==(pokes!=0));
  unsigned saved_pokes=pokes;
  if(pokes)CHECK(sun60i_a733_pck_display_peer_on(1,&owner)==EIO&&pokes==saved_pokes);
  CHECK(sun60i_a733_pck_display_isolation_release(1,&owner)==EBUSY);scenarios++;
 }
 static const bus_size_t written[]={0x2170,0x2174,0x2c00,0x2c04,0x2c10,0x2000};
 for(unsigned n=1;n<=6;n++)for(unsigned kind=0;kind<2;kind++) {
  peer_power_setup(&owner,0);memcpy(before,sc.sc_lease[2].before,sizeof(before));
  if(kind==0)fail_poke=n;else fail_write=written[n-1];
  CHECK(sun60i_a733_pck_display_peer_on(1,&owner)==(kind==0?EFAULT:EIO));
  CHECK(pokes==n&&sc.sc_failed[2]&&memcmp(before,sc.sc_lease[2].before,sizeof(before))==0);
  CHECK(sun60i_a733_pck_display_peer_on(1,&owner)==EIO&&pokes==n);scenarios++;
 }
 for(unsigned test=0;test<5;test++) {
  peer_power_setup(&owner,0);memcpy(before,sc.sc_lease[2].before,sizeof(before));
  if(test==0)complete_after=0;
  if(test==1){complete_after=0;deny_after=2;}
  if(test>=2){change_after_poke=5;peer_change_mask=1;peer_change_reg=test==2?0x2004:(test==3?0x1004:0x2000);}
  CHECK(sun60i_a733_pck_display_peer_on(1,&owner)!=0);
  CHECK(sc.sc_failed[2]&&memcmp(before,sc.sc_lease[2].before,sizeof(before))==0);
  CHECK(pokes==(test<2?6U:5U));
  if(test==0)CHECK(delay_us==PCK600_TIMEOUT_US);
  CHECK(delay_us<=PCK600_TIMEOUT_US);scenarios++;
 }
 printf("Actual PCK peer ON: %u always-active scenarios passed\n",scenarios);
 peer_on_test=lease_test=false;
}

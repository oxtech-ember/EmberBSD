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

/* Standalone target-format numerical contracts. */
#include <assert.h>
#include <stdint.h>
#include "../../common/lint_fp.h"
static void bits(lint_fp_t v, uint64_t hi, uint64_t lo) { assert(v.hi==hi && v.lo==lo); }
int main(void) {
 unsigned st; uint64_t u; int64_t i;
 lint_fp_t one=lint_fp_from_i64(1,LINT_FP128,&st), two=lint_fp_from_i64(2,LINT_FP128,&st);
 bits(lint_fp_binary('+',one,two,LINT_FP128,&st),0x4000800000000000ULL,0); assert(st==0);
 bits(lint_fp_binary('+',(lint_fp_t){0x4063000000000000ULL,0},one,LINT_FP128,&st),0x4063000000000000ULL,0x1000);
 lint_fp_t u63=lint_fp_from_u64(UINT64_C(1)<<63,LINT_FP128,&st); bits(u63,0x403e000000000000ULL,0); assert(lint_fp_to_u64(u63,&u)&&u==(UINT64_C(1)<<63)); assert(!lint_fp_to_i64(u63,&i));
 lint_fp_t umax=lint_fp_from_u64(UINT64_MAX,LINT_FP128,&st); assert(lint_fp_to_u64(umax,&u)&&u==UINT64_MAX); assert(!lint_fp_to_u64((lint_fp_t){0x403f000000000000ULL,0},&u));
 assert(lint_fp_to_i64(lint_fp_from_i64(INT64_MIN,LINT_FP128,&st),&i)&&i==INT64_MIN);
 bits(lint_fp_binary('/',one,lint_fp_from_i64(3,LINT_FP128,&st),LINT_FP128,&st),0x3ffd555555555555ULL,0x5555555555555555ULL); assert(st&LINT_FP_INEXACT);
 bits(lint_fp_round((lint_fp_t){0x3fff000000000000ULL,0x10000000000ULL},LINT_FP32,&st),0x3fff000000000000ULL,0); assert(st&LINT_FP_INEXACT);
 lint_fp_t ulp80={0x3fc0000000000000ULL,0};
 bits(lint_fp_binary('+',one,ulp80,LINT_FP80,&st),0x3fff000000000000ULL,0x2000000000000ULL); assert(st==0);
 bits(lint_fp_binary('+',one,ulp80,LINT_FP64,&st),0x3fff000000000000ULL,0); assert(st&LINT_FP_INEXACT);
 lint_fp_t z=lint_fp_binary('/',one,(lint_fp_t){0,0},LINT_FP128,&st); bits(z,0x7fff000000000000ULL,0); assert(st&LINT_FP_DIVZERO);
 assert(lint_fp_to_u64((lint_fp_t){0xbffe000000000000ULL,0},&u)&&u==0);
 assert(lint_fp_compare((lint_fp_t){0,0},(lint_fp_t){0x8000000000000000ULL,0})==0); assert(lint_fp_compare((lint_fp_t){0x7fff800000000000ULL,0},one)==2);
 return 0; }

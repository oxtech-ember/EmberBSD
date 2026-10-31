#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>

static char output[4096];
void espwifi_os_vlog(const char *fmt, va_list ap)
{
 size_t n=strlen(output);
 vsnprintf(output+n,sizeof(output)-n,fmt,ap);
}
void espwifi_os_log(const char *fmt,...)
{
 va_list ap;va_start(ap,fmt);espwifi_os_vlog(fmt,ap);va_end(ap);
}
void wpa_printf(int,const char *,...);
void wpa_hexdump(int,const char *,const void *,size_t);
void wpa_hexdump_key(int,const char *,const void *,size_t);
int main(void)
{
 unsigned char data[]={0xaa,0xbb};
 wpa_printf(3,"test %d",7);assert(strstr(output,"test 7"));
 output[0]=0;wpa_printf(2,"hidden");assert(output[0]==0);
 wpa_hexdump(3,"data",data,sizeof(data));assert(strstr(output,"aa")&&strstr(output,"bb"));
 output[0]=0;wpa_hexdump_key(3,"key",data,sizeof(data));assert(!strstr(output,"aa")&&!strstr(output,"bb"));assert(strstr(output,"redacted"));
 return 0;
}

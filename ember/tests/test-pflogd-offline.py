#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Offline pflogd record and filter control tests with mocked pcap/ioctl."""
import argparse
import pathlib,re,subprocess,tempfile
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cc', required=True, help='selected native C compiler')
args = parser.parse_args()
root = pathlib.Path(__file__).resolve().parents[2]
s=(root/'dist/pf/sbin/pflogd/pflogd.c').read_text()
def function(name):
 m=re.search(r'(?m)^(?:int|void)\n'+name+r'\([^;]*?\)\n\{',s); assert m,name
 end=s.index('\n}\n',m.end())+2
 return s[m.start():end]

pre=r'''#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
typedef unsigned char u_char;
#define LOG_WARNING 1
#define LOG_NOTICE 2
#define LOG_ERR 3
#define PCAP_VERSION_MAJOR 2
#define PCAP_VERSION_MINOR 4
#define PFLOGD_MAXSNAPLEN 65535
#define PCAP_OPT_FIL 1
#define BIOCSETF 7
struct pcap_sf_pkthdr { struct { uint32_t tv_sec,tv_usec; } ts; uint32_t caplen,len; };
struct pcap_file_header { uint32_t magic; uint16_t version_major,version_minor; int32_t thiszone; uint32_t sigfigs,snaplen,linktype; };
struct pcap_pkthdr { struct timeval ts; uint32_t caplen,len; };
typedef struct { int x; } pcap_t; struct bpf_program { int x; };
pcap_t live,dead,*hpcap=&live; FILE *dpcap; char *filter="ok", *filename="mock";
uint32_t cur_snaplen=4,snaplen=4; int suspended,gotsig_close,packets_dropped; char storage[1024],*buffer=storage,*bufpos=storage; int buflen=1024,bufleft=1024,bufpkt; int fail_open,fail_compile,fail_ioctl,fail_ipc,moved,closed,freed,compiled_length; char path[256];
void logmsg(int x,const char *f,...){(void)x;(void)f;} void set_suspended(int x){suspended=x;}
int pcap_datalink(pcap_t *x){(void)x;return 1;} pcap_t *pcap_open_dead(int d,int n){(void)d;compiled_length=n;return fail_open?NULL:&dead;} int pcap_compile(pcap_t *p,struct bpf_program *b,const char *f,int o,unsigned n){(void)p;(void)b;(void)f;(void)o;(void)n;return fail_compile?-1:0;} char *pcap_geterr(pcap_t *p){(void)p;return "mock";} void pcap_close(pcap_t *p){(void)p;closed++;} void pcap_freecode(struct bpf_program *b){(void)b;freed++;} int pcap_fileno(pcap_t *p){(void)p;return 9;} int ioctl(int fd,unsigned long req,...){(void)fd;(void)req;return fail_ioctl?-1:0;}
int priv_set_snaplen(uint32_t n){(void)n;return fail_ipc;} int priv_open_log(void){return open(path,O_CREAT|O_RDWR,0600);} int priv_move_log(void){moved++;return 0;}
int set_snaplen(uint32_t); int scan_dump(FILE*,off_t); int flush_buffer(FILE*); void purge_buffer(void); void dump_packet_nobuf(u_char*,const struct pcap_pkthdr*,const u_char*);
'''
post=r'''
int main(int argc, char **argv){
 assert(argc == 2);
 struct pcap_pkthdr h={{1,2},8,8}; unsigned char data[8]={1,2,3,4,5,6,7,8}; struct pcap_sf_pkthdr ph; FILE *f;
 assert(set_pcap_filter(4)==0 && compiled_length==4 && freed==1 && closed==1);
 fail_open=1; assert(set_pcap_filter(4)<0); fail_open=0; fail_compile=1; assert(set_pcap_filter(4)<0); fail_compile=0; fail_ioctl=1; assert(set_pcap_filter(4)<0); fail_ioctl=0;
 f=tmpfile(); assert(f); dump_packet_nobuf((u_char*)f,&h,data); rewind(f); assert(fread(&ph,sizeof ph,1,f)==1 && ph.caplen==4 && ph.len==8); fclose(f);
 f=tmpfile(); assert(f); dump_packet((u_char*)f,&h,data); assert(flush_buffer(f)==0); rewind(f); assert(fread(&ph,sizeof ph,1,f)==1 && ph.caplen==4 && ph.len==8); fclose(f);
 cur_snaplen=0; suspended=0; f=tmpfile(); assert(f); dump_packet_nobuf((u_char*)f,&h,data); if(suspended) {fprintf(stderr,"FAIL zero-length direct record incorrectly suspends logging\n"); return 2;} fclose(f); f=tmpfile(); assert(f); dump_packet((u_char*)f,&h,data); assert(flush_buffer(f)==0); rewind(f); assert(fread(&ph,sizeof ph,1,f)==1 && ph.caplen==0 && !suspended); fclose(f);
 snprintf(path,sizeof(path),"%s",argv[1]); cur_snaplen=snaplen=4; assert(try_reset_dump(0)==0); fclose(dpcap);dpcap=NULL; cur_snaplen=8; fail_ipc=1; assert(try_reset_dump(0)==-1 && moved==0); fail_ipc=0; assert(try_reset_dump(0)==0 && cur_snaplen==4); fclose(dpcap); dpcap=NULL; { FILE *bad=fopen(path,"wb"); struct pcap_file_header wrong={0xa1b2c3d4,2,4,0,0,4,7}; assert(bad); assert(fwrite(&wrong,sizeof wrong,1,bad)==1); fclose(bad); } assert(try_reset_dump(0)==1 && moved==1); unlink(path); puts("PASS"); return 0;
}
'''
code=pre+'\n'.join(function(x) for x in ('set_pcap_filter','set_snaplen','try_reset_dump','scan_dump','dump_packet_nobuf','flush_buffer','purge_buffer','dump_packet'))+post
with tempfile.TemporaryDirectory() as d:
 p=pathlib.Path(d); (p/'test.c').write_text(code)
 cc=pathlib.Path(args.cc).resolve(strict=True); subprocess.run([str(cc),'-std=c11','-Wno-deprecated-declarations',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test'), str(p/'fixture.pcap')],check=True)

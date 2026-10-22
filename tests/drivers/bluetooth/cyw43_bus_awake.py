#!/usr/bin/env python3
"""Compile actual BT memory shims against a sleeping-bus/failure model."""
from pathlib import Path
import argparse,subprocess,tempfile

root=Path(__file__).resolve().parents[3]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--root',type=Path,default=root)
parser.add_argument('--baseline',metavar='REVISION',help='Extract the old implementation with git show; expected to fail')
args=parser.parse_args()
relative='drivers/bluetooth/hci/cyw43_bt_whd_glue.c'
source=(subprocess.check_output(['git','show',args.baseline+':'+relative],cwd=args.root,text=True)
        if args.baseline else (args.root/relative).read_text())

def extract(token):
    start=source.index(token)
    brace=source.index('{',start)
    end=brace+1
    depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}')
        end+=1
    return source[start:end]
parts=[]
if 'static whd_result_t bt_backplane_begin(' in source:
    parts.append(extract('static whd_result_t bt_backplane_begin('))
parts.extend(extract('int '+name+'(') for name in ('cyw43_ll_write_backplane_mem','cyw43_ll_read_backplane_mem'))
harness=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef void cyw43_ll_t;
typedef void *whd_driver_t;
typedef int whd_result_t;
#define ARG_UNUSED(x) (void)(x)
#define WHD_SUCCESS 0
#define WHD_BADARG -2
#define BUS_WRITE 1
#define BUS_READ 0
static int depth,awake,wake_failure,data_failure,accesses,missing;
static void whd_bus_lock(void){depth++;}
static void whd_bus_unlock(void){assert(depth>0);depth--;}
static whd_driver_t bt_whd_driver(void){return missing?NULL:(void*)1;}
static int whd_ensure_wlan_bus_is_up(whd_driver_t d){
 (void)d;assert(depth>0);if(wake_failure)return -10;awake=1;return 0;
}
static int whd_bus_transfer_backplane_bytes(void*d,int dir,uint32_t a,uint32_t n,uint8_t*b){
 (void)d;(void)a;(void)n;accesses++;
 if(!awake)return -80;
 if(data_failure)return -11;
 if(dir==BUS_READ)*b=42;
 return 0;
}
'''
main=r'''
int main(void){int count=0;
 for(int op=0;op<2;op++)for(int initial=0;initial<2;initial++)
 for(int failure=0;failure<3;failure++)for(int recursive=0;recursive<2;recursive++){
  depth=recursive;awake=initial;wake_failure=failure==1;data_failure=failure==2;missing=0;accesses=0;
  uint8_t byte=19;
  int result=op?cyw43_ll_write_backplane_mem(NULL,0x6a61c,1,&byte):cyw43_ll_read_backplane_mem(NULL,0x6a61c,1,&byte);
  int expected=failure==1?-10:failure==2?-11:0;
  if(result!=expected||depth!=recursive||accesses!=(failure==1?0:1)){
   fprintf(stderr,"op=%d initial_awake=%d failure=%d recursive=%d result=%d expected=%d depth=%d accesses=%d\n",op,initial,failure,recursive,result,expected,depth,accesses);
   return 1;
  }
  assert(byte==((!op&&!failure)?42:19));count++;
 }
 missing=1;depth=0;accesses=0;uint8_t byte=19;
 assert(cyw43_ll_read_backplane_mem(NULL,0,1,&byte)==WHD_BADARG);
 assert(depth==0&&accesses==0&&byte==19);count++;
 printf("BT_WAKE_ATOMIC_ACCESS_PASS %d cases\n",count);
}
'''
with tempfile.TemporaryDirectory(prefix='envhub-bt-awake-') as tmp:
    path=Path(tmp)/'test.c'
    path.write_text(harness+'\n'.join(parts)+main)
    binary=Path(tmp)/'test'
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Wno-unused-function','-fsanitize=undefined','-fno-sanitize-recover=all',str(path),'-o',str(binary)],check=True)
    raise SystemExit(subprocess.run([str(binary)]).returncode)

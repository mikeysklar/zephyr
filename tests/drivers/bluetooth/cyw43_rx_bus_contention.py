#!/usr/bin/env python3
"""Exercise actual CYW43 RX functions with a contending buffer consumer.

This proves the driver's lock contract, not the cause of a particular DUT hang.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--root', type=Path, required=True)
parser.add_argument('--baseline', help='Read source from this Git revision')
args = parser.parse_args()

def source(name):
    path = 'drivers/bluetooth/hci/' + name
    if args.baseline:
        return subprocess.check_output(['git', 'show', args.baseline + ':' + path], cwd=args.root, text=True)
    return (args.root / path).read_text()

def function(text, signature):
    start = text.index(signature)
    brace = text.index('{', start)
    end, depth = brace + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

driver = source('hci_cyw43_shared_bus.c')
glue = source('cyw43_bt_whd_glue.c')
constants = driver[driver.index('#define PACKET_TYPE '):driver.index('static uint8_t __noinit')]
parts = [function(driver, signature) for signature in (
    'static void hci_cyw43_process_one(',
    'bool cyw43_bluetooth_has_pending(',
    'void cyw43_bluetooth_hci_process(',
)]
parts.append(function(glue, 'static void bt_poll_thread('))
prefix = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#define ARG_UNUSED(x) (void)(x)
#define LOG_ERR(...) ((void)0)
#define LOG_WRN(...) ((void)0)
#define LOG_DBG(...) ((void)0)
#define LOG_HEXDUMP_DBG(...) ((void)0)
#define CONFIG_BT_ISO 1
#define K_FOREVER -1
#define K_NO_WAIT 0
#define BT_POLL_INTERVAL_MS 4
#define BT_HCI_H4_EVT 4
#define BT_HCI_H4_ACL 2
#define BT_HCI_H4_SCO 3
#define BT_HCI_H4_ISO 5
#define BT_HCI_EVT_LE_META_EVENT 0x3e
#define BT_HCI_EVT_LE_ADVERTISING_REPORT 2
#define BT_BUF_ACL_IN 2
#define BT_BUF_ISO_IN 5
#define CYBT_SUCCESS 0
#define DT_DRV_INST(x) 0
#define DEVICE_DT_GET(x) (&device)
typedef int k_timeout_t;
struct device {int unused;};
static struct device device;
struct bt_hci_evt_hdr {uint8_t evt,len;};
struct bt_hci_acl_hdr {uint16_t handle,len;};
struct bt_hci_iso_hdr {uint16_t handle,len;};
struct net_buf {uint8_t data[256];size_t len;};
typedef struct {unsigned bt2host_in_val,bt2host_out_val;} cybt_fw_membuf_index_t;
static struct {bool bt_loaded;} cyw43_bt_state = {true};
static uint16_t sys_le16_to_cpu(uint16_t x) {return x;}
static uint16_t bt_iso_hdr_len(uint16_t x) {return x & 0x3fff;}
static pthread_mutex_t bus;
static int depth,blocked,unprotected,delivered,allocations,remaining,read_error,index_error;
static int packet_type,event_type,oversize,discard_full,wire_length;
static unsigned last_timeout;
static struct net_buf buffer;
static jmp_buf done;
static void whd_bus_lock(void) {assert(!pthread_mutex_lock(&bus));depth++;}
static void whd_bus_unlock(void) {assert(depth>0);depth--;assert(!pthread_mutex_unlock(&bus));}
static void k_msleep(int ms) {(void)ms;longjmp(done,1);}
/* A consumer must complete a bus operation before it can release its buffer.
 * Timed locking makes the deadlock deterministic without hanging the test. */
static void *consumer(void *unused) {
 (void)unused;struct timespec limit;clock_gettime(CLOCK_REALTIME,&limit);
 limit.tv_nsec+=100000000;if(limit.tv_nsec>=1000000000){limit.tv_sec++;limit.tv_nsec-=1000000000;}
 int result=pthread_mutex_timedlock(&bus,&limit);
 if(result==ETIMEDOUT) return (void *)1;
 assert(result==0);assert(!pthread_mutex_unlock(&bus));return NULL;
}
static bool consumer_can_progress(void) {
 pthread_t thread;void *result;assert(!pthread_create(&thread,NULL,consumer,NULL));
 assert(!pthread_join(thread,&result));if(result){blocked++;return false;}return true;
}
static struct net_buf *allocate(k_timeout_t timeout) {
 allocations++;last_timeout=timeout;buffer.len=0;
 if(timeout==K_NO_WAIT)return discard_full?NULL:&buffer;
 return consumer_can_progress()?&buffer:NULL;
}
static struct net_buf *bt_buf_get_evt(uint8_t event,bool discardable,k_timeout_t timeout) {
 (void)event;assert(discardable==(event_type==BT_HCI_EVT_LE_META_EVENT));return allocate(timeout);
}
static struct net_buf *bt_buf_get_rx(int type,k_timeout_t timeout) {(void)type;return allocate(timeout);}
static size_t net_buf_tailroom(struct net_buf *buf) {return sizeof(buf->data)-buf->len;}
static void net_buf_add_mem(struct net_buf *buf,const void *data,size_t n) {memcpy(buf->data,data,n);buf->len=n;}
static void net_buf_unref(struct net_buf *buf) {(void)buf;}
static void bt_hci_recv(const struct device *dev,struct net_buf *buf) {
 (void)dev;(void)buf;if(consumer_can_progress())delivered++;
}
static int cybt_get_bt_buf_index(cybt_fw_membuf_index_t *idx) {
 if(!depth)unprotected++;idx->bt2host_in_val=remaining;idx->bt2host_out_val=0;return index_error?-1:0;
}
static int cyw43_bluetooth_hci_read(uint8_t *data,uint32_t capacity,uint32_t *length) {
 if(!depth)unprotected++;remaining--;memset(data,0,capacity);
 if(read_error)return -EIO;
 data[3]=packet_type;
 if(packet_type==BT_HCI_H4_EVT){data[4]=event_type;data[5]=1;data[6]=2;*length=7;}
 else {data[6]=1;data[8]=42;*length=9;}
 if(oversize)data[5]=250;
 if(wire_length>=0)*length=wire_length;
 return 0;
}
'''
main = r'''
int main(void) {
 pthread_mutexattr_t attr;pthread_mutexattr_init(&attr);
 pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE);pthread_mutex_init(&bus,&attr);
 int failures=0;
 for(int scenario=0;scenario<12;scenario++) {
  depth=blocked=unprotected=delivered=allocations=read_error=index_error=oversize=discard_full=0;
  remaining=1;wire_length=-1;packet_type=BT_HCI_H4_EVT;event_type=0x0e;
  switch(scenario){
   case 1:event_type=0xff;break;
   case 2:packet_type=BT_HCI_H4_ACL;break;
   case 3:packet_type=BT_HCI_H4_ISO;break;
   case 4:event_type=BT_HCI_EVT_LE_META_EVENT;break;
   case 5:event_type=BT_HCI_EVT_LE_META_EVENT;discard_full=1;break;
   case 6:read_error=1;break;
   case 7:wire_length=3;break;
   case 8:oversize=1;break;
   case 9:index_error=1;break;
   case 10:remaining=40;break;
   case 11:packet_type=BT_HCI_H4_SCO;break;
  }
  if(!setjmp(done))bt_poll_thread(NULL,NULL,NULL);
  int expected=scenario<5?1:scenario==10?32:0;
  bool ok=!blocked&&!unprotected&&!depth&&delivered==expected;
  if(scenario==10)ok=ok&&remaining==8;
  if(scenario==4||scenario==5)ok=ok&&last_timeout==K_NO_WAIT;
  printf("scenario=%d %s blocked=%d unprotected=%d delivered=%d remaining=%d\n",scenario,ok?"PASS":"FAIL",blocked,unprotected,delivered,remaining);
  failures+=!ok;
 }
 printf("CYW43_RX_BUS_CONTENTION %d/12 passed\n",12-failures);
 return failures?1:0;
}
'''
with tempfile.TemporaryDirectory(prefix='cyw43-rx-contention-') as tmp:
    path = Path(tmp) / 'test.c'
    path.write_text(prefix + constants + '\nstatic uint8_t cyw43_rxbuf[MAX_BT_MSG_SIZE + CYW43_PACKET_HEADER_SIZE];\n' + '\n'.join(parts) + main)
    binary = Path(tmp) / 'test'
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation', '-Wno-clobbered', '-pthread', '-fsanitize=undefined', '-fno-sanitize-recover=all', str(path), '-o', str(binary)], check=True)
    raise SystemExit(subprocess.run([str(binary)], timeout=30).returncode)

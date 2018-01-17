/*
 *  Eddystone Bluetooth LE Beacon setup
 *
 *  Copyright (C) 2017 Kaj-Michael Lang
 *  Parts peeked from Bluez hcitool
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <stdio.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

int sec_cnt=0;
int adv_cnt=0;

static const char* eddystone_url_prefix[] = {
    "http://www.",
    "https://www.",
    "http://",
    "https://",
    "urn:uuid:",
    NULL
};

static const char* eddystone_url_suffix[] = {
    ".com/",
    ".org/",
    ".edu/",
    ".net/",
    ".info/",
    ".biz/",
    ".gov/",
    ".com",
    ".org",
    ".edu",
    ".net",
    ".info",
    ".biz",
    ".gov",
    NULL
};

int setup_filter(int dev)
{
struct hci_filter flt;
hci_filter_clear(&flt);
hci_filter_set_ptype(HCI_EVENT_PKT, &flt);
hci_filter_all_events(&flt);
if (setsockopt(dev, SOL_HCI, HCI_FILTER, &flt, sizeof(flt)) < 0) {
	perror("HCI filter setup failed");
	return -1;
}

return 0;
}

int read_event(int dev)
{
unsigned char buf[HCI_MAX_EVENT_SIZE], *ptr = buf;
int len;
hci_event_hdr *hdr;

len = read(dev, buf, sizeof(buf));
if (len < 0) {
	perror("Read failed");
	return -1;
}

hdr = (void *)(buf + 1);
ptr = buf + (1 + HCI_EVENT_HDR_SIZE);
len -= (1 + HCI_EVENT_HDR_SIZE);

printf("> HCI Event: 0x%02x plen %d\n", hdr->evt, hdr->plen);

return 0;
}

#if 0
void lescan(int dev)
{
int err;
uint8_t filter_dup = 1;

err = hci_le_set_scan_enable(dev, 0x01, filter_dup, 1000);

printf("Scanning...\n");

err = hci_le_set_scan_enable(dev, 0x00, filter_dup, 1000);
}
#endif

int disable_scan(int dev)
{
struct hci_dev_req dr;
int ctl;

dr.dev_id=dev;
dr.dev_opt=SCAN_DISABLED;

if ((ctl = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI)) < 0) {
	perror("Can't open HCI socket.");
	return -1;
}

return ioctl(ctl, HCISETSCAN, (unsigned long) &dr);
}

int advertise_frame(int dev, le_set_advertising_data_cp *frame)
{
uint8_t ogf=OGF_LE_CTL; // LE
uint16_t ocf=OCF_LE_SET_ADVERTISING_DATA; // LE_Set_Advertising_Data

if (hci_send_cmd(dev, ogf, ocf, sizeof(le_set_advertising_data_cp), frame) < 0) {
	perror("hci_send_cmd failed");
	return -1;
}
return 0;
}

void eddystone_frame_prepare(le_set_advertising_data_cp *f, uint8_t type)
{
memset(f->data, 0, sizeof(f->data));

f->length=31; // Total length
f->data[0]=0x02; // Length, next
f->data[1]=0x01; // Flags
f->data[2]=0x06; // Flag data
f->data[3]=0x03; // Length, next
f->data[4]=0x03;
f->data[5]=0xAA; // UUID
f->data[6]=0xFE; // UUID
f->data[7]=0x00; // Service Data Length
f->data[8]=0x16; //
f->data[9]=0xAA; // UUID
f->data[10]=0xFE; // UUID
f->data[11]=type; // Eddystone Frame Type
}

int eddystone_uid_beacon(int dev, uint8_t tx, const char *nid, const char *bid)
{
int i;
le_set_advertising_data_cp f;

if (strlen(nid)!=10)
	return -1;

if (strlen(bid)!=6)
	return -1;

eddystone_frame_prepare(&f, 0x00);

f.data[7]=0x17; // Service Data Length

f.data[12]=tx;

for (i=0;i<10;i++)
	f.data[13+i]=nid[i];

for (i=0;i<6;i++)
	f.data[23+i]=bid[i];

printf("UID Frame: %s %s\n", nid, bid);

return advertise_frame(dev, &f);
}

int eddystone_tlm_beacon(int dev)
{
le_set_advertising_data_cp f;

eddystone_frame_prepare(&f, 0x20);

f.data[7]=0x11; // Total length

f.data[12]=0x00; // TLM Frame Version
f.data[13]=0x00; // VBatt1
f.data[14]=0x00; // VBatt2
f.data[15]=0x80; // Temp1
f.data[16]=0x00; // Temp2
f.data[17]=(uint8_t)(adv_cnt>>24); // Advertising PDU count
f.data[18]=(uint8_t)(adv_cnt>>16);
f.data[19]=(uint8_t)(adv_cnt>>8);
f.data[20]=(uint8_t)(adv_cnt>>0);
f.data[21]=(uint8_t)(sec_cnt>>24); // Time since power-on or reboot
f.data[22]=(uint8_t)(sec_cnt>>16);
f.data[23]=(uint8_t)(sec_cnt>>8);
f.data[24]=(uint8_t)(sec_cnt>>0);

printf("TLM Frame: %d %d\n", adv_cnt, sec_cnt);

return advertise_frame(dev, &f);
}

int eddystone_url_beacon(int dev, int8_t tx, const char *url)
{
le_set_advertising_data_cp f;
int i;

memset(f.data, 0, sizeof(f.data));

f.length=31; // Total length
f.data[0]=0x02; // Length, next
f.data[1]=0x01; // Flags
f.data[2]=0x06; // Flag data
f.data[3]=0x03; // Length, next
f.data[4]=0x03;
f.data[5]=0xAA; // UUID
f.data[6]=0xFE; // UUID
f.data[7]=31-8; // Beacon data length
f.data[8]=0x16; //
f.data[9]=0xAA; // UUID
f.data[10]=0xFE; // UUID
f.data[11]=0x10; // Eddystone URL Type
f.data[12]=tx; // TX power
f.data[13]=0x03; // URL Scheme (https://)

for (i=0;i<strlen(url) && i<18;i++) {
	f.data[i+14]=url[i];
}

if (i==18) {
	printf("URL too long\n");
	return -2;
}

f.data[7]=6+i;

printf("URL Frame: %s\n", url);
advertise_frame(dev, &f);

read_event(dev);

return 0;
}

int enable_advertise(int dev, uint8_t e)
{
uint8_t ogf=0x08; // LE
uint16_t ocf=OCF_LE_SET_ADVERTISE_ENABLE; // LE_Set_Advertising_Data
char data[1];

data[0]=e==0 ? 0 : 1;

if (hci_send_cmd(dev, ogf, ocf, 1, data) < 0) {
	perror("Send failed");
	return -1;
}

read_event(dev);

return 0;
}

void usage()
{
printf("beacon url [nid bid]\n\n");
printf("URL assumes https:// and must be max 17 characters\n");
printf("NID must be 10 characters, BID must be 6 characters\n");
printf("Example: www.example.ex 0123456789 abcdef\n\n");
}

int main(int argc, char *argv[])
{
int dev_id, dev;
int oneshot=0;
char *nid="0123456789";
char *bid="abcdef";

if (argc<2) {
	usage();
	return 1;
}
if (argc==4 && strlen(argv[2])==10 && strlen(argv[3])==6) {
	nid=argv[2];
	bid=argv[3];
} else if (argc==3) {
	usage();
	return 1;
}
if (strlen(argv[1])>17) {
	usage();
	return 1;
}

printf("URL: %s\nNID: %s\nBID: %s\n", argv[1], nid, bid);

dev_id = hci_get_route(NULL);
if (dev_id<0) {
	perror("hci_get_route");
	return 1;
}

dev = hci_open_dev(dev_id);
if (dev<0) {
	perror("hci_open_dev");
	return 1;
}
if (disable_scan(0)<0) {
	perror("disable_scan");
	return 1;
}

hci_le_set_scan_enable(dev, 0x00, 1, 1000);

setup_filter(dev);

enable_advertise(dev, 1);

while(1 || !oneshot) {
	if (eddystone_url_beacon(dev, 0xed, argv[1])<0)
		break;
	sleep(1);
	if (eddystone_uid_beacon(dev, 0xed, nid, bid)<0)
		break;
	sleep(1);
	if (eddystone_tlm_beacon(dev)<0)
		break;
	sleep(1);

	sec_cnt+=20;
	adv_cnt++;
}

hci_close_dev(dev);
}

/* os_write.c */
/*
 * Program to write raw data to an OnStream device
 * (c) 2000 Kurt Garloff <garloff@suse.de>
 * heavily based on Doug Gilbert's sg_rbuf program.
 * (c) 1999 Doug Gilbert
 * 
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 * 
 * $Id$
 */

#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <linux/../scsi/sg.h>		/* cope with silly includes */
#include <linux/../scsi/scsi.h>		/* SCSI commands */
#include "sg_err.h"

#define BPI (signed)(sizeof(int))

#define OFF sizeof(struct sg_header)
#define RB_MODE_DESC 3
#define RB_MODE_DATA 2
#define RB_DESC_LEN 4

const unsigned char wrCmdBlk [6] = {WRITE_6, 1, 0, 0, 0, 0};
const unsigned char lcCmdBlk[10] = {SEEK_10, 0, 0, 0, 0, 0, 0, 0, 0, 0};
const unsigned char rpCmdBlk[10] = {READ_POSITION, 0, 0, 0, 0, 0, 0, 0, 0, 0};
const unsigned char msCmdBlk[] = {MODE_SELECT, 0x10, 0, 0, 8, 0,
	4, 0, 0, 0,
	0xb0, 2, 0, 0xa2
};
const unsigned char viCmdBlk[] = {MODE_SELECT, 0x10, 0, 0, 12, 0,
	8, 0, 0, 0,
	0xb6, 6, 'L', 'I', 'N', 'S', 0, 0
};

const char *prog = "os_write";
/* Options */
int no = 20;
int bufsz = 33280;
char *file_name = 0;
int ctr = 0;
int startpos = 0;

int do_cmnd (int sg_fd, unsigned char *buf, int outln, int inln, char *errtext)
{
	int res;
        res = write(sg_fd, buf, outln);
        if (res < 0) {
	    fprintf (stderr, "%s: write (%s)", prog, errtext);
            perror("error");
            return 1;
        }
        if (res < outln) {
            fprintf(stderr, "%s: wrote less (%s), ask=%d, got=%d\n", 
		    prog, errtext, outln, res);
            return 2;
        }
        
        res = read(sg_fd, buf, inln);
        if (res < 0) {
	    fprintf (stderr, "%s: read (%s)", prog, errtext);
            perror("error");
            return 3;
        }
        if (res < inln) {
	    fprintf(stderr, "%s: read less (%s), ask=%d, got=%d\n", 
		    prog, errtext, inln, res);
            return 4;
        }
	return 0;
}
	

int input (unsigned char *buf, int ln)
{
	int pos = 0; int res;
	while (pos < ln) {
		res = read (0, buf+pos, ln-pos);
		if (res < 0)
		{
			//if (res > 0) pos += res;
			memset (buf+pos, 0, ln-pos);
			return 1;
		}
		//if (res < 0) perror ("os_write: read stdin");
		pos += res;
	}
	return 0;
}

int do_locate (int sg_fd, int pos)
{
	int res;
	unsigned char * rbBuff = malloc(OFF + sizeof(lcCmdBlk));
	struct sg_header * rsghp = (struct sg_header *)rbBuff;
	
        int rbInLen = OFF;
	int rbOutLen = OFF + sizeof (lcCmdBlk);
	memset(rbBuff, 0, OFF + sizeof(lcCmdBlk));
        rsghp->pack_len = 0;                /* don't care */
        rsghp->reply_len = rbInLen;
        rsghp->twelve_byte = 0;
        rsghp->result = 0;
        memcpy(rbBuff + OFF, lcCmdBlk, sizeof(lcCmdBlk));
        rbBuff[OFF + 3] = 0xff & (pos >> 24);
        rbBuff[OFF + 4] = 0xff & (pos >> 16);
        rbBuff[OFF + 5] = 0xff & (pos >> 8);
        rbBuff[OFF + 6] = 0xff & (pos);

	rsghp->pack_id = 0;
	res = do_cmnd (sg_fd, rbBuff, rbOutLen, rbInLen, "locate");
	if (rbBuff) free(rbBuff);
	return res;
}

int onstream_set325 (int sg_fd)
{
	int res;
	unsigned char * rbBuff = malloc(OFF + sizeof (msCmdBlk));
	struct sg_header * rsghp = (struct sg_header *)rbBuff;
	int rbInLen = OFF;
	int rbOutLen = OFF + sizeof (msCmdBlk);
	memset(rbBuff, 0, OFF + sizeof(msCmdBlk));
        rsghp->pack_len = 0;                /* don't care */
        rsghp->reply_len = rbInLen;
        rsghp->twelve_byte = 0;
        rsghp->result = 0;
        memcpy(rbBuff + OFF, msCmdBlk, sizeof(msCmdBlk));

	rsghp->pack_id = 0;
	res = do_cmnd (sg_fd, rbBuff, rbOutLen, rbInLen, "mode select");
	if (rbBuff) free(rbBuff);
	return res;
}

int onstream_appid (int sg_fd)
{
	int res;
	unsigned char * rbBuff = malloc(OFF + sizeof (viCmdBlk));
	struct sg_header * rsghp = (struct sg_header *)rbBuff;
	int rbInLen = OFF;
	int rbOutLen = OFF + sizeof (viCmdBlk);
	memset(rbBuff, 0, OFF + sizeof(viCmdBlk));
        rsghp->pack_len = 0;                /* don't care */
        rsghp->reply_len = rbInLen;
        rsghp->twelve_byte = 0;
        rsghp->result = 0;
        memcpy(rbBuff + OFF, viCmdBlk, sizeof(viCmdBlk));

	rsghp->pack_id = 0;
	res = do_cmnd (sg_fd, rbBuff, rbOutLen, rbInLen, "vendor ID");
	if (rbBuff) free(rbBuff);
	return res;
}


int do_write (int sg_fd, int size, int pos, char pr)
{
	int res;
	unsigned char * rbBuff = malloc(OFF + 12 + size);
	struct sg_header * rsghp = (struct sg_header *)rbBuff;
	int fblk, lblk; int lctr = 0;
	int rbInLen = OFF + 20;
	int rbOutLen = OFF + sizeof (rpCmdBlk);
	
    loop:
	memset(rbBuff, 0, OFF + 12 + size);
	rsghp->pack_len = 0;                /* don't care */
	rsghp->reply_len = rbInLen;
	rsghp->twelve_byte = 0;
	rsghp->result = 0;
	memcpy(rbBuff + OFF, rpCmdBlk, sizeof(rpCmdBlk));
	rsghp->pack_id = pos*2;
	res = do_cmnd (sg_fd, rbBuff, rbOutLen, rbInLen, "read pos");
	if (res) return res;
	fblk = ((rbBuff + OFF)[4] << 24)
		+ ((rbBuff + OFF)[5] << 16)
		+ ((rbBuff + OFF)[6] << 8)
		+ (rbBuff + OFF)[7];
	lblk = ((rbBuff + OFF)[8] << 24)
		+ ((rbBuff + OFF)[9] << 16)
		+ ((rbBuff + OFF)[10] << 8)
		+ (rbBuff + OFF)[11];
	fprintf (stderr, "os_write: First blk %i, last %i, want %i.",
		 fblk, lblk, pos);
	if (fblk != pos || lblk+50 <= pos || ((rbBuff + OFF)[0] & 4)) {
		fprintf (stderr, "       Wait  %8i\r",
			 lctr);
		//if (fblk < pos && lblk == pos) do_locate (sg_fd, pos);
#if 1		
		usleep (100*1000); lctr++;
		goto loop;
#endif		
	}
	fprintf (stderr, " OK.\n");
	/* write */
	rbInLen = OFF;
	rbOutLen = OFF + sizeof (wrCmdBlk) + size;
	memset(rbBuff, 0, OFF + 12 + size);
	res = input (rbBuff + OFF + sizeof (wrCmdBlk), size);
	rsghp->pack_len = 0;                /* don't care */
	rsghp->reply_len = rbInLen;
	rsghp->twelve_byte = 0;
	rsghp->result = 0;
	memcpy(rbBuff + OFF, wrCmdBlk, sizeof(wrCmdBlk));
	//rbBuff[OFF + 2] = 0xff & (size >> 16);
	//rbBuff[OFF + 3] = 0xff & (size >> 8);
	//rbBuff[OFF + 4] = 0xff & (size);
	rbBuff[OFF + 4] = 1;

	rsghp->pack_id = pos*2+1;
	res += do_cmnd (sg_fd, rbBuff, rbOutLen, rbInLen, "data");
	if (rbBuff) free(rbBuff);
	if (res) return res;
	return 0;
}


void usage ()
{
	fprintf (stderr, "Usage: os_write /dev/sgX no [locate] [blksz]\n");
	fprintf (stderr, "os_write writes data in chunks of 33280 (blksz) bytes to device\n");
	fprintf (stderr, " /dev/sgX. Data is read from standard output. This is done for\n");
	fprintf (stderr, " no blocks.\n");
	fprintf (stderr, "(c) Douglas Gilbert, Kurt Garloff, 2000, GNU GPL\n");
	exit (1);
}

void parseargs (int argc, char *argv[])
{
	if (argc < 3) usage ();
	file_name = argv[1];
	no = atol (argv[2]);
	if (argc > 3) startpos = atol (argv[3]);
	if (argc > 4) bufsz = atol (argv[4]);
}


int main (int argc, char * argv[])
{
	int sg_fd; int res;
   
	parseargs (argc, argv);
	sg_fd = open(file_name, O_RDWR);
	if (sg_fd < 0) {
		perror("os_write: open error");
        return 1;
	}
	/* Don't worry, being very careful not to write to a none-sg file ... */
	res = ioctl(sg_fd, SG_GET_TIMEOUT, 0);
	if (res < 0) {
		/* perror("ioctl on generic device, error"); */
		fprintf(stderr, "os_write: not a sg device, or wrong driver\n");
		return 1;
	}
	
	res = onstream_set325 (sg_fd);
	if (res) fprintf (stderr, "os_write: mode_select failed!\n");
	res = onstream_appid (sg_fd);
	if (res) fprintf (stderr, "os_write: write vendor_id failed!\n");
	res = do_locate (sg_fd, startpos);
	if (res) fprintf (stderr, "os_write: locate failed!\n");
	while (no--) {
		res = do_write (sg_fd, bufsz, startpos+ctr, 1);
		if (res) return (4);
		ctr++;
	}

	res = close(sg_fd);
	if (res < 0) {
		perror("os_write: close error");
		return 6;
	}
	fprintf (stderr, "Success\n");
	return 0;
}
	

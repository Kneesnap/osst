#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/../scsi/sg.h>  /* cope with silly includes */
#include "sg_err.h"  /* alternatively include <linux/../scsi/scsi.h> */

/* This is a simple program executing a SCSI INQUIRY command and a
   TEST UNIT READY command using the SCSI generic (sg) driver
   There is another variant of this program called "sg_simple2"
   which does not include the sg_err.h header and logic and so has
   simpler but more primitive error processing.

*  Copyright (C) 1999 D. Gilbert
*  This program is free software; you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 2, or (at your option)
*  any later version.

   Invocation: sg_simple1 <sg_device>

   Version 0.53 (991007)

6 byte INQUIRY command:
[0x12][   |lu][pg cde][res   ][al len][cntrl ]

6 byte TEST UNIT READY command:
[0x00][   |lu][res   ][res   ][res   ][res   ]

*/

#define OFF sizeof(struct sg_header)
#define INQ_REPLY_LEN 96        /* logic assumes >= sizeof(inqCmdBlk) */
#define INQ_CMD_LEN 6
#define TUR_CMD_LEN 6
#define INQ_PACK_ID 1234
#define TUR_PACK_ID 1235

int main(int argc, char * argv[])
{
    int sg_fd, k, ok;
    unsigned char inqCmdBlk [INQ_CMD_LEN] =
                                {0x12, 0, 0, 0, INQ_REPLY_LEN, 0};
    unsigned char turCmdBlk [TUR_CMD_LEN] =
                                {0x00, 0, 0, 0, 0, 0};
    unsigned char inqBuff[OFF + INQ_REPLY_LEN];
    unsigned char * turBuff = inqBuff;  /* Use same buffer */
    int inqInLen = OFF + sizeof(inqCmdBlk);
    int turInLen = OFF + sizeof(turCmdBlk);
    int inqOutLen = OFF + INQ_REPLY_LEN;
    unsigned char * buffp = inqBuff + OFF;
    struct sg_header * sghp = (struct sg_header *)inqBuff;
    char * file_name = 0;
    char ebuff[128];

    for (k = 1; k < argc; ++k) {
        if (*argv[k] == '-') {
            printf("Unrecognized switch: %s\n", argv[k]);
            file_name = 0;
            break;
        }
        else if (0 == file_name)
            file_name = argv[k];
        else {
            printf("too many arguments\n");
            file_name = 0;
            break;
        }
    }
    if (0 == file_name) {
        printf("Usage: 'sg_simple1 <sg_device>'\n");
        return 1;
    }
    
    if ((sg_fd = open(file_name, O_RDWR)) < 0) {
        sprintf(ebuff, "sg_simple1: error opening file: %s", file_name);
        perror(ebuff);
        return 1;
    }
    /* Just to be safe, check we have a sg device by trying an ioctl */
    if (ioctl(sg_fd, SG_GET_TIMEOUT, 0) < 0) {
        printf("sg_simple1: %s doesn't seem to be an sg device\n", file_name);
        close(sg_fd);
        return 1;
    }
    
    /* Prepare for INQUIRY command */
    sghp->reply_len = inqOutLen;
    sghp->pack_id = INQ_PACK_ID;
    sghp->twelve_byte = 0;
    sghp->other_flags = 0;     /* some apps assume this is done ?!? */
    memcpy(inqBuff + OFF, inqCmdBlk, INQ_CMD_LEN);

    /* Send for INQUIRY command */
    if (write(sg_fd, inqBuff, inqInLen) < 0) {
        perror("sg_simple1: Inquiry write error");
        close(sg_fd);
        return 1;
    }
    
    /* Read back status (sense_buffer) and data */
    if (read(sg_fd, inqBuff, inqOutLen) < 0) {
        perror("sg_simple1: Inquiry read error");
        close(sg_fd);
        return 1;
    }
    if (INQ_PACK_ID != sghp->pack_id) /* this shouldn't happen */
        printf("Inquiry pack_id mismatch, wanted=%d, got=%d\n", 
               INQ_PACK_ID, sghp->pack_id);

    /* now for the error processing */
    ok = 0;
    switch (sg_err_category(sghp->target_status, sghp->host_status,
            sghp->driver_status, sghp->sense_buffer, SG_MAX_SENSE)) {
    case SG_ERR_CAT_CLEAN:
        ok = 1;
        break;
    case SG_ERR_CAT_RECOVERED:
        printf("Recovered error on INQUIRY, continuing\n");
        ok = 1;
        break;
    default: /* won't bother decoding other categories */
        sg_chk_n_print("INQUIRY command error", sghp->target_status, 
                       sghp->host_status, sghp->driver_status, 
                       sghp->sense_buffer, SG_MAX_SENSE);
        break;
    }

    if (ok) { /* output result if it is available */
        int f = (int)*(buffp + 7);
        printf("Some of the INQUIRY command's results:\n");
        printf("    %.8s  %.16s  %.4s  ", buffp + 8, buffp + 16, buffp + 32);
        printf("[wide=%d sync=%d cmdque=%d sftre=%d]\n",
               !!(f & 0x20), !!(f & 0x10), !!(f & 2), !!(f & 1));
    }
    
    /* Prepare for TEST UNIT READY command */
    sghp->reply_len = OFF;
    sghp->pack_id = TUR_PACK_ID;
    sghp->twelve_byte = 0;
    sghp->other_flags = 0;     /* some apps assume this is done ?!? */
    memcpy(turBuff + OFF, turCmdBlk, TUR_CMD_LEN);

    /* Send TEST UNIT READY command */
    if (write(sg_fd, turBuff, turInLen) < 0) {
        perror("sg_simple1: Test Unit Ready write error");
        close(sg_fd);
        return 1;
    }
    
    /* Read back status (sense_buffer) */
    if (read(sg_fd, turBuff, OFF) < 0) {
        perror("sg_simple1: Test Unit Ready read error");
        close(sg_fd);
        return 1;
    }
    if (TUR_PACK_ID != sghp->pack_id) /* this shouldn't happen */
        printf("Test Unit Ready pack_id mismatch, wanted=%d, got=%d\n", 
               TUR_PACK_ID, sghp->pack_id);

    /* now for the error processing */
    ok = 0;
    switch (sg_err_category(sghp->target_status, sghp->host_status,
            sghp->driver_status, sghp->sense_buffer, SG_MAX_SENSE)) {
    case SG_ERR_CAT_CLEAN:
        ok = 1;
        break;
    case SG_ERR_CAT_RECOVERED:
        printf("Recovered error on Test Unit Ready, continuing\n");
        ok = 1;
        break;
    default: /* won't bother decoding other categories */
        sg_chk_n_print("Test Unit Ready command error", sghp->target_status, 
                       sghp->host_status, sghp->driver_status, 
                       sghp->sense_buffer, SG_MAX_SENSE);
        break;
    }
    
    if (ok)
        printf("Test Unit Ready successful so unit is ready!\n");
    else
        printf("Test Unit Ready failed so unit may _not_ be ready!\n");

    close(sg_fd);
    return 0;
}

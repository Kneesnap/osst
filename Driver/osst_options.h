/*
   The compile-time configurable defaults for the Linux SCSI tape driver.

   Copyright 1995 Kai Makisara.
   
   Last modified: Wed Sep  2 21:24:07 1998 by root@home
   
   Changed (and renamed) for OnStream SCSI drives garloff@suse.de
   2000-06-21
*/

#ifndef _OSST_OPTIONS_H
#define _OSST_OPTIONS_H

/* The driver allocates the tape buffers when needed if OSST_RUNTIME_BUFFERS
   is nonzero. Otherwise a number of buffers are allocated at initialization.
   The drawback of runtime allocation is that allocation may fail. In any
   case the driver tries to allocate a new tape buffer when none is free. */
#define OSST_RUNTIME_BUFFERS 0

/* The minimum limit for the number of SCSI tape devices is determined by
   OSST_MAX_TAPES. If the number of tape devices and the "slack" defined by
   OSST_EXTRA_DEVS exceeds OSST_MAX_TAPES, the large number is used. */
#define OSST_MAX_TAPES 4

/* The driver does not wait for some operations to finish before returning
   to the user program if OSST_NOWAIT is non-zero. This helps if the SCSI
   adapter does not support multiple outstanding commands. However, the user
   should not give a new tape command before the previous one has finished. */
#define OSST_NOWAIT 0

/* If OSST_IN_FILE_POS is nonzero, the driver positions the tape after the
   record been read by the user program even if the tape has moved further
   because of buffered reads. Should be set to zero to support also drives
   that can't space backwards over records. NOTE: The tape will be
   spaced backwards over an "accidentally" crossed filemark in any case. */
#define OSST_IN_FILE_POS 0

/* If OSST_RECOVERED_WRITE_FATAL is non-zero, recovered errors while writing
   are considered "hard errors". */
#define OSST_RECOVERED_WRITE_FATAL 0

/* The "guess" for the block size for devices that don't support MODE
   SENSE. */
#define OSST_DEFAULT_BLOCK 0

/* The tape driver buffer size in kilobytes. */
/* Don't change, as this is the HW blocksize */
#define OSST_BUFFER_BLOCKS 32

/* The number of kilobytes of data in the buffer that triggers an
   asynchronous write in fixed block mode. See also OSST_ASYNC_WRITES
   below. */
#define OSST_WRITE_THRESHOLD_BLOCKS 30

/* The maximum number of tape buffers the driver allocates. The number
   is also constrained by the number of drives detected. Determines the
   maximum number of concurrently active tape drives. */
#define OSST_MAX_BUFFERS (2 + OSST_EXTRA_DEVS)

/* Maximum number of scatter/gather segments */
/* Fit one buffer in pages and add one for the AUX header */
//#define OSST_MAX_SG      (((OSST_BUFFER_BLOCKS*1024) / PAGE_SIZE) + 1)
/* Currently, some code assumes contigous memory */
#define OSST_MAX_SG	2

/* The number of scatter/gather segments to allocate at first try (must be
   smaller or equal to the maximum). */
#define OSST_FIRST_SG    6

/* The size of the first scatter/gather segments (determines the maximum block
   size for SCSI adapters not supporting scatter/gather). The default is set
   to try to allocate the buffer as one chunk. */
#define OSST_FIRST_ORDER  5


/* The following lines define defaults for properties that can be set
   separately for each drive using the MTSTOPTIONS ioctl. */

/* If OSST_TWO_FM is non-zero, the driver writes two filemarks after a
   file being written. Some drives can't handle two filemarks at the
   end of data. */
#define OSST_TWO_FM 0

/* If OSST_BUFFER_WRITES is non-zero, writes in fixed block mode are
   buffered until the driver buffer is full or asynchronous write is
   triggered. May make detection of End-Of-Medium early enough fail. */
#define OSST_BUFFER_WRITES 1

/* If OSST_ASYNC_WRITES is non-zero, the SCSI write command may be started
   without waiting for it to finish. May cause problems in multiple
   tape backups. */
#define OSST_ASYNC_WRITES 1

/* If OSST_READ_AHEAD is non-zero, blocks are read ahead in fixed block
   mode. */
#define OSST_READ_AHEAD 1

/* If OSST_AUTO_LOCK is non-zero, the drive door is locked at the first
   read or write command after the device is opened. The door is opened
   when the device is closed. */
#define OSST_AUTO_LOCK 0

/* If OSST_FAST_MTEOM is non-zero, the MTEOM ioctl is done using the
   direct SCSI command. The file number status is lost but this method
   is fast with some drives. Otherwise MTEOM is done by spacing over
   files and the file number status is retained. */
#define OSST_FAST_MTEOM 0

/* If OSST_SCSI2LOGICAL is nonzero, the logical block addresses are used for
   MTIOCPOS and MTSEEK by default. Vendor addresses are used if OSST_SCSI2LOGICAL
   is zero. */
#define OSST_SCSI2LOGICAL 0

/* If OSST_SYSV is non-zero, the tape behaves according to the SYS V semantics.
   The default is BSD semantics. */
#define OSST_SYSV 0


#endif

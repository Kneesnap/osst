/*
  SCSI Tape Driver for Linux version 1.1 and newer. See the accompanying
  file README.st for more information.

  History:

  OnStream SCSI Tape support (osst) cloned from st.c by
  Willem Riede (wriede@monmouth.com) Feb 2000
  Fixes ... Kurt Garloff <garloff@suse.de> Mar 2000

  Rewritten from Dwayne Forsyth's SCSI tape driver by Kai Makisara.
  Contribution and ideas from several people including (in alphabetical
  order) Klaus Ehrenfried, Wolfgang Denk, Steve Hirsch, Andreas Koppenh"ofer,
  Michael Leodolter, Eyal Lebedinsky, J"org Weule, and Eric Youngdale.

  Copyright 1992 - 2000 Kai Makisara
		 email Kai.Makisara@metla.fi

  $Header$

  Last modified: Wed Feb  2 22:04:05 2000 by makisara@kai.makisara.local
  Some small formal changes - aeb, 950809
*/

static const char * cvsid = "$Id$";
const char * osst_version = "0.6.1";

/* The "failure to reconnect" firmware bug */
#define OS_NEED_POLL_MIN 10602 /*(107A)*/
#define OS_NEED_POLL_MAX 10708 /*(108D)*/
#define OS_NEED_POLL(x) ((x) >= OS_NEED_POLL_MIN && (x) <= OS_NEED_POLL_MAX)

#include <linux/module.h>

#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/mtio.h>
#include <linux/ioctl.h>
#include <linux/fcntl.h>
#include <asm/uaccess.h>
#include <asm/dma.h>
#include <asm/system.h>
#include <asm/spinlock.h>

/* The driver prints some debugging information on the console if DEBUG
   is defined and non-zero. */
#define DEBUG 1

/* The message level for the debug messages is currently set to KERN_NOTICE
   so that people can easily see the messages. Later when the debugging messages
   in the drivers are more widely classified, this may be changed to KERN_DEBUG. */
#define ST_DEB_MSG  KERN_NOTICE

#define MAJOR_NR SCSI_TAPE_MAJOR
#include <linux/blk.h>

#include "scsi.h"
#include "hosts.h"
#include <scsi/scsi_ioctl.h>

#define ST_KILOBYTE 1024

#include "st_options.h"
#include "osst.h"
#include "st.h"

#include "constants.h"

#ifdef MODULE
MODULE_PARM(buffer_kbs, "i");
MODULE_PARM(write_threshold_kbs, "i");
MODULE_PARM(max_buffers, "i");
MODULE_PARM(max_sg_segs, "i");
static int buffer_kbs = 0;
static int write_threshold_kbs = 0;
static int max_buffers = 0;
static int max_sg_segs = 0;
#endif

/* The default definitions have been moved to st_options.h */

#define ST_BUFFER_SIZE (ST_BUFFER_BLOCKS * ST_KILOBYTE)
#define ST_WRITE_THRESHOLD (ST_WRITE_THRESHOLD_BLOCKS * ST_KILOBYTE)

/* The buffer size should fit into the 24 bits for length in the
   6-byte SCSI read and write commands. */
#if ST_BUFFER_SIZE >= (2 << 24 - 1)
#error "Buffer size should not exceed (2 << 24 - 1) bytes!"
#endif

#if DEBUG
static int debugging = 1;
#endif

#define MAX_RETRIES 0
#define MAX_WRITE_RETRIES 0
#define MAX_READY_RETRIES 5
#define NO_TAPE  NOT_READY

#define ST_TIMEOUT (300 * HZ)
#define ST_LONG_TIMEOUT (1800 * HZ)

#define TAPE_NR(x) (MINOR(x) & ~(128 | ST_MODE_MASK))
#define TAPE_MODE(x) ((MINOR(x) & ST_MODE_MASK) >> ST_MODE_SHIFT)

/* Internal ioctl to set both density (uppermost 8 bits) and blocksize (lower
   24 bits) */
#define SET_DENS_AND_BLK 0x10001

static int st_nbr_buffers;
static ST_buffer **st_buffers;
static int st_buffer_size = ST_BUFFER_SIZE;
static int st_write_threshold = ST_WRITE_THRESHOLD;
static int st_max_buffers = ST_MAX_BUFFERS;
static int st_max_sg_segs = ST_MAX_SG;

static Scsi_Tape * scsi_tapes = NULL;

static int modes_defined = FALSE;

static ST_buffer *new_tape_buffer(int, int);
static int enlarge_buffer(ST_buffer *, int, int);
static void normalize_buffer(ST_buffer *);
static int append_to_buffer(const char *, ST_buffer *, int);
static int from_buffer(ST_buffer *, char *, int);

static int st_init(void);
static int st_attach(Scsi_Device *);
static int st_detect(Scsi_Device *);
static void st_detach(Scsi_Device *);

struct Scsi_Device_Template st_template = {NULL, "OnStream tape", "osst", NULL, TYPE_TAPE,
					     SCSI_TAPE_MAJOR, 0, 0, 0, 0,
					     st_detect, st_init,
					     NULL, st_attach, st_detach};

static int osst_int_ioctl(Scsi_Tape *STp, unsigned int cmd_in,unsigned long arg);

static int osst_set_frame_position(Scsi_Tape *STp, unsigned int frame, int skip);

static int osst_get_frame_position(Scsi_Tape *STp);

static int osst_flush_write_buffer(Scsi_Tape *STp, int file_blk);

static int osst_write_error_recovery(Scsi_Tape * STp, Scsi_Cmnd ** aSCpnt, int pending);


/* This set of routines are unmodified from st.h and in fact will be calls
 * from osst.c to st.c when we re-integrate
 */

/* Convert the result to success code */
	static int
st_chk_result(Scsi_Cmnd * SCpnt)
{
  int dev = TAPE_NR(SCpnt->request.rq_dev);
  int result = SCpnt->result;
  unsigned char * sense = SCpnt->sense_buffer, scode;
#if DEBUG
  const char *stp;
#endif

  if (!result /* && SCpnt->sense_buffer[0] == 0 */ )
    return 0;

  if (driver_byte(result) & DRIVER_SENSE)
      scode = sense[2] & 0x0f;
  else {
      sense[0] = 0;    /* We don't have sense data if this byte is zero */
      scode = 0;
  }

#if DEBUG
  if (debugging) {
    printk(ST_DEB_MSG "osst%d: Error: %x, cmd: %x %x %x %x %x %x Len: %d\n",
	   dev, result,
	   SCpnt->data_cmnd[0], SCpnt->data_cmnd[1], SCpnt->data_cmnd[2],
	   SCpnt->data_cmnd[3], SCpnt->data_cmnd[4], SCpnt->data_cmnd[5],
	   SCpnt->request_bufflen);
    if (driver_byte(result) & DRIVER_SENSE)
      print_sense("st", SCpnt);
  }
  else
#endif
  if (!(driver_byte(result) & DRIVER_SENSE) ||
      ((sense[0] & 0x70) == 0x70 &&
       scode != NO_SENSE &&
       scode != RECOVERED_ERROR &&
/*       scode != UNIT_ATTENTION && */
       scode != BLANK_CHECK &&
       scode != VOLUME_OVERFLOW &&
       SCpnt->data_cmnd[0] != MODE_SENSE &&
       SCpnt->data_cmnd[0] != TEST_UNIT_READY)) { /* Abnormal conditions for tape */
    if (driver_byte(result) & DRIVER_SENSE) {
      printk(KERN_WARNING "osst%d: Error with sense data: ", dev);
      print_sense("st", SCpnt);
    }
    else
      printk(KERN_WARNING
	     "osst%d: Error %x (sugg. bt 0x%x, driver bt 0x%x, host bt 0x%x).\n",
	     dev, result, suggestion(result), driver_byte(result),
	     host_byte(result));
  }

  if ((sense[0] & 0x70) == 0x70 &&
      scode == RECOVERED_ERROR
#if ST_RECOVERED_WRITE_FATAL
      && SCpnt->data_cmnd[0] != WRITE_6
      && SCpnt->data_cmnd[0] != WRITE_FILEMARKS
#endif
      ) {
    scsi_tapes[dev].recover_count++;
    scsi_tapes[dev].mt_status->mt_erreg += (1 << MT_ST_SOFTERR_SHIFT);
#if DEBUG
    if (debugging) {
      if (SCpnt->data_cmnd[0] == READ_6)
	stp = "read";
      else if (SCpnt->data_cmnd[0] == WRITE_6)
	stp = "write";
      else
	stp = "ioctl";
      printk(ST_DEB_MSG "osst%d: Recovered %s error (%d).\n", dev, stp,
	     scsi_tapes[dev].recover_count);
    }
#endif
    if ((sense[2] & 0xe0) == 0)
      return 0;
  }
  return (-EIO);
}


/* Wakeup from interrupt */
	static void
st_sleep_done (Scsi_Cmnd * SCpnt)
{
  unsigned int st_nbr;
  int remainder;
  Scsi_Tape * STp;

  if ((st_nbr = TAPE_NR(SCpnt->request.rq_dev)) < st_template.nr_dev) {
    STp = &(scsi_tapes[st_nbr]);
    if ((STp->buffer)->writing &&
	(SCpnt->sense_buffer[0] & 0x70) == 0x70 &&
	(SCpnt->sense_buffer[2] & 0x40)) {
      /* EOM at write-behind, has all been written? */
      if ((SCpnt->sense_buffer[0] & 0x80) != 0)
	remainder = (SCpnt->sense_buffer[3] << 24) |
	      (SCpnt->sense_buffer[4] << 16) |
		(SCpnt->sense_buffer[5] << 8) | SCpnt->sense_buffer[6];
      else
	remainder = 0;
      if ((SCpnt->sense_buffer[2] & 0x0f) == VOLUME_OVERFLOW ||
	  remainder > 0)
	(STp->buffer)->last_result = SCpnt->result; /* Error */
      else
	(STp->buffer)->last_result = INT_MAX; /* OK */
    }
    else
      (STp->buffer)->last_result = SCpnt->result;
    SCpnt->request.rq_status = RQ_SCSI_DONE;
    (STp->buffer)->last_SCpnt = SCpnt;

#if DEBUG
    STp->write_pending = 0;
#endif
    up(SCpnt->request.sem);
  }
#if DEBUG
  else if (debugging)
    printk(KERN_ERR "st?: Illegal interrupt device %x\n", st_nbr);
#endif
}


/* Do the scsi command. Waits until command performed if do_wait is true.
   Otherwise osst_write_behind_check() is used to check that the command
   has finished. */
	static Scsi_Cmnd *
st_do_scsi(Scsi_Cmnd *SCpnt, Scsi_Tape *STp, unsigned char *cmd, int bytes,
	   int timeout, int retries, int do_wait)
{
  unsigned long flags;
  unsigned char *bp;

  spin_lock_irqsave(&io_request_lock, flags);
  if (SCpnt == NULL)
    if ((SCpnt = scsi_allocate_device(NULL, STp->device, 1)) == NULL) {
      printk(KERN_ERR "osst%d: Can't get SCSI request.\n", TAPE_NR(STp->devt));
      spin_unlock_irqrestore(&io_request_lock, flags);
      return NULL;
    }

  cmd[1] |= (SCpnt->lun << 5) & 0xe0;
  STp->sem = MUTEX_LOCKED;
  SCpnt->use_sg = (bytes > (STp->buffer)->sg[0].length) ?
      (STp->buffer)->use_sg : 0;
  if (SCpnt->use_sg) {
      bp = (char *)&((STp->buffer)->sg[0]);
      if ((STp->buffer)->sg_segs < SCpnt->use_sg)
	  SCpnt->use_sg = (STp->buffer)->sg_segs;
  }
  else
      bp = (STp->buffer)->b_data;
  SCpnt->cmd_len = 0;
  SCpnt->request.sem = &(STp->sem);
  SCpnt->request.rq_status = RQ_SCSI_BUSY;
  SCpnt->request.rq_dev = STp->devt;

  scsi_do_cmd(SCpnt, (void *)cmd, bp, bytes,
	      st_sleep_done, timeout, retries);
  spin_unlock_irqrestore(&io_request_lock, flags);

  if (do_wait) {
      down(SCpnt->request.sem);

      (STp->buffer)->last_result_fatal = st_chk_result(SCpnt);
  }

  return SCpnt;
}


/* Handle the write-behind checking (downs the semaphore) */
	static void
osst_write_behind_check(Scsi_Tape *STp)
{
  ST_buffer * STbuffer;
  ST_partstat * STps;

  STbuffer = STp->buffer;

#if DEBUG
  if (STp->write_pending)
    STp->nbr_waits++;
  else
    STp->nbr_finished++;
#endif

  down(&(STp->sem));

  (STp->buffer)->last_result_fatal = st_chk_result((STp->buffer)->last_SCpnt);

  if ((STp->buffer)->last_result_fatal)
    osst_write_error_recovery(STp, &((STp->buffer)->last_SCpnt), 1);

  scsi_release_command((STp->buffer)->last_SCpnt);

  if (STbuffer->writing < STbuffer->buffer_bytes)
#if 0
    memcpy(STbuffer->b_data,
	   STbuffer->b_data + STbuffer->writing,
	   STbuffer->buffer_bytes - STbuffer->writing);
#else
  printk(KERN_WARNING "st: write_behind_check: something left in buffer!\n");
#endif
  STbuffer->buffer_bytes -= STbuffer->writing;
  STps = &(STp->ps[STp->partition]);
  if (STps->drv_block >= 0) {
    if (STp->block_size == 0)
      STps->drv_block++;
    else
      STps->drv_block += STbuffer->writing / STp->block_size;
  }
  STbuffer->writing = 0;

  return;
}



/* Onstream specific Routines */
/*
 * Initialize the OnStream AUX
 */
static void osst_init_aux(Scsi_Tape * STp, int frame_type, int logical_blk_num)
{
	os_aux_t       *aux = STp->buffer->aux;
	os_partition_t *par = &aux->partition;
	os_dat_t       *dat = &aux->dat;

	if (STp->raw) return;

	memset(aux, 0, sizeof(*aux));
	aux->format_id = htonl(0);
	memcpy(aux->application_sig, "LIN3", 4);
	aux->hdwr = htonl(0);
	aux->frame_type = frame_type;

	if (frame_type == OS_FRAME_TYPE_HEADER) {
		aux->update_frame_cntr = htonl(STp->update_frame_cntr);
		par->partition_num = OS_CONFIG_PARTITION;
		par->par_desc_ver = OS_PARTITION_VERSION;
		par->wrt_pass_cntr = htons(0xffff);
		/* KG: 0-4 = reserved, 5-9 = header, 2990-2994 = header, 2995-2999 = reserved */
		par->first_frame_addr = htonl(0);
		par->last_frame_addr = htonl(0xbb7);
	} else {
		aux->update_frame_cntr = htonl(0);
		par->partition_num = OS_DATA_PARTITION;
		par->par_desc_ver = OS_PARTITION_VERSION;
		par->wrt_pass_cntr = htons(STp->wrt_pass_cntr);
		par->first_frame_addr = htonl(STp->first_data_addr);
		/* KG: This is not true for SC-50 tapes */
		//par->last_frame_addr = htonl(19239 * 24);
		par->last_frame_addr = htonl(STp->capacity);
	}
	if (frame_type != OS_FRAME_TYPE_HEADER) {
		aux->frame_seq_num = htonl(logical_blk_num);
		aux->logical_blk_num_high = htonl(0);
		aux->logical_blk_num = htonl(logical_blk_num);
	} else {
		aux->frame_seq_num = htonl(0);
		aux->logical_blk_num_high = htonl(0);
		aux->logical_blk_num = htonl(0);
	}

	if (frame_type != OS_FRAME_TYPE_HEADER) {
		dat->dat_sz = 8;
		dat->reserved1 = 0;
		dat->entry_cnt = 1;
		dat->reserved3 = 0;
		if (frame_type == OS_FRAME_TYPE_DATA)
			dat->dat_list[0].blk_sz = htonl(32 * 1024);
		else
			dat->dat_list[0].blk_sz = 0;
		dat->dat_list[0].blk_cnt = htons(1);
		if (frame_type == OS_FRAME_TYPE_MARKER)
			dat->dat_list[0].flags = OS_DAT_FLAGS_MARK;
		else
			dat->dat_list[0].flags = OS_DAT_FLAGS_DATA;
		dat->dat_list[0].reserved = 0;
	} else
		aux->next_mark_addr = htonl(STp->first_mark_addr);
	aux->filemark_cnt = ntohl(STp->filemark_cnt);
	aux->phys_fm = ntohl(0xffffffff);
	aux->last_mark_addr = ntohl(STp->last_mark_addr);
}

/*
 * Verify that we have the correct tape frame
 */
static int osst_verify_frame(Scsi_Tape * STp, int logical_blk_num, int quiet)
{
	os_aux_t       * aux  = STp->buffer->aux;
        os_partition_t * par  = &(aux->partition);
	ST_partstat    * STps = &(STp->ps[STp->partition]);
        int              i;
	int		 dev  = TAPE_NR(STp->devt);

        if (STp->raw) {
                if (STp->buffer->last_result_fatal) {
                        for (i=0; i < STp->buffer->sg_segs; i++)
                                memset(STp->buffer->sg[i].address, 0, STp->buffer->sg[i].length);
                        strcpy(STp->buffer->b_data, "READ ERROR ON FRAME");
                }
                return 1;
        }
        if (STp->buffer->last_result_fatal) {
                printk(KERN_INFO "osst%d: skipping frame, read error\n", dev);
                return 0;
        }
        if (ntohl(aux->format_id) != 0) {
                printk(KERN_INFO "osst%d: skipping frame, format_id %u\n", dev, ntohl(aux->format_id));
                return 0;
        }
        if (memcmp(aux->application_sig, STp->application_sig, 4) != 0) {
                printk(KERN_INFO "osst%d: skipping frame, incorrect application signature\n", dev);
                return 0;
        }
        if (aux->frame_type != OS_FRAME_TYPE_DATA &&
            aux->frame_type != OS_FRAME_TYPE_EOD &&
            aux->frame_type != OS_FRAME_TYPE_MARKER) {
                printk(KERN_INFO "osst%d: skipping frame, frame type %x\n", dev, aux->frame_type);
                return 0;
        }
        if (par->partition_num != OS_DATA_PARTITION) {
                if (!STp->linux_media || STp->linux_media_version != 2) {
                        printk(KERN_INFO "osst%d: skipping frame, partition num %d\n", dev, par->partition_num);              		    return 0;
                }
        }
        if (par->par_desc_ver != OS_PARTITION_VERSION) {
                printk(KERN_INFO "osst%d: skipping frame, partition version %d\n", dev, par->par_desc_ver);
                return 0;
        }
        if (ntohs(par->wrt_pass_cntr) != STp->wrt_pass_cntr) {
                printk(KERN_INFO "osst%d: skipping frame, wrt_pass_cntr %d (expected %d)\n", 
				 dev, ntohs(par->wrt_pass_cntr), STp->wrt_pass_cntr);
                return 0;
        }
        if (aux->frame_seq_num != aux->logical_blk_num) {
                printk(KERN_INFO "osst%d: skipping frame, seq != logical\n", dev);
                return 0;
        }
	STp->logical_blk_in_buffer = 1;

        if (logical_blk_num != -1 && ntohl(aux->logical_blk_num) != logical_blk_num) {
                if (!quiet)
                        printk(KERN_INFO "osst%d: skipping frame, logical_blk_num %u (expected %d)\n", 
					 dev, ntohl(aux->logical_blk_num), logical_blk_num);
                return 0;
        }
        if (aux->frame_type == OS_FRAME_TYPE_MARKER) {
                STps->eof = ST_FM_HIT;
        }
        if (aux->frame_type == OS_FRAME_TYPE_EOD) {
                STps->eof = ST_EOD_1;
        }
        return 1;
}

static int osst_update_stats(Scsi_Tape * STp)
{
	unsigned char	cmd[10];
	Scsi_Cmnd     * SCpnt;
#if DEBUG
	int		dev  = TAPE_NR(STp->devt);
#endif

	memset(cmd, 0, 10);
	cmd[0] = MODE_SENSE;
	cmd[1] = 8;
	cmd[2] = BUFFER_FILLING_PAGE;
	cmd[4] = BUFFER_FILLING_PAGE_LENGTH + MODE_HEADER_LENGTH;

	SCpnt = st_do_scsi(NULL, STp, cmd, cmd[4], STp->timeout, 0, TRUE);
	if (!SCpnt) return (-EBUSY);

	STp->max_frames = STp->buffer->b_data[4 + 2];
	STp->cur_frames = STp->buffer->b_data[4 + 3];

#if DEBUG
	printk(ST_DEB_MSG "osst%i: %d of max %d buffers queued\n", dev, 
		STp->cur_frames, STp->max_frames);
#endif

	scsi_release_command(SCpnt);
	return 0;
}

/*
 * Wait for the unit to become Ready
 */
static int osst_wait_ready(Scsi_Tape * STp, unsigned timeout)
{
	unsigned char	cmd[10];
	Scsi_Cmnd     * SCpnt;
	long		startwait = jiffies;
#if DEBUG
	int		dbg = debugging;
	int		dev  = TAPE_NR(STp->devt);

	printk(ST_DEB_MSG "osst%d: reached onstream wait ready\n", dev);
#endif

	memset(cmd, 0, 10);
	cmd[0] = TEST_UNIT_READY;

	SCpnt = st_do_scsi(NULL, STp, cmd, 0, STp->long_timeout, MAX_READY_RETRIES,
			   TRUE);
	if (!SCpnt) return (-EBUSY);

	while ( SCpnt->sense_buffer[2] == 2 && SCpnt->sense_buffer[12] == 4 &&
		( SCpnt->sense_buffer[13] == 1 || SCpnt->sense_buffer[13] == 8 ) && 
		time_before(jiffies, startwait + timeout*HZ) ) {
#if DEBUG
	    if (debugging) {
		printk(ST_DEB_MSG "osst%d: Sleeping in onstream wait ready\n", dev);
		printk(ST_DEB_MSG "osst%d: Turning off debugging for a while\n", dev);
		debugging = 0;
	    }
#endif
	    current->state = TASK_INTERRUPTIBLE;
	    schedule_timeout(HZ / 10);

	    memset(cmd, 0, 10);
	    cmd[0] = TEST_UNIT_READY;

	    SCpnt = st_do_scsi(SCpnt, STp, cmd, 0, STp->long_timeout, MAX_READY_RETRIES,
			       TRUE);
	}
#if DEBUG
	debugging = dbg;
#endif
	if ( SCpnt->sense_buffer[2] &&
	     osst_write_error_recovery(STp, &SCpnt, 0) ) {
#if DEBUG
	    printk(ST_DEB_MSG "osst%d: abnormal exit from onstream wait ready\n", dev);
#endif
	    scsi_release_command(SCpnt);
	    return (-EIO);
	}
#if DEBUG
	printk(ST_DEB_MSG "osst%d: normal exit from onstream wait ready\n", dev);
#endif

	scsi_release_command(SCpnt);
	return 0;
}

static int osst_position_tape_and_confirm(Scsi_Tape * STp, int frame)
{
	int           retval;

	osst_wait_ready(STp, 5 * 60);
	retval = osst_set_frame_position(STp, frame, 0);
	if (retval) return (retval);
	return (osst_get_frame_position(STp));
}

/*
 * Wait for write(s) to complete
 */
static int osst_flush_drive_buffer(Scsi_Tape * STp)
{
	unsigned char	cmd[10];
	Scsi_Cmnd     * SCpnt;
#if DEBUG
	int		dev  = TAPE_NR(STp->devt);

	printk(ST_DEB_MSG "osst%d: reached onstream flush drive buffer (write filemark)\n", dev);
#endif

	memset(cmd, 0, 10);
	cmd[0] = WRITE_FILEMARKS;
	cmd[1] = 1;

	SCpnt = st_do_scsi(NULL, STp, cmd, 0, STp->timeout, MAX_WRITE_RETRIES,
			   TRUE);
	if (!SCpnt) return (-EBUSY);

	if ((STp->buffer)->last_result_fatal)
		osst_write_error_recovery(STp, &SCpnt, 0);

	scsi_release_command(SCpnt);

	return (osst_wait_ready(STp, 5 * 60));
}

#define OSST_POLL_PER_SEC 10
static int osst_wait_frame(Scsi_Tape * STp, int curr, int minlast, int to)
{
	long startwait = jiffies;
#ifdef DEBUG	
	int  dev = TAPE_NR(STp->devt);
	char notyetprinted = 1;
#endif
	while (time_before (jiffies, startwait + to*HZ))
	{ 
		int result;
		result = osst_get_frame_position (STp);
		if (result < 0) break;
		if (STp->first_frame_position == curr &&
		    (signed)STp->last_frame_position > (signed)curr + minlast &&
		    result >= 0)
		{
#ifdef DEBUG			
			if (jiffies - startwait >= 2*HZ/OSST_POLL_PER_SEC)
				printk ("osst%i: Succ wait f fr %i (>%i): %i-%i %i (%i): %3li.%li s\n",
					dev, curr, curr+minlast, STp->first_frame_position,
					STp->last_frame_position, STp->cur_frames,
					result, (jiffies-startwait)/HZ, 
					(((jiffies-startwait)%HZ)*10)/HZ);
#endif
			return 0;
		}
#ifdef DEBUG
		if (jiffies - startwait >= 2*HZ/OSST_POLL_PER_SEC && notyetprinted)
		{
			printk ("osst%i: Wait for frame %i (>%i): %i-%i %i (%i)\n",
				dev, curr, curr+minlast, STp->first_frame_position,
				STp->last_frame_position, STp->cur_frames, result);
			notyetprinted--;
		}
#endif
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout (HZ / OSST_POLL_PER_SEC);
	}
#ifdef DEBUG
	printk ("osst%i: Fail wait f fr %i (>%i): %i-%i %i: %3li.%li s\n",
		dev, curr, curr+minlast, STp->first_frame_position,
		STp->last_frame_position, STp->cur_frames,
		(jiffies-startwait)/HZ, (((jiffies-startwait)%HZ)*10)/HZ);
#endif	
	return -EBUSY;
}

/*
 * Read the next OnStream tape block at the current location
 */
static int osst_read_block(Scsi_Tape * STp, Scsi_Cmnd ** aSCpnt, int timeout)
{
	unsigned char	cmd[10];
	Scsi_Cmnd     * SCpnt  = * aSCpnt;
	int		retval = 0;
#if DEBUG
	os_aux_t      * aux    = STp->buffer->aux;
	int		dev    = TAPE_NR(STp->devt);
#endif

	/* TODO: Error handling */
	if (OS_NEED_POLL(STp->os_fw_rev))
		retval = osst_wait_frame (STp, STp->first_frame_position, 0, timeout);
#if 0//def DEBUG
	printk ("osst_read: wait for frame returned %i\n", retval);
#endif
	
	memset(cmd, 0, 10);
	cmd[0] = READ_6;
	cmd[1] = 1;
	cmd[4] = 1;

#if DEBUG
	if (debugging)
	    printk(ST_DEB_MSG "osst%i: Reading block from OnStream tape\n", dev);
#endif
	SCpnt = st_do_scsi(SCpnt, STp, cmd, STp->frame_size, STp->timeout, MAX_RETRIES, TRUE);
	*aSCpnt = SCpnt;
        if (!SCpnt)
	    return (-EBUSY);

	if ((STp->buffer)->last_result_fatal) {
	    retval = 1;
#if DEBUG
	    if (debugging)
	        printk(ST_DEB_MSG "osst%d: Sense: %2x %2x %2x %2x %2x %2x %2x %2x\n",
		   dev,
		   SCpnt->sense_buffer[0], SCpnt->sense_buffer[1],
		   SCpnt->sense_buffer[2], SCpnt->sense_buffer[3],
		   SCpnt->sense_buffer[4], SCpnt->sense_buffer[5],
		   SCpnt->sense_buffer[6], SCpnt->sense_buffer[7]);
#endif
	}
	else
	    STp->first_frame_position++;
#if DEBUG
	if (debugging) {
	   printk(ST_DEB_MSG "osst%i: AUX: %s #%d %s seq#%d log#%d\n", dev, aux->application_sig,
			ntohl(aux->update_frame_cntr), aux->frame_type==1?"EOD":aux->frame_type==2?"MARK":
			aux->frame_type==8?"HEADR":aux->frame_type==0x80?"DATA":"FILL", 
			ntohl(aux->frame_seq_num), ntohl(aux->logical_blk_num) );
	   if (aux->frame_type==2)
		printk(ST_DEB_MSG "osst%i: mark_cnt=%d, last_mark=%d, next_mark=%d\n", dev,
			ntohl(aux->filemark_cnt), ntohl(aux->last_mark_addr), ntohl(aux->next_mark_addr));
	   printk(ST_DEB_MSG "osst%i: Exit read block from OnStream tape with code %d\n", dev, retval);
	}
#endif
	return (retval);
}

static int osst_initiate_read(Scsi_Tape * STp, Scsi_Cmnd ** aSCpnt)
{
        ST_partstat   * STps   = &(STp->ps[STp->partition]);
	Scsi_Cmnd     * SCpnt  = * aSCpnt;
	unsigned char	cmd[10];
	int		retval = 0;
#if DEBUG
	int		dev    = TAPE_NR(STp->devt);
#endif

        if (STps->rw != ST_READING) {         /* Initialize read operation */
                if (STps->rw == ST_WRITING) {
                        osst_flush_write_buffer(STp, 1);
			osst_flush_drive_buffer(STp);
                }
                STps->rw = ST_READING;
#if 0
                STp->logical_blk_num = 0;
#endif
		STp->logical_blk_in_buffer = 0;

                /*
                 *      Issue a read 0 command to get the OnStream drive
                 *      read blocks into its buffer.
                 */
		memset(cmd, 0, 10);
		cmd[0] = READ_6;
		cmd[1] = 1;

#if DEBUG
		printk(ST_DEB_MSG "osst%i: Start Read Ahead on OnStream tape\n", dev);
#endif
		SCpnt   = st_do_scsi(SCpnt, STp, cmd, 0, STp->timeout, MAX_RETRIES, TRUE);
		*aSCpnt = SCpnt;
		retval  = STp->buffer->last_result_fatal;
        }

        return retval;
}

static int osst_get_logical_blk(Scsi_Tape * STp, int logical_blk_num, int quiet)
{
	ST_partstat * STps  = &(STp->ps[STp->partition]);
	Scsi_Cmnd   * SCpnt = NULL;
	int           dev   = TAPE_NR(STp->devt);
        int           cnt   = 0,
		      x,
		      position;

        /*
         * Search and wait for the next logical tape block
         */
        while (1) {
                if (cnt++ > 100) {
                        printk(KERN_INFO "osst%d: couldn't find logical block %d, aborting\n", dev, logical_blk_num);
			if (SCpnt)
				scsi_release_command(SCpnt);
                        return (-EIO);
                }
#if DEBUG
		if (debugging)
			printk(ST_DEB_MSG "osst%d: Looking for block %d, attempt %d\n", dev, logical_blk_num, cnt);
#endif
                if (osst_initiate_read(STp, &SCpnt)) {
                        position = osst_get_frame_position(STp);
#if DEBUG
                        printk(KERN_INFO "osst%d: blank block detected, positioning tape to block %d\n",
					 dev, position + 40);
#endif
                        osst_set_frame_position(STp, position + 40, 0);
                        cnt += 30;
                        continue;
                }
                if (!STp->logical_blk_in_buffer)
			osst_read_block(STp, &SCpnt, 30);
                if (osst_verify_frame(STp, logical_blk_num, quiet))
                        break;
                if (osst_verify_frame(STp, -1, quiet)) {
                        x = ntohl(STp->buffer->aux->logical_blk_num);
                        if (x > logical_blk_num) {
                                printk(KERN_ERR "osst%d: couldn't find logical block %d, aborting (block %d found)\n",
						dev, logical_blk_num, x);
			        if (SCpnt)
					scsi_release_command(SCpnt);
                                return (-EIO);
                        }
                }
		STp->logical_blk_in_buffer = 0;
        }
       
        STp->logical_blk_num = ntohl(STp->buffer->aux->logical_blk_num);

        if (SCpnt)
        	scsi_release_command(SCpnt);

#if DEBUG
	if (debugging || STps->eof)
		printk(ST_DEB_MSG "osst%i: Exit get logical block (%d=>%d) from OnStream tape with code %d\n",							 dev, logical_blk_num, STp->logical_blk_num, STps->eof);
#endif
        return (STps->eof);
}

static int osst_seek_logical_blk(Scsi_Tape * STp, int logical_blk_num)
{
	int  estimate;
	int  retries = 0;
	int  dev     = TAPE_NR(STp->devt);

	if (logical_blk_num < 0) logical_blk_num = 0;
	/* FIXME -- this may not be valid for foreign formats */
	if (logical_blk_num < 2980) estimate  = logical_blk_num + 10;
	else			    estimate  = logical_blk_num + 20;

	if (osst_get_logical_blk(STp, logical_blk_num, 1) >= 0)
	   return 0;
#if DEBUG
	printk(ST_DEB_MSG "osst%d: Seeking logical block %d (now at %d)\n",
			  dev, logical_blk_num, STp->logical_blk_num);
#endif
	while (++retries < 10) {
	   osst_set_frame_position(STp, estimate, 0);
	   if (osst_get_logical_blk(STp, logical_blk_num, 1) >= 0)
	      return 0;
	   if (osst_get_logical_blk(STp, -1, 1) < 0)
	      goto error;
	   if (STp->logical_blk_num < logical_blk_num)
	      estimate += logical_blk_num - STp->logical_blk_num;
	   else
	      break;
	}
error:
	printk(KERN_INFO "osst%d: Couldn't seek to logical block %d (at %d), %d retries\n", 
			 dev, logical_blk_num, STp->logical_blk_num, retries);
	return (-EIO);
}


/*
 * Read back the drive's internal buffer contents, as a part
 * of the write error recovery mechanism for old OnStream
 * firmware revisions.
 */
static int osst_read_back_buffer_and_rewrite(Scsi_Tape * STp, Scsi_Cmnd ** aSCpnt,
					unsigned int block, unsigned int skip, int pending)
{
	Scsi_Cmnd     * SCpnt = * aSCpnt;
	unsigned char * buffer, * p;
	unsigned char	cmd[10];
	int             frames, flag, new_block, i, logical_blk_num;
	int		dev  = TAPE_NR(STp->devt);

	osst_update_stats(STp);
	frames = STp->cur_frames;
	if ((buffer = (unsigned char *)vmalloc((frames + pending) * STp->block_size)) == NULL)
		return (-EIO);

	logical_blk_num = STp->logical_blk_num - frames - pending;
	printk(KERN_INFO "osst%d: reading back %d frames from the drive's internal buffer\n", dev, frames);

	if (pending)
		memcpy(&buffer[frames * STp->block_size], STp->buffer->b_data, STp->block_size);

	for (i = 0, p = buffer; i < frames; i++, p += STp->block_size) {

		memset(cmd, 0, 10);
		cmd[0] = 0x3C;		/* Buffer Read           */
		cmd[1] = 6;		/* Retrieve Faulty Block */
		cmd[8] = 1;		/* One at a time         */

		SCpnt = st_do_scsi(SCpnt, STp, cmd, STp->frame_size, STp->timeout, MAX_RETRIES, TRUE);
	
		if ((STp->buffer)->last_result_fatal) {
			printk(KERN_ERR "osst%d: Failed to read block back from OnStream buffer\n", dev);
			vfree((void *)buffer);
			*aSCpnt = SCpnt;
			return (-EIO);
		}
		memcpy(p, STp->buffer->b_data, STp->block_size);
#if DEBUG
		if (debugging)
			printk(KERN_INFO "osst%d: read back logical block %d, data %x %x %x %x\n",
					  dev, logical_blk_num + i, p[0], p[1], p[2], p[3]);
#endif
	}
	osst_update_stats(STp);
#if DEBUG
	printk(KERN_INFO "osst %d: frames left in buffer: %d\n", dev, STp->cur_frames);
#endif
	/* Write synchronously so we can be sure we're OK again and don't have to recover recursively */

	for (flag=1, new_block=block, p=buffer, i=0; i < frames + pending; i++) {

		if (flag) {
			new_block += skip;
			osst_position_tape_and_confirm(STp, new_block);
			flag = 0;
		}
		memcpy(STp->buffer->b_data, p, STp->block_size);
		osst_init_aux(STp, OS_FRAME_TYPE_DATA, STp->logical_blk_num );
		memset(cmd, 0, 10);
		cmd[0] = WRITE_6;
		cmd[1] = 1;
		cmd[4] = 1;

		SCpnt = st_do_scsi(SCpnt, STp, cmd, STp->frame_size, STp->timeout, MAX_WRITE_RETRIES, TRUE);

		if (STp->buffer->last_result_fatal) {
			if (new_block > block + 1000) {
				printk(KERN_ERR "osst%d: Failed to find valid tape media\n", dev);
				vfree((void *)buffer);
				*aSCpnt = SCpnt;
				return (-EIO);
			}
			flag = 1;
			i--;
		}
		else {
			p += STp->block_size;
			STp->logical_blk_num++;
			new_block++;
		}
	}    
	vfree((void *)buffer);
	*aSCpnt = SCpnt;
	return 0;
}

/*
 * Error recovery algorithm for the OnStream tape.
 */

static int osst_write_error_recovery(Scsi_Tape * STp, Scsi_Cmnd ** aSCpnt, int pending)
{
	Scsi_Cmnd  * SCpnt  = * aSCpnt;
	int          dev    = TAPE_NR(STp->devt);
	int          retval = 0;
	unsigned int block, skip;

	if ((SCpnt->sense_buffer[ 2] & 0x0f) != 3
	  || SCpnt->sense_buffer[12]         != 12
	  || SCpnt->sense_buffer[13]         != 0)
		return (-EIO);

	block =	(SCpnt->sense_buffer[3] << 24) |
	        (SCpnt->sense_buffer[4] << 16) |
		(SCpnt->sense_buffer[5] <<  8) |
		 SCpnt->sense_buffer[6];
	skip  =  SCpnt->sense_buffer[9];
 
	printk(KERN_ERR "osst%d: detected physical bad block at %u\n", dev, block);
	osst_update_stats(STp);
	printk(KERN_ERR "osst%d: relocating %d buffered logical blocks to physical block %u\n",
			dev, STp->cur_frames, block + skip);
	osst_update_stats(STp);
	if (STp->os_fw_rev >= 10600)
		osst_set_frame_position(STp, block + skip, 1);
	else
		retval = osst_read_back_buffer_and_rewrite(STp, aSCpnt, block, skip, pending);

	osst_get_frame_position(STp);
#if DEBUG
	printk(KERN_ERR "osst%d: positioning complete, cur_frames %d, pos %d, tape pos %d\n", 
			dev, STp->cur_frames, STp->first_frame_position, STp->last_frame_position);
#endif
	return retval;
}

static int osst_space_over_filemarks_backward(Scsi_Tape * STp, int mt_op, int mt_count)
{
	int           dev = TAPE_NR(STp->devt);
	int           cnt = 0;
	int           last_mark_addr;

#if DEBUG
	printk(KERN_INFO "osst%d: Reached space_over_filemarks_backwards %d %d\n", dev, mt_op, mt_count);
#endif
	if (osst_get_logical_blk(STp, -1, 0) < 0) {
		printk(KERN_INFO "osst%i: couldn't get logical blk num in space_filemarks_bwd\n", dev);
		return -EIO;
	}
	while (cnt != mt_count) {
		last_mark_addr = ntohl(STp->buffer->aux->last_mark_addr);
		if (last_mark_addr == -1)
			return (-EIO);
#if DEBUG
		printk(KERN_INFO "osst%i: positioning to last mark at %d\n", dev, last_mark_addr);
#endif
		osst_set_frame_position(STp, last_mark_addr, 0);
		cnt++;
		if (osst_get_logical_blk(STp, -1, 0) < 0) {
			printk(KERN_INFO "osst%i: couldn't get logical blk num in space_filemarks\n", dev);
			return (-EIO);
		}
		if (STp->buffer->aux->frame_type != OS_FRAME_TYPE_MARKER) {
			printk(KERN_INFO "osst%i: expected to find marker at block %d, not found\n", dev, last_mark_addr);
			return (-EIO);
		}
	}
	if (mt_op == MTBSFM) {
		STp->logical_blk_num++;
		STp->logical_blk_in_buffer = 0;
	}
	return 0;
}

/*
 * ADRL 1.1 compatible "slow" space filemarks fwd version
 *
 * Just scans for the filemark sequentially.
 */
static int osst_space_over_filemarks_forward_slow(Scsi_Tape * STp, int mt_op, int mt_count)
{
	int           dev = TAPE_NR(STp->devt);
	int           cnt = 0;

#if DEBUG
	printk(KERN_INFO "osst%d: Reached space_over_filemarks_forward_slow %d %d\n", dev, mt_op, mt_count);
#endif
	if (osst_get_logical_blk(STp, -1, 0) < 0) {
		printk(KERN_INFO "osst%i: couldn't get logical blk num in space_filemarks_fwd\n", dev);
		return (-EIO);
	}
	while (1) {
		if (osst_get_logical_blk(STp, -1, 0) < 0) {
			printk(KERN_INFO "osst%i: couldn't get logical blk num in space_filemarks\n", dev);
			return (-EIO);
		}
		if (STp->buffer->aux->frame_type == OS_FRAME_TYPE_MARKER)
			cnt++;
		if (STp->buffer->aux->frame_type == OS_FRAME_TYPE_EOD) {
#if DEBUG
			printk(KERN_INFO "osst%i: space_fwd: EOD reached\n", dev);
#endif
			return (-EIO);
		}
		if (cnt == mt_count)
			break;
		STp->logical_blk_in_buffer = 0;
	}
	if (mt_op == MTFSF) {
		STp->logical_blk_num++;
		STp->logical_blk_in_buffer = 0;
	}
	return 0;
}

/*
 * Fast linux specific version of OnStream FSF
 */
static int osst_space_over_filemarks_forward_fast(Scsi_Tape * STp, int mt_op, int mt_count)
{
	int           dev = TAPE_NR(STp->devt);
	int           cnt = 0,
		      next_mark_addr;

#if DEBUG
	printk(KERN_INFO "osst%d: Reached space_over_filemarks_forward_fast %d %d\n", dev, mt_op, mt_count);
#endif
	if (osst_get_logical_blk(STp, -1, 0) < 0) {
		printk(KERN_INFO "osst%i: couldn't get logical blk num in space_filemarks_fwd\n", dev);
		return (-EIO);
	}

	/*
	 * Find nearest (usually previous) marker
	 */
	while (1) {
		if (STp->buffer->aux->frame_type == OS_FRAME_TYPE_MARKER)
			break;
		if (STp->buffer->aux->frame_type == OS_FRAME_TYPE_EOD) {
#if DEBUG
			printk(KERN_INFO "osst%i: space_fwd: EOD reached\n", dev);
#endif
			return (-EIO);
		}
		if (ntohl(STp->buffer->aux->filemark_cnt) == 0) {
			if (STp->first_mark_addr == -1) {
				printk(KERN_INFO "osst%i: reverting to slow filemark space\n", dev);
				return osst_space_over_filemarks_forward_slow(STp, mt_op, mt_count);
			}
			osst_set_frame_position(STp, STp->first_mark_addr, 0);
			if (osst_get_logical_blk(STp, -1, 0) < 0) {
				printk(KERN_INFO "osst%i: couldn't get logical blk num in space_filemarks_fwd_fast\n", dev);
				return (-EIO);
			}
			if (STp->buffer->aux->frame_type != OS_FRAME_TYPE_MARKER) {
				printk(KERN_INFO "osst%i: expected to find filemark at %d\n", dev, STp->first_mark_addr);
				return (-EIO);
			}
		} else {
			if (osst_space_over_filemarks_backward(STp, MTBSF, 1) < 0)
				return (-EIO);
			mt_count++;
		}
	}
	cnt++;
	while (cnt != mt_count) {
		next_mark_addr = ntohl(STp->buffer->aux->next_mark_addr);
		if (!next_mark_addr || next_mark_addr > STp->eod_frame_addr) {
			printk(KERN_INFO "osst%i: reverting to slow filemark space\n", dev);
			return osst_space_over_filemarks_forward_slow(STp, mt_op, mt_count - cnt);
		}
#if DEBUG
		else printk(KERN_INFO "osst%i: positioning to next mark at %d\n", dev, next_mark_addr);
#endif
		osst_set_frame_position(STp, next_mark_addr, 0);
		cnt++;
		if (osst_get_logical_blk(STp, -1, 0) < 0) {
			printk(KERN_INFO "osst%i: couldn't get logical blk num in space_filemarks\n", dev);
			return (-EIO);
		}
		if (STp->buffer->aux->frame_type != OS_FRAME_TYPE_MARKER) {
			printk(KERN_INFO "osst%i: expected to find marker at block %d, not found\n", dev, next_mark_addr);
			return (-EIO);
		}
	}
	if (mt_op == MTFSF) 
		STp->logical_blk_num++;
		STp->logical_blk_in_buffer = 0;
	return 0;
}

/*
 * In debug mode, we want to see as many errors as possible
 * to test the error recovery mechanism.
 */
#if DEBUG
static void osst_set_retries(Scsi_Tape * STp, Scsi_Cmnd ** aSCpnt, int retries)
{
	unsigned char	cmd[10];
	Scsi_Cmnd     * SCpnt  = * aSCpnt;
	int		dev  = TAPE_NR(STp->devt);

	memset(cmd, 0, 10);
	cmd[0] = MODE_SELECT;
	cmd[1] = 0x10;
	cmd[4] = NUMBER_RETRIES_PAGE_LENGTH + MODE_HEADER_LENGTH;

	(STp->buffer)->b_data[0] = cmd[4] - 1;
	(STp->buffer)->b_data[1] = 0;			/* Medium Type - ignoring */
	(STp->buffer)->b_data[2] = 0;			/* Reserved */
	(STp->buffer)->b_data[3] = 0;			/* Block Descriptor Length */
	(STp->buffer)->b_data[MODE_HEADER_LENGTH + 0] = NUMBER_RETRIES_PAGE | (1 << 7);
	(STp->buffer)->b_data[MODE_HEADER_LENGTH + 1] = 2;
	(STp->buffer)->b_data[MODE_HEADER_LENGTH + 2] = 4;
	(STp->buffer)->b_data[MODE_HEADER_LENGTH + 3] = retries;

	if (debugging)
	    printk(ST_DEB_MSG "osst%i: Setting number of retries on OnStream tape to %d\n", dev, retries);

	SCpnt = st_do_scsi(SCpnt, STp, cmd, cmd[4], STp->timeout, 0, TRUE);
	*aSCpnt = SCpnt;

	if ((STp->buffer)->last_result_fatal)
	    printk (KERN_ERR "osst%d: Couldn't set retries to %d\n", dev, retries);
}
#endif

static void osst_update_last_marker(Scsi_Tape * STp, int last_mark_addr, int next_mark_addr)
{
	Scsi_Cmnd   * SCpnt = NULL;
	int           dev = TAPE_NR(STp->devt);
	int	      frame,
		      reslt;

	if (STp->raw)		  return;
	if (last_mark_addr == -1) return;

	osst_flush_drive_buffer(STp);
	frame = osst_get_frame_position(STp);
#if DEBUG
	printk(ST_DEB_MSG "osst%i: Update last_marker at frame %d\n", dev, last_mark_addr);
	printk(ST_DEB_MSG "osst%i: current position %d, lblk %d, tape blk %d\n",
			  dev, frame, STp->logical_blk_num, STp->last_frame_position);
#endif
	osst_set_frame_position(STp, last_mark_addr, 0);
	osst_initiate_read (STp, &SCpnt);
	reslt = osst_read_block(STp, &SCpnt, 180);
	if (SCpnt) scsi_release_command(SCpnt);
	if (reslt) {
		printk(KERN_WARNING "osst%i: couldn't read last marker\n", dev);
		osst_set_frame_position(STp, frame, 0);
		return;
	}
	if (STp->buffer->aux->frame_type  != OS_FRAME_TYPE_MARKER) {
		printk(KERN_WARNING "osst%i: expected marker at addr %d\n", dev, last_mark_addr);
		osst_set_frame_position(STp, frame, 0);
		return;
	}
#if DEBUG
	printk(ST_DEB_MSG "osst%i: writing back marker\n", dev);
#endif
	STp->buffer->aux->next_mark_addr = htonl(next_mark_addr);
	osst_set_frame_position(STp, last_mark_addr, 0);
	STp->dirty = 1;
	if (osst_flush_write_buffer(STp, 0)) {
		printk(KERN_WARNING "osst%i: couldn't write marker back at addr %d\n", dev, last_mark_addr);
		osst_set_frame_position(STp, frame, 0);
		return;
	}
	osst_flush_drive_buffer(STp);
	osst_set_frame_position(STp, frame, 0);	

	return;
}

static void osst_write_filemark(Scsi_Tape * STp)
{
	int	last_mark_addr;
#if DEBUG
	int	dev = TAPE_NR(STp->devt);
#endif

	if (STp->raw) return;

	last_mark_addr = osst_get_frame_position(STp);
#if DEBUG
	printk(ST_DEB_MSG "osst%i: Writing Filemark %i at %d\n", 
	       dev, STp->filemark_cnt, STp->logical_blk_num);
#endif
	osst_init_aux(STp, OS_FRAME_TYPE_MARKER, STp->logical_blk_num++);
	STp->dirty = 1;
	osst_flush_write_buffer(STp, 0);
	if (STp->filemark_cnt)
		osst_update_last_marker(STp, STp->last_mark_addr, last_mark_addr);
	STp->last_mark_addr = last_mark_addr;
	if (STp->filemark_cnt++ == 0)
		STp->first_mark_addr = last_mark_addr;
	return;
}

static int osst_write_eod(Scsi_Tape * STp)
{
#if DEBUG
	int	dev = TAPE_NR(STp->devt);
#endif

	if (STp->raw) return 0;

	STp->eod_frame_addr = osst_get_frame_position(STp);
#if DEBUG
	printk(ST_DEB_MSG "osst%i: Writing EOD at %d=>%d\n", dev, STp->logical_blk_num, STp->eod_frame_addr);
#endif
	osst_init_aux(STp, OS_FRAME_TYPE_EOD, STp->logical_blk_num);
	STp->dirty = 1;
	
	return osst_flush_write_buffer(STp, 0);
}

static int __osst_write_header(Scsi_Tape * STp, int block, int count)
{
	os_header_t   header;
	int	      dev = TAPE_NR(STp->devt);

#if DEBUG
	printk(ST_DEB_MSG "osst%i: reached onstream write header group %d\n", dev, block);
#endif
	osst_init_aux(STp, OS_FRAME_TYPE_HEADER, STp->logical_blk_num);
	osst_wait_ready(STp, 60 * 5);
	osst_set_frame_position(STp, block, 0);
	memset(&header, 0, sizeof(header));
	strcpy(header.ident_str, "ADR_SEQ");
	header.major_rev = 1;
	header.minor_rev = 2;
	header.par_num = 1;
	header.partition.partition_num = OS_DATA_PARTITION;
	header.partition.par_desc_ver = OS_PARTITION_VERSION;
	header.partition.first_frame_addr = htonl(STp->first_data_addr);
	header.partition.last_frame_addr = htonl(STp->capacity);
	header.partition.wrt_pass_cntr = htons(STp->wrt_pass_cntr);
	header.partition.eod_frame_addr = htonl(STp->eod_frame_addr);
	while (count--) {
	    memcpy(STp->buffer->b_data, &header, sizeof(header));
	    STp->buffer->buffer_bytes = sizeof(header);
	    STp->dirty   = 1;
	    if (osst_flush_write_buffer(STp, 0)) {
		printk(KERN_INFO "osst%i: couldn't write header frame\n", dev);
		return (-EIO);
	    }
	}
#if DEBUG
	printk(ST_DEB_MSG "osst%i: exiting onstream write header group\n", dev);
#endif
	return osst_flush_drive_buffer(STp);
}

static int osst_write_header(Scsi_Tape * STp, int locate_eod)
{
	int reslt;
#if DEBUG
	int dev = TAPE_NR(STp->devt);

	printk(KERN_INFO "osst%i: writing tape header\n", dev);
#endif
	if (STp->raw) return 0;

	STp->update_frame_cntr++;
	reslt  = __osst_write_header(STp,     5, 5);
	reslt |= __osst_write_header(STp, 0xbae, 5);

	if (locate_eod) {
#if DEBUG
		printk(KERN_INFO "osst%i: locating back to eod frame addr %d\n", dev, STp->eod_frame_addr);
#endif
		osst_set_frame_position(STp, STp->eod_frame_addr, 0);
	}
	return reslt;
}


static int __osst_analyze_headers(Scsi_Tape * STp, int block, Scsi_Cmnd ** aSCpnt)
{
	int           dev = TAPE_NR(STp->devt);
	os_header_t * header;
	os_aux_t    * aux;
	char          id_string[8];

	if (STp->raw) {
		STp->header_ok = STp->linux_media = 1;
		return 1;
	}
	STp->header_ok = STp->linux_media = 0;
	STp->update_frame_cntr = 0;
	STp->wrt_pass_cntr = 0;
	STp->eod_frame_addr = STp->first_data_addr = 0x0000000A;
	STp->first_mark_addr = STp->last_mark_addr = -1;
#if DEBUG
	printk(KERN_INFO "osst%i: reading header\n", dev);
#endif
	if (osst_set_frame_position(STp, block, 0))
		printk(KERN_WARNING "osst%i: Couldn't position tape\n", dev);
	if (osst_initiate_read (STp, aSCpnt)) return 0;
	if (osst_read_block(STp, aSCpnt, 180)) {
		printk(KERN_INFO "osst%i: couldn't read header frame\n", dev);
		return 0;
	}
	header = (os_header_t *) STp->buffer->b_data;
	aux = STp->buffer->aux;
	if (strncmp(header->ident_str, "ADR_SEQ", 7) != 0 &&
	    strncmp(header->ident_str, "ADR-SEQ", 7) != 0) {
                strncpy(id_string, header->ident_str, 7);
		id_string[7] = 0;
		printk(KERN_INFO "osst%i: invalid header identification string %s\n", dev, id_string);
		return 0;
	}
	if (header->major_rev != 1 || (header->minor_rev != 2 && header->minor_rev != 1))
		printk(KERN_INFO "osst%i: warning: revision %d.%d detected (1.2 supported)\n", 
				 dev, header->major_rev, header->minor_rev);
	if (header->par_num != 1)
		printk(KERN_INFO "osst%i: warning: %d partitions defined, only one supported\n", 
				 dev, header->par_num);
	STp->wrt_pass_cntr = ntohs(header->partition.wrt_pass_cntr);
	STp->first_data_addr = ntohl(header->partition.first_frame_addr);
	STp->eod_frame_addr = ntohl(header->partition.eod_frame_addr);
	STp->filemark_cnt = ntohl(aux->filemark_cnt);
	STp->first_mark_addr = ntohl(aux->next_mark_addr);
	STp->last_mark_addr = ntohl(aux->last_mark_addr);
	STp->update_frame_cntr = ntohl(aux->update_frame_cntr);
	memcpy(STp->application_sig, aux->application_sig, 4); STp->application_sig[4] = 0;
	if (memcmp(STp->application_sig, "LIN", 3) == 0) {
		STp->linux_media = 1;
		STp->linux_media_version = STp->application_sig[3] - '0';
		if (STp->linux_media_version != 3)
			printk(KERN_INFO "osst%i: Linux media version %d detected (current 3)\n",
					 dev, STp->linux_media_version);
	} else {
		printk(KERN_INFO "osst%i: non Linux media detected (%s)\n", dev, STp->application_sig);
		STp->linux_media = 0;
	}
#if DEBUG
	printk(ST_DEB_MSG "osst%i: detected write pass counter %d, update frame counter %d, filemark counter %d\n",
			  dev, STp->wrt_pass_cntr, STp->update_frame_cntr, STp->filemark_cnt);
	printk(ST_DEB_MSG "osst%i: first frame on tape = %d, last = %d, eod frame = %d\n", dev,
			  STp->first_data_addr,
			  ntohl(header->partition.last_frame_addr),
			  ntohl(header->partition.eod_frame_addr));
	printk(ST_DEB_MSG "osst%i: first mark on tape = %d, last = %d, eod frame = %d\n", 
			  dev, STp->first_mark_addr, STp->last_mark_addr, STp->eod_frame_addr);
#endif
	return 1;
}

static int osst_analyze_headers(Scsi_Tape * STp, Scsi_Cmnd ** aSCpnt)
{
	int position, block;
#if DEBUG
	int dev = TAPE_NR(STp->devt);
#endif

        position = osst_get_frame_position(STp);

	if (STp->raw) {
		STp->header_ok = STp->linux_media = 1;
		return 1;
	}
	STp->header_ok = STp->linux_media = 0;

	for (block = 5; block < 10; block++)
		if (__osst_analyze_headers(STp, block, aSCpnt))
			goto ok;
	/* let's try both the ADR 1.1 and the ADR 1.2 locations... */
	for (block = 0xbae; block < 0xbb8; block++)
		if (__osst_analyze_headers(STp, block, aSCpnt))
			goto ok;
#if DEBUG
	printk(KERN_ERR "osst%i: failed to find valid ADRL header\n", dev);
#endif
	return 0;
ok:
	if (position < STp->first_data_addr)
		position = STp->first_data_addr;
	
	osst_set_frame_position(STp, position, 0);
	STp->header_ok = 1;

	return 1;
}


/* Acc. to OnStream, the vers. numbering is the following:
 * X.XX for released versions (X=digit), 
 * XXXY for unreleased versions (Y=letter)
 * Ordering 1.05 < 106A < 106a < 106B < ... < 1.06
 * This fn makes monoton numbers out of this scheme ...
 */
static unsigned int osst_parse_firmware_rev (const char * str)
{
	unsigned int rev;
	if (str[1] == '.') {
		rev = (str[0]-0x30)*10000
			+(str[2]-0x30)*1000
			+(str[3]-0x30)*100;
	} else {
		rev = (str[0]-0x30)*10000
			+(str[1]-0x30)*1000
			+(str[2]-0x30)*100 - 100;
		rev += 2*(str[3] & 0x1f)
			+(str[3] >= 0x60? 1: 0);
	}
	return rev;
}

/*
 * Configure the OnStream SCII tape drive for default operation
 */
static int osst_configure_onstream(Scsi_Tape *STp)
{
	int                            dev = TAPE_NR(STp->devt);
	unsigned char                  cmd[10];
	Scsi_Cmnd                    * SCpnt = NULL;
        osst_mode_parameter_header_t * header;
        osst_block_size_page_t       * bs;
	osst_capabilities_page_t     * cp;
	osst_tape_paramtr_page_t     * prm;
	int                            drive_buffer_size;

	if (STp->ready != ST_READY) {
#if DEBUG
	    printk(ST_DEB_MSG "osst%i: Not Ready\n", dev);
#endif
	    return (-EIO);
	}
	
	if (STp->os_fw_rev < 10600) {
            printk("osst%i: Old OnStream firmware revision detected (%s)\n", 
                       dev, STp->device->rev);
            printk("osst%i: An upgrade to version 1.06 or above is recommended\n",
                       dev);
        }

        /*
         * Configure 32.5KB (data+aux) frame size.
         * Get the current block size from the block size mode page
         */
	memset(cmd, 0, 10);
	cmd[0] = MODE_SENSE;
	cmd[1] = 8;
	cmd[2] = BLOCK_SIZE_PAGE;
	cmd[4] = BLOCK_SIZE_PAGE_LENGTH + MODE_HEADER_LENGTH;

	SCpnt = st_do_scsi(SCpnt, STp, cmd, cmd[4], STp->timeout, 0, TRUE);
	if (SCpnt == NULL) {
#if DEBUG
 	    printk(ST_DEB_MSG "st: Busy\n");
#endif
	    return (-EBUSY);
	}
	dev = TAPE_NR(SCpnt->request.rq_dev);
	if ((STp->buffer)->last_result_fatal != 0) {
	    printk (KERN_ERR "osst%i: can't get tape block size mode page\n", dev);
	    scsi_release_command(SCpnt);
	    return (-EIO);
	}

        header = (osst_mode_parameter_header_t *) (STp->buffer)->b_data;
        bs = (osst_block_size_page_t *) ((STp->buffer)->b_data + sizeof(osst_mode_parameter_header_t) + header->bdl);

#if DEBUG
        printk(KERN_INFO "osst%i: 32KB play back: %s\n",   dev, bs->play32     ? "Yes" : "No");
        printk(KERN_INFO "osst%i: 32.5KB play back: %s\n", dev, bs->play32_5   ? "Yes" : "No");
        printk(KERN_INFO "osst%i: 32KB record: %s\n",      dev, bs->record32   ? "Yes" : "No");
        printk(KERN_INFO "osst%i: 32.5KB record: %s\n",    dev, bs->record32_5 ? "Yes" : "No");
#endif

        /*
         * Configure default auto columns mode, 32.5KB transfer mode
         */ 
        bs->one = 1;
        bs->play32 = 0;
        bs->play32_5 = 1;
        bs->record32 = 0;
        bs->record32_5 = 1;

	memset(cmd, 0, 10);
	cmd[0] = MODE_SELECT;
	cmd[1] = 0x10;
	cmd[4] = BLOCK_SIZE_PAGE_LENGTH + MODE_HEADER_LENGTH;

	SCpnt = st_do_scsi(SCpnt, STp, cmd, cmd[4], STp->timeout, 0, TRUE);
        if ((STp->buffer)->last_result_fatal != 0) {
            printk (KERN_ERR "osst%i: Couldn't set tape block size mode page\n", dev);
	    scsi_release_command(SCpnt);
	    return (-EIO);
	}

	STp->frame_size = OS_FRAME_SIZE;
	STp->block_size = (STp->raw) ? OS_FRAME_SIZE : OS_DATA_SIZE;
	STp->min_block  = STp->frame_size;
	STp->max_block  = STp->block_size;

#if DEBUG
        printk(KERN_INFO "osst%i: Block Size changed to 32.5K\n", dev);
         /*
         * In debug mode, we want to see as many errors as possible
         * to test the error recovery mechanism.
         */
        osst_set_retries(STp, &SCpnt, 0);
#endif

        /*
         * Set vendor name to 'LIN3' for "Linux support version 3".
         */

	memset(cmd, 0, 10);
	cmd[0] = MODE_SELECT;
	cmd[1] = 0x10;
	cmd[4] = VENDOR_IDENT_PAGE_LENGTH + MODE_HEADER_LENGTH;

	header->mode_data_length = VENDOR_IDENT_PAGE_LENGTH + MODE_HEADER_LENGTH - 1;
	header->medium_type      = 0;	/* Medium Type - ignoring */
	header->dsp              = 0;	/* Reserved */
	header->bdl              = 0;	/* Block Descriptor Length */
	
	(STp->buffer)->b_data[MODE_HEADER_LENGTH + 0] = VENDOR_IDENT_PAGE | (1 << 7);
	(STp->buffer)->b_data[MODE_HEADER_LENGTH + 1] = 6;
	(STp->buffer)->b_data[MODE_HEADER_LENGTH + 2] = 'L';
	(STp->buffer)->b_data[MODE_HEADER_LENGTH + 3] = 'I';
	(STp->buffer)->b_data[MODE_HEADER_LENGTH + 4] = 'N';
	(STp->buffer)->b_data[MODE_HEADER_LENGTH + 5] = '3';
	(STp->buffer)->b_data[MODE_HEADER_LENGTH + 6] = 0;
	(STp->buffer)->b_data[MODE_HEADER_LENGTH + 7] = 0;

	SCpnt = st_do_scsi(SCpnt, STp, cmd, cmd[4], STp->timeout, 0, TRUE);
        if ((STp->buffer)->last_result_fatal != 0) {
            printk (KERN_ERR "osst%i: Couldn't set vendor name to %s\n", dev, 
			(char *) ((STp->buffer)->b_data + MODE_HEADER_LENGTH + 2));
	    scsi_release_command(SCpnt);
	    return (-EIO);
	}

	memset(cmd, 0, 10);
	cmd[0] = MODE_SENSE;
	cmd[1] = 8;
	cmd[2] = CAPABILITIES_PAGE;
	cmd[4] = CAPABILITIES_PAGE_LENGTH + MODE_HEADER_LENGTH;

	SCpnt = st_do_scsi(SCpnt, STp, cmd, cmd[4], STp->timeout, 0, TRUE);

	if ((STp->buffer)->last_result_fatal != 0) {
	    printk (KERN_ERR "osst%i: can't get capabilities page\n", dev);
	    scsi_release_command(SCpnt);
	    return (-EIO);
	}

        header = (osst_mode_parameter_header_t *) (STp->buffer)->b_data;
	cp = (osst_capabilities_page_t *) ((STp->buffer)->b_data + sizeof(osst_mode_parameter_header_t) + header->bdl);

	drive_buffer_size = ntohs(cp->buffer_size) / 2;

	memset(cmd, 0, 10);
	cmd[0] = MODE_SENSE;
	cmd[1] = 8;
	cmd[2] = TAPE_PARAMTR_PAGE;
	cmd[4] = TAPE_PARAMTR_PAGE_LENGTH + MODE_HEADER_LENGTH;

	SCpnt = st_do_scsi(SCpnt, STp, cmd, cmd[4], STp->timeout, 0, TRUE);

	if ((STp->buffer)->last_result_fatal != 0) {
	    printk (KERN_ERR "osst%i: can't get tape parameter page\n", dev);
	    scsi_release_command(SCpnt);
	    return (-EIO);
	}

        header = (osst_mode_parameter_header_t *) (STp->buffer)->b_data;
	prm = (osst_tape_paramtr_page_t *) ((STp->buffer)->b_data + sizeof(osst_mode_parameter_header_t) + header->bdl);

	STp->density  = prm->density;
	STp->capacity = ntohs(prm->segtrk) * ntohs(prm->trks);
#if DEBUG
	printk(ST_DEB_MSG "osst%d: Density %d, tape length: %dMB, drive buffer size: %dKB\n",
			  dev, STp->density, STp->capacity / 32, drive_buffer_size);
#endif

	scsi_release_command(SCpnt);
	return 0;
	
}


/* Step over EOF if it has been inadvertently crossed (ioctl not used because
   it messes up the block number). */
	static int
cross_eof(Scsi_Tape *STp, int forward)
{
	int  result;
	int  dev  = TAPE_NR(STp->devt);

#if DEBUG
  if (debugging)
    printk(ST_DEB_MSG "osst%d: Stepping over filemark %s.\n",
	   		dev, forward ? "forward" : "backward");
#endif

	if (forward) {
	   /* assumes that the filemark is already read by the drive, so this is low cost */
	   result = osst_space_over_filemarks_forward_slow(STp, MTFSF, 1);
	}
	else
	   /* assumes this is only called if we just read the filemark! */
	   result = osst_seek_logical_blk(STp, STp->logical_blk_num - 1);

	if (result < 0)
	   printk(KERN_ERR "osst%d: Stepping over filemark %s failed.\n",
				dev, forward ? "forward" : "backward");

  return result;
}


/* Get the tape position. */

	static int
osst_get_frame_position(Scsi_Tape *STp)
{
    int result;
    unsigned char scmd[10];
    Scsi_Cmnd *SCpnt;
    /* KG: We want to be able to use it for checking Write Buffer availability
     *  and thus don't want to risk to overwrite anything. Exchange buffers ... */
    char mybuf[24];
    char * olddata = STp->buffer->b_data;
    int oldsize = STp->buffer->buffer_size;
#if DEBUG
    int dev = TAPE_NR(STp->devt);
#endif

    if (STp->ready != ST_READY)
      return (-EIO);

    memset (scmd, 0, 10);
    scmd[0] = READ_POSITION;
/*    scmd[1] = 1;*/

    STp->buffer->b_data = mybuf; STp->buffer->buffer_size = 24;
    SCpnt = st_do_scsi(NULL, STp, scmd, 20, STp->timeout, MAX_READY_RETRIES, TRUE);
    if (!SCpnt) {
	    STp->buffer->b_data = olddata; STp->buffer->buffer_size = oldsize;
	    return (-EBUSY);
    }

    if ((STp->buffer)->last_result_fatal != 0 ||
	(STp->device->scsi_level >= SCSI_2 &&
	 ((STp->buffer)->b_data[0] & 4) != 0)) {
      result = 0;
#if DEBUG
	printk(ST_DEB_MSG "osst%d: Can't read tape position.\n", dev);
#endif
      result = (-EIO);
    }
    else {
/*      if (((STp->buffer)->b_data[0] & 0x80) &&
	   (STp->buffer)->b_data[1] == 0) / * BOP of partition 0 * /
	    STp->ps[0].drv_block = STp->ps[0].drv_file = 0; FIXME -- there may be some place(s) this should now go */

      STp->first_frame_position = result
				= ((STp->buffer)->b_data[4] << 24)
				+ ((STp->buffer)->b_data[5] << 16)
				+ ((STp->buffer)->b_data[6] << 8)
				+  (STp->buffer)->b_data[7];
      STp->last_frame_position  = ((STp->buffer)->b_data[ 8] << 24)
				+ ((STp->buffer)->b_data[ 9] << 16)
				+ ((STp->buffer)->b_data[10] <<  8)
				+  (STp->buffer)->b_data[11];
      STp->cur_frames           =  (STp->buffer)->b_data[15];
#if 0 //def DEBUG
      if (debugging) {
	printk(ST_DEB_MSG "osst%d: Got tape pos. blk %d %s.\n", dev,
			   result, ((STp->buffer)->b_data[0]&0x80)?"(BOP)":
				   ((STp->buffer)->b_data[0]&0x40)?"(EOP)":"");
	printk(ST_DEB_MSG "osst%d: Device frames: host %d, tape %d, buffer %d\n", dev,
			   result, STp->last_frame_position, STp->cur_frames);
      }
#endif
      if (STp->cur_frames == 0 && result != STp->last_frame_position) {
#if DEBUG
	  printk(KERN_WARNING "osst%d: Correcting read position %d, %d, %d\n", dev,
				result, STp->last_frame_position, STp->cur_frames);
#endif
	  result = STp->first_frame_position = STp->last_frame_position;
      }
    }
    scsi_release_command(SCpnt);
    STp->buffer->b_data = olddata; STp->buffer->buffer_size = oldsize;
    SCpnt = NULL;

    return result;
}


/* Set the tape block */
	static int
osst_set_frame_position(Scsi_Tape *STp, unsigned int block, int skip)
{
    ST_partstat *STps;
    int result;
    int timeout;
    unsigned char scmd[10];
    Scsi_Cmnd *SCpnt;
#if DEBUG
    int dev = TAPE_NR(STp->devt);
#endif

    if (STp->ready != ST_READY)
      return (-EIO);
    timeout = STp->long_timeout;
    STps = &(STp->ps[STp->partition]);

#if DEBUG
    if (debugging)
      printk(ST_DEB_MSG "osst%d: Setting block to %d.\n", dev, block);
#endif

    memset (scmd, 0, 10);
    scmd[0] = SEEK_10;
    scmd[1] = 1;
    scmd[3] = (block >> 24);
    scmd[4] = (block >> 16);
    scmd[5] = (block >> 8);
    scmd[6] = block;
    if (skip)
      scmd[9] = 0x80;

    timeout = STp->timeout;

    SCpnt = st_do_scsi(NULL, STp, scmd, 20, timeout, MAX_READY_RETRIES, TRUE);
    if (!SCpnt)
      return (-EBUSY);

    STp->first_frame_position = STp->last_frame_position = block;
    STps->eof = ST_NOEOF;
    if ((STp->buffer)->last_result_fatal != 0) {
      result = (-EIO);
    }
    else {
      STps->at_sm = 0;
      STps->rw = ST_IDLE;
      result = 0;
    }

    scsi_release_command(SCpnt);
    SCpnt = NULL;

    return result;
}



/* osst versions of st functions - augmented and stripped to suit OnStream only */

/* Flush the write buffer (never need to write if variable blocksize). */
	static int
osst_flush_write_buffer(Scsi_Tape *STp, int file_blk)
{
  int offset, transfer, blks = 0;
  int result = 0;
  unsigned char cmd[10];
  Scsi_Cmnd *SCpnt;
  ST_partstat * STps;
  int dev = TAPE_NR(STp->devt);

  if ((STp->buffer)->writing) {
    osst_write_behind_check(STp);
    if ((STp->buffer)->last_result_fatal) {
#if DEBUG
      if (debugging)
	printk(ST_DEB_MSG "osst%d: Async write error (flush) %x.\n",
	       dev, (STp->buffer)->last_result);
#endif
      if ((STp->buffer)->last_result == INT_MAX)
	return (-ENOSPC);
      return (-EIO);
    }
    else
      STp->first_frame_position++;
  }

  result = 0;
  if (STp->dirty == 1) {

    offset   = (STp->buffer)->buffer_bytes;
    transfer = STp->frame_size;
    blks     = 1;
    
#if DEBUG
    if (debugging)
      printk(ST_DEB_MSG "osst%d: Flushing %d bytes, Tranfering %d bytes in %d blocks.\n",
			 dev, offset, transfer, blks);
#endif
    memset((STp->buffer)->b_data + offset, 0, transfer - offset);

    /* TODO: Error handling! */
    if (OS_NEED_POLL(STp->os_fw_rev))
	result = osst_wait_frame (STp, STp->first_frame_position, -50, 120);

    memset(cmd, 0, 10);
    cmd[0] = WRITE_6;
    cmd[1] = 1;
    cmd[4] = blks;

    SCpnt = st_do_scsi(NULL, STp, cmd, transfer, STp->timeout, MAX_WRITE_RETRIES,
		       TRUE);
    if (!SCpnt)
      return (-EBUSY);

    STps = &(STp->ps[STp->partition]);
    if ((STp->buffer)->last_result_fatal != 0) {
	printk(ST_DEB_MSG "osst%d: write sense [0]=0x%2x [2]=0x%2x [12]=%2x [13]=%2x\n", TAPE_NR(STp->devt),
		SCpnt->sense_buffer[0], SCpnt->sense_buffer[2], SCpnt->sense_buffer[12], SCpnt->sense_buffer[13]);
      if ((SCpnt->sense_buffer[0] & 0x70) == 0x70 &&
	  (SCpnt->sense_buffer[2] & 0x40) &&
	  (SCpnt->sense_buffer[2] & 0x0f) == NO_SENSE) {
	STp->dirty = 0;
	(STp->buffer)->buffer_bytes = 0;
	result = (-ENOSPC);
      }
      else {
	if (osst_write_error_recovery(STp, &SCpnt, 1)) {
	  printk(KERN_ERR "osst%d: Error on flush.\n", dev);
	  result = (-EIO);
        }
      }
      STps->drv_block = (-1);
    }
    else {
      if (file_blk && STps->drv_block >= 0)
	STps->drv_block += blks;
      STp->first_frame_position += blks;
      STp->dirty = 0;
      (STp->buffer)->buffer_bytes = 0;
    }
    scsi_release_command(SCpnt);
    SCpnt = NULL;
  }
#if DEBUG
  printk(ST_DEB_MSG "osst%d: Exit flush write buffer with code %d\n", dev, result);
#endif
  return result;
}


/* Flush the tape buffer. The tape will be positioned correctly unless
   seek_next is true. */
	static int
osst_flush_buffer(Scsi_Tape * STp, int seek_next)
{
  int backspace, result;
  ST_buffer * STbuffer;
  ST_partstat * STps;
#if DEBUG
  int dev = TAPE_NR(STp->devt);
#endif

  STbuffer = STp->buffer;

  /*
   * If there was a bus reset, block further access
   * to this device.
   */
  if( STp->device->was_reset )
    return (-EIO);

  if (STp->ready != ST_READY)
    return 0;

  STps = &(STp->ps[STp->partition]);
  if (STps->rw == ST_WRITING)  /* Writing */
    return osst_flush_write_buffer(STp, 1);

  if (STp->block_size == 0)
    return 0;

#if DEBUG
	printk(ST_DEB_MSG "osst%i: Reached flush (read) buffer\n", dev);
#endif
  backspace = ((STp->buffer)->buffer_bytes +
    (STp->buffer)->read_pointer) / STp->block_size -
      ((STp->buffer)->read_pointer + STp->block_size - 1) /
	STp->block_size;
  (STp->buffer)->buffer_bytes = 0;
  (STp->buffer)->read_pointer = 0;
  result = 0;
  if (!seek_next) {
    if (STps->eof == ST_FM_HIT) {
      result = cross_eof(STp, FALSE); /* Back over the EOF hit */
      if (!result)
	  STps->eof = ST_NOEOF;
      else {
	  if (STps->drv_file >= 0)
	      STps->drv_file++;
	  STps->drv_block = 0;
      }
    }
    if (!result && backspace > 0)
      result = osst_int_ioctl(STp, MTBSR, backspace);
  }
  else if (STps->eof == ST_FM_HIT) {
    if (STps->drv_file >= 0)
	STps->drv_file++;
    STps->drv_block = 0;
    STps->eof = ST_NOEOF;
  }

  return result;

}


/* Entry points to osst */

/* Write command */
static ssize_t
st_write(struct file * filp, const char * buf, size_t count, loff_t *ppos)
{
    struct inode *inode = filp->f_dentry->d_inode;
    ssize_t total;
    ssize_t i, do_count, blks, retval, transfer;
    int write_threshold;
    int doing_write = 0;
    static unsigned char cmd[10];
    const char *b_point;
    Scsi_Cmnd * SCpnt = NULL;
    Scsi_Tape * STp;
    ST_mode * STm;
    ST_partstat * STps;
    int dev = TAPE_NR(inode->i_rdev);

    STp = &(scsi_tapes[dev]);

    /*
     * If we are in the middle of error recovery, don't let anyone
     * else try and use this device.  Also, if error recovery fails, it
     * may try and take the device offline, in which case all further
     * access to the device is prohibited.
     */
    if( !scsi_block_when_processing_errors(STp->device) ) {
        return -ENXIO;
    }
    
    if (ppos != &filp->f_pos) {
      /* "A request was outside the capabilities of the device." */
      return -ENXIO;
    }

    if (STp->ready != ST_READY) {
      if (STp->ready == ST_NO_TAPE)
	return (-ENOMEDIUM);
      else
        return (-EIO);
    }
    STm = &(STp->modes[STp->current_mode]);
    if (!STm->defined)
      return (-ENXIO);
    if (count == 0)
      return 0;

    /*
     * If there was a bus reset, block further access
     * to this device.
     */
    if( STp->device->was_reset )
      return (-EIO);

#if DEBUG
    if (!STp->in_use) {
      printk(ST_DEB_MSG "osst%d: Incorrect device.\n", dev);
      return (-EIO);
    }
#endif

    if (STp->write_prot)
      return (-EACCES);

    /* Up to this point generic checking of call's validity - may rely on st.c for that */
    /* From here on adapted to OnStream only */
    if (!STp->onstream) return (-ENXIO);

    /* Write must be integral number of blocks */
    if (STp->block_size != 0 && (count % STp->block_size) != 0) {
	printk(KERN_WARNING "osst%d: Write not multiple of tape block size (%d bytes).\n",
	       dev, count);
	return (-EIO);
    }

    STps = &(STp->ps[STp->partition]);

    if (STp->do_auto_lock && STp->door_locked == ST_UNLOCKED &&
       !osst_int_ioctl(STp, MTLOCK, 0))
      STp->door_locked = ST_LOCKED_AUTO;

    if (STps->rw == ST_READING) {
      retval = osst_flush_buffer(STp, 0);
      if (retval)
	return retval;
      STps->rw = ST_IDLE;
    }
    else if (STps->rw != ST_WRITING &&
	     STps->drv_file == 0 && STps->drv_block == 0) {
	STp->logical_blk_num = 0;
	STp->logical_blk_in_buffer = 0;
	STp->wrt_pass_cntr++;
#if DEBUG
	printk(ST_DEB_MSG "osst%d: Allocating next write pass counter: %d\n", dev, STp->wrt_pass_cntr);
#endif
	STp->filemark_cnt = 0;
	STp->eod_frame_addr = STp->first_data_addr = 0x0000000A;
	STp->first_mark_addr = STp->last_mark_addr = -1;
	osst_write_header(STp, 1);
    }

    if ((STp->buffer)->writing) {
      osst_write_behind_check(STp);
      if ((STp->buffer)->last_result_fatal) {
#if DEBUG
	if (debugging)
	  printk(ST_DEB_MSG "osst%d: Async write error (write) %x.\n", dev,
		 (STp->buffer)->last_result);
#endif
	if ((STp->buffer)->last_result == INT_MAX)
	  STps->eof = ST_EOM_OK;
	else
	  STps->eof = ST_EOM_ERROR;
      }
      else
        STp->first_frame_position++;
    }
    if (STps->eof == ST_EOM_OK)
      return (-ENOSPC);
    else if (STps->eof == ST_EOM_ERROR)
      return (-EIO);

    /* Check the buffer readability in cases where copy_user might catch
       the problems after some tape movement. */
    if ((copy_from_user(&i, buf, 1) != 0 ||
	 copy_from_user(&i, buf + count - 1, 1) != 0))
	return (-EFAULT);

    if (!STm->do_buffer_writes) {
#if 0
      if (STp->block_size != 0 && (count % STp->block_size) != 0)
	return (-EIO);   /* Write must be integral number of blocks */
#endif
      write_threshold = 1;
    }
    else
      write_threshold = (STp->buffer)->buffer_blocks * STp->block_size;
    if (!STm->do_async_writes)
      write_threshold--;

    total = count;

    if ((!STp-> raw) && (STp->first_frame_position == 0xbae)) {
#if DEBUG
	printk(KERN_INFO "osst%d: Skipping over config partition.\n", dev);
#endif
	osst_flush_drive_buffer(STp);
	osst_position_tape_and_confirm(STp, 0xbb8);
    }
	
    if (OS_NEED_POLL(STp->os_fw_rev))
	retval = osst_wait_frame (STp, STp->first_frame_position, -50, 60);
    /* TODO: Check for an error ! */
	
    memset(cmd, 0, 10);
    cmd[0] = WRITE_6;
    cmd[1] = 1;

    STps->rw = ST_WRITING;

#if DEBUG
	printk(ST_DEB_MSG "osst%d: Writing %d bytes to file %d block %d lblk %d frame %d\n",
			  dev, count, STps->drv_file, STps->drv_block,
			  STp->logical_blk_num, STp->first_frame_position);
#endif

    b_point = buf;
    while ((STp->buffer)->buffer_bytes + count > write_threshold)
    {
      doing_write = 1;
      do_count = (STp->buffer)->buffer_blocks * STp->block_size -
	  (STp->buffer)->buffer_bytes;
      if (do_count > count)
	  do_count = count;

      i = append_to_buffer(b_point, STp->buffer, do_count);
      if (i) {
	  if (SCpnt != NULL) {
	      scsi_release_command(SCpnt);
	      SCpnt = NULL;
	  }
	  return i;
      }

      transfer = STp->frame_size;
      blks     = 1;

      osst_init_aux(STp, OS_FRAME_TYPE_DATA, STp->logical_blk_num++ );

      cmd[2] = blks >> 16;
      cmd[3] = blks >> 8;
      cmd[4] = blks;

      SCpnt = st_do_scsi(SCpnt, STp, cmd, transfer, STp->timeout,
			 MAX_WRITE_RETRIES, TRUE);
      if (!SCpnt)
	return (-EBUSY);

      if ((STp->buffer)->last_result_fatal != 0) {
#if DEBUG
	if (debugging)
	  printk(ST_DEB_MSG "osst%d: Error on write:\n", dev);
#endif
	if ((SCpnt->sense_buffer[0] & 0x70) == 0x70 &&
	    (SCpnt->sense_buffer[2] & 0x40)) {
	  if ((SCpnt->sense_buffer[0] & 0x80) != 0)
	    transfer = (SCpnt->sense_buffer[3] << 24) |
		       (SCpnt->sense_buffer[4] << 16) |
		       (SCpnt->sense_buffer[5] <<  8) | SCpnt->sense_buffer[6];
	  else
	    transfer = 0;
	  transfer *= STp->block_size;
	  if (transfer <= do_count) {
	    filp->f_pos += do_count - transfer;
	    count -= do_count - transfer;
	    if (STps->drv_block >= 0) {
		STps->drv_block += (do_count - transfer) / STp->block_size;
	    }
	    STps->eof = ST_EOM_OK;
	    retval = (-ENOSPC); /* EOM within current request */
#if DEBUG
	    if (debugging)
	      printk(ST_DEB_MSG "osst%d: EOM with %d bytes unwritten.\n",
		     dev, transfer);
#endif
	  }
	  else {
	    STps->eof = ST_EOM_ERROR;
	    STps->drv_block = (-1);    /* Too cautious? */
	    retval = (-EIO); /* EOM for old data */
#if DEBUG
	    if (debugging)
	      printk(ST_DEB_MSG "osst%d: EOM with lost data.\n", dev);
#endif
	  }
	}
	else {
	  if (osst_write_error_recovery(STp, &SCpnt, 1) == 0) goto ok;
	  STps->drv_block = (-1);    /* Too cautious? */
	  retval = (-EIO);
	}

	scsi_release_command(SCpnt);
	SCpnt = NULL;
	(STp->buffer)->buffer_bytes = 0;
	STp->dirty = 0;
	if (count < total)
	  return total - count;
	else
	  return retval;
      }
ok:
      filp->f_pos += do_count;
      b_point += do_count;
      count -= do_count;
      if (STps->drv_block >= 0) {
	  STps->drv_block += blks;
      }
      STp->first_frame_position += blks;
      (STp->buffer)->buffer_bytes = 0;
      STp->dirty = 0;
    }
    if (count != 0) {
      STp->dirty = 1;
      i = append_to_buffer(b_point, STp->buffer, count);
      if (i) {
	  if (SCpnt != NULL) {
	      scsi_release_command(SCpnt);
	      SCpnt = NULL;
	  }
	  return i;
      }
      filp->f_pos += count;
      count = 0;
    }

    if (doing_write && (STp->buffer)->last_result_fatal != 0) {
      scsi_release_command(SCpnt);
      SCpnt = NULL;
      return (STp->buffer)->last_result_fatal;
    }

    if (STm->do_async_writes &&
	((STp->buffer)->buffer_bytes >= STp->write_threshold &&
	 (STp->buffer)->buffer_bytes >= STp->block_size)      ) {
      /* Schedule an asynchronous write */
      (STp->buffer)->writing = ((STp->buffer)->buffer_bytes /
	  STp->block_size) * STp->block_size;
      STp->dirty = !((STp->buffer)->writing ==
		     (STp->buffer)->buffer_bytes);

      transfer = STp->frame_size;
      blks     = 1;

      osst_init_aux(STp, OS_FRAME_TYPE_DATA, STp->logical_blk_num++ );

      cmd[2] = blks >> 16;
      cmd[3] = blks >> 8;
      cmd[4] = blks;
#if DEBUG
      STp->write_pending = 1;
#endif

      SCpnt = st_do_scsi(SCpnt, STp, cmd, transfer, STp->timeout,
			 MAX_WRITE_RETRIES, FALSE);
      if (SCpnt == NULL)
	  return (-EIO);
    }
    else if (SCpnt != NULL) {
	scsi_release_command(SCpnt);
	SCpnt = NULL;
    }
    STps->at_sm &= (total == 0);
    if (total > 0)
	STps->eof = ST_NOEOF;

    return( total);
}


/* Read command */
static ssize_t
st_read(struct file * filp, char * buf, size_t count, loff_t *ppos)
{
    struct inode * inode = filp->f_dentry->d_inode;
    ssize_t total;
    ssize_t i, transfer;
    int special;
    Scsi_Tape * STp;
    ST_mode * STm;
    ST_partstat * STps;
    int dev = TAPE_NR(inode->i_rdev);

    STp = &(scsi_tapes[dev]);

    /*
     * If we are in the middle of error recovery, don't let anyone
     * else try and use this device.  Also, if error recovery fails, it
     * may try and take the device offline, in which case all further
     * access to the device is prohibited.
     */
    if( !scsi_block_when_processing_errors(STp->device) ) {
        return -ENXIO;
    }
    
    if (ppos != &filp->f_pos) {
      /* "A request was outside the capabilities of the device." */
      return -ENXIO;
    }

    if (STp->ready != ST_READY) {
      if (STp->ready == ST_NO_TAPE)
	return (-ENOMEDIUM);
      else
        return (-EIO);
    }
    STm = &(STp->modes[STp->current_mode]);
    if (!STm->defined)
      return (-ENXIO);
#if DEBUG
    if (!STp->in_use) {
      printk(ST_DEB_MSG "osst%d: Incorrect device.\n", dev);
      return (-EIO);
    }
#endif

    /* Up to this point generic checking of call's validity - may rely on st.c for that */
    /* From here on adapted to OnStream only */
    if (!STp->onstream) return (-ENXIO);
    if (!STp->header_ok) return (-EIO);

    if ((count % STp->block_size) != 0) {
      printk(KERN_WARNING "osst%d: Use multiple of %d bytes as block size (%d requested)\n",
			  dev, STp->block_size, count);
      return (-EIO);	/* Read must be integral number of blocks */
    }

    if (STp->do_auto_lock && STp->door_locked == ST_UNLOCKED &&
	!osst_int_ioctl(STp, MTLOCK, 0))
      STp->door_locked = ST_LOCKED_AUTO;

    STps = &(STp->ps[STp->partition]);
    if (STps->rw == ST_WRITING) {
      transfer = osst_flush_buffer(STp, 0);
      if (transfer)
	return transfer;
      STps->rw = ST_READING;
    }

#if DEBUG
    if (debugging && STps->eof != ST_NOEOF)
      printk(ST_DEB_MSG "osst%d: EOF/EOM flag up (%d). Bytes %d\n", dev,
	     STps->eof, (STp->buffer)->buffer_bytes);
#endif
    if ((STp->buffer)->buffer_bytes == 0 &&
	STps->eof >= ST_EOD_1) {
	if (STps->eof < ST_EOD) {
	    STps->eof += 1;
	    return 0;
	}
	return (-EIO);  /* EOM or Blank Check */
    }

    /* Check the buffer writability before any tape movement. Don't alter
       buffer data. */
    if (copy_from_user(&i, buf, 1) != 0 ||
	copy_to_user(buf, &i, 1) != 0 ||
	copy_from_user(&i, buf + count - 1, 1) != 0 ||
	copy_to_user(buf + count - 1, &i, 1) != 0)
	return (-EFAULT);

    STps->rw = ST_READING;


    /* Loop until enough data in buffer or a special condition found */
    for (total = 0, special = 0; total < count && !special; ) {

      /* Get new data if the buffer is empty */
      if ((STp->buffer)->buffer_bytes == 0) {
	  special = osst_get_logical_blk(STp, STp->logical_blk_num, 0);
	  STp->buffer->buffer_bytes = special ? 0 : STp->block_size;
	  STp->buffer->read_pointer = 0;
	  STp->logical_blk_num++;		/* block to look for next time */
	  STp->logical_blk_in_buffer = 0;
	  if (special < 0)  			/* No need to continue read */
	      return special;
	  STps->drv_block++;
      }

      /* Move the data from driver buffer to user buffer */
      if ((STp->buffer)->buffer_bytes > 0) {
#if DEBUG
	if (debugging && STps->eof != ST_NOEOF)
	  printk(ST_DEB_MSG "osst%d: EOF up (%d). Left %d, needed %d.\n", dev,
		 STps->eof, (STp->buffer)->buffer_bytes, count - total);
#endif
	transfer = (STp->buffer)->buffer_bytes < count - total ?
	  (STp->buffer)->buffer_bytes : count - total;
	i = from_buffer(STp->buffer, buf, transfer);
	if (i) 
	    return i;
	filp->f_pos += transfer;
	buf += transfer;
	total += transfer;
      }

    } /* for (total = 0, special = 0; total < count && !special; ) */

    /* Change the eof state if no data from tape or buffer */
    if (total == 0) {
	if (STps->eof == ST_FM_HIT) {
	    STps->eof = ST_FM;
	    STps->drv_block = 0;
	    if (STps->drv_file >= 0)
		STps->drv_file++;
	}
	else if (STps->eof == ST_EOD_1) {
	    STps->eof = ST_EOD_2;
	    STps->drv_block = 0;
	    if (STps->drv_file >= 0)
		STps->drv_file++;
	}
	else if (STps->eof == ST_EOD_2)
	    STps->eof = ST_EOD;
    }
    else if (STps->eof == ST_FM)
	STps->eof = ST_NOEOF;

    return total;
}




/* Set the driver options */
	static void
st_log_options(Scsi_Tape *STp, ST_mode *STm, int dev)
{
  printk(KERN_INFO
"osst%d: Mode %d options: buffer writes: %d, async writes: %d, read ahead: %d\n",
	 dev, STp->current_mode, STm->do_buffer_writes, STm->do_async_writes,
	 STm->do_read_ahead);
  printk(KERN_INFO
"osst%d:    can bsr: %d, two FMs: %d, fast mteom: %d, auto lock: %d,\n",
	 dev, STp->can_bsr, STp->two_fm, STp->fast_mteom, STp->do_auto_lock);
  printk(KERN_INFO
"osst%d:    defs for wr: %d, no block limits: %d, partitions: %d, s2 log: %d\n",
	 dev, STm->defaults_for_writes, STp->omit_blklims, STp->can_partitions,
	 STp->scsi2_logical);
  printk(KERN_INFO
"osst%d:    sysv: %d\n", dev, STm->sysv);
#if DEBUG
  printk(KERN_INFO
	 "osst%d:    debugging: %d\n",
	 dev, debugging);
#endif
}


	static int
st_set_options(struct inode * inode, long options)
{
  int value;
  long code;
  Scsi_Tape *STp;
  ST_mode *STm;
  int dev = TAPE_NR(inode->i_rdev);

  STp = &(scsi_tapes[dev]);
  STm = &(STp->modes[STp->current_mode]);
  if (!STm->defined) {
    memcpy(STm, &(STp->modes[0]), sizeof(ST_mode));
    modes_defined = TRUE;
#if DEBUG
    if (debugging)
      printk(ST_DEB_MSG "osst%d: Initialized mode %d definition from mode 0\n",
	     dev, STp->current_mode);
#endif
  }

  code = options & MT_ST_OPTIONS;
  if (code == MT_ST_BOOLEANS) {
    STm->do_buffer_writes = (options & MT_ST_BUFFER_WRITES) != 0;
    STm->do_async_writes  = (options & MT_ST_ASYNC_WRITES) != 0;
    STm->defaults_for_writes = (options & MT_ST_DEF_WRITES) != 0;
    STm->do_read_ahead    = (options & MT_ST_READ_AHEAD) != 0;
    STp->two_fm		  = (options & MT_ST_TWO_FM) != 0;
    STp->fast_mteom	  = (options & MT_ST_FAST_MTEOM) != 0;
    STp->do_auto_lock     = (options & MT_ST_AUTO_LOCK) != 0;
    STp->can_bsr          = (options & MT_ST_CAN_BSR) != 0;
    STp->omit_blklims	  = (options & MT_ST_NO_BLKLIMS) != 0;
    if ((STp->device)->scsi_level >= SCSI_2)
      STp->can_partitions = (options & MT_ST_CAN_PARTITIONS) != 0;
    STp->scsi2_logical    = (options & MT_ST_SCSI2LOGICAL) != 0;
    STm->sysv		  = (options & MT_ST_SYSV) != 0;
#if DEBUG
    debugging = (options & MT_ST_DEBUGGING) != 0;
#endif
    st_log_options(STp, STm, dev);
  }
  else if (code == MT_ST_SETBOOLEANS || code == MT_ST_CLEARBOOLEANS) {
    value = (code == MT_ST_SETBOOLEANS);
    if ((options & MT_ST_BUFFER_WRITES) != 0)
      STm->do_buffer_writes = value;
    if ((options & MT_ST_ASYNC_WRITES) != 0)
      STm->do_async_writes = value;
    if ((options & MT_ST_DEF_WRITES) != 0)
      STm->defaults_for_writes = value;
    if ((options & MT_ST_READ_AHEAD) != 0)
      STm->do_read_ahead = value;
    if ((options & MT_ST_TWO_FM) != 0)
      STp->two_fm = value;
    if ((options & MT_ST_FAST_MTEOM) != 0)
      STp->fast_mteom = value;
    if ((options & MT_ST_AUTO_LOCK) != 0)
      STp->do_auto_lock = value;
    if ((options & MT_ST_CAN_BSR) != 0)
      STp->can_bsr = value;
    if ((options & MT_ST_NO_BLKLIMS) != 0)
      STp->omit_blklims = value;
    if ((STp->device)->scsi_level >= SCSI_2 &&
	(options & MT_ST_CAN_PARTITIONS) != 0)
      STp->can_partitions = value;
    if ((options & MT_ST_SCSI2LOGICAL) != 0)
      STp->scsi2_logical = value;
    if ((options & MT_ST_SYSV) != 0)
      STm->sysv = value;
#if DEBUG
    if ((options & MT_ST_DEBUGGING) != 0)
      debugging = value;
#endif
    st_log_options(STp, STm, dev);
  }
  else if (code == MT_ST_WRITE_THRESHOLD) {
    value = (options & ~MT_ST_OPTIONS) * ST_KILOBYTE;
    if (value < 1 || value > st_buffer_size) {
      printk(KERN_WARNING "osst%d: Write threshold %d too small or too large.\n",
	     dev, value);
      return (-EIO);
    }
    STp->write_threshold = value;
    printk(KERN_INFO "osst%d: Write threshold set to %d bytes.\n",
	   dev, value);
  }
  else if (code == MT_ST_DEF_BLKSIZE) {
    value = (options & ~MT_ST_OPTIONS);
    if (value == ~MT_ST_OPTIONS) {
      STm->default_blksize = (-1);
      printk(KERN_INFO "osst%d: Default block size disabled.\n", dev);
    }
    else {
      STm->default_blksize = value;
      printk(KERN_INFO "osst%d: Default block size set to %d bytes.\n",
	     dev, STm->default_blksize);
    }
  }
  else if (code == MT_ST_TIMEOUTS) {
    value = (options & ~MT_ST_OPTIONS);
    if ((value & MT_ST_SET_LONG_TIMEOUT) != 0) {
      STp->long_timeout = (value & ~MT_ST_SET_LONG_TIMEOUT) * HZ;
      printk(KERN_INFO "osst%d: Long timeout set to %d seconds.\n", dev,
	     (value & ~MT_ST_SET_LONG_TIMEOUT));
    }
    else {
      STp->timeout = value * HZ;
      printk(KERN_INFO "osst%d: Normal timeout set to %d seconds.\n", dev,
	     value);
    }
  }
  else if (code == MT_ST_DEF_OPTIONS) {
    code = (options & ~MT_ST_CLEAR_DEFAULT);
    value = (options & MT_ST_CLEAR_DEFAULT);
    if (code == MT_ST_DEF_DENSITY) {
      if (value == MT_ST_CLEAR_DEFAULT) {
	STm->default_density = (-1);
	printk(KERN_INFO "osst%d: Density default disabled.\n", dev);
      }
      else {
	STm->default_density = value & 0xff;
	printk(KERN_INFO "osst%d: Density default set to %x\n",
	       dev, STm->default_density);
      }
    }
    else if (code == MT_ST_DEF_DRVBUFFER) {
      if (value == MT_ST_CLEAR_DEFAULT) {
	STp->default_drvbuffer = 0xff;
	printk(KERN_INFO "osst%d: Drive buffer default disabled.\n", dev);
      }
      else {
	STp->default_drvbuffer = value & 7;
	printk(KERN_INFO "osst%d: Drive buffer default set to %x\n",
	       dev, STp->default_drvbuffer);
      }
    }
    else if (code == MT_ST_DEF_COMPRESSION) {
      if (value == MT_ST_CLEAR_DEFAULT) {
	STm->default_compression = ST_DONT_TOUCH;
	printk(KERN_INFO "osst%d: Compression default disabled.\n", dev);
      }
      else {
	STm->default_compression = (value & 1 ? ST_YES : ST_NO);
	printk(KERN_INFO "osst%d: Compression default set to %x\n",
	       dev, (value & 1));
      }
    }
  }
  else
    return (-EIO);

  return 0;
}


/* Internal ioctl function */
	static int
osst_int_ioctl(Scsi_Tape * STp, unsigned int cmd_in, unsigned long arg)
{
   int timeout;
   long ltmp;
   int i, ioctl_result;
   int chg_eof = TRUE;
   unsigned char cmd[10];
   Scsi_Cmnd * SCpnt = NULL;
   ST_partstat * STps;
   int fileno, blkno, at_sm, undone, datalen;
   int logical_blk_num;
   int dev = TAPE_NR(STp->devt);

   if (STp->ready != ST_READY && cmd_in != MTLOAD) {
     if (STp->ready == ST_NO_TAPE)
       return (-ENOMEDIUM);
     else
       return (-EIO);
   }
   timeout = STp->long_timeout;
   STps = &(STp->ps[STp->partition]);
   fileno = STps->drv_file;
   blkno = STps->drv_block;
   at_sm = STps->at_sm;
   logical_blk_num = STp->logical_blk_num;

   memset(cmd, 0, 10);
   datalen = 0;
   switch (cmd_in) {
     case MTFSFM:
	chg_eof = FALSE; /* Changed from the FSF after this */
     case MTFSF:
	if (STp->raw)
	   return (-EIO);
	if (STp->linux_media)
	   ioctl_result = osst_space_over_filemarks_forward_fast(STp, cmd_in, arg);
	else
	   ioctl_result = osst_space_over_filemarks_forward_slow(STp, cmd_in, arg);
	logical_blk_num = STp->logical_blk_num;
	if (fileno >= 0)
	   fileno += arg;
	blkno = 0;
	at_sm &= (arg == 0);
	goto os_bypass;

     case MTBSF:
	chg_eof = FALSE; /* Changed from the FSF after this */
     case MTBSFM:
	if (STp->raw)
	   return (-EIO);
	ioctl_result = osst_space_over_filemarks_backward(STp, cmd_in, arg);
	logical_blk_num = STp->logical_blk_num;
	if (fileno >= 0)
	   fileno -= arg;
	blkno = (-1);  /* We can't know the block number */
	at_sm &= (arg == 0);
	goto os_bypass;

     case MTFSR:
     case MTBSR:
	if (cmd_in == MTFSR) {
	   logical_blk_num += arg;
	   if (blkno >= 0) blkno += arg;
	}
	else {
	   logical_blk_num -= arg;
	   if (blkno >= 0) blkno -= arg;
	}
	ioctl_result = osst_seek_logical_blk(STp, logical_blk_num);
	logical_blk_num = STp->logical_blk_num;
	at_sm &= (arg == 0);
	goto os_bypass;

     case MTFSS:
       cmd[0] = SPACE;
       cmd[1] = 0x04; /* Space Setmarks */   /* FIXME -- OS can't do this? */
       cmd[2] = (arg >> 16);
       cmd[3] = (arg >> 8);
       cmd[4] = arg;
#if DEBUG
       if (debugging)
	 printk(ST_DEB_MSG "osst%d: Spacing tape forward %d setmarks.\n", dev,
		cmd[2] * 65536 + cmd[3] * 256 + cmd[4]);
#endif
       if (arg != 0) {
	 blkno = fileno = (-1);
	 at_sm = 1;
       }
       break;
     case MTBSS:
       cmd[0] = SPACE;
       cmd[1] = 0x04; /* Space Setmarks */   /* FIXME -- OS can't do this? */
       ltmp = (-arg);
       cmd[2] = (ltmp >> 16);
       cmd[3] = (ltmp >> 8);
       cmd[4] = ltmp;
#if DEBUG
       if (debugging) {
	 if (cmd[2] & 0x80)
	   ltmp = 0xff000000;
	 ltmp = ltmp | (cmd[2] << 16) | (cmd[3] << 8) | cmd[4];
	 printk(ST_DEB_MSG "osst%d: Spacing tape backward %ld setmarks.\n",
		dev, (-ltmp));
       }
#endif
       if (arg != 0) {
	 blkno = fileno = (-1);
	 at_sm = 1;
       }
       break;
     case MTWEOF:
     case MTWSM:
       if (STp->write_prot)
	 return (-EACCES);
       cmd[0] = WRITE_FILEMARKS;   /* FIXME -- need OS version */
       if (cmd_in == MTWSM)
	 cmd[1] = 2;
       cmd[2] = (arg >> 16);
       cmd[3] = (arg >> 8);
       cmd[4] = arg;
       timeout = STp->timeout;
#if DEBUG
       if (debugging) {
	 if (cmd_in == MTWEOF)
	   printk(ST_DEB_MSG "osst%d: Writing %d filemarks.\n", dev,
		  cmd[2] * 65536 + cmd[3] * 256 + cmd[4]);
	 else
	   printk(ST_DEB_MSG "osst%d: Writing %d setmarks.\n", dev,
		  cmd[2] * 65536 + cmd[3] * 256 + cmd[4]);
       }
#endif
       if (fileno >= 0)
	 fileno += arg;
       blkno = 0;
       at_sm = (cmd_in == MTWSM);
       break;
     case MTOFFL:
     case MTLOAD:
     case MTUNLOAD:
       cmd[0] = START_STOP;
       if (cmd_in == MTLOAD)
	 cmd[4] |= 1;
       /*
        * If arg >= 1 && arg <= 6 Enhanced load/unload in HP C1553A
       */
       if (cmd_in != MTOFFL &&
	   arg >= 1 + MT_ST_HPLOADER_OFFSET
	   && arg <= 6 + MT_ST_HPLOADER_OFFSET) {
#if DEBUG
	   if (debugging) {
	       printk(ST_DEB_MSG "osst%d: Enhanced %sload slot %2ld.\n",
		      dev, (cmd[4]) ? "" : "un",
		      arg - MT_ST_HPLOADER_OFFSET);
	   }
#endif
	   cmd[3] = arg - MT_ST_HPLOADER_OFFSET; /* MediaID field of C1553A */
       }
       cmd[1] = 1;  /* Don't wait for completion */
       timeout = STp->timeout;
#if DEBUG
       if (debugging) {
	 if (cmd_in != MTLOAD)
	   printk(ST_DEB_MSG "osst%d: Unloading tape.\n", dev);
	 else
	   printk(ST_DEB_MSG "osst%d: Loading tape.\n", dev);
       }
#endif
       fileno = blkno = at_sm = logical_blk_num = 0 ;
       break;
     case MTNOP:
#if DEBUG
       if (debugging)
	 printk(ST_DEB_MSG "osst%d: No op on tape.\n", dev);
#endif
       return 0;  /* Should do something ? */
       break;
     case MTRETEN:
       cmd[0] = START_STOP;
       cmd[1] = 1;  /* Don't wait for completion */
       timeout = STp->timeout;
       cmd[4] = 3;
#if DEBUG
       if (debugging)
	 printk(ST_DEB_MSG "osst%d: Retensioning tape.\n", dev);
#endif
       fileno = blkno = at_sm = logical_blk_num = 0;
       break;
     case MTEOM:
#if DEBUG
	if (debugging)
	   printk(ST_DEB_MSG "osst%d: Spacing to end of recorded medium.\n", dev);
#endif
	osst_set_frame_position(STp, STp->eod_frame_addr, 0);
	if (osst_get_logical_blk(STp, -1, 0) < 0)
	   return (-EIO);
	if (STp->buffer->aux->frame_type != OS_FRAME_TYPE_EOD)
	   return (-EIO);
	ioctl_result = osst_set_frame_position(STp, STp->eod_frame_addr, 0);
	logical_blk_num = STp->logical_blk_num;
	fileno          = STp->filemark_cnt;
	blkno  = at_sm  = 0;
	goto os_bypass;

     case MTERASE:
	if (STp->write_prot)
	   return (-EACCES);
	fileno = blkno = at_sm = 0 ;
	STp->first_data_addr = STp->eod_frame_addr = 0x0000000A;
	STp->logical_blk_num = logical_blk_num     =  0;
	STp->first_mark_addr = STp->last_mark_addr = -1;
	ioctl_result = osst_position_tape_and_confirm(STp, STp->eod_frame_addr);
	i = osst_write_eod(STp);
	if (i < ioctl_result) ioctl_result = i;
	i = osst_flush_drive_buffer(STp);
	if (i < ioctl_result) ioctl_result = i;
	i = osst_write_header(STp, 1);
	if (i < ioctl_result) ioctl_result = i;
	i = osst_flush_drive_buffer(STp);
	if (i < ioctl_result) ioctl_result = i;
	fileno = blkno = at_sm = logical_blk_num = 0 ;
	goto os_bypass;

     case MTREW:
	cmd[0] = REZERO_UNIT; /* rewind */
	cmd[1] = 1;
	timeout = STp->timeout;
#if DEBUG
	if (debugging)
	   printk(ST_DEB_MSG "osst%d: Rewinding tape, Immed=%d.\n", dev, cmd[1]);
#endif
	fileno = blkno = at_sm = logical_blk_num = 0 ;
	break;

     case MTLOCK:
	chg_eof = FALSE;
	cmd[0] = ALLOW_MEDIUM_REMOVAL;
	cmd[4] = SCSI_REMOVAL_PREVENT;
#if DEBUG
	if (debugging)
	    printk(ST_DEB_MSG "osst%d: Locking drive door.\n", dev);
#endif;
	break;

     case MTUNLOCK:
	chg_eof = FALSE;
	cmd[0] = ALLOW_MEDIUM_REMOVAL;
	cmd[4] = SCSI_REMOVAL_ALLOW;
#if DEBUG
	if (debugging)
	   printk(ST_DEB_MSG "osst%d: Unlocking drive door.\n", dev);
#endif;
	break;

     case MTSETBLK:           /* Set block length */
     case MTSETDENSITY:       /* Set tape density */
     case MTSETDRVBUFFER:     /* Set drive buffering */
     case SET_DENS_AND_BLK:   /* Set density and block size */
       chg_eof = FALSE;
       if (STp->dirty || (STp->buffer)->buffer_bytes != 0)
	 return (-EIO);       /* Not allowed if data in buffer */
       if ((cmd_in == MTSETBLK || cmd_in == SET_DENS_AND_BLK) &&
	   (arg & MT_ST_BLKSIZE_MASK) != 0 &&
	   ((arg & MT_ST_BLKSIZE_MASK) < STp->min_block ||
	    (arg & MT_ST_BLKSIZE_MASK) > STp->max_block ||
	    (arg & MT_ST_BLKSIZE_MASK) > st_buffer_size)) {
	 printk(KERN_WARNING "osst%d: Illegal block size.\n", dev);
	 return (-EINVAL);
       }
       return 0;  /* silently ignore if block size didn't change */

     default:
	return (-ENOSYS);
   }

   SCpnt = st_do_scsi(SCpnt, STp, cmd, datalen, timeout, MAX_RETRIES, TRUE);
   if (!SCpnt) {
#if DEBUG
      printk(ST_DEB_MSG "osst%d: couldn't exec scsi cmd for IOCTL\n", dev);
#endif
      return (-EBUSY);
   }

   ioctl_result = (STp->buffer)->last_result_fatal;

os_bypass:
#if DEBUG
   if (debugging)
      printk(ST_DEB_MSG "osst%d: IOCTL (%d) Result=%d\n", dev, cmd_in, ioctl_result);
#endif

   if (!ioctl_result) {  /* SCSI command successful */

      if (cmd_in == MTFSFM) {
	 fileno--;
	 blkno--;
      }
      if (cmd_in == MTBSFM) {
	 fileno++;
	 blkno++;
      }
      STps->drv_block = blkno;
      STps->drv_file = fileno;
      STps->at_sm = at_sm;
      STp->logical_blk_num = logical_blk_num;

      if (cmd_in == MTLOCK)
         STp->door_locked = ST_LOCKED_EXPLICIT;
      else if (cmd_in == MTUNLOCK)
         STp->door_locked = ST_UNLOCKED;

      if (cmd_in == MTEOM)
         STps->eof = ST_EOD;
      else if (cmd_in == MTFSF)
         STps->eof = ST_FM;
      else if (chg_eof)
         STps->eof = ST_NOEOF;

      if (cmd_in == MTOFFL || cmd_in == MTUNLOAD)
         STp->rew_at_close = 0;
      else if (cmd_in == MTLOAD) {
/*       STp->rew_at_close = (MINOR(inode->i_rdev) & 0x80) == 0;  FIXME */
	 for (i=0; i < ST_NBR_PARTITIONS; i++) {
	    STp->ps[i].rw = ST_IDLE;
	    STp->ps[i].last_block_valid = FALSE;
	 }
	 STp->partition = 0;
      }

      if (cmd_in == MTREW)
	ioctl_result = osst_position_tape_and_confirm(STp, STp->first_data_addr); 

   } else if (SCpnt) {  /* SCSI command was not completely successful. Don't return
	        	from this block without releasing the SCSI command block! */
/* FIXME -- if osst_* stuff failed we have no SCpnt, but do we need any of this? */
      if (SCpnt->sense_buffer[2] & 0x40) {
         if (cmd_in != MTBSF && cmd_in != MTBSFM &&
	    cmd_in != MTBSR && cmd_in != MTBSS)
	    STps->eof = ST_EOM_OK;
	 STps->drv_block = 0;
      }

      undone = (
	  (SCpnt->sense_buffer[3] << 24) +
	  (SCpnt->sense_buffer[4] << 16) +
	  (SCpnt->sense_buffer[5] << 8) +
	  SCpnt->sense_buffer[6] );
      if (cmd_in == MTWEOF &&
	 (SCpnt->sense_buffer[0] & 0x70) == 0x70 &&
	 (SCpnt->sense_buffer[2] & 0x4f) == 0x40 &&
	 ((SCpnt->sense_buffer[0] & 0x80) == 0 || undone == 0)) {
	    ioctl_result = 0;  /* EOF written succesfully at EOM */
	    if (fileno >= 0)
		fileno++;
	    STps->drv_file = fileno;
	    STps->eof = ST_NOEOF;
      }
      else if ( (cmd_in == MTFSF) || (cmd_in == MTFSFM) ) {
         if (fileno >= 0)
	    STps->drv_file = fileno - undone ;
	 else
	    STps->drv_file = fileno;
       STps->drv_block = 0;
       STps->eof = ST_NOEOF;
     }
     else if ( (cmd_in == MTBSF) || (cmd_in == MTBSFM) ) {
       if (fileno >= 0)
         STps->drv_file = fileno + undone ;
       else
	 STps->drv_file = fileno;
       STps->drv_block = 0;
       STps->eof = ST_NOEOF;
     }
     else if (cmd_in == MTFSR) {
       if (SCpnt->sense_buffer[2] & 0x80) { /* Hit filemark */
	 if (STps->drv_file >= 0)
	   STps->drv_file++;
	 STps->drv_block = 0;
	 STps->eof = ST_FM;
       }
       else {
	 if (blkno >= undone)
	   STps->drv_block = blkno - undone;
	 else
	   STps->drv_block = (-1);
	 STps->eof = ST_NOEOF;
       }
     }
     else if (cmd_in == MTBSR) {
       if (SCpnt->sense_buffer[2] & 0x80) { /* Hit filemark */
	 STps->drv_file--;
	 STps->drv_block = (-1);
       }
       else {
	 if (blkno >= 0)
	   STps->drv_block = blkno + undone;
	 else
	   STps->drv_block = (-1);
       }
       STps->eof = ST_NOEOF;
     }
     else if (cmd_in == MTEOM) {
       STps->drv_file = (-1);
       STps->drv_block = (-1);
       STps->eof = ST_EOD;
     }
     else if (chg_eof)
       STps->eof = ST_NOEOF;

      if ((SCpnt->sense_buffer[2] & 0x0f) == BLANK_CHECK)
	 STps->eof = ST_EOD;

      if (cmd_in == MTLOCK)
	 STp->door_locked = ST_LOCK_FAILS;

   }
   if (SCpnt)
	scsi_release_command(SCpnt);
   SCpnt = NULL;

   return ioctl_result;
}



/* Open the device */
static int
scsi_tape_open(struct inode * inode, struct file * filp)
{
    unsigned short flags;
    int i, b_size, need_dma_buffer, new_session = FALSE;
    unsigned char cmd[10];
    Scsi_Cmnd * SCpnt;
    Scsi_Tape * STp;
    ST_mode * STm;
    ST_partstat * STps;
    int dev = TAPE_NR(inode->i_rdev);
    int mode = TAPE_MODE(inode->i_rdev);

    if (dev >= st_template.dev_max || !scsi_tapes[dev].device)
      return (-ENXIO);

    if( !scsi_block_when_processing_errors(scsi_tapes[dev].device) ) {
        return -ENXIO;
    }

    STp = &(scsi_tapes[dev]);
    if (STp->in_use) {
#if DEBUG
      printk(ST_DEB_MSG "osst%d: Device already in use.\n", dev);
#endif
      return (-EBUSY);
    }
    STp->in_use       = 1;
    STp->rew_at_close = (MINOR(inode->i_rdev) & 0x80) == 0;

    if (scsi_tapes[dev].device->host->hostt->module)
       __MOD_INC_USE_COUNT(scsi_tapes[dev].device->host->hostt->module);
    if (st_template.module)
       __MOD_INC_USE_COUNT(st_template.module);

    if (mode != STp->current_mode) {
#if DEBUG
      if (debugging)
	printk(ST_DEB_MSG "osst%d: Mode change from %d to %d.\n",
	       dev, STp->current_mode, mode);
#endif
      new_session = TRUE;
      STp->current_mode = mode;
    }
    STm = &(STp->modes[STp->current_mode]);

    flags = filp->f_flags;
    STp->write_prot = ((flags & O_ACCMODE) == O_RDONLY);

    /* Up to this point generic checking of call's validity - may rely on st.c for that */
    /* From here on adapted to OnStream only */
    if (!STp->onstream) return (-ENXIO);

    STp->raw = (MINOR(inode->i_rdev) & 0x40) != 0;

    /* Allocate a buffer for this user */
    need_dma_buffer = STp->restr_dma;
    for (i=0; i < st_nbr_buffers; i++)
      if (!st_buffers[i]->in_use &&
	  (!need_dma_buffer || st_buffers[i]->dma))
	break;
    if (i >= st_nbr_buffers) {
      STp->buffer = new_tape_buffer(FALSE, need_dma_buffer);
      if (STp->buffer == NULL) {
	printk(KERN_WARNING "osst%d: Can't allocate tape buffer.\n", dev);
	STp->in_use = 0;
	if (scsi_tapes[dev].device->host->hostt->module)
	    __MOD_DEC_USE_COUNT(scsi_tapes[dev].device->host->hostt->module);
	if(st_template.module)
	    __MOD_DEC_USE_COUNT(st_template.module);
	return (-EBUSY);
      }
    }
    else
      STp->buffer = st_buffers[i];
    (STp->buffer)->in_use = 1;
    (STp->buffer)->writing = 0;
    (STp->buffer)->last_result_fatal = 0;
    (STp->buffer)->use_sg = STp->device->host->sg_tablesize;

    /* Compute the usable buffer size for this SCSI adapter */
    if (!(STp->buffer)->use_sg)
	(STp->buffer)->buffer_size = (STp->buffer)->sg[0].length;
    else {
	for (i=0, (STp->buffer)->buffer_size = 0; i < (STp->buffer)->use_sg &&
		 i < (STp->buffer)->sg_segs; i++)
	    (STp->buffer)->buffer_size += (STp->buffer)->sg[i].length;
    }

    STp->dirty = 0;
    for (i=0; i < ST_NBR_PARTITIONS; i++) {
	STps = &(STp->ps[i]);
	STps->rw = ST_IDLE;
    }
    STp->ready = ST_READY;
    STp->recover_count = 0;
#if DEBUG
    STp->nbr_waits = STp->nbr_finished = 0;
#endif

    memset ((void *) &cmd[0], 0, 10);
    cmd[0] = TEST_UNIT_READY;

    SCpnt = st_do_scsi(NULL, STp, cmd, 0, STp->long_timeout, MAX_READY_RETRIES,
		       TRUE);
    if (!SCpnt) {
        (STp->buffer)->in_use = 0;
        STp->buffer = NULL;
        STp->in_use = 0;

	if (scsi_tapes[dev].device->host->hostt->module)
	    __MOD_DEC_USE_COUNT(scsi_tapes[dev].device->host->hostt->module);
	if (st_template.module)
	    __MOD_DEC_USE_COUNT(st_template.module);
	return (-EBUSY);
    }
    if ((SCpnt->sense_buffer[0] & 0x70) == 0x70 &&
	(SCpnt->sense_buffer[2] & 0x0f) == UNIT_ATTENTION) { /* New media? */
#if DEBUG
      printk(ST_DEB_MSG "osst%d: Unit wants attention\n", dev);
#endif
      for (i=0; i < 10; i++) {

        memset ((void *) &cmd[0], 0, 10);
        cmd[0] = TEST_UNIT_READY;

        SCpnt = st_do_scsi(SCpnt, STp, cmd, 0, STp->long_timeout, MAX_READY_RETRIES,
			   TRUE);
        if ((SCpnt->sense_buffer[0] & 0x70) != 0x70 ||
            (SCpnt->sense_buffer[2] & 0x0f) != UNIT_ATTENTION)
            break;
      }

      (STp->device)->was_reset = 0;
      STp->partition = STp->new_partition = 0;
      if (STp->can_partitions)
	STp->nbr_partitions = 1;  /* This guess will be updated later if necessary */
      for (i=0; i < ST_NBR_PARTITIONS; i++) {
	  STps = &(STp->ps[i]);
	  STps->rw = ST_IDLE;
	  STps->eof = ST_NOEOF;
	  STps->at_sm = 0;
	  STps->last_block_valid = FALSE;
	  STps->drv_block = 0;
	  STps->drv_file = 0 ;
      }
      new_session = TRUE;
    }

    if ((STp->buffer)->last_result_fatal != 0) {
      if ((STp->device)->scsi_level >= SCSI_2 &&
	  (SCpnt->sense_buffer[0] & 0x70) == 0x70 &&
	  (SCpnt->sense_buffer[2] & 0x0f) == NOT_READY &&
	  SCpnt->sense_buffer[12] == 0x3a) { /* Check ASC */
	STp->ready = ST_NO_TAPE;
      } else
	STp->ready = ST_NOT_READY;
      scsi_release_command(SCpnt);
      SCpnt = NULL;
      STp->density = 0;   	/* Clear the erroneous "residue" */
      STp->write_prot = 0;
      STp->block_size = 0;
      STp->frame_size = 0;
      STp->ps[0].drv_file = STp->ps[0].drv_block = (-1);
      STp->partition = STp->new_partition = 0;
      STp->door_locked = ST_UNLOCKED;
      return 0;
    }

    STp->min_block = STp->max_block = (-1);

    osst_configure_onstream(STp);

/*	STp->drv_write_prot = ((STp->buffer)->b_data[2] & 0x80) != 0; FIXME */

    if (STp->frame_size > (STp->buffer)->buffer_size &&
	!enlarge_buffer(STp->buffer, STp->frame_size, STp->restr_dma)) {
      printk(KERN_NOTICE "osst%d: Framesize %d too large for buffer.\n", dev,
	     STp->frame_size);
      scsi_release_command(SCpnt);
      SCpnt = NULL;
      (STp->buffer)->in_use = 0;
      STp->buffer = NULL;
      if (scsi_tapes[dev].device->host->hostt->module)
	 __MOD_DEC_USE_COUNT(scsi_tapes[dev].device->host->hostt->module);
      if (st_template.module)
	 __MOD_DEC_USE_COUNT(st_template.module);
      return (-EIO);
    }

    if ((STp->buffer)->buffer_size >= OS_FRAME_SIZE)
    {
      for (i = 0, b_size = 0; 
           i < STp->buffer->sg_segs && (b_size + STp->buffer->sg[i].length) <= OS_DATA_SIZE; 
           b_size += STp->buffer->sg[i++].length);
      STp->buffer->aux = (os_aux_t *) (STp->buffer->sg[i].address + OS_DATA_SIZE - b_size);
#if DEBUG
      printk(ST_DEB_MSG "osst%d: AUX points to %p in segment %d at %p\n", dev,
                         STp->buffer->aux, i, STp->buffer->sg[i].address);
#endif
    } else
      STp->buffer->aux = NULL; /* this had better never happen! */

    (STp->buffer)->buffer_blocks = 1;
    (STp->buffer)->buffer_bytes  =
    (STp->buffer)->read_pointer  =
    STp->logical_blk_in_buffer   = 0;

#if DEBUG
    if (debugging)
      printk(ST_DEB_MSG "osst%d: Block size: %d, frame size: %d, buffer size: %d (%d blocks).\n",
	     dev, STp->block_size, STp->frame_size, (STp->buffer)->buffer_size,
	     (STp->buffer)->buffer_blocks);
#endif

    if (STp->drv_write_prot) {
      STp->write_prot = 1;
#if DEBUG
      if (debugging)
	printk(ST_DEB_MSG "osst%d: Write protected\n", dev);
#endif
      if ((flags & O_ACCMODE) == O_WRONLY || (flags & O_ACCMODE) == O_RDWR) {
	(STp->buffer)->in_use = 0;
	STp->buffer = NULL;
	STp->in_use = 0;
	STp->in_use = 0;
	if (scsi_tapes[dev].device->host->hostt->module)
	    __MOD_DEC_USE_COUNT(scsi_tapes[dev].device->host->hostt->module);
	if(st_template.module)
	    __MOD_DEC_USE_COUNT(st_template.module);
        scsi_release_command(SCpnt);
        SCpnt = NULL;
	return (-EROFS);
      }
    }

    if (new_session) {  /* Change the drive parameters for the new mode */
#if DEBUG
      if (debugging)
	printk(ST_DEB_MSG "osst%d: New Session\n", dev);
#endif
      STp->density_changed = STp->blksize_changed = FALSE;
      STp->compression_changed = FALSE;
    }

    /*
     * properly position the tape and check the ADR headers
     */
    if (STp->door_locked == ST_UNLOCKED) {
       if (osst_int_ioctl(STp, MTLOCK, 0))
          printk(KERN_WARNING "osst%d: Can't lock drive door\n", dev);
       else
	  STp->door_locked = ST_LOCKED_AUTO;
    }

    osst_analyze_headers(STp, &SCpnt);

    scsi_release_command(SCpnt);
    SCpnt = NULL;

    return 0;
}



/* Flush the tape buffer before close */
	static int
scsi_tape_flush(struct file * filp)
{
    int result = 0, result2;
    Scsi_Tape * STp;
    ST_mode * STm;
    ST_partstat * STps;

    struct inode *inode = filp->f_dentry->d_inode;
    kdev_t devt = inode->i_rdev;
    int dev;

    if (filp->f_count > 1)
	return 0;

    dev = TAPE_NR(devt);
    STp = &(scsi_tapes[dev]);
    STm = &(STp->modes[STp->current_mode]);
    STps = &(STp->ps[STp->partition]);

    /* Up to this point generic checking of call's validity - may rely on st.c for that */
    /* From here on adapted to OnStream only */
    if (!STp->onstream) return (-ENXIO);

    if ( STps->rw == ST_WRITING && !(STp->device)->was_reset) {
      result = osst_flush_write_buffer(STp, 1);
      if (result != 0 && result != (-ENOSPC))
	  goto out;
    }

    if ( STps->rw == ST_WRITING && !(STp->device)->was_reset) {

#if DEBUG
      if (debugging) {
	printk(ST_DEB_MSG "osst%d: File length %ld bytes.\n",
	       dev, (long)(filp->f_pos));
	printk(ST_DEB_MSG "osst%d: Async write waits %d, finished %d.\n",
	       dev, STp->nbr_waits, STp->nbr_finished);
      }
#endif

      osst_write_filemark(STp);

      if (STps->drv_file >= 0)
	STps->drv_file++ ;
      STps->drv_block = 0;

      osst_write_eod(STp);
      osst_flush_drive_buffer(STp);
      osst_write_header(STp, !(STp->rew_at_close));
      osst_flush_drive_buffer(STp);

      STps->eof = ST_FM;

#if DEBUG
      if (debugging)
	printk(ST_DEB_MSG "osst%d: Buffer flushed, %d EOF(s) written\n",
	       dev, 1+STp->two_fm);
#endif
    }
    else if (!STp->rew_at_close) {
	STps = &(STp->ps[STp->partition]);
	if (!STm->sysv || STps->rw != ST_READING) {
	    if (STp->can_bsr)
		result = osst_flush_buffer(STp, 0);
	    else if (STps->eof == ST_FM_HIT) {
		result = cross_eof(STp, FALSE);
		if (result) {
		    if (STps->drv_file >= 0)
			STps->drv_file++;
		    STps->drv_block = 0;
		    STps->eof = ST_FM;
		}
		else
		    STps->eof = ST_NOEOF;
	    }
	}
	else if ((STps->eof == ST_NOEOF &&
		  !(result = cross_eof(STp, TRUE))) ||
		 STps->eof == ST_FM_HIT) {
	    if (STps->drv_file >= 0)
		STps->drv_file++;
	    STps->drv_block = 0;
	    STps->eof = ST_FM;
	}
    }

out:
    if (STp->rew_at_close) {
      result2 = osst_int_ioctl(STp, MTREW, 1);
      if (result == 0)
	  result = result2;
    }

    return result;
}


/* Close the device and release it */
	static int
scsi_tape_close(struct inode * inode, struct file * filp)
{
    int result = 0;
    Scsi_Tape * STp;

    kdev_t devt = inode->i_rdev;
    int dev;

    dev = TAPE_NR(devt);
    STp = &(scsi_tapes[dev]);

    /* Up to this point generic checking of call's validity - may rely on st.c for that */
    /* From here on adapted to OnStream only */
    if (!STp->onstream) return (-ENXIO);

    if (STp->door_locked == ST_LOCKED_AUTO)
      osst_int_ioctl(STp, MTUNLOCK, 0);

    if (STp->buffer != 0)
      STp->buffer->in_use = 0;

    STp->in_use = 0;
    if (scsi_tapes[dev].device->host->hostt->module)
	__MOD_DEC_USE_COUNT(scsi_tapes[dev].device->host->hostt->module);
    if(st_template.module)
	__MOD_DEC_USE_COUNT(st_template.module);

    return result;
}



/* The ioctl command */
	static int
st_ioctl(struct inode * inode,struct file * file,
	 unsigned int cmd_in, unsigned long arg)
{
   int i, cmd_nr, cmd_type;
   unsigned int blk;
   struct mtop mtc;
   struct mtpos mt_pos;
   Scsi_Tape *STp;
   ST_mode *STm;
   ST_partstat *STps;
   int dev = TAPE_NR(inode->i_rdev);

   STp = &(scsi_tapes[dev]);
#if DEBUG
   if (debugging && !STp->in_use) {
     printk(ST_DEB_MSG "osst%d: Incorrect device.\n", dev);
     return (-EIO);
   }
#endif
   STm = &(STp->modes[STp->current_mode]);
   STps = &(STp->ps[STp->partition]);

    /*
     * If we are in the middle of error recovery, don't let anyone
     * else try and use this device.  Also, if error recovery fails, it
     * may try and take the device offline, in which case all further
     * access to the device is prohibited.
     */
    if( !scsi_block_when_processing_errors(STp->device) ) {
        return -ENXIO;
    }

    /* Up to this point generic checking of call's validity - may rely on st.c for that */
    /* From here on adapted to OnStream only */
    if (!STp->onstream) return (-ENXIO);

   cmd_type = _IOC_TYPE(cmd_in);
   cmd_nr   = _IOC_NR(cmd_in);

   if (cmd_type == _IOC_TYPE(MTIOCTOP) && cmd_nr == _IOC_NR(MTIOCTOP)) {
     if (_IOC_SIZE(cmd_in) != sizeof(mtc))
       return (-EINVAL);

     i = copy_from_user((char *) &mtc, (char *)arg, sizeof(struct mtop));
     if (i)
	 return (-EFAULT);

     if (mtc.mt_op == MTSETDRVBUFFER && !capable(CAP_SYS_ADMIN)) {
       printk(KERN_WARNING "osst%d: MTSETDRVBUFFER only allowed for root.\n", dev);
       return (-EPERM);
     }
     if (!STm->defined &&
	 (mtc.mt_op != MTSETDRVBUFFER && (mtc.mt_count & MT_ST_OPTIONS) == 0))
       return (-ENXIO);

     if (!(STp->device)->was_reset) {

       if (STps->eof == ST_FM_HIT) {
	 if (mtc.mt_op == MTFSF || mtc.mt_op == MTFSFM|| mtc.mt_op == MTEOM) {
	   mtc.mt_count -= 1;
	   if (STps->drv_file >= 0)
	     STps->drv_file += 1;
	 }
	 else if (mtc.mt_op == MTBSF || mtc.mt_op == MTBSFM) {
	   mtc.mt_count += 1;
	   if (STps->drv_file >= 0)
	     STps->drv_file += 1;
	 }
       }

       if (mtc.mt_op == MTSEEK) {
	   /* Old position must be restored if partition will be changed */
	   i = !STp->can_partitions ||
	       (STp->new_partition != STp->partition);
       }
       else {
	   i = mtc.mt_op == MTREW || mtc.mt_op == MTOFFL ||
	       mtc.mt_op == MTRETEN || mtc.mt_op == MTEOM ||
	       mtc.mt_op == MTLOCK || mtc.mt_op == MTLOAD ||
	       mtc.mt_op == MTCOMPRESSION;
       }
       i = osst_flush_buffer(STp, i);
       if (i < 0)
	 return i;
     }
     else {
       /*
	* If there was a bus reset, block further access
	* to this device.  If the user wants to rewind the tape,
	* then reset the flag and allow access again.
	*/
       if(mtc.mt_op != MTREW &&
	  mtc.mt_op != MTOFFL &&
	  mtc.mt_op != MTRETEN &&
	  mtc.mt_op != MTERASE &&
	  mtc.mt_op != MTSEEK &&
	  mtc.mt_op != MTEOM)
	 return (-EIO);
       STp->device->was_reset = 0;
       if (STp->door_locked != ST_UNLOCKED &&
	   STp->door_locked != ST_LOCK_FAILS) {
	 if (osst_int_ioctl(STp, MTLOCK, 0)) {
	   printk(KERN_NOTICE "osst%d: Could not relock door after bus reset.\n",
		  dev);
	   STp->door_locked = ST_UNLOCKED;
	 }
       }
     }

     if (mtc.mt_op != MTNOP && mtc.mt_op != MTSETBLK &&
	 mtc.mt_op != MTSETDENSITY && mtc.mt_op != MTWSM &&
	 mtc.mt_op != MTSETDRVBUFFER && mtc.mt_op != MTSETPART)
       STps->rw = ST_IDLE;  /* Prevent automatic WEOF and fsf */

     if (mtc.mt_op == MTOFFL && STp->door_locked != ST_UNLOCKED)
       osst_int_ioctl(STp, MTUNLOCK, 0);  /* Ignore result! */

     if (mtc.mt_op == MTSETDRVBUFFER &&
	 (mtc.mt_count & MT_ST_OPTIONS) != 0)
       return st_set_options(inode, mtc.mt_count);
     if (mtc.mt_op == MTSETPART) {
/*     if (!STp->can_partitions ||
	   mtc.mt_count < 0 || mtc.mt_count >= ST_NBR_PARTITIONS)
	 return (-EINVAL);
       if (mtc.mt_count >= STp->nbr_partitions &&
	   (STp->nbr_partitions = nbr_partitions(inode)) < 0)
	 return (-EIO);*/
       if (mtc.mt_count >= STp->nbr_partitions)
	 return (-EINVAL);
       STp->new_partition = mtc.mt_count;
       return 0;
     }
     if (mtc.mt_op == MTMKPART) {
       if (!STp->can_partitions)
	 return (-EINVAL);
       if ((i = osst_int_ioctl(STp, MTREW, 0)) < 0 /*||
	   (i = partition_tape(inode, mtc.mt_count)) < 0*/)
	 return i;
       for (i=0; i < ST_NBR_PARTITIONS; i++) {
	 STp->ps[i].rw = ST_IDLE;
	 STp->ps[i].at_sm = 0;
	 STp->ps[i].last_block_valid = FALSE;
       }
       STp->partition = STp->new_partition = 0;
       STp->nbr_partitions = 1;  /* Bad guess ?-) */
       STps->drv_block = STps->drv_file = 0;
       return 0;
     }
     if (mtc.mt_op == MTSEEK) {
       i = osst_set_frame_position(STp, mtc.mt_count, 0);
       if (!STp->can_partitions)
	   STp->ps[0].rw = ST_IDLE;
       return i;
     }
/*   if (STp->can_partitions && STp->ready == ST_READY &&
	 (i = update_partition(inode)) < 0)
       return i;*/
     if (mtc.mt_op == MTCOMPRESSION)
       return (-EINVAL)/*st_compression(STp, (mtc.mt_count & 1))*/;
     else
       return osst_int_ioctl(STp, mtc.mt_op, mtc.mt_count);
   }

   if (!STm->defined)
     return (-ENXIO);

   if ((i = osst_flush_buffer(STp, FALSE)) < 0)
     return i;
/* if (STp->can_partitions &&
       (i = update_partition(inode)) < 0)
     return i;*/

   if (cmd_type == _IOC_TYPE(MTIOCGET) && cmd_nr == _IOC_NR(MTIOCGET)) {

     if (_IOC_SIZE(cmd_in) != sizeof(struct mtget))
       return (-EINVAL);

     (STp->mt_status)->mt_dsreg =
       ((STp->block_size << MT_ST_BLKSIZE_SHIFT) & MT_ST_BLKSIZE_MASK) |
       ((STp->density << MT_ST_DENSITY_SHIFT) & MT_ST_DENSITY_MASK);
     (STp->mt_status)->mt_blkno = STps->drv_block;
     (STp->mt_status)->mt_fileno = STps->drv_file;
     if (STp->block_size != 0) {
       if (STps->rw == ST_WRITING)
	 (STp->mt_status)->mt_blkno +=
	   (STp->buffer)->buffer_bytes / STp->block_size;
       else if (STps->rw == ST_READING)
	 (STp->mt_status)->mt_blkno -= ((STp->buffer)->buffer_bytes +
	   STp->block_size - 1) / STp->block_size;
     }

     (STp->mt_status)->mt_gstat = 0;
     if (STp->drv_write_prot)
       (STp->mt_status)->mt_gstat |= GMT_WR_PROT(0xffffffff);
     if ((STp->mt_status)->mt_blkno == 0) {
       if ((STp->mt_status)->mt_fileno == 0)
	 (STp->mt_status)->mt_gstat |= GMT_BOT(0xffffffff);
       else
	 (STp->mt_status)->mt_gstat |= GMT_EOF(0xffffffff);
     }
     (STp->mt_status)->mt_resid = STp->partition;
     if (STps->eof == ST_EOM_OK || STps->eof == ST_EOM_ERROR)
       (STp->mt_status)->mt_gstat |= GMT_EOT(0xffffffff);
     else if (STps->eof >= ST_EOM_OK)
       (STp->mt_status)->mt_gstat |= GMT_EOD(0xffffffff);
     if (STp->density == 1)
       (STp->mt_status)->mt_gstat |= GMT_D_800(0xffffffff);
     else if (STp->density == 2)
       (STp->mt_status)->mt_gstat |= GMT_D_1600(0xffffffff);
     else if (STp->density == 3)
       (STp->mt_status)->mt_gstat |= GMT_D_6250(0xffffffff);
     if (STp->ready == ST_READY)
       (STp->mt_status)->mt_gstat |= GMT_ONLINE(0xffffffff);
     if (STp->ready == ST_NO_TAPE)
       (STp->mt_status)->mt_gstat |= GMT_DR_OPEN(0xffffffff);
     if (STps->at_sm)
       (STp->mt_status)->mt_gstat |= GMT_SM(0xffffffff);
     if (STm->do_async_writes || (STm->do_buffer_writes && STp->block_size != 0) ||
	 STp->drv_buffer != 0)
       (STp->mt_status)->mt_gstat |= GMT_IM_REP_EN(0xffffffff);

     i = copy_to_user((char *)arg, (char *)(STp->mt_status),
		      sizeof(struct mtget));
     if (i)
	 return (-EFAULT);

     (STp->mt_status)->mt_erreg = 0;  /* Clear after read */
     return 0;
   } /* End of MTIOCGET */

   if (cmd_type == _IOC_TYPE(MTIOCPOS) && cmd_nr == _IOC_NR(MTIOCPOS)) {
     if (_IOC_SIZE(cmd_in) != sizeof(struct mtpos))
       return (-EINVAL);
     if ((blk = osst_get_frame_position(STp)) < 0)
       return blk;
     mt_pos.mt_blkno = blk;
     i = copy_to_user((char *)arg, (char *) (&mt_pos), sizeof(struct mtpos));
     if (i)
	 return (-EFAULT);
     return 0;
   }

   return scsi_ioctl(STp->device, cmd_in, (void *) arg);
}


/* Memory handling routines */

/* Try to allocate a new tape buffer */
	static ST_buffer *
new_tape_buffer( int from_initialization, int need_dma )
{
  int i, priority, b_size, got = 0, segs = 0;
  ST_buffer *tb;

  if (st_nbr_buffers >= st_template.dev_max)
    return NULL;  /* Should never happen */

  if (from_initialization)
    priority = GFP_ATOMIC;
  else
    priority = GFP_KERNEL;

  i = sizeof(ST_buffer) + (st_max_sg_segs - 1) * sizeof(struct scatterlist);
  tb = (ST_buffer *)scsi_init_malloc(i, priority);
  if (tb) {
    tb->this_size = i;
    if (need_dma)
      priority |= GFP_DMA;

    /* Try to allocate the first segment up to ST_FIRST_ORDER and the
       others big enough to reach the goal */
    for (b_size = PAGE_SIZE << ST_FIRST_ORDER;
	 b_size / 2 >= st_buffer_size && b_size > PAGE_SIZE; )
	b_size /= 2;
    for ( ; b_size >= PAGE_SIZE; b_size /= 2) {
	tb->sg[0].address =
	    (unsigned char *)scsi_init_malloc(b_size, priority);
	if (tb->sg[0].address != NULL) {
	    tb->sg[0].alt_address = NULL;
	    tb->sg[0].length = b_size;
	    break;
	}
    }
    if (tb->sg[segs].address == NULL) {
	scsi_init_free((char *)tb, tb->this_size);
	tb = NULL;
    }
    else {  /* Got something, continue */

	for (b_size = PAGE_SIZE;
	     st_buffer_size > tb->sg[0].length + (ST_FIRST_SG - 1) * b_size; )
	    b_size *= 2;

	for (segs=1, got=tb->sg[0].length;
	     got < st_buffer_size && segs < ST_FIRST_SG; ) {
	    tb->sg[segs].address =
		(unsigned char *)scsi_init_malloc(b_size, priority);
	    if (tb->sg[segs].address == NULL) {
		if (st_buffer_size - got <=
		    (ST_FIRST_SG - segs) * b_size / 2) {
		    b_size /= 2; /* Large enough for the rest of the buffers */
		    continue;
		}
		for (i=0; i < segs - 1; i++)
		    scsi_init_free(tb->sg[i].address, tb->sg[i].length);
		scsi_init_free((char *)tb, tb->this_size);
		tb = NULL;
		break;
	    }
	    tb->sg[segs].alt_address = NULL;
	    tb->sg[segs].length = b_size;
	    got += b_size;
	    segs++;
	}
    }
  }
  if (!tb) {
    printk(KERN_NOTICE "st: Can't allocate new tape buffer (nbr %d).\n",
	   st_nbr_buffers);
    return NULL;
  }
  tb->sg_segs = tb->orig_sg_segs = segs;
  tb->b_data = tb->sg[0].address;

#if DEBUG
  if (debugging) {
    printk(ST_DEB_MSG
    "st: Allocated tape buffer %d (%d bytes, %d segments, dma: %d, a: %p).\n",
	   st_nbr_buffers, got, tb->sg_segs, need_dma, tb->b_data);
    printk(ST_DEB_MSG
	   "st: segment sizes: first %d, last %d bytes.\n",
	   tb->sg[0].length, tb->sg[segs-1].length);
  }
#endif
  tb->in_use = 0;
  tb->dma = need_dma;
  tb->buffer_size = got;
  tb->writing = 0;
  st_buffers[st_nbr_buffers++] = tb;

  return tb;
}


/* Try to allocate a temporary enlarged tape buffer */
	static int
enlarge_buffer(ST_buffer *STbuffer, int new_size, int need_dma)
{
  int segs, nbr, max_segs, b_size, priority, got;

  normalize_buffer(STbuffer);

  max_segs = STbuffer->use_sg;
  if (max_segs > st_max_sg_segs)
      max_segs = st_max_sg_segs;
  nbr = max_segs - STbuffer->sg_segs;
  if (nbr <= 0)
      return FALSE;

  priority = GFP_KERNEL;
  if (need_dma)
    priority |= GFP_DMA;
  for (b_size = PAGE_SIZE; b_size * nbr < new_size - STbuffer->buffer_size; )
      b_size *= 2;

  for (segs=STbuffer->sg_segs, got=STbuffer->buffer_size;
       segs < max_segs && got < new_size; ) {
      STbuffer->sg[segs].address =
	  (unsigned char *)scsi_init_malloc(b_size, priority);
      if (STbuffer->sg[segs].address == NULL) {
	  if (new_size - got <= (max_segs - segs) * b_size / 2) {
	      b_size /= 2;  /* Large enough for the rest of the buffers */
	      continue;
	  }
	  printk(KERN_NOTICE "st: failed to enlarge buffer to %d bytes.\n",
		 new_size);
	  normalize_buffer(STbuffer);
	  return FALSE;
      }
      STbuffer->sg[segs].alt_address = NULL;
      STbuffer->sg[segs].length = b_size;
      STbuffer->sg_segs += 1;
      got += b_size;
      STbuffer->buffer_size = got;
      segs++;
  }
#if DEBUG
  if (debugging)
      printk(ST_DEB_MSG
	     "st: Succeeded to enlarge buffer to %d bytes (segs %d->%d, %d).\n",
	     got, STbuffer->orig_sg_segs, STbuffer->sg_segs, b_size);
#endif
#if DEBUG
  if (debugging) {
    for (nbr=0; st_buffers[nbr] == STbuffer && nbr < st_nbr_buffers; nbr++);
    printk(ST_DEB_MSG
    "st: Expanded tape buffer %d (%d bytes, %d segments, dma: %d, a: %p).\n",
	   nbr, got, STbuffer->sg_segs, need_dma, STbuffer->b_data);
    printk(ST_DEB_MSG
	   "st: segment sizes: first %d, last %d bytes.\n",
	   STbuffer->sg[0].length, STbuffer->sg[segs-1].length);
  }
#endif

  return TRUE;
}


/* Release the extra buffer */
	static void
normalize_buffer(ST_buffer *STbuffer)
{
  int i;

  for (i=STbuffer->orig_sg_segs; i < STbuffer->sg_segs; i++) {
      scsi_init_free(STbuffer->sg[i].address, STbuffer->sg[i].length);
      STbuffer->buffer_size -= STbuffer->sg[i].length;
  }
#if DEBUG
  if (debugging && STbuffer->orig_sg_segs < STbuffer->sg_segs)
      printk(ST_DEB_MSG "st: Buffer at %p normalized to %d bytes (segs %d).\n",
	     STbuffer->b_data, STbuffer->buffer_size, STbuffer->sg_segs);
#endif
  STbuffer->sg_segs = STbuffer->orig_sg_segs;
}


/* Move data from the user buffer to the tape buffer. Returns zero (success) or
   negative error code. */
	static int
append_to_buffer(const char *ubp, ST_buffer *st_bp, int do_count)
{
    int i, cnt, res, offset;

    for (i=0, offset=st_bp->buffer_bytes;
	 i < st_bp->sg_segs && offset >= st_bp->sg[i].length; i++)
	offset -= st_bp->sg[i].length;
    if (i == st_bp->sg_segs) {  /* Should never happen */
	printk(KERN_WARNING "st: append_to_buffer offset overflow.\n");
	return (-EIO);
    }
    for ( ; i < st_bp->sg_segs && do_count > 0; i++) {
	cnt = st_bp->sg[i].length - offset < do_count ?
	    st_bp->sg[i].length - offset : do_count;
	res = copy_from_user(st_bp->sg[i].address + offset, ubp, cnt);
	if (res)
	    return (-EFAULT);
	do_count -= cnt;
	st_bp->buffer_bytes += cnt;
	ubp += cnt;
	offset = 0;
    }
    if (do_count) {  /* Should never happen */
	printk(KERN_WARNING "st: append_to_buffer overflow (left %d).\n",
	       do_count);
	return (-EIO);
    }
    return 0;
}


/* Move data from the tape buffer to the user buffer. Returns zero (success) or
   negative error code. */
	static int
from_buffer(ST_buffer *st_bp, char *ubp, int do_count)
{
    int i, cnt, res, offset;

    for (i=0, offset=st_bp->read_pointer;
	 i < st_bp->sg_segs && offset >= st_bp->sg[i].length; i++)
	offset -= st_bp->sg[i].length;
    if (i == st_bp->sg_segs) {  /* Should never happen */
	printk(KERN_WARNING "st: from_buffer offset overflow.\n");
	return (-EIO);
    }
    for ( ; i < st_bp->sg_segs && do_count > 0; i++) {
	cnt = st_bp->sg[i].length - offset < do_count ?
	    st_bp->sg[i].length - offset : do_count;
	res = copy_to_user(ubp, st_bp->sg[i].address + offset, cnt);
	if (res)
	    return (-EFAULT);
	do_count -= cnt;
	st_bp->buffer_bytes -= cnt;
	st_bp->read_pointer += cnt;
	ubp += cnt;
	offset = 0;
    }
    if (do_count) {  /* Should never happen */
	printk(KERN_WARNING "st: from_buffer overflow (left %d).\n",
	       do_count);
	return (-EIO);
    }
    return 0;
}


/* Module housekeeping -- most of this stays with st.c -- osst setup should be simple */

#ifndef MODULE
/* Set the boot options. Syntax: st=xxx,yyy
   where xxx is buffer size in 1024 byte blocks and yyy is write threshold
   in 1024 byte blocks. */
	__initfunc( void
st_setup(char *str, int *ints))
{
  if (ints[0] > 0 && ints[1] > 0)
    st_buffer_size = ints[1] * ST_KILOBYTE;
  if (ints[0] > 1 && ints[2] > 0) {
    st_write_threshold = ints[2] * ST_KILOBYTE;
    if (st_write_threshold > st_buffer_size)
      st_write_threshold = st_buffer_size;
  }
  if (ints[0] > 2 && ints[3] > 0)
    st_max_buffers = ints[3];
}
#endif


static struct file_operations st_fops = {
   NULL,            /* lseek - default */
   st_read,         /* read - general block-dev read */
   st_write,        /* write - general block-dev write */
   NULL,            /* readdir - bad */
   NULL,            /* select */
   st_ioctl,        /* ioctl */
   NULL,            /* mmap */
   scsi_tape_open,  /* open */
   scsi_tape_flush, /* flush */
   scsi_tape_close, /* release */
   NULL		    /* fsync */
};

static int st_attach(Scsi_Device * SDp){
   Scsi_Tape * tpnt;
   ST_mode * STm;
   ST_partstat * STps;
   int i;

   if (SDp->type != TYPE_TAPE)
       return 1;

   if (st_template.nr_dev >= st_template.dev_max) {
       SDp->attached--;
       return 1;
   }

   for(tpnt = scsi_tapes, i=0; i<st_template.dev_max; i++, tpnt++)
     if(!tpnt->device) break;

   if(i >= st_template.dev_max) panic ("scsi_devices corrupt (st)");

   scsi_tapes[i].device = SDp;
   if (SDp->scsi_level <= 2)
     scsi_tapes[i].mt_status->mt_type = MT_ISSCSI1;
   else
     scsi_tapes[i].mt_status->mt_type = MT_ISSCSI2;
   
   tpnt->devt = MKDEV(SCSI_TAPE_MAJOR, i);
   tpnt->onstream = 0;
   tpnt->dirty = 0;
   tpnt->in_use = 0;
   tpnt->drv_buffer = 1;  /* Try buffering if no mode sense */
   tpnt->restr_dma = (SDp->host)->unchecked_isa_dma;
   tpnt->density = 0;
   tpnt->do_auto_lock = ST_AUTO_LOCK;
   tpnt->can_bsr = ST_IN_FILE_POS;
   tpnt->can_partitions = 0;
   tpnt->two_fm = ST_TWO_FM;
   tpnt->fast_mteom = ST_FAST_MTEOM;
   tpnt->scsi2_logical = ST_SCSI2LOGICAL;
   tpnt->write_threshold = st_write_threshold;
   tpnt->default_drvbuffer = 0xff; /* No forced buffering */
   tpnt->partition = 0;
   tpnt->new_partition = 0;
   tpnt->nbr_partitions = 0;
   tpnt->timeout = ST_TIMEOUT;
   tpnt->long_timeout = ST_LONG_TIMEOUT;

   /* Recognize OnStream tapes */
   printk ("osst%i: Tape driver with OnStream support osst %s\nosst%i: %s\n",
	       i, osst_version, i, cvsid);
   if (!memcmp (SDp->vendor, "OnStream", 8) &&
       ( !memcmp (SDp->model, "SC-30", 5) 
       ||!memcmp (SDp->model, "SC-50", 5)
       ||!memcmp (SDp->model, "SC-70", 5))) {
	tpnt->onstream = 1;
	tpnt->os_fw_rev = osst_parse_firmware_rev (SDp->rev);
#ifdef DEBUG
	printk("osst%i: OnStream tape drive recognized, Model %s, FW=%i\n",
		    i, SDp->model, tpnt->os_fw_rev);
#endif
     tpnt->omit_blklims = 1;
     tpnt->logical_blk_in_buffer = 0;
     tpnt->header_ok = 0;
     tpnt->linux_media = 0;
   }
   else
     printk("osst%i: Do not attempt to use this driver on a non-OnStream drive\n", i);

   for (i=0; i < ST_NBR_MODES; i++) {
     STm = &(tpnt->modes[i]);
     STm->defined = FALSE;
     STm->sysv = ST_SYSV;
     STm->defaults_for_writes = 0;
     STm->do_async_writes = ST_ASYNC_WRITES;
     STm->do_buffer_writes = ST_BUFFER_WRITES;
     STm->do_read_ahead = ST_READ_AHEAD;
     STm->default_compression = ST_DONT_TOUCH;
     STm->default_blksize = (-1);  /* No forced size */
     STm->default_density = (-1);  /* No forced density */
   }

   for (i=0; i < ST_NBR_PARTITIONS; i++) {
     STps = &(tpnt->ps[i]);
     STps->rw = ST_IDLE;
     STps->eof = ST_NOEOF;
     STps->at_sm = 0;
     STps->last_block_valid = FALSE;
     STps->drv_block = (-1);
     STps->drv_file = (-1);
   }

   tpnt->current_mode = 0;
   tpnt->modes[0].defined = TRUE;

   tpnt->density_changed = tpnt->compression_changed =
     tpnt->blksize_changed = FALSE;

   st_template.nr_dev++;
   return 0;
};

static int st_detect(Scsi_Device * SDp)
{
  if(SDp->type != TYPE_TAPE) return 0;

  printk(KERN_WARNING
	 "Detected scsi tape st%d at scsi%d, channel %d, id %d, lun %d\n",
	 st_template.dev_noticed++,
	 SDp->host->host_no, SDp->channel, SDp->id, SDp->lun);

  return 1;
}

static int st_registered = 0;

/* Driver initialization (not __initfunc because may be called later) */
static int st_init()
{
  int i;
  Scsi_Tape * STp;
#if !ST_RUNTIME_BUFFERS
  int target_nbr;
#endif

  if (st_template.dev_noticed == 0) return 0;

  if(!st_registered) {
    if (register_chrdev(SCSI_TAPE_MAJOR,"st",&st_fops)) {
      printk(KERN_ERR "Unable to get major %d for SCSI tapes\n",MAJOR_NR);
      return 1;
    }
    st_registered++;
  }

  if (scsi_tapes) return 0;
  st_template.dev_max = st_template.dev_noticed + ST_EXTRA_DEVS;
  if (st_template.dev_max < ST_MAX_TAPES)
    st_template.dev_max = ST_MAX_TAPES;
  if (st_template.dev_max > 128 / ST_NBR_MODES)
    printk(KERN_INFO "st: Only %d tapes accessible.\n", 128 / ST_NBR_MODES);
  scsi_tapes =
    (Scsi_Tape *) scsi_init_malloc(st_template.dev_max * sizeof(Scsi_Tape),
				   GFP_ATOMIC);
  if (scsi_tapes == NULL) {
    printk(KERN_ERR "Unable to allocate descriptors for SCSI tapes.\n");
    unregister_chrdev(SCSI_TAPE_MAJOR, "st");
    return 1;
  }

#if DEBUG
  printk(ST_DEB_MSG "st: Buffer size %d bytes, write threshold %d bytes.\n",
	 st_buffer_size, st_write_threshold);
#endif

  memset(scsi_tapes, 0, st_template.dev_max * sizeof(Scsi_Tape));
  for (i=0; i < st_template.dev_max; ++i) {
    STp = &(scsi_tapes[i]);
    STp->capacity = 0xfffff;
    STp->mt_status = (struct mtget *) scsi_init_malloc(sizeof(struct mtget),
						       GFP_ATOMIC);
    /* Initialize status */
    memset((void *) scsi_tapes[i].mt_status, 0, sizeof(struct mtget));
  }

  /* Allocate the buffers */
  st_buffers =
    (ST_buffer **) scsi_init_malloc(st_template.dev_max * sizeof(ST_buffer *),
				    GFP_ATOMIC);
  if (st_buffers == NULL) {
    printk(KERN_ERR "Unable to allocate tape buffer pointers.\n");
    unregister_chrdev(SCSI_TAPE_MAJOR, "st");
    scsi_init_free((char *) scsi_tapes,
		   st_template.dev_max * sizeof(Scsi_Tape));
    return 1;
  }

#if ST_RUNTIME_BUFFERS
  st_nbr_buffers = 0;
#else
  target_nbr = st_template.dev_noticed;
  if (target_nbr < ST_EXTRA_DEVS)
    target_nbr = ST_EXTRA_DEVS;
  if (target_nbr > st_max_buffers)
    target_nbr = st_max_buffers;

  for (i=st_nbr_buffers=0; i < target_nbr; i++) {
    if (!new_tape_buffer(TRUE, TRUE)) {
      if (i == 0) {
#if 0
	printk(KERN_ERR "Can't continue without at least one tape buffer.\n");
	unregister_chrdev(SCSI_TAPE_MAJOR, "st");
	scsi_init_free((char *) st_buffers,
		       st_template.dev_max * sizeof(ST_buffer *));
	scsi_init_free((char *) scsi_tapes,
		       st_template.dev_max * sizeof(Scsi_Tape));
	return 1;
#else
	printk(KERN_INFO "No tape buffers allocated at initialization.\n");
	break;
#endif
      }
      printk(KERN_INFO "Number of tape buffers adjusted.\n");
      break;
    }
  }
#endif
  return 0;
}

static void st_detach(Scsi_Device * SDp)
{
  Scsi_Tape * tpnt;
  int i;

  for(tpnt = scsi_tapes, i=0; i<st_template.dev_max; i++, tpnt++)
    if(tpnt->device == SDp) {
      tpnt->device = NULL;
      SDp->attached--;
      st_template.nr_dev--;
      st_template.dev_noticed--;
      return;
    }
  return;
}


#ifdef MODULE

int init_module(void) {
  int result;

  if (buffer_kbs > 0)
      st_buffer_size = buffer_kbs * ST_KILOBYTE;
  if (write_threshold_kbs > 0)
      st_write_threshold = write_threshold_kbs * ST_KILOBYTE;
  if (st_write_threshold > st_buffer_size)
      st_write_threshold = st_buffer_size;
  if (max_buffers > 0)
      st_max_buffers = max_buffers;
  if (max_sg_segs >= ST_FIRST_SG)
      st_max_sg_segs = max_sg_segs;
  printk(KERN_INFO "st: bufsize %d, wrt %d, max buffers %d, s/g segs %d.\n",
	 st_buffer_size, st_write_threshold, st_max_buffers, st_max_sg_segs);

  st_template.module = &__this_module;
  result = scsi_register_module(MODULE_SCSI_DEV, &st_template);
  if (result)
      return result;

  return 0;
}

void cleanup_module( void)
{
  int i, j;

  scsi_unregister_module(MODULE_SCSI_DEV, &st_template);
  unregister_chrdev(SCSI_TAPE_MAJOR, "st");
  st_registered--;
  if(scsi_tapes != NULL) {
    scsi_init_free((char *) scsi_tapes,
		   st_template.dev_max * sizeof(Scsi_Tape));

    if (st_buffers != NULL) {
      for (i=0; i < st_nbr_buffers; i++)
	if (st_buffers[i] != NULL) {
	  for (j=0; j < st_buffers[i]->sg_segs; j++)
	      scsi_init_free((char *) st_buffers[i]->sg[j].address,
			     st_buffers[i]->sg[j].length);
	  scsi_init_free((char *) st_buffers[i], st_buffers[i]->this_size);
	}

      scsi_init_free((char *) st_buffers,
		     st_template.dev_max * sizeof(ST_buffer *));
    }
  }
  st_template.dev_max = 0;
  printk(KERN_INFO "st: Unloaded.\n");
}
#endif /* MODULE */

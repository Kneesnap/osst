SHELL = /bin/sh

CC = gcc
LD = gcc

EXECS = sg_test_rwbuf os_dump os_write sginfo sg_map sg_scan sg_simple1 stream

#EXECS = sg_poll sg_dd512 sgq_dd512 sg_whoami \
	sg_inquiry sg_tst_med sg_debug sg_scan sg_dd2048 \
	sg_rbuf sginfo sg_runt_ex sg_simple1 sg_simple2 \
	sg_readcap scsi_inquiry sgp_dd sg_map \

#COMMON = sg_dd512 sg_dd2048 sg_scan sg_rbuf sg_tst_med sg_inquiry \
	 sginfo sg_runt_ex sg_readcap sg_simple2 scsi_inquiry

ILIBS = 


INCLUDES =

CFLAGS = -g -O2 -Wall #-pedantic

LDFLAGS =

all: $(EXECS)

common: $(COMMON)

clean:
	/bin/rm -f *.o $(EXECS) core

sg_poll: sg_poll.o sg_err.o
	$(LD) -o sg_poll $(LDFLAGS) sg_poll.o sg_err.o $(ILIBS) 

sg_dd512: sg_dd512.o sg_err.o
	$(LD) -o sg_dd512 $(LDFLAGS) sg_dd512.o sg_err.o $(ILIBS) 

sgq_dd512: sgq_dd512.o sg_err.o
	$(LD) -o sgq_dd512 $(LDFLAGS) sgq_dd512.o sg_err.o $(ILIBS) 

sg_whoami: sg_whoami.o sg_err.o
	$(LD) -o sg_whoami $(LDFLAGS) sg_whoami.o sg_err.o $(ILIBS) 

sg_inquiry: sg_inquiry.o
	$(LD) -o sg_inquiry $(LDFLAGS) sg_inquiry.o $(ILIBS) 

sg_tst_med: sg_tst_med.o
	$(LD) -o sg_tst_med $(LDFLAGS) sg_tst_med.o $(ILIBS) 

sg_debug: sg_debug.o
	$(LD) -o sg_debug $(LDFLAGS) sg_debug.o $(ILIBS) 

sg_scan: sg_scan.o sg_err.o
	$(LD) -o sg_scan $(LDFLAGS) sg_scan.o sg_err.o $(ILIBS) 

sg_dd2048: sg_dd2048.o sg_err.o
	$(LD) -o sg_dd2048 $(LDFLAGS) sg_dd2048.o sg_err.o $(ILIBS) 

sg_rbuf: sg_rbuf.o sg_err.o
	$(LD) -o sg_rbuf $(LDFLAGS) sg_rbuf.o sg_err.o $(ILIBS) 

sg_test_rwbuf: sg_test_rwbuf.o sg_err.o
	$(LD) -o sg_test_rwbuf $(LDFLAGS) sg_test_rwbuf.o sg_err.o $(ILIBS) 

os_dump: os_dump.o sg_err.o
	$(LD) -o $@ $(LDFLAGS) $^ $(ILIBS) 

os_write: os_write.o sg_err.o
	$(LD) -o $@ $(LDFLAGS) $^ $(ILIBS) 

sginfo: sginfo.o sg_err.o
	$(LD) -o sginfo $(LDFLAGS) sginfo.o sg_err.o $(ILIBS) 

sg_runt_ex: sg_runt_ex.o sg_err.o
	$(LD) -o sg_runt_ex $(LDFLAGS) sg_runt_ex.o sg_err.o $(ILIBS) 

sg_simple1: sg_simple1.o sg_err.o
	$(LD) -o sg_simple1 $(LDFLAGS) sg_simple1.o sg_err.o $(ILIBS) 

sg_simple2: sg_simple2.o
	$(LD) -o sg_simple2 $(LDFLAGS) sg_simple2.o $(ILIBS) 

sg_readcap: sg_readcap.o sg_err.o
	$(LD) -o sg_readcap $(LDFLAGS) sg_readcap.o sg_err.o $(ILIBS) 

scsi_inquiry: scsi_inquiry.o
	$(LD) -o scsi_inquiry $(LDFLAGS) scsi_inquiry.o $(ILIBS) 

sgp_dd.o: sgp_dd.c sg_err.h
	$(CC) $(CFLAGS) -D_REENTRANT -c sgp_dd.c -o sgp_dd.o

sgp_dd: sgp2_dd.o sg_err.o
	$(LD) -o sgp_dd $(LDFLAGS) sgp2_dd.o sg_err.o -lpthread $(ILIBS) 

sg_map: sg_map.o
	$(LD) -o sg_map $(LDFLAGS) sg_map.o $(ILIBS) 

stream: stream.c
	$(CC) $(CFLAGS) -o $@ $<
	

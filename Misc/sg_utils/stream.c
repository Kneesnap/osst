/* stream.c */
/*
 * stream buffers data read from stdin and written to stdout
 * It's useful to provide a buffer for apps that don't 
 * provide a buffer, but should, like eg. tar when dealing
 * with tapes
 */

/*
 * (c) Kurt Garloff <garloff@suse.de>, 3/2000
 * Copyright: GNU GPL
 * $Id$
 */

#define VERSION "0.50"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/fcntl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>

unsigned char * buffer;
unsigned int readpos = 0, writepos = 0, wrappos = 0;
char full = 0, empty = 1;

int eof = 0;

/* options */
unsigned int blksize = 10 * 1024;
unsigned int bufsize = 10 * 1024 * 1024;
char pad = 0;
unsigned int chunksize;
char verbose = 0;
char debug = 0;
char padbyte = 0;
char reportlevel = 0;

int fdin = 0, fdout = 1;
/* counter */
unsigned long long int readtotal = 0, writetotal = 0;

void usage (int exitcode)
{
	fprintf (stderr, "stream " VERSION " (c) Kurt Garloff <garloff@suse.de>, 3/2000, GNU GPL\n");
	fprintf (stderr, "Usage: stream [options] [infile] [outfile]\n");
	fprintf (stderr, "stream read data from infile, buffers it and writes it to outfile\n");
	fprintf (stderr, "Options: -b <blk> sets the block  size in 512byte units (tar compat.)\n");
	fprintf (stderr, "         -B <blk> sets the block  size in bytes (suffixes b,k,m accepted)\n");
	fprintf (stderr, "         -s <sz>  sets the buffer size in 512byte units\n");
	fprintf (stderr, "         -S <sz>  sets the buffer size in bytes (suffixes accepted)\n");
	fprintf (stderr, "         -p pads the output to a full block (optional arg: pad byte)\n");
	fprintf (stderr, "         -h displays this little help.\n");
	fprintf (stderr, "         -v sets verbose mode, -d debug output, -r reports buffer fill.\n");
	fprintf (stderr, "Defaults: block  size %ik (0: read/write in arbitrary chunks)\n", blksize/1024);
	fprintf (stderr, "          buffer size %iM\n", bufsize/(1024*1024));
	fprintf (stderr, "          defaults for infile and outfile are stdin and stdout resp.\n");
	exit (exitcode);
}

unsigned int myatol (const char * str)
{
	unsigned int ret; char * suff; char c;
	ret = strtoul (str, &suff, 0);
	if (!suff) return ret;
	c = *suff;
	switch (tolower (c)) {
	    case 'b': return ret * 512;
	    case 'k': return ret * 1024;
	    case 'm': return ret * 1024 *1024;
	    case 0: return ret;
	    default: usage (1);
	}
	return 0;
}

void parseargs (int argc, char *argv[])
{
	int c;
	char * inname = 0, * outname = 0;
	while ((c = getopt (argc, argv, ":b:B:s:S:p::hvdr")) != -1)
		switch (c) {
		  case 'b': blksize = 512 * atol (optarg); break;
		  case 'B': blksize = myatol (optarg); break;
		  case 's': bufsize = 512 * atol (optarg); break;
		  case 'S': bufsize = myatol (optarg); break;
		  case 'p': pad = 1; if (optarg) padbyte = atol (optarg); break;
		  case 'h': usage (0); break;
		  case 'v': verbose = 1; break;
		  case 'd': verbose = 1; debug = 1; break;
		  case 'r': reportlevel = 1; break;
		  case ':': fprintf (stderr, "stream: Missing argument to option \"%c\"!\n", optopt);
			usage (1); break;
		  case '?': fprintf (stderr, "stream: Unknown option \"%c\"!\n", optopt);
			usage (1); break;
		  default: fprintf (stderr, "stream: getopt error \"%c\"!\n", optopt);
			abort ();
		}
	if (argc > optind) inname  = argv[optind++];
	if (argc > optind) outname = argv[optind++];
	if (argc > optind) fprintf (stderr, "stream: ignore spurios arguments!\n");
	
	if (inname  && !strcmp (inname , "-")) inname  = 0;
	if (outname && !strcmp (outname, "-")) outname = 0;
	
	if (inname)  fdin  = open (inname,  O_RDONLY);
	if (fdin < 0) {
		perror ("stream: open  input file");
		exit (2);
	}
	if (outname) fdout = open (outname, O_WRONLY | O_CREAT /*| O_EXCL*/, 0644);
	if (fdout < 0) {
		perror ("stream: open output file");
		exit (2);
	}
	if (verbose) {
		fprintf (stderr, "stream: read from %s, write to %s\n   buffer %ik in %i sized blks\n",
			 inname? inname: "stdin", outname? outname: "stdout",
			 bufsize/1024, blksize);
		if (pad) fprintf (stderr, "   pad with \\x%02x\n", padbyte);
	}
}

inline int inbuf ()
{
	return wrappos + readpos - writepos;
}

void sighandler (int sig)
{
	fprintf (stderr, "stream: signal %i caught. Terminating!\n", sig);
	eof = 1; if (inbuf () > 0) empty = 0;
	signal (sig, SIG_DFL);
}

int tryread (int fin)
{
	int rd; int toread = chunksize; //int inbf;
	if (full || eof) return 0;
	/* Use TEMP_FAILURE_RETRY() ? */
	errno = 0;
	if (bufsize - readpos < chunksize) toread = bufsize - readpos;
	rd = read (fin, buffer+readpos, toread);
	if (rd < 0 || rd == 0) {
		if (errno == EAGAIN || errno == EINTR) return 0;
		if (debug) fprintf (stderr, "stream: rp %i wp %i wr %i\n",
				    readpos, writepos, wrappos);
		eof = 1; 
		if (blksize) toread = blksize - (inbuf () % blksize);
		if (pad && blksize && (inbuf () % blksize)) {
			if (verbose) fprintf (stderr, "stream: pad with %i bytes\n", toread);
			memset (buffer+readpos, padbyte, toread);
			readpos += toread;
		}
		if (inbuf () > 0) empty = 0;
		if (rd < 0) perror ("stream: read");
		else if (verbose) fprintf (stderr, "stream: EOF on input\n");
		if (verbose) fprintf (stderr, "stream: buffer contains %i bytes!\n", inbuf ());
		return 0; 
	}
	readpos += rd; readtotal += rd;
	if (inbuf () >= chunksize) empty = 0;
	if (bufsize <= readpos) {
		if (wrappos) fprintf (stderr, "\nstream: Oops! Wraparound!\n");
		wrappos = readpos;
		readpos = 0;
	}
	if (bufsize - inbuf () <= chunksize) full = 1;
	return rd;
}

int trywrite (int fout)
{
	int wr; int inbf; int towrite;
	if (empty) return 0;
	inbf = inbuf ();
	towrite = inbf > chunksize? chunksize: inbf;
	if (towrite < blksize && !eof) return 0;
	if (towrite < blksize && pad) towrite = blksize;
	/* Oops: this may eventually produce blocks with the wrong size */
	if (wrappos && towrite + writepos > wrappos) towrite = wrappos - writepos;
	errno = 0;
	wr = write (fout, buffer+writepos, towrite);
	if (wr < 0 || wr == 0) {
		if (errno == EAGAIN || errno == EINTR) return 0;
		eof = 1;
		if (wr < 0) perror ("stream: write");
		else if (verbose) fprintf (stderr, "stream: EOF on output!\n");
		exit (3);
		//return 0;
	}
	writepos += wr; writetotal += wr;
	if (wrappos && writepos >= wrappos) {
		writepos = 0;
		wrappos = 0;
	}
	if (inbuf () <= 0) empty = 1;
	if (inbuf () < chunksize && !eof) empty = 1;
	if (bufsize - inbuf () > chunksize) full = 0;
	return wr;
}


int main (int argc, char *argv[])
{
	int rd, wr; int ready; int ctr = 0; int lastempty = 0;
	int wasempty = 0; time_t start = time (0);
	//time_t clk = clock ();
	struct timeval timeout;
	fd_set fdsin, fdsout, fdsexc;
	
	parseargs (argc, argv);
	
	if (blksize) chunksize = blksize;
	else chunksize = getpagesize ();
	
	bufsize -= bufsize % chunksize;
	buffer = malloc (bufsize);
	if (!buffer) { 
		fprintf (stderr, "stream: buffer malloc() failed!\n"); 
		exit (2); 
	}
	/* Set non-blocking mode */
	fcntl (fdin,  F_SETFL, O_NONBLOCK);
	fcntl (fdout, F_SETFL, O_NONBLOCK);
	/* Graceful kill */
	signal (SIGTERM, sighandler);
	signal (SIGINT , sighandler);
	signal (SIGQUIT, sighandler);
	signal (SIGHUP , sighandler);
	/* Main loop */
	while (!eof || !empty) {
		rd = tryread  (fdin);
		wr = trywrite (fdout);
		if (rd && !eof && inbuf() < 1*bufsize / 3)
			rd += tryread (fdin);
		if (wr && !empty && inbuf() > 2*bufsize / 3)
			wr += trywrite (fdout);
		if (rd && !eof && inbuf() < 1*bufsize / 10)
			rd += tryread (fdin);
		if (wr && !empty && inbuf() > 9*bufsize / 10)
			wr += trywrite (fdout);
		if (reportlevel && !(ctr++ % 128)) 
			fprintf (stderr, "stream: buffer %2i%% %9i %11Li %11Li %6.3fMB/s \r",
				 100*inbuf() / bufsize, inbuf (), readtotal, 
				 writetotal, writetotal/(1+1024*1024*difftime (time(0), start)));
#if 1
		if (wrappos + readpos - writepos != readtotal - writetotal && !eof) {
			fprintf (stderr, "\nstream: Oops!\n");
			abort ();
		}
#endif
		if (inbuf () <= chunksize) lastempty++; else lastempty = 0;
		if (lastempty == 1) wasempty++;
		if (rd == 0 && wr == 0) {
			/* Prepare filedescriptor sets for select() */
			FD_ZERO (&fdsin ); FD_ZERO (&fdsout); FD_ZERO (&fdsexc);
			FD_SET (fdin , &fdsin ); FD_SET (fdout, &fdsout);
			FD_SET (fdin , &fdsexc); FD_SET (fdout, &fdsexc);
			timeout.tv_usec = 0; timeout.tv_sec = 30;
			if (debug) fprintf (stderr, "stream: select() (%i %i) ", empty, full);
			/* There might be three possible reasons for idling:
			 * - buffer empty and no input 
			 * - buffer full and no output
			 * - both no input nor output
			 */
			if (full)
				ready = select (FD_SETSIZE, NULL  , &fdsout, &fdsexc, &timeout);
			else if (empty)
				ready = select (FD_SETSIZE, &fdsin, NULL   , &fdsexc, &timeout);
			else
				ready = select (FD_SETSIZE, &fdsin, &fdsout, &fdsexc, &timeout);
			if (debug) fprintf (stderr, "= %i\n", ready);
			if (!ready)
				if (verbose) fprintf (stderr, "\nstream: no progress.\n");
		}
	}
	if (reportlevel) fprintf (stderr, "\n");
	if (debug) fprintf (stderr, "stream: normal exit (%i %i)\n", empty, eof);
	if (verbose) fprintf (stderr, "stream: read %Li bytes, wrote %Li\n"
			      "stream: Buf was %i times empty, avg. speed %6.3fMB/s\n",
			      readtotal, writetotal,
			      wasempty, writetotal/(1+1024*1024*difftime (time(0), start)));
	return 0;
}

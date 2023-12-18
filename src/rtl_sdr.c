/*
 * rtl-sdr, turns your Realtek RTL2832 based DVB dongle into a SDR receiver
 * Copyright (C) 2012 by Steve Markgraf <steve@steve-m.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* Modified for 2-Frequency-Operation by DC9ST for TDOA operation
   samples at the first frequency, switches to the second frequency and then back to the first again
   http://www.panoradio-sdr.de/tdoa-transmitter-localization-with-rtl-sdrs/
   
   main changes:
   samples 3n IQ samples: first n at frequency f, then n at freuqency h and then again n at frequency f
*/

#include <stdio.h>
#include <netdb.h> 
#include <stdbool.h>
#include <netinet/in.h> 
#include <stdlib.h> 
#include <string.h> 
#include <sys/socket.h> 
#include <sys/types.h> 
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef _WIN32
#include <unistd.h>
#else
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include "getopt/getopt.h"
#endif

#include "rtl-sdr.h"
#include "convenience/convenience.h"

#define DEFAULT_SAMPLE_RATE		2048000
#define DEFAULT_BUF_LENGTH		(16 * 16384)
#define MINIMAL_BUF_LENGTH		512
#define MAXIMAL_BUF_LENGTH		(256 * 16384)

static int do_exit = 0;
static uint32_t bytes_to_read_per_freq = 2000000 * 2;
static uint32_t bytes_to_read = 6000000 * 2; // sum of i and q samples
static uint32_t bytes_to_read_per_freq_backup = 0;
static uint32_t bytes_to_read_backup = 0;
static rtlsdr_dev_t *dev = NULL;
static int frequency_changed = 0;
static int frequency_changed_back = 0;
uint32_t frequency1 = 100000000;
uint32_t frequency2 = 100000000;

/** START of TCP mod ------------------------------------------ **/

#define srv_PORT 4500 
#define srv_BUFMAX 256

static bool server_loop = true;
int srv_fd, conn_fd;
struct sockaddr_in servaddr, cli; 

/* Set up TCP server on port 4500 */

bool tcp_init_server()
{
	int len; 
	int opt=1;
	struct timeval tv;
   
	srv_fd = socket(AF_INET, SOCK_STREAM, 0); 
	if (srv_fd == -1)
	{ 
		printf("Can't create socket\n"); 
		return false;
	} 


	if(setsockopt(srv_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
	{
		printf("Can't setsockopt to SO_REUSEADDR\n");
		return false;
	}

	tv.tv_sec = 5;
	tv.tv_usec = 0;

	if(setsockopt(srv_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)))
	{
		printf("Can't setsockopt to SO_RCVTIMEO\n");
		return false;
	}

	bzero(&servaddr, sizeof(servaddr)); 
   
	servaddr.sin_family = AF_INET; 
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY); 
	servaddr.sin_port = htons(srv_PORT); 
   
	if ((bind(srv_fd, (struct sockaddr*)&servaddr, sizeof(servaddr))) != 0)
	{ 
		printf("Can't bind to socket\n"); 
		close(srv_fd);
		return false;
	} 
   
	if ((listen(srv_fd, 5)) != 0)
	{ 
		printf("Can't listen to socket\n"); 
		close(srv_fd);
		return false;
	} 
   
        printf("Ready to accept TCP on port %d to trigger recording\n", srv_PORT); 

	return true;
}

/* Waits for connection and command from master */

long wait_id_from_conn()
{
	int retval = 0;
	char buffer[srv_BUFMAX] = {};
	socklen_t len = sizeof(cli); 

	conn_fd = accept(srv_fd, (struct sockaddr*)&cli, &len); 
	if (conn_fd < 0)
	{ 
		return 0;
	} 

	read(conn_fd, buffer, sizeof(buffer));

	if(strncasecmp(buffer,"TDOA:", 5) == 0)
	{
		long identificator = 0;
		char *id = buffer+5;
		identificator = atol(id);
		if(identificator>0)
		{
			retval = identificator;
		}
		else
		{
			retval = 0;
		}
		
	}
	else if(strncasecmp(buffer,"QUIT",4)==0)
	{
		retval = -1;
	}
	else
	{
		printf( "Unknown command: '%s'\n", buffer );
	}

	close(conn_fd);

	return retval;
}

/** END of TCP mod --------------------------------------- **/

void usage(void)
{
	fprintf(stderr,
		"\n"
		"rtl_sdr, an I/Q recorder for RTL2832 based DVB-T receivers\n"
		"2-Frequency-Mode for TDOA by DC9ST, 2017\n\n"
		"receives 3xn IQ samples:\n"
		"first n at frequency 1, then n at frequency 2, then n at frequency 1 again\n\n"
		"Usage:\t-f frequency_to_tune_to frequency 1/reference [Hz]\n"
		"\t-h frequency_to_tune_to frequency 2/measure [Hz]\n"
		"\t[-s samplerate (default: 2048000 Hz)]\n"
		"\t[-d device_index (default: 0)]\n"
		"\t[-g gain (default: 0 for auto)]\n"
		"\t[-p ppm_error (default: 0)]\n"
		"\t[-b output_block_size (default: 16 * 16384)]\n"
		"\t[-n number n of IQ samples to read per frequency (total length = 3x specified)(default: 2e6)]\n"
		"\t[-S force sync output (default: async)]\n"
		"\tfilename (a '-' dumps samples to stdout)\n\n");
	exit(1);
}

#ifdef _WIN32
BOOL WINAPI
sighandler(int signum)
{
	if (CTRL_C_EVENT == signum) {
		fprintf(stderr, "Signal caught, exiting!\n");
		do_exit = 1;
		server_loop = false;
		rtlsdr_cancel_async(dev);
		return TRUE;
	}
	return FALSE;
}
#else
static void sighandler(int signum)
{
	fprintf(stderr, "Signal caught, exiting!\n");
	do_exit = 1;
	server_loop = false;
	rtlsdr_cancel_async(dev);
}
#endif

static void rtlsdr_callback(unsigned char *buf, uint32_t len, void *ctx)
{
	if (ctx) {
		if (do_exit) {
			return;
		}

		if ((bytes_to_read > 0) && (bytes_to_read < len)) {
			len = bytes_to_read;
			do_exit = 1;
			rtlsdr_cancel_async(dev);
		}

		if (fwrite(buf, 1, len, (FILE*)ctx) != len) {
			fprintf(stderr, "Short write, samples lost, exiting!\n");
			rtlsdr_cancel_async(dev);
		}

		if (bytes_to_read > 0) {
			bytes_to_read -= len;
			
			//switch to frequency 2
			if ((bytes_to_read < bytes_to_read_per_freq*2) && (frequency_changed == 0)) {
				verbose_set_frequency(dev, frequency2);
				frequency_changed = 1;
			}
			
			// switch back to frequency 1 again
			if ((bytes_to_read < bytes_to_read_per_freq) && (frequency_changed_back == 0)) {
				verbose_set_frequency(dev, frequency1);
				frequency_changed_back = 1;
			}

		}
	} else {
		printf("ctx=null, returning\n");
	}
}

int main(int argc, char **argv)
{
#ifndef _WIN32
	struct sigaction sigact;
#endif
	char *filename = NULL;
	int n_read;
	int r, opt;
	int gain = 0;
	int ppm_error = 0;
	int sync_mode = 0;
	FILE *file;
	uint8_t *buffer;
	int dev_index = 0;
	int dev_given = 0;
	uint32_t samp_rate = DEFAULT_SAMPLE_RATE;
	uint32_t out_block_size = DEFAULT_BUF_LENGTH;
	bool server_ok = false;

	while ((opt = getopt(argc, argv, "d:f:h:g:s:b:n:p:S")) != -1) {
		switch (opt) {
		case 'd':
			dev_index = verbose_device_search(optarg);
			dev_given = 1;
			break;
		case 'f':
			frequency1 = (uint32_t)atofs(optarg);
			break;
		case 'h':
			frequency2 = (uint32_t)atofs(optarg);
			break;
		case 'g':
			gain = (int)(atof(optarg) * 10); /* tenths of a dB */
			break;
		case 's':
			samp_rate = (uint32_t)atofs(optarg);
			break;
		case 'p':
			ppm_error = atoi(optarg);
			break;
		case 'b':
			out_block_size = (uint32_t)atof(optarg);
			break;
		case 'n':
			bytes_to_read_per_freq = (uint32_t)atof(optarg) * 2;
			bytes_to_read = bytes_to_read_per_freq * 3; // three recordings with different frequencies
			break;
		case 'S':
			sync_mode = 1;
			break;
		default:
			usage();
			break;
		}
	}

// Removed as we do not require a filename
//	if (argc <= optind) {
//		usage();
//	}
// 	else {
//		filename = argv[optind];
//	}

	if(out_block_size < MINIMAL_BUF_LENGTH ||
	   out_block_size > MAXIMAL_BUF_LENGTH ){
		fprintf(stderr,
			"Output block size wrong value, falling back to default\n");
		fprintf(stderr,
			"Minimal length: %u\n", MINIMAL_BUF_LENGTH);
		fprintf(stderr,
			"Maximal length: %u\n", MAXIMAL_BUF_LENGTH);
		out_block_size = DEFAULT_BUF_LENGTH;
	}

	buffer = malloc(out_block_size * sizeof(uint8_t));

	if (!dev_given) {
		dev_index = verbose_device_search("0");
	}

	if (dev_index < 0) {
		exit(1);
	}

	r = rtlsdr_open(&dev, (uint32_t)dev_index);
	if (r < 0) {
		fprintf(stderr, "Failed to open rtlsdr device #%d.\n", dev_index);
		exit(1);
	}
#ifndef _WIN32
	sigact.sa_handler = sighandler;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigaction(SIGINT, &sigact, NULL);
	sigaction(SIGTERM, &sigact, NULL);
	sigaction(SIGQUIT, &sigact, NULL);
	sigaction(SIGPIPE, &sigact, NULL);
#else
	SetConsoleCtrlHandler( (PHANDLER_ROUTINE) sighandler, TRUE );
#endif
	/* Set the sample rate */
	verbose_set_sample_rate(dev, samp_rate);

	/* Set the frequency */
	verbose_set_frequency(dev, frequency1);

	if (0 == gain) {
		 /* Enable automatic gain */
		verbose_auto_gain(dev);
	} else {
		/* Enable manual gain */
		gain = nearest_gain(dev, gain);
		verbose_gain_set(dev, gain);
	}

	verbose_ppm_set(dev, ppm_error);

	server_ok = tcp_init_server();
	if( ! server_ok )
	{
		printf( "Unable to start TCP server\n" );
		exit(1);
	}
	bytes_to_read_per_freq_backup = bytes_to_read_per_freq;
	bytes_to_read_backup = bytes_to_read;

	while( server_loop ) 
	{
		char filename[256];
		long cap_id = 0;
		do_exit = 0;
		frequency_changed = 0;
		frequency_changed_back = 0;
		bytes_to_read_per_freq = bytes_to_read_per_freq_backup;
		bytes_to_read = bytes_to_read_backup;

		while( cap_id == 0 && server_loop )
		{
			cap_id = wait_id_from_conn();
			if(cap_id == -1) {
				server_loop = false;
				break;
			}
		}


		if(cap_id > 0) {
			sprintf(filename, "%ld.dat", cap_id);
		
			file = fopen(filename, "wb");
			if (!file) {
				fprintf(stderr, "Failed to open %s\n", filename);
				goto out;
			}
		
			/* Reset endpoint before we start reading from it (mandatory) */
			verbose_reset_buffer(dev);
		
			if (sync_mode) {
				fprintf(stderr, "Reading samples in sync mode...\n");
				while (!do_exit) {
					r = rtlsdr_read_sync(dev, buffer, out_block_size, &n_read);
					if (r < 0) {
						fprintf(stderr, "WARNING: sync read failed.\n");
						break;
					}
		
					if ((bytes_to_read > 0) && (bytes_to_read < (uint32_t)n_read)) {
						n_read = bytes_to_read;
						do_exit = 1;
					}
		
					if (fwrite(buffer, 1, n_read, file) != (size_t)n_read) {
						fprintf(stderr, "Short write, samples lost, exiting!\n");
						break;
					}
		
					if ((uint32_t)n_read < out_block_size) {
						fprintf(stderr, "Short read, samples lost, exiting!\n");
						break;
					}
		
					if (bytes_to_read > 0)
						bytes_to_read -= n_read;
				}
			} else {
				fprintf(stderr, "Reading samples in async mode...\n");
				r = rtlsdr_read_async(dev, rtlsdr_callback, (void *)file,
						      0, out_block_size);
			}
	
			printf("Closing output file\n");
			fclose(file);
		}	
	}	// while(true)
	
	close(srv_fd);

	if (do_exit)
		fprintf(stderr, "\nUser cancel, exiting...\n");
	else
		fprintf(stderr, "\nLibrary error %d, exiting...\n", r);

	rtlsdr_close(dev);
	free (buffer);
out:
	return r >= 0 ? r : -r;
}



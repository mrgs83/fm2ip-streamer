/*
 * rtl_fm_streamer, turns your Realtek RTL2832 based DVB dongle into a SDR receiver
 * Based on "rtl_fm", see http://sdr.osmocom.org/trac/wiki/rtl-sdr for details
 *
 * Copyright (C) 2012 by Steve Markgraf <steve@steve-m.de>
 * Copyright (C) 2012 by Hoernchen <la@tfc-server.de>
 * Copyright (C) 2012 by Kyle Keen <keenerd@gmail.com>
 * Copyright (C) 2013 by Elias Oenal <EliasOenal@gmail.com>
 * Copyright (C) 2015 by Miroslav Slugen <thunder.m@email.cz>
 * Copyright (C) 2015 by Albrecht Lohoefener <albrechtloh@gmx.de>
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

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef _WIN32
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#else
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include "getopt/getopt.h"
#define msleep(x) Sleep(x)
#define usleep(x) Sleep(x/1000)
#ifdef _MSC_VER
#define round(x) (x > 0.0 ? floor(x + 0.5): ceil(x - 0.5))
#endif
#define _USE_MATH_DEFINES
#endif

#include <math.h>
#include <pthread.h>
#include <libusb.h>

#include "rtl-sdr.h"
#include "convenience/convenience.h"
#include "jsonrpc-c/jsonrpc-c.h"

#define VERSION "0.0.6"

#define MAX_TUNERS 4  // Define a maximum number of tuners supported

struct tuner_state {
    int device_index;         // Index of the device (tuner)
    uint32_t freq;            // Frequency to tune
    int port;                 // Port to stream
    pthread_t thread;         // Thread for this tuner
    rtlsdr_dev_t *dev;        // RTL-SDR device handle
    struct demod_state demod; // Demodulator state
    struct output_state output; // Output state
};

struct tuner_state tuners[MAX_TUNERS];

// Initialize each tuner with a device, port, and frequency
void init_tuners(int num_tuners) {
    for (int i = 0; i < num_tuners; i++) {
        tuners[i].device_index = i;  // Device index is the tuner number
        tuners[i].port = 1000 + i;   // Assign a unique port for each tuner (e.g., 1000, 1001)
        tuners[i].freq = 98300000;   // Set an example frequency
        pthread_create(&tuners[i].thread, NULL, tuner_thread_fn, (void*)&tuners[i]);
    }
}

// Thread function for each tuner
void* tuner_thread_fn(void* arg) {
    struct tuner_state *tuner = (struct tuner_state*)arg;
    // Configure and start streaming for this tuner
    configure_tuner(tuner->device_index, tuner->port, tuner->freq);
    return NULL;
}

#define DEFAULT_SAMPLE_RATE		240000
#define DEFAULT_BUF_LENGTH		(1 * 16384)
#define MAXIMUM_OVERSAMPLE		16
#define MAXIMUM_BUF_LENGTH		(MAXIMUM_OVERSAMPLE * DEFAULT_BUF_LENGTH)
#define AUTO_GAIN				100
#define BUFFER_DUMP				4096

#define FREQUENCIES_LIMIT		1000

#define PI2_F           6.28318531f
#define PI_F            3.14159265f
#define PI_2_F          1.5707963f
#define PI_4_F          0.78539816f

#define DEEMPHASIS_NONE         0
#define DEEMPHASIS_FM_EU        0.000050
#define DEEMPHASIS_FM_USA       0.000075

typedef enum
{
    false = 0,
    true
}bool;

static volatile int do_exit = 0;
static int lcm_post[17] =
{ 1, 1, 1, 3, 1, 5, 3, 7, 1, 9, 5, 11, 3, 13, 7, 15, 1 };
static int ACTUAL_BUF_LENGTH;

static int ConnectionDesc;
bool isStartStream;
bool isReading;

struct lp_real
{
	float *br;
	float *bm;
	float *bs;
	float *fm;
	float *fp;
	float *fs;
	float swf;
	float cwf;
	float pp;
	int pos;
	int size;
	int rsize;
	int mode;
};

struct dongle_state
{
	int exit_flag;
	pthread_t thread;
	rtlsdr_dev_t *dev;
	int dev_index;
	uint32_t freq;
	uint32_t rate;
	int gain;
	int ppm_error;
	int direct_sampling;
	int mute;
	struct demod_state *demod_target;
};

struct demod_state
{
	int exit_flag;
	pthread_t thread;
	uint8_t buf[MAXIMUM_BUF_LENGTH];
	uint32_t buf_len;
	/* required 4 bytes for F32 part */
	int16_t lowpassed[MAXIMUM_BUF_LENGTH << 1];
	int lp_len;
	/* tmp buffer for low pass filter F32 */
	float lowpass_tb[48];
	int16_t lp_i_hist[10][6];
	int16_t lp_q_hist[10][6];
	/* result buffer fo FM will be always 1/2 of lowpassed or less, so no need to shift */
	int16_t result[MAXIMUM_BUF_LENGTH];
	int result_len;
	int16_t droop_i_hist[9];
	int16_t droop_q_hist[9];
	int offset_tuning;
	int rate_in;
	int rate_out;
	int rate_out2;
	int now_r, now_j;
	int pre_r, pre_j;
	float pre_r_f32, pre_j_f32;
	int prev_index;
	int downsample; /* min 1, max 256 */
	int post_downsample;
	int output_scale;
	int squelch_level, conseq_squelch, squelch_hits, terminate_on_squelch;
	int downsample_passes;
	int comp_fir_size;
	int custom_atan;
	double deemph;
	int deemph_a;
	int deemph_l;
	int deemph_r;
	float deemph_l_f32;
	float deemph_r_f32;
	float deemph_lambda;
	float volume;
	int now_lpr;
	int prev_lpr_index;
	struct lp_real lpr;
	pthread_rwlock_t rw;
	pthread_cond_t ready;
	pthread_mutex_t ready_m;
	struct output_state *output_target;
};

struct output_state
{
	int exit_flag;
	pthread_t thread;
	FILE *file;
	char *filename;
	int rate;
	int16_t *result;
	int result_len;
	pthread_rwlock_t rw;
	pthread_cond_t ready;
	pthread_mutex_t ready_m;
};

struct controller_state
{
	int exit_flag;
	pthread_t thread;
	uint32_t freqs[FREQUENCIES_LIMIT];
	int freq_len;
	int freq_now;
	int edge;
	int wb_mode;
	pthread_cond_t hop;
	pthread_mutex_t hop_m;
};

typedef struct
{
	int SocketDesc;
	struct sockaddr_in serv_addr, client_addr;
	socklen_t size;
	pthread_t ConnectionThread;
} connection_state;

typedef struct
{
	int Port;
	struct jrpc_server Server;
	float RMSShadowBuf[MAXIMUM_BUF_LENGTH << 1];
	int RMSShadowBuf_len;
	pthread_t thread;
} json_rpc_state;

// Instances for components
struct dongle_state dongle;
struct demod_state demod;
struct output_state output;
struct controller_state controller;
connection_state connection;
json_rpc_state json_rpc;

int main(int argc, char **argv)
{
#ifndef _WIN32
	struct sigaction sigact;
#endif
	int r, opt;
	int dev_given = 0;
	int custom_ppm = 0;

	fprintf(stderr, "RTL SDR FM Streamer Version %s\n", VERSION);

	dongle_init(&dongle);
	demod_init(&demod);
	output_init(&output);
	controller_init(&controller);
	connection_init(&connection);
	json_rpc_init(&json_rpc);

	isStartStream = false;
	isReading = false;

	// Parse command-line options
	while((opt = getopt(argc, argv, "d:f:g:s:b:l:o:t:r:p:E:F:h:P:j:v:XY")) != -1)
	{
		// handle command line arguments here
		switch (opt)
		{
            // Option handling code here
		}
	}

	// Initialize multiple tuners
	init_tuners(MAX_TUNERS);  // Call the function to initialize tuners

	// Setup TCP connection
	TCPSetup(&connection);

	/* quadruple sample_rate to limit to Δθ to ±π/2 */
	demod.rate_in *= demod.post_downsample;

	if (!output.rate) {
		output.rate = demod.rate_out;
	}

	sanity_checks();

	if (controller.freq_len > 1) {
		demod.terminate_on_squelch = 0;
	}

	if (argc <= optind) {
		output.filename = "-";
	} else {
		output.filename = argv[optind];
	}

	ACTUAL_BUF_LENGTH = lcm_post[demod.post_downsample] * DEFAULT_BUF_LENGTH;

	if (!dev_given) {
		dongle.dev_index = verbose_device_search("0");
	}

	if (dongle.dev_index < 0) {
		exit(1);
	}

	r = rtlsdr_open(&dongle.dev, (uint32_t) dongle.dev_index);
	if (r < 0) {
		fprintf(stderr, "Failed to open rtlsdr device #%d.\n", dongle.dev_index);
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

	if (demod.deemph) {
		// Calculate de-emphasis filter parameters
	}

	// Set tuner gain
	if (dongle.gain == AUTO_GAIN) {
		verbose_auto_gain(dongle.dev);
	} else {
		dongle.gain = nearest_gain(dongle.dev, dongle.gain);
		verbose_gain_set(dongle.dev, dongle.gain);
	}

	verbose_ppm_set(dongle.dev, dongle.ppm_error);

	if (strcmp(output.filename, "-") == 0) { 
		// Write samples to stdout
		output.file = stdout;
#ifdef _WIN32
		_setmode(_fileno(output.file), _O_BINARY);
#endif
	} else {
		output.file = fopen(output.filename, "wb");
		if (!output.file) {
			fprintf(stderr, "Failed to open %s\n", output.filename);
			exit(1);
		}
	}

	// Init FM float demodulator
	init_u8_f32_table();
	init_lp_f32();
	init_lp_real_f32(&demod);

	/* Reset endpoint before we start reading from it (mandatory) */
	verbose_reset_buffer(dongle.dev);

	pthread_create(&controller.thread, NULL, controller_thread_fn, (void *) (&controller));
	usleep(100000);
	pthread_create(&output.thread, NULL, output_thread_fn, (void *) (&output));
	pthread_create(&demod.thread, NULL, demod_thread_fn, (void *) (&demod));
	pthread_create(&connection.ConnectionThread, NULL, connection_thread_fn, (void *) (&connection));
	pthread_create(&json_rpc.thread, NULL, JsonRPC_thread_fn, (void *) (&json_rpc));

	while (!do_exit) {
		usleep(100000);
	}

	if (do_exit) {
		fprintf(stderr, "\nUser cancel, exiting...\n");
	} else {
		fprintf(stderr, "\nLibrary error %d, exiting...\n", r);
	}

	rtlsdr_cancel_async(dongle.dev);
	pthread_join(dongle.thread, NULL);
	safe_cond_signal(&demod.ready, &demod.ready_m);
	pthread_join(demod.thread, NULL);
	safe_cond_signal(&output.ready, &output.ready_m);
	pthread_join(output.thread, NULL);
	safe_cond_signal(&controller.hop, &controller.hop_m);
	pthread_join(controller.thread, NULL);

	// Close TCP connection
	close(ConnectionDesc);
	close(connection.SocketDesc);

	demod_cleanup(&demod);
	output_cleanup(&output);
	controller_cleanup(&controller);

	if (output.file != stdout) {
		fclose(output.file);
	}

	rtlsdr_close(dongle.dev);
	return r >= 0 ? r : -r;
}

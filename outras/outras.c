#include <hackrf.h>

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static hackrf_device* rx_device = NULL;

unsigned int SAMPLE_RATE_MHZ = 20;

unsigned int FREQS_MHZ[] = {
  715,
  790,
  1400,
  1515,
  1540,
  2000,
  2025,
  2430
};

unsigned int NUM_FREQS = sizeof(FREQS_MHZ) / sizeof(unsigned int);

// Last HackRf to be plugged in is id=0
int RX_ID = 0;

uint32_t lna_gain = 32;
uint32_t vga_gain = 52;
uint32_t sample_rate_hz = SAMPLE_RATE_MHZ * 1e6;

uint64_t samples_to_rxfer = 1 << 20; // ~ 1e6
uint64_t bytes_to_rxfer = 2 * samples_to_rxfer;

int8_t* rxsamples;
int8_t rxsamples_max;
int8_t rxsamples_min;

int rx_callback(hackrf_transfer* transfer) {
    int bytes_to_read = transfer->valid_length;
    int8_t* tbuf = (int8_t*) transfer->buffer;

    if (bytes_to_read >= bytes_to_rxfer) {
        bytes_to_read = bytes_to_rxfer;
    }

    int offset = 2 * samples_to_rxfer - bytes_to_rxfer;

    for(unsigned int i = 0; i < bytes_to_read; i++) {
        rxsamples[offset + i] = tbuf[i];
        if(tbuf[i] > rxsamples_max) {
            rxsamples_max = tbuf[i];
        }
        if(tbuf[i] < rxsamples_min) {
            rxsamples_min = tbuf[i];
        }
    }

    bytes_to_rxfer -= bytes_to_read;
    return 0;
}

void millisleep(int mls) {
    struct timespec sleep_m;
    sleep_m.tv_sec = mls / 1000;
    sleep_m.tv_nsec = (mls % 1000) * 1000 * 1000;
    nanosleep(&sleep_m, NULL);
    return;
}

void cleanUpExit() {
    free(rxsamples);
    hackrf_exit();
}

int check(int result, const char msg[]) {
    if( result != HACKRF_SUCCESS ) {
        fprintf(stderr, "%s failed: %s (%d)\n",
                msg,
                hackrf_error_name((hackrf_error)result),
                result);
        cleanUpExit();
        return HACKRF_ERROR_OTHER;
    } else {
        return HACKRF_SUCCESS;
    }
}

int initRx(uint32_t freq) {
    return hackrf_set_sample_rate(rx_device, sample_rate_hz) |
        hackrf_set_hw_sync_mode(rx_device, 0) |
        hackrf_set_amp_enable(rx_device, 1) |
        hackrf_set_antenna_enable(rx_device, 0) |
        hackrf_set_lna_gain(rx_device, lna_gain) |
        hackrf_set_vga_gain(rx_device, vga_gain) |
        hackrf_set_freq(rx_device, freq);
}

int main(int argc, char** argv) {
    uint32_t rx_freq_hz = FREQS_MHZ[0] * 1e6;

    char outfilename_root[128];
    char outfilename[128];
    strcpy(outfilename_root, "tmp/out_outras");
    strncpy(outfilename, outfilename_root, 128);

    rxsamples = (int8_t*) calloc(bytes_to_rxfer, sizeof(int8_t));

    /////// init hackrf env
    if(check(hackrf_init(), "hackrf_init")) {
        return EXIT_FAILURE;
    }

    hackrf_device_list_t* device_list = hackrf_device_list();

    // init RX
    if(check(hackrf_device_list_open(device_list, RX_ID, &rx_device), "rx open")) {
        return EXIT_FAILURE;
    }
    if(check(initRx(rx_freq_hz), "init Rx")) {
        return EXIT_FAILURE;
    }

    // start RX
    bytes_to_rxfer = 0;
    rxsamples_max = 0;
    rxsamples_min = 0;

    if(check(hackrf_start_rx(rx_device, rx_callback, NULL), "rx start")) {
        return EXIT_FAILURE;
    }

    ////// start rx once, but transmit many frequencies
    for(uint8_t i = 0; i < NUM_FREQS; i += 1) {
        uint16_t rxf = FREQS_MHZ[i];
        fprintf(stdout, "rxing %d MHz\n", rxf);
        rx_freq_hz = rxf * 1e6;

        // update frequencies
        if(check(hackrf_set_freq(rx_device, rx_freq_hz), "rx set freq")) {
            return EXIT_FAILURE;
        }
        millisleep(100);

        bytes_to_rxfer = 2 * samples_to_rxfer;
        rxsamples_max = 0;
        rxsamples_min = 0;

        while(bytes_to_rxfer > 0) {
            millisleep(10);
        }

        strncpy(outfilename, outfilename_root, 128);

        char intbuf[10];
        snprintf(intbuf, 10, "_%04d.csv", rxf);
        strcat(outfilename, intbuf);

        FILE *outfile = fopen(outfilename, "wb");
        fwrite(rxsamples, sizeof(int8_t), 2 * samples_to_rxfer, outfile);
        fclose(outfile);
    }

    // stop+close RX
    if(check(hackrf_stop_rx(rx_device), "rx stop")) {
        return EXIT_FAILURE;
    }
    if(check(hackrf_close(rx_device), "rx close")) {
        return EXIT_FAILURE;
    }
    millisleep(500);

    cleanUpExit();
    return EXIT_SUCCESS;
}

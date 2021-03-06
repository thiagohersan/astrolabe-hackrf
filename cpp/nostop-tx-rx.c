#include <hackrf.h>

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fftw3.h>

static hackrf_device* tx_device = NULL;
static hackrf_device* rx_device = NULL;

unsigned int MIN_FREQ_MHZ = 10;
unsigned int MAX_FREQ_MHZ = 3000;
unsigned int SAMPLE_RATE_MHZ = 20;

// Last HackRf to be plugged in is id=0
int RX_ID = 0;

uint32_t lna_gain = 32;
uint32_t vga_gain = 52;
uint32_t txvga_gain = 44;
uint32_t sample_rate_hz = SAMPLE_RATE_MHZ * 1e6;

uint64_t samples_to_rxfer = 1 << 20; // ~ 1e6
uint64_t bytes_to_rxfer = 2 * samples_to_rxfer;

int8_t* rxsamples;
int8_t rxsamples_max;
int8_t rxsamples_min;

int tx_callback(hackrf_transfer* transfer) {
    for(unsigned int i = 0; i < transfer->buffer_length; i++)
        transfer->buffer[i] = 0x7f;
    return 0;
}

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

int initTx(uint32_t freq) {
    return hackrf_set_sample_rate(tx_device, sample_rate_hz) |
        hackrf_set_hw_sync_mode(tx_device, 0) |
        hackrf_set_amp_enable(tx_device, 1) |
        hackrf_set_antenna_enable(tx_device, 0) |
        hackrf_set_txvga_gain(tx_device, txvga_gain) |
        hackrf_set_freq(tx_device, freq);
}

int main(int argc, char** argv) {
    uint32_t txrx_freq_hz = MIN_FREQ_MHZ * 1e6;

    char outfilename_root[128];
    char outfilename[128];
    strcpy(outfilename_root, "out/out_");

    fprintf(stdout, "Enter a name/nickname: ");
    scanf("%[^\n]%*c", &outfilename_root[8]);
    strncpy(outfilename, outfilename_root, 128);

    rxsamples = (int8_t*) calloc(bytes_to_rxfer, sizeof(int8_t));

    /////// init hackrf env
    if(check(hackrf_init(), "hackrf_init")) {
        return EXIT_FAILURE;
    }

    hackrf_device_list_t* device_list = hackrf_device_list();

    // init TX
    if(check(hackrf_device_list_open(device_list, (1 - RX_ID), &tx_device), "tx open")) {
        return EXIT_FAILURE;
    }
    if(check(initTx(txrx_freq_hz), "init Tx")) {
        return EXIT_FAILURE;
    }

    // init RX
    if(check(hackrf_device_list_open(device_list, RX_ID, &rx_device), "rx open")) {
        return EXIT_FAILURE;
    }
    if(check(initRx(txrx_freq_hz), "init Rx")) {
        return EXIT_FAILURE;
    }

    // start TX+RX
    bytes_to_rxfer = 0;
    rxsamples_max = 0;
    rxsamples_min = 0;

    if(check(hackrf_start_tx(tx_device, tx_callback, NULL), "tx start")) {
        return EXIT_FAILURE;
    }

    if(check(hackrf_start_rx(rx_device, rx_callback, NULL), "rx start")) {
        return EXIT_FAILURE;
    }

    ////// start and stop tx many times
    for(uint16_t rxf = MIN_FREQ_MHZ; rxf <= MAX_FREQ_MHZ; rxf += SAMPLE_RATE_MHZ) {
        fprintf(stdout, "txing %d MHz\n", rxf);
        txrx_freq_hz = rxf * 1e6;

        // update frequencies
        if(check(hackrf_set_freq(tx_device, txrx_freq_hz), "tx set freq")) {
            return EXIT_FAILURE;
        }
        if(check(hackrf_set_freq(rx_device, txrx_freq_hz), "rx set freq")) {
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

    // stop+close TX+RX
    if(check(hackrf_stop_tx(tx_device), "tx stop")) {
        return EXIT_FAILURE;
    }
    if(check(hackrf_close(tx_device), "tx close")) {
        return EXIT_FAILURE;
    }

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

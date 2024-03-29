// Raspberry Pi MCP3202 ADC streaming interface; see https://iosoft.blog for details
//
// Version to use MCP33131 and PWM1/GPIO13
/*
SPI_FREQ 10000000
sudo ./rpi_adc_stream -n 1000 -r 300000 -s /tmp/adc.fifo
  timing past, alleen af en toe vertraging van  spi_clk. Er is een marge van 1 usec.
     - maar af en toe langer. Probleem?
     - vrij veel overruns bij: cat /dev/adc.fifo >/dev/null
  Dus net voldoende, 150 kHz, vleermuizen gaan tot 120 kHz.

Alternatively via UDP with -U <ip_number>
  sudo ./rpi_adc_stream -n 1000 -r 300000 -u 192.168.178.59

 */

//
// Copyright (c) 2020 Jeremy P Bentham
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// v0.20 JPB 16/11/20 Tidied up for first Github release

#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include "rpi_dma_utils.h"

#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <math.h>

#define VERSION "0.20"

#define SAMPLE_RATE     100     // Default & max sample rate (samples/sec)
#define MAX_SAMPLE_RATE 50000

// PWM definitions: divisor, and reload value
#define PWM_FREQ        4000000
#define PWM_VALUE       2

// ADC sample size (2 bytes)
#define ADC_RAW_LEN     2

// ADC and DAC chip-enables
#define ADC_CE_NUM      0
#define DAC_CE_NUM      1

// Definitions for 2 bytes per ADC sample (all 16-bit)
#define ADC_VOLTAGE(n)  (((n) * 3.3) / 65536.0)
// swap 2 lower bytes
#define ADC_RAW_VAL(d)  (((uint16_t)(d)<<8 | (uint16_t)(d)>>8))

// Non-cached memory size
#define MAX_SAMPS       1024
#define SAMP_SIZE       4
#define BUFF_LEN        (MAX_SAMPS * SAMP_SIZE)
#define MAX_BUFFS       2
#define VC_MEM_SIZE     (PAGE_SIZE + (BUFF_LEN * MAX_BUFFS))

// DMA control block macros
#define NUM_CBS         12
#define GPIO(r)         BUS_GPIO_REG(r)
#define REG(r, a)       REG_BUS_ADDR(r, a)
#define MEM(m, a)       MEM_BUS_ADDR(m, a)
#define CBS(n)          MEM_BUS_ADDR(mp, &dp->cbs[(n)])

// DMA transfer information for PWM and SPI
#define PWM_TI          (DMA_DEST_DREQ | (DMA_PWM_DREQ << 16)    | DMA_WAIT_RESP)
#define SPI_RX_TI       (DMA_SRCE_DREQ | (DMA_SPI_RX_DREQ << 16) | DMA_WAIT_RESP | DMA_CB_DEST_INC)
#define SPI_TX_TI       (DMA_DEST_DREQ | (DMA_SPI_TX_DREQ << 16) | DMA_WAIT_RESP | DMA_CB_SRCE_INC)

// SPI clock frequency
#define MIN_SPI_FREQ    10000
#define MAX_SPI_FREQ    20000000
//#define SPI_FREQ        10000000
#define SPI_FREQ        1000000

// SPI 0 pin definitions
#define SPI0_CE0_PIN    8
#define SPI0_CE1_PIN    7
#define SPI0_MISO_PIN   9
#define SPI0_MOSI_PIN   10
#define SPI0_SCLK_PIN   11

// SPI registers and constants
#define SPI0_BASE       (PHYS_REG_BASE + 0x204000)
#define SPI_CS          0x00
#define SPI_FIFO        0x04
#define SPI_CLK         0x08
#define SPI_DLEN        0x0c
#define SPI_DC          0x14
#define SPI_FIFO_CLR    (3 << 4)
#define SPI_RX_FIFO_CLR (2 << 4)
#define SPI_TX_FIFO_CLR (1 << 4)
#define SPI_TFR_ACT     (1 << 7)
#define SPI_DMA_EN      (1 << 8)
#define SPI_AUTO_CS     (1 << 11)
#define SPI_RXD         (1 << 17)
#define SPI_CE0         0
#define SPI_CE1         1

// SPI register strings
char *spi_regstrs[] = {"CS", "FIFO", "CLK", "DLEN", "LTOH", "DC", ""};

// Microsecond timer
#define USEC_BASE       (PHYS_REG_BASE + 0x3000)
#define USEC_TIME       0x04
uint32_t usec_start;

// Buffer for streaming output, and raw Rx data
#define STREAM_BUFFLEN  10000
char stream_buff[STREAM_BUFFLEN];
uint32_t rx_buff[MAX_SAMPS];

// fcntl constant to get free FIFO length
#define F_GETPIPE_SZ    1032

#define LED_PIN        14

// Virtual memory pointers to access peripherals & memory
extern MEM_MAP gpio_regs, dma_regs, clk_regs, pwm_regs;
MEM_MAP vc_mem, spi_regs, usec_regs;

// File descriptor for FIFO
int fifo_fd;

// Data formats for -f option
#define FMT_USEC        1

// Command-line variables
int in_chans=1, sample_count=0, sample_rate=SAMPLE_RATE;
int data_format, testmode, verbose, lockstep;
uint32_t pwm_range, samp_total, overrun_total, fifo_size;
char *fifo_name;

// UDP
int sockfd=0;
struct sockaddr_in addr_con;
int addrlen = sizeof(addr_con);
// for now
#define PORT_NO 5005
#define IP_ADDRESS "127.0.0.1"
char *ip_addr;

void terminate(int sig);
void map_devices(void);
void get_uncached_mem(MEM_MAP *mp, int size);
int adc_get_sample(int chan);
float test_pwm_frequency(MEM_MAP *mp);
float test_spi_frequency(MEM_MAP *mp);
void adc_dma_init(MEM_MAP *mp, int ns, int single);
void adc_stream_start(void);
void adc_stream_wait(void);
void adc_stream_stop(void);
int adc_stream_csv(MEM_MAP *mp, char *vals, int maxlen, int nsamp);
int adc_stream_raw(MEM_MAP *mp, char *vals, int maxlen, int nsamp, int num);
int adc_stream_packet(MEM_MAP *mp, char *vals, int maxlen, int nsamp, int num);
void dma_wait(int chan);
void do_streaming(MEM_MAP *mp, char *vals, int maxlen, int nsamp);
void do_streaming_udp(MEM_MAP *mp, char *vals, int maxlen, int nsamp, int num);
int create_fifo(char *fname);
int create_udp_channel(char *fname);
int open_fifo_write(char *fname);
int write_fifo(int fd, void *data, int dlen);
uint32_t fifo_freespace(int fd);
void destroy_fifo(char *fname, int fd);
int init_spi(int hz);
void spi_clear(void);
void spi_cnvcs(void);
void spi_xfer(uint8_t *txd, uint8_t *rxd, int len);
void spi_disable(void);
void disp_spi(void);


// Main program
int main(int argc, char *argv[])
{
    int args=0, f, val=0;
    float freq;

    printf("RPi ADC streamer v" VERSION "\n");
    while (argc > ++args)               // Process command-line args
    {
        if (argv[args][0] == '-')
        {
            switch (toupper(argv[args][1]))
            {
            case 'F':                   // -F: output format
                if (args>=argc-1 || !isdigit((int)argv[args+1][0]))
                    fprintf(stderr, "Error: no format value\n");
                else
                    data_format = atoi(argv[++args]);
                break;
            case 'I':                   // -I: number of input channels
                if (args>=argc-1 || !isdigit((int)argv[args+1][0]))
                    fprintf(stderr, "Error: no input chan count\n");
                else
                    in_chans = atoi(argv[++args]);
                break;
            case 'L':                   // -L: lockstep streaming
                lockstep = 1;
                break;
            case 'N':                   // -N: number of samples per block
                if (args>=argc-1 || !isdigit((int)argv[args+1][0]) ||
                    (sample_count = atoi(argv[++args])) < 1)
                    fprintf(stderr, "Error: no sample count\n");
                else if (sample_count > MAX_SAMPS)
                {
                    fprintf(stderr, "Error: maximum sample count %u\n", MAX_SAMPS);
                    sample_count = MAX_SAMPS;
                }
                break;
            case 'R':                   // -R: sample rate (samples/sec)
                if (args>=argc-1 || !isdigit((int)argv[args+1][0]))
                    fprintf(stderr, "Error: no sample rate\n");
                else if (sample_rate > MAX_SAMPLE_RATE)
                    fprintf(stderr, "Error: exceeded max sample rate\n");
                else
                    sample_rate = atoi(argv[++args]);
                break;
            case 'S':                   // -S: stream into named pipe (FIFO)
                if (args>=argc-1 || !argv[args+1][0])
                    fprintf(stderr, "Error: no FIFO name\n");
                else
                    fifo_name = argv[++args];
                break;
            case 'T':                   // -T: test mode
                testmode = 1;
                break;
            case 'U':                   // -U: stream into UDP channel
                if (args>=argc-1 || !argv[args+1][0])
                    fprintf(stderr, "Error: no IP address\n");
                else
                    ip_addr = argv[++args];
                break;
            case 'V':                   // -V: verbose mode (display hex data)
                verbose = 1;
                break;
            default:
                printf("Error: unrecognised option '%s'\n", argv[args]);
                exit(1);
            }
        }
    }
    map_devices();
    map_uncached_mem(&vc_mem, VC_MEM_SIZE);
    signal(SIGINT, terminate);
    pwm_range = (PWM_FREQ * 2) / sample_rate;
    f = init_spi(SPI_FREQ);

    gpio_mode(LED_PIN, GPIO_OUT);

    if (testmode)
    {
        printf("Testing %1.3f MHz SPI frequency: ", f/1e6);
	freq = test_spi_frequency(&vc_mem);
        printf("%7.3f MHz\n", freq);
        printf("Testing %5u Hz  PWM frequency: ", sample_rate);
        freq = test_pwm_frequency(&vc_mem);
        printf("%7.3f Hz\n", freq);
    }
    else if (sample_count == 0)
    {
        printf("SPI frequency %u Hz\n", f);
        val = adc_get_sample(0);
        printf("ADC value %u = %4.3fV\n", val, ADC_VOLTAGE(val));
    }
    else if (fifo_name)
    {
        if (create_fifo(fifo_name))
        {
            printf("Created FIFO '%s'\n", fifo_name);
            printf("Streaming %u samples per block at %u S/s %s\n",
                   sample_count, sample_rate, lockstep ? "(lockstep)" : "");
            adc_dma_init(&vc_mem, sample_count, 0);
            adc_stream_start();
            while (1)
                do_streaming(&vc_mem, stream_buff, STREAM_BUFFLEN, sample_count);
        }
    }
    else if (ip_addr)
    {
        if (create_udp_channel(ip_addr))
        {
            printf("Opened UDP channel to port 5005 on '%s'\n", ip_addr);
            printf("Streaming %u samples per block at %u S/s %s\n",
                   sample_count, sample_rate, lockstep ? "(lockstep)" : "");
            adc_dma_init(&vc_mem, sample_count, 0);
            adc_stream_start();
	    int  num = 0;
            while (1) {
	      do_streaming_udp(&vc_mem, stream_buff, STREAM_BUFFLEN, sample_count, num);
	      num++;
	    }
        }
    }
    else
    {
        printf("Reading %u samples at %u S/s\n", sample_count, sample_rate);
        adc_dma_init(&vc_mem, sample_count, 1);
        adc_stream_start();
        adc_stream_wait();
        adc_stream_stop();
        adc_stream_csv(&vc_mem, stream_buff, STREAM_BUFFLEN, sample_count);
        printf("%s", stream_buff);
    }
    terminate(0);
}

// Catastrophic failure in initial setup
void fail(char *s)
{
    printf(s);
    terminate(0);
}

// Free memory & peripheral mapping and exit
void terminate(int sig)
{
    printf("Closing\n");
    spi_disable();
    stop_dma(DMA_CHAN_A);
    stop_dma(DMA_CHAN_B);
    stop_dma(DMA_CHAN_C);
    unmap_periph_mem(&vc_mem);
    unmap_periph_mem(&usec_regs);
    unmap_periph_mem(&pwm_regs);
    unmap_periph_mem(&clk_regs);
    unmap_periph_mem(&spi_regs);
    unmap_periph_mem(&dma_regs);
    unmap_periph_mem(&gpio_regs);
    if (fifo_name)
        destroy_fifo(fifo_name, fifo_fd);
    if (samp_total)
        printf("Total samples %u, overruns %u\n", samp_total, overrun_total);
    exit(0);
}

// Map GPIO, DMA and SPI registers into virtual mem (user space)
// If any of these fail, program will be terminated
void map_devices(void)
{
    map_periph(&gpio_regs, (void *)GPIO_BASE, PAGE_SIZE);
    map_periph(&dma_regs, (void *)DMA_BASE, PAGE_SIZE);
    map_periph(&spi_regs, (void *)SPI0_BASE, PAGE_SIZE);
    map_periph(&clk_regs, (void *)CLK_BASE, PAGE_SIZE);
    map_periph(&pwm_regs, (void *)PWM_BASE, PAGE_SIZE);
    map_periph(&usec_regs, (void *)USEC_BASE, PAGE_SIZE);
}

// Get uncached memory
 void get_uncached_mem(MEM_MAP *mp, int size)
{
    if (!map_uncached_mem(mp, size))
        fail("Error: can't allocate uncached memory\n");
}

// Fetch single sample from ADC channel 0 or 1
#define BIG 10000
int adc_get_sample(int chan)
{
    uint8_t txdata[ADC_RAW_LEN] = {0, 0}; // dummy value
    uint8_t rxdata[ADC_RAW_LEN];

    int16_t filebuf[BIG];

    FILE *f = fopen("adc.data", "wb");
    printf("Dumping to adc.data\n");

    gpio_set(PWM_PIN, GPIO_OUT, GPIO_NOPULL);
    gpio_out(PWM_PIN, 0);
    uint32_t csval = *REG32(spi_regs, SPI_CS);
    while (1) {
      for (int x=0; x<BIG; x++) {
	spi_cnvcs();

	// set transfer active
	*REG32(spi_regs, SPI_CS) = csval | 0x80 ;
	spi_xfer(txdata, rxdata, ADC_RAW_LEN);
	/*
	  for (int i=0; i<ADC_RAW_LEN; i++)
	      printf("%02X ", rxdata[i]);
          int16_t data =   ((rxdata[0]) << 8) | rxdata[1];
          printf("     %d\n", data);
	*/
	// kan dit niet slimmer?
	int16_t data =   ((rxdata[0]) << 8) | rxdata[1];
	filebuf[x] = data;
	usleep(3);
	// staat het nog active?
      }
      // copy to file
      fwrite(filebuf, sizeof(int16_t), sizeof(filebuf), f);
      break;
    }
    printf("Next portion to file.  %d   %d\n", sizeof(int16_t), sizeof(filebuf));
    fclose(f);
    sync();
    
    if (verbose)
    {
        for (int i=0; i<ADC_RAW_LEN; i++)
            printf("%02X ", rxdata[i]);
        printf("\n");
    }
    return(ADC_RAW_VAL(*(uint16_t *)rxdata));
}

// Definitions for SPI frequency test
#define SPI_TEST_TI  (DMA_DEST_DREQ | (DMA_SPI_TX_DREQ << 16) | DMA_WAIT_RESP  | DMA_CB_SRCE_INC)
#define TEST_NSAMPS  10

typedef struct {
    DMA_CB cbs[NUM_CBS];
    uint32_t txd[TEST_NSAMPS], val;
    volatile uint32_t usecs[2];
} TEST_DMA_DATA;

// Test SPI frequency
float test_spi_frequency(MEM_MAP *mp)
{
    TEST_DMA_DATA *dp=mp->virt;
    TEST_DMA_DATA dma_data = {
        .txd = {0,0,0,0,0,0,0,0,0,0}, .usecs = {0, 0},
        .cbs = {
        // Tx output: 2 initial transfers, then 10 timed transfers
            {SPI_TEST_TI, MEM(mp, dp->txd), REG(spi_regs, SPI_FIFO),           2*4, 0, CBS(1), 0}, // 0
            {SPI_TEST_TI, REG(usec_regs, USEC_TIME), MEM(mp, &dp->usecs[0]),     4, 0, CBS(2), 0}, // 1
            {SPI_TEST_TI, MEM(mp, dp->txd), REG(spi_regs, SPI_FIFO), TEST_NSAMPS*4, 0, CBS(3), 0}, // 2
            {SPI_TEST_TI, REG(usec_regs, USEC_TIME), MEM(mp, &dp->usecs[1]),     4, 0, 0,      0}, // 3
        }
    };
    memcpy(dp, &dma_data, sizeof(dma_data));                // Copy DMA data into uncached memory
    *REG32(spi_regs, SPI_DC) = (8<<24)|(1<<16)|(8<<8)|1;    // Set DMA priorities
    *REG32(spi_regs, SPI_CS) = SPI_FIFO_CLR;                // Clear SPI FIFOs
    start_dma(mp, DMA_CHAN_A, &dp->cbs[0], 0);              // Start SPI Tx DMA
    *REG32(spi_regs, SPI_DLEN) = (TEST_NSAMPS + 2) * 4;     // Set data length, and SPI flags
    *REG32(spi_regs, SPI_CS) = SPI_TFR_ACT | SPI_DMA_EN;
    dma_wait(DMA_CHAN_A);                                   // Wait until complete
    *REG32(spi_regs, SPI_CS) = SPI_FIFO_CLR;                // Clear accumulated Rx data
    return(dp->usecs[1] > dp->usecs[0] ?
           32.0 * TEST_NSAMPS / (dp->usecs[1] - dp->usecs[0]) : 0);
}

// Test PWM frequency
float test_pwm_frequencyXX(MEM_MAP *mp)
{
    init_pwm(PWM_FREQ, pwm_range, PWM_VALUE);               // Initialise PWM    
    start_pwm();                                            // Start PWM
    usleep(50000);
    //    stop_pwm();                                             // Stop PWM
    return(0.123);
}

float test_pwm_frequency(MEM_MAP *mp)
{
    TEST_DMA_DATA *dp=mp->virt;
    TEST_DMA_DATA dma_data = {
        .val = pwm_range, .usecs = {0, 0},
        .cbs = {
        // Tx output: 2 initial transfers, then timed transfer
            {PWM_TI, MEM(mp, &dp->val),         REG(pwm_regs, PWM_FIF1), 4, 0, CBS(1), 0}, // 0
            {PWM_TI, MEM(mp, &dp->val),         REG(pwm_regs, PWM_FIF1), 4, 0, CBS(2), 0}, // 1
            {PWM_TI, REG(usec_regs, USEC_TIME), MEM(mp, &dp->usecs[0]),  4, 0, CBS(3), 0}, // 2
            {PWM_TI, MEM(mp, &dp->val),         REG(pwm_regs, PWM_FIF1), 4, 0, CBS(4), 0}, // 3
            {PWM_TI, REG(usec_regs, USEC_TIME), MEM(mp, &dp->usecs[1]),  4, 0, 0,      0}, // 4
        }
    };
    memcpy(dp, &dma_data, sizeof(dma_data));                // Copy DMA data into uncached memory
    init_pwm(PWM_FREQ, pwm_range, PWM_VALUE);               // Initialise PWM
    *REG32(pwm_regs, PWM_DMAC) = PWM_DMAC_ENAB | PWM_DMAC_TH; // Enable PWM DMA
    start_dma(mp, DMA_CHAN_A, &dp->cbs[0], 0);              // Start DMA
    start_pwm();                                            // Start PWM
    dma_wait(DMA_CHAN_A);                                   // Wait until complete
    stop_pwm();                                             // Stop PWM
    return(dp->usecs[1] > dp->usecs[0] ? 1e6 / (dp->usecs[1] - dp->usecs[0]) : 0);
}

typedef struct {
    DMA_CB cbs[NUM_CBS];
  uint32_t samp_size, pwm_val, adc_csd, txd[2], pindata;
    volatile uint32_t usecs[2], states[2], rxd1[MAX_SAMPS], rxd2[MAX_SAMPS];
} ADC_DMA_DATA;

// Initialise PWM-paced DMA for ADC sampling
//  send data only to get the clock signal!

void adc_dma_init(MEM_MAP *mp, int nsamp, int single)
{
    ADC_DMA_DATA *dp=mp->virt;
    ADC_DMA_DATA dma_data = {
      // -2 creates the CNVCS pulse, 240 nsec
        .pindata = 1<<LED_PIN,
        .samp_size = 2, .pwm_val = pwm_range-PWM_VALUE, .txd={0xd0, in_chans>1 ? 0xf0 : 0xd0},
        .adc_csd = SPI_TFR_ACT | SPI_DMA_EN | ADC_CE_NUM,
	//	.adc_csd = SPI_TFR_ACT | SPI_DMA_EN | SPI_FIFO_CLR | ADC_CE_NUM,
        .usecs = {0, 0}, .states = {0, 0}, .rxd1 = {0}, .rxd2 = {0},
        .cbs = {
        // Rx input: read data from usec clock and SPI, into 2 ping-pong buffers
            {SPI_RX_TI, REG(usec_regs, USEC_TIME), MEM(mp, &dp->usecs[0]),  4, 0, CBS(1), 0}, // 0
            {SPI_RX_TI, REG(spi_regs, SPI_FIFO),   MEM(mp, dp->rxd1), nsamp*4, 0, CBS(2), 0}, // 1
            {SPI_RX_TI, REG(spi_regs, SPI_CS),     MEM(mp, &dp->states[0]), 4, 0, CBS(3), 0}, // 2
            {SPI_RX_TI, REG(usec_regs, USEC_TIME), MEM(mp, &dp->usecs[1]),  4, 0, CBS(4), 0}, // 3
            {SPI_RX_TI, REG(spi_regs, SPI_FIFO),   MEM(mp, dp->rxd2), nsamp*4, 0, CBS(5), 0}, // 4
            {SPI_RX_TI, REG(spi_regs, SPI_CS),     MEM(mp, &dp->states[1]), 4, 0, CBS(0), 0}, // 5
        // Tx output: 2 data writes to SPI for chan 0 & 1, or both chan 0
            {SPI_TX_TI, MEM(mp, dp->txd),          REG(spi_regs, SPI_FIFO), 4, 0, CBS(6), 0}, // 6  dummy data to get a clock
        // PWM ADC trigger: wait for PWM, set sample length, trigger SPI
            {PWM_TI,    MEM(mp, &dp->pwm_val),     REG(pwm_regs, PWM_FIF1), 4, 0, CBS(8), 0}, // 7
            {PWM_TI,    MEM(mp, &dp->samp_size),   REG(spi_regs, SPI_DLEN), 4, 0, CBS(9), 0}, // 8
            {PWM_TI,    MEM(mp, &dp->adc_csd),     REG(spi_regs, SPI_CS),   4, 0, CBS(7), 0}, // 9
	    /*
	    {PWM_TI,    MEM(mp, &dp->pindata),     GPIO(GPIO_CLR0),         4, 0, CBS(8), 0}, // 7
	    {PWM_TI,    MEM(mp, &dp->pindata),     GPIO(GPIO_SET0),         4, 0, CBS(11), 0}, // 11
	    */
        }
    };
    if (single)                                 // If single-shot, stop after first Rx block
        dma_data.cbs[2].next_cb = 0;
    memcpy(dp, &dma_data, sizeof(dma_data));    // Copy DMA data into uncached memory
    init_pwm(PWM_FREQ, pwm_range, PWM_VALUE);   // Initialise PWM, with DMA
    *REG32(pwm_regs, PWM_DMAC) = PWM_DMAC_ENAB | PWM_DMAC_TH;
    *REG32(spi_regs, SPI_DC) = (8<<24) | (1<<16) | (8<<8) | 1;  // Set DMA priorities
    *REG32(spi_regs, SPI_CS) = SPI_FIFO_CLR;                    // Clear SPI FIFOs
    start_dma(mp, DMA_CHAN_C, &dp->cbs[6], 0);  // Start SPI Tx DMA
    start_dma(mp, DMA_CHAN_B, &dp->cbs[0], 0);  // Start SPI Rx DMA
    start_dma(mp, DMA_CHAN_A, &dp->cbs[7], 0);  // Start PWM DMA, for SPI trigger
}

// Manage streaming output
void do_streaming(MEM_MAP *mp, char *vals, int maxlen, int nsamp)
{
    int n;

    if (!fifo_fd)
    {
        if ((fifo_fd = open_fifo_write(fifo_name)) > 0)
        {
            printf("Started streaming to FIFO '%s'\n", fifo_name);
            fifo_size = fifo_freespace(fifo_fd);
        }
    }
    if (fifo_fd)
    {
        if ((n=adc_stream_csv(mp, vals, maxlen, nsamp)) > 0)
        {
            if (!write_fifo(fifo_fd, vals, n))
            {
                printf("Stopped streaming\n");
                close(fifo_fd);
                fifo_fd = 0;
                usleep(100000);
            }
        }
        else
            usleep(10);
    }
}

// Streaming to UDP channel
void do_streaming_udp(MEM_MAP *mp, char *vals, int maxlen, int nsamp, int num) {
  int n, flags=0;

  if ((n=adc_stream_packet(mp, vals, maxlen, nsamp, num)) > 0) {
    //printf("Y, with n=%d\n",n);
    if (!sendto(sockfd, vals, n, flags, (struct sockaddr*)&addr_con, addrlen)) {
      printf("UDP error %s\n", strerror(errno));  
      // close socket?  dit gaat anders !!
      usleep(100);
    }
  }
  else {
    // goeie waarde?
    printf("N");
    usleep(100);
  }
}

// Start ADC data acquisition
void adc_stream_start(void)
{
    start_pwm();
}

// Wait until a (single) DMA cycle is complete
void adc_stream_wait(void)
{
    dma_wait(DMA_CHAN_B);
}

// Stop ADC data acquisition
void adc_stream_stop(void)
{
    stop_pwm();
}

// Fetch samples from ADC buffer, return comma-delimited integer values
// If in lockstep mode, discard new data if FIFO isn't empty
int adc_stream_csv(MEM_MAP *mp, char *vals, int maxlen, int nsamp)
{
    ADC_DMA_DATA *dp=mp->virt;
    uint32_t i, n, usec, slen=0;
    for (n=0; n<2 && slen==0; n++)
    {
        if (dp->states[n])
        {
            samp_total += nsamp;
            memcpy(rx_buff, n ? (void *)dp->rxd2 : (void *)dp->rxd1, nsamp*4);
            usec = dp->usecs[n];
            if (dp->states[n^1])
            {
                dp->states[0] = dp->states[1] = 0;
                overrun_total++;
                break;
            }
            dp->states[n] = 0;
            if (usec_start == 0)
                usec_start = usec;
            if (!lockstep || fifo_freespace(fifo_fd)>=fifo_size)
            {
                if (data_format == FMT_USEC)
                    slen += sprintf(&vals[slen], "%u", usec-usec_start);
                for (i=0; i<nsamp && slen+20<maxlen; i++)
                    slen += sprintf(&vals[slen], "%s%4.3f", slen ? "," : "",
                        ADC_VOLTAGE(ADC_RAW_VAL(rx_buff[i])));
                slen += sprintf(&vals[slen], "\n");
                if (verbose)
                {
                    for (int i=0; i<nsamp*4; i++)
                        printf("%02X ", *(((uint8_t *)rx_buff)+i));
                    printf("\n");
                }
            }
        }
    }
    vals[slen] = 0;
    return(slen);
}

// return raw data.
int adc_stream_raw(MEM_MAP *mp, char *vals, int maxlen, int nsamp, int num)
{
    ADC_DMA_DATA *dp=mp->virt;
    uint32_t i, n, usec, slen=0;
    int16_t v;

    // data available?
    int da = 0;
    while (da==0) {

    for (n=0; n<2 && slen==0; n++)
    {
        if (dp->states[n])
        {
	  da = 1;

            samp_total += nsamp;
            memcpy(rx_buff, n ? (void *)dp->rxd2 : (void *)dp->rxd1, nsamp*4);
            usec = dp->usecs[n];
            if (dp->states[n^1])
            {
                dp->states[0] = dp->states[1] = 0;
                overrun_total++;
                break;
            }
            dp->states[n] = 0;
            if (usec_start == 0)
                usec_start = usec;

	    for (int x=0; x<nsamp; x++) {
	      i = x*2;
	      // test
	      v = x;
	      if (x == 0)
		v = num;
	      // real data
	      v = ADC_RAW_VAL(rx_buff[x]);
	      // dc eruit
	      v -= 250;

	      vals[i]   = v % 256;
	      vals[i+1] = v / 256;
	      
	    }
	    //printf("v = %d\n", v);
	}
    }
    if (da == 0)
      usleep(100);

    }
    return(nsamp*2);
}

int nrpackets = 0;

/* create packet

      (uint16_t): 2**15-1, 2**15, data_size, serial_number, <data_nsamp>, checksum (starting from serial number)
 */
int adc_stream_packet(MEM_MAP *mp, char *vals, int maxlen, int nsamp, int num)
{
    ADC_DMA_DATA *dp=mp->virt;
    uint32_t i, n, usec, slen=0;
    uint16_t v, sum; //, s1, s2;

    // data available?
    int da = 0;
    while (da==0) {

    for (n=0; n<2 && slen==0; n++)
    {
        if (dp->states[n])
        {
	  da = 1;
            samp_total += nsamp;
            memcpy(rx_buff, n ? (void *)dp->rxd2 : (void *)dp->rxd1, nsamp*4);
	    //printf("packet %d   %d", n, nsamp);
            usec = dp->usecs[n];
            if (dp->states[n^1])
            {
                dp->states[0] = dp->states[1] = 0;
                overrun_total++;
		printf("O %d Total %d\n", overrun_total, nrpackets);
		nrpackets = 0;
                break;
            }
            dp->states[n] = 0;
            if (usec_start == 0)
                usec_start = usec;

	    // header (little endian)
	    vals[0] = 0xff, vals[1] = 0x7f;
	    vals[2] = 0x00, vals[3] = 0x80;
	    vals[4] = 2*(nsamp+5)% 256;
	    vals[5] = 2*(nsamp+5) / 256;
	    uint16_t nm = (uint16_t) num;
	    vals[6] = nm % 256;
	    vals[7] = nm / 256;

	    // checksum of header
	    sum = -1 + 2*(nsamp+5) + nm;

	    // data
	    //	    printf("Next:\n");
	    for (int x=0; x<nsamp; x++) {
	      i = 2*x;
	      /*
	      s1 = (uint16_t)rx_buff[x] & 0x0000ffff;
	      s2 = (uint16_t)rx_buff[x] >> 16;
	      printf("%d, %d ", s2, s1);
	      if (x%5 == 0) printf("\n");
	      */
	      v = ADC_RAW_VAL(rx_buff[x]);
	      //	      if (((int16_t)v>1000) || ((int16_t)v<-1000))
	      //		printf("n= %d, x= %d, v= %d\n", nm, x, (int16_t)v);		       
	      // remove dc offset?  PAS OP, gaat fout bij overflow! Do at other side.
	      vals[i+8]   = v % 256;
	      vals[i+8+1] = v / 256;
	      
	      // checksum 2
	      sum += v;
	      //printf("Y %d\n", (int16_t)v);

	    }
	    // store checksum
	    vals[2*nsamp+8] = sum % 256;
	    vals[2*nsamp+9] = sum / 256;
	    /*
	    printf("Checksum: %d\n", sum);
	    for (int x=2*nsamp; x<(2*nsamp+10); x++) {
	      printf("vals[%d]=0x%x, ", x, vals[x]);
	    }
	    printf("\n");
	    */
	    nrpackets += 1;
	}
    }
    if (da == 0)
      usleep(100);

    }
    return(nsamp*2+10);
}

// Wait until DMA is complete
void dma_wait(int chan)
{
    int n = 10000;

    do {
        usleep(10);
    } while (dma_transfer_len(chan) && --n);
    if (n == 0)
        printf("DMA transfer timeout\n");
}

// Create a FIFO (named pipe)
int create_fifo(char *fname)
{
    int ok=0;

    umask(0);
    if (mkfifo(fname, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH) < 0 && errno != EEXIST)
        printf("Can't open FIFO '%s'\n", fname);
    else
        ok = 1;
    return(ok);
}

// Open a FIFO for writing, return 0 if there is no reader
int open_fifo_write(char *fname)
{
    int f = open(fname, O_WRONLY | O_NONBLOCK);

    return(f == -1 ? 0 : f);
}

// Write to FIFO, return 0 if no reader
int write_fifo(int fd, void *data, int dlen)
{
    struct pollfd pollfd = {fd, POLLOUT, 0};

    poll(&pollfd, 1, 0);
    if (pollfd.revents&POLLOUT && !(pollfd.revents&POLLERR))
        return(fd ? write(fd, data, dlen) : 0);
    return(0);
}

// Return the free space in FIFO
uint32_t fifo_freespace(int fd)
{
    return(fcntl(fd, F_GETPIPE_SZ));
}

// Remove the FIFO
void destroy_fifo(char *fname, int fd)
{
    if (fd > 0)
        close(fd);
    unlink(fname);
}

int create_udp_channel(char *fname)
{
  int ok=0;

  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sockfd < 0) {
    printf("ERROR opening socket %s", strerror(errno));
    return(ok);
  }
  ok = 1;

  addr_con.sin_family = AF_INET;
  addr_con.sin_port = htons(PORT_NO);
  addr_con.sin_addr.s_addr = inet_addr(ip_addr);

  printf("Socket created.\n");
  return(ok);
}



// Initialise SPI0, given desired clock freq; return actual value
int init_spi(int hz)
{
    int f, div = (SPI_CLOCK_HZ / hz + 1) & ~1;

    gpio_set(SPI0_CE0_PIN, GPIO_ALT0, GPIO_NOPULL);
    gpio_set(SPI0_CE1_PIN, GPIO_ALT0, GPIO_NOPULL);
    gpio_set(SPI0_MISO_PIN, GPIO_ALT0, GPIO_PULLUP);
    gpio_set(SPI0_MOSI_PIN, GPIO_ALT0, GPIO_NOPULL);
    gpio_set(SPI0_SCLK_PIN, GPIO_ALT0, GPIO_NOPULL);
    while (div==0 || (f = SPI_CLOCK_HZ/div) > MAX_SPI_FREQ)
        div += 2;
    *REG32(spi_regs, SPI_CS) = 0x30;
    *REG32(spi_regs, SPI_CLK) = div;
    printf("SPI div: %i\n", div);
    return(f);
}

// Clear SPI FIFOs
void spi_clear(void)
{
    *REG32(spi_regs, SPI_CS) = SPI_FIFO_CLR;
}

// Create CNVCS pulse  (no pwm yet)
void spi_cnvcs(void)
{
  gpio_out(PWM_PIN, 1);
  usleep(1);
  gpio_out(PWM_PIN, 0);
}

// Transfer SPI bytes
void spi_xfer(uint8_t *txd, uint8_t *rxd, int len)
{
    while (len--)
    {
        *REG8(spi_regs, SPI_FIFO) = *txd++;
        while((*REG32(spi_regs, SPI_CS) & (1<<17)) == 0) ;
        *rxd++ = *REG32(spi_regs, SPI_FIFO);
    }
}

// Disable SPI
void spi_disable(void)
{
    *REG32(spi_regs, SPI_CS) = SPI_FIFO_CLR;
    *REG32(spi_regs, SPI_CS) = 0;
}

// Display SPI registers
void disp_spi(void)
{
    volatile uint32_t *p=REG32(spi_regs, SPI_CS);
    int i=0;

    while (spi_regstrs[i][0])
        printf("%-4s %08X ", spi_regstrs[i++], *p++);
    printf("\n");
}

// EOF


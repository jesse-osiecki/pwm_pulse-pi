#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>


#define CLK_BASE  0x20101000
#define GPIO_BASE 0x20200000
#define PWM_BASE  0x2020C000

#define CLK_LEN   0xA8
#define GPIO_LEN  0xB4
#define PWM_LEN   0x28

#define GPSET0     7
#define GPSET1     8

#define GPCLR0    10
#define GPCLR1    11

#define GPLEV0    13
#define GPLEV1    14

#define PWM_CTL      0
#define PWM_STA      1
#define PWM_DMAC     2
#define PWM_RNG1     4
#define PWM_DAT1     5
#define PWM_FIFO     6
#define PWM_RNG2     8
#define PWM_DAT2     9

#define PWM_CTL_CLRF1 (1<<6)
#define PWM_CTL_USEF1 (1<<5)
#define PWM_CTL_MODE1 (1<<1)
#define PWM_CTL_PWEN1 (1<<0)

#define PWM_DMAC_ENAB      (1 <<31)
#define PWM_DMAC_PANIC(x) ((x)<< 8)
#define PWM_DMAC_DREQ(x)   (x)

#define CLK_PASSWD  (0x5A<<24)

#define CLK_CTL_MASH(x)((x)<<9)
#define CLK_CTL_BUSY    (1 <<7)
#define CLK_CTL_KILL    (1 <<5)
#define CLK_CTL_ENAB    (1 <<4)
#define CLK_CTL_SRC(x) ((x)<<0)

#define CLK_CTL_SRC_OSC  1  /*  19.2 MHz */
#define CLK_CTL_SRC_PLLD 6  /* 500.0 MHz */

#define CLK_DIV_DIVI(x) ((x)<<12)
#define CLK_DIV_DIVF(x) ((x)<< 0)

#define CLK_PCMCTL 38
#define CLK_PCMDIV 39

#define CLK_PWMCTL 40
#define CLK_PWMDIV 41

static volatile uint32_t  *clkReg  = MAP_FAILED;
static volatile uint32_t  *gpioReg = MAP_FAILED;
static volatile uint32_t  *pwmReg  = MAP_FAILED;

int gpioSetMode(unsigned gpio, unsigned mode)
{
   int reg, shift;

   reg   =  gpio/10;
   shift = (gpio%10) * 3;

   gpioReg[reg] = (gpioReg[reg] & ~(7<<shift)) | (mode<<shift);

   return 0;
}

int gpioGetMode(unsigned gpio)
{
   int reg, shift;

   reg   =  gpio/10;
   shift = (gpio%10) * 3;

   return (*(gpioReg + reg) >> shift) & 7;
}

uint32_t gpioRead_Bits_0_31(void) { return (*(gpioReg + GPLEV0)); }

uint32_t gpioRead_Bits_32_53(void) { return (*(gpioReg + GPLEV1)); }

int gpioRead(unsigned gpio)
{
   unsigned bank, bit;

   bank = gpio >> 5;

   bit = (1<<(gpio&0x1F));

   if ((*(gpioReg + GPLEV0 + bank) & bit) != 0) return 1;
   else                                         return 0;
}


int gpioWrite(unsigned gpio, unsigned level)
{
   unsigned bank, bit;

   bank = gpio >> 5;

   bit = (1<<(gpio&0x1F));

   if (level == 0) *(gpioReg + GPCLR0 + bank) = bit;
   else            *(gpioReg + GPSET0 + bank) = bit;

   return 0;
}

static void initClock(unsigned divider)
{
   clkReg[CLK_PWMCTL] = CLK_PASSWD | CLK_CTL_KILL;

   usleep(10);

   clkReg[CLK_PWMDIV] = CLK_PASSWD | CLK_DIV_DIVI(divider) | CLK_DIV_DIVF(0);

   usleep(10);

   clkReg[CLK_PWMCTL] = CLK_PASSWD | CLK_CTL_MASH(0) |
                        CLK_CTL_SRC(CLK_CTL_SRC_PLLD);

   usleep(10);

   clkReg[CLK_PWMCTL] |= (CLK_PASSWD | CLK_CTL_ENAB);

   usleep(2000);
}

static void initPWM(unsigned bits)
{
   int i, words, word;

   /* reset PWM */

   pwmReg[PWM_CTL] = 0;

   usleep(10);

   pwmReg[PWM_STA] = -1;

   usleep(10);

   /* set number of bits to transmit */

   pwmReg[PWM_RNG1] = 32;

   usleep(20);

   /* clear PWM fifo */

   pwmReg[PWM_CTL] = PWM_CTL_CLRF1;

   usleep(20);

   if (bits == 0) bits = 1;
   if (bits > 256) bits = 256;

   words = 8;

   while (bits >= 32)
   {
      pwmReg[PWM_FIFO] = 0xFFFFFFFF;
      usleep(10);
      //printf("FFFFFFFF\n");
      --words;
      bits -= 32;
   }

   if (bits)
   {
      word = 0;

      for (i=0; i<bits; i++) word |= (1<<(31-i));

      pwmReg[PWM_FIFO] = word;
      usleep(10);
      //printf("%08X\n", word);
      --words;
   }

   while (words > 0)
   {
      pwmReg[PWM_FIFO] = 0x00000000;
      usleep(10);
      //printf("00000000\n");
      --words;
   }

   //printf("\n");

   usleep(10);

   /* enable PWM channel 1 and use fifo */

   pwmReg[PWM_CTL] = PWM_CTL_USEF1 | PWM_CTL_MODE1 | PWM_CTL_PWEN1;
}


static uint32_t * mapMem(int fd, unsigned base, unsigned len)
{
   return mmap
   (
      0,
      len,
      PROT_READ|PROT_WRITE|PROT_EXEC,
      MAP_SHARED|MAP_LOCKED,
      fd,
      base
   );
}

int main(int argc, char *argv[])
{
   int fd, i, gpio;

   int div=500, bits=32, pulses=100;

   if (argc > 1) div    = atoi(argv[1]);
   if (argc > 2) bits   = atoi(argv[2]);
   if (argc > 3) pulses = atoi(argv[3]);

   fd = open("/dev/mem", O_RDWR | O_SYNC);

   if (fd<0)
   {
      printf("need to run as root, e.g. sudo %s\n", argv[0]);
      exit(1);
   }

   gpioReg = mapMem(fd, GPIO_BASE, GPIO_LEN);
   pwmReg  = mapMem(fd, PWM_BASE,  PWM_LEN);
   clkReg  = mapMem(fd, CLK_BASE,  CLK_LEN);

   close(fd);
   gpioSetMode(18, 2);

   initClock(div);

   for (i=0; i< pulses; i++)
   {
      initPWM(bits);
      usleep(50);
   }
}

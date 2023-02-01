#ifndef ALSASOUND_H
#define ALSASOUND_H

#include <alsa/asoundlib.h>
#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <stdarg.h>
#include "UZ7HOStuff.h"
#include "ossaudio.h"

#ifdef __ARM_ARCH
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

#define DEBUGLOG 0
#define EXCEPTLOG 1
#define SESSIONLOG 2

#define BOOL int
#define TRUE 1
#define FALSE 0

#ifdef __ARM_ARCH
#define GPSET0 7
#define GPSET1 8

#define GPCLR0 10
#define GPCLR1 11

#define GPLEV0 13
#define GPLEV1 14

#define GPPUD     37
#define GPPUDCLK0 38
#define GPPUDCLK1 39

#define PI_BANK (gpio>>5)
#define PI_BIT  (1<<(gpio&0x1F))

#define PI_PUD_OFF  0
#define PI_PUD_DOWN 1
#define PI_PUD_UP   2
#endif

// from QtSoundModem
extern "C" void WriteDebugLog(char * Mess);
extern int Closing;

// from SMMain.c
extern "C" void ProcessNewSamples(unsigned short * Samples, int nSamples);

// export SoundInit as a C function
// extern "C" short * SoundInit();


// from Modulate.c

extern unsigned short * DMABuffer;

// from SMMain.c

extern int Number;

struct speed_struct
{
    int	user_speed;
    speed_t termios_speed;
};



class ALSASound
{
public:
    ALSASound();
    void printtick(char * msg);
    void Sleep(int mS);
    void PlatformSleep(int mS);
    void platformInit();
    void GetSoundDevices();
    unsigned short * SendtoCard(unsigned short * buf, int n);
    int InitSound(BOOL Quiet);
    void PollReceivedSamples();
    void CloseSound();
    unsigned short * SoundInit();
    void SoundFlush();
    int stricmp(const unsigned char * pStr1, const unsigned char *pStr2);
private:
#ifdef NOTDEF
    extern BOOL blnDISCRepeating;
#endif
    OSSAudio audio;
    int SoundMode;
    int stdinMode;
    BOOL UseLeft;
    BOOL UseRight;
    char LogDir[256];
    unsigned short buffer[2][1200 * 2];			// Two Transfer/DMA buffers of 0.1 Sec
    unsigned short inbuffer[1200 * 2];		// Two Transfer/DMA buffers of 0.1 Sec

    BOOL Loopback;
    //BOOL Loopback;

    char CaptureDevice[80];
    char PlaybackDevice[80];

    char * CaptureDevices;
    char * PlaybackDevices;

    int CaptureIndex;
    int PlayBackIndex;

    int Ticks;

    int LastNow;

    snd_pcm_sframes_t MaxAvail;

    FILE *logfile[3];
    char LogName[3][256];
    FILE *statslogfile;
    char * PortString;

    snd_pcm_t *	playhandle;
    snd_pcm_t *	rechandle;

    int m_playchannels;
    int m_recchannels;


    char SavedCaptureDevice[256];	// Saved so we can reopen
    char SavedPlaybackDevice[256];

    int Savedplaychannels;

    int SavedCaptureRate;
    int SavedPlaybackRate;

    char CaptureNames[16][256];
    char PlaybackNames[16][256];

    int PlaybackCount;
    int CaptureCount;
    int PriorSize;

    int Index;				// DMA Buffer being used 0 or 1
    int inIndex;				// DMA Buffer being used 0 or 1

    BOOL DMARunning;		// Used to start DMA on first write
    char Leds[8];
    unsigned int PKTLEDTimer;
    struct timespec time_start;
    short loopbuff[1200];
    int min, max;
    int lastlevelreport, lastlevelGUI;
    UCHAR CurrentLevel = 0;
    const struct speed_struct
    {
        int	user_speed;
        speed_t termios_speed;
    } speed_table[12] = {
        {300,         B300},
        {600,         B600},
        {1200,        B1200},
        {2400,        B2400},
        {4800,        B4800},
        {9600,        B9600},
        {19200,       B19200},
        {38400,       B38400},
        {57600,       B57600},
        {115200,      B115200},
        {-1,          B0}
    };
#ifdef __ARM_ARCH
    unsigned piModel;
    unsigned piRev;

    static volatile uint32_t  *gpioReg;
#endif
    unsigned int getTicks();
    static void sigterm_handler(int n);
    static void sigint_handler(int n);
    void txSleep(int mS);
    int CheckifLoaded();
    int GetOutputDeviceCollection();
    int GetInputDeviceCollection();
    int OpenSoundPlayback(char * PlaybackDevice, int m_sampleRate, int channels, int Report);
    int OpenSoundCapture(char * CaptureDevice, int m_sampleRate, int Report);
    int OpenSoundCard(char * CaptureDevice, char * PlaybackDevice, int c_sampleRate, int p_sampleRate, int Report);
    int CloseSoundCard();
    int SoundCardWrite(short * input, int nSamples);
    int PackSamplesAndSend(short * input, int nSamples);
    int SoundCardRead(short * input, int nSamples);
    void StopCapture();
    void StartCapture();
#ifdef TXSILENCE
    void SendSilence();
#endif
#ifdef __ARM_ARCH
    void gpioSetMode(unsigned gpio, unsigned mode);
    int gpioGetMode(unsigned gpio);
    void gpioSetPullUpDown(unsigned gpio, unsigned pud);
    int gpioRead(unsigned gpio);
    void gpioWrite(unsigned gpio, unsigned level);
    void gpioTrigger(unsigned gpio, unsigned pulseLen, unsigned level);
    uint32_t gpioReadBank1(void);
    uint32_t gpioReadBank2(void);
    void gpioClearBank1(uint32_t bits);
    void gpioClearBank2(uint32_t bits);
    void gpioSetBank1(uint32_t bits);
    void gpioSetBank2(uint32_t bits);
    unsigned gpioHardwareRevision(void);
    int gpioInitialise(void);
#endif
};

#endif // ALSASOUND_H

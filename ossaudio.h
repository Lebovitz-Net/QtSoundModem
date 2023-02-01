#ifndef OSSAUDIO_H
#define OSSAUDIO_H
//#include <fcntl.h>

// Originally 40.  Version 1.2, try 10 for lower latency.

#define ONE_BUF_TIME 10
#define roundup1k(n) (((n) + 0x3ff) & ~0x3ff)

class OSSAudio
{
public:
    OSSAudio();
    int open(char * adevice_in, char * adevice_out);
    int setParams(int fd);
    int read(short * samples, int nSamples);
    int write(short * ptr, int len);
    void flush();
    void close(void);
private:
    int oss_fd;	/* Single device, both directions. */
    int insize;
    short rxbuffer[2400];		// 1200 stereo samples
    int num_channels;		/* Should be 1 for mono or 2 for stereo. */
    int samples_per_sec;	/* Audio sampling rate.  Typically 11025, 22050, or 44100. */
    int bits_per_sample;	/* 8 (unsigned char) or 16 (signed short). */
    int calcbufsize(int rate, int chans, int bits);
};

#endif // OSSAUDIO_H

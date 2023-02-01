#ifndef SOUNDMODEMFFT_H
#define SOUNDMODEMFFT_H
#include "fftw3.h"
#include "UZ7HOStuff.h"

class SoundModemFFT
{
public:
    SoundModemFFT();
    void initfft();
    void dofft(short * inp, float * outr, float * outi);
    void freefft();
private:
    int fft_size = 2048;
    float fft_window_arr[2048];
    float  fft_s[2048], fft_d[2048];
    short fft_buf[5][2048];
    UCHAR fft_disp[5][2048];
    int fft_mult = 0;
    int fft_spd = 3;
    fftwf_complex *in, *out;
    fftwf_plan p;

    #define N 2048
};

#endif // SOUNDMODEMFFT_H

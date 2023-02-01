#include "soundmodemfft.h"
#include "fftw3.h"
#include "UZ7HOStuff.h"

SoundModemFFT::SoundModemFFT()
{
    UCHAR foo;
    UNUSED(foo);

}
void SoundModemFFT::initfft()
{
    in = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * N);
    out = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * N);
    p = fftwf_plan_dft_1d(N, in, out, FFTW_FORWARD, FFTW_ESTIMATE);
}

void SoundModemFFT::dofft(short * inp, float * outr, float * outi)
{
    int i;

    fftwf_complex * fft = in;

    for (i = 0; i < N; i++)
    {
        fft[0][0] = inp[0] * 1.0f;
        fft[0][1] = 0;
        fft++;
        inp++;
    }

    fftwf_execute(p);

    fft = out;

    for (i = 0; i < N; i++)
    {
        outr[0] = fft[0][0];
        outi[0] = fft[0][1];
        fft++;
        outi++;
        outr++;
    }
}

void SoundModemFFT::freefft()
{
    fftwf_destroy_plan(p);
    fftwf_free(in);
    fftwf_free(out);
}

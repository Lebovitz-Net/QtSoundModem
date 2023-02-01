#include "alsasound.h"
#include "ossaudio.h"
#include "UZ7HOStuff.h"

// from audio.c
extern "C" {
int listpulse();
int pulse_read(short * ptr, int len);
int pulse_write(short * ptr, int len);
int pulse_flush();
int pulse_audio_open(char * adevice_in, char * adevice_out);
void pulse_audio_close();

// from SMMain.c

void ProcessNewSamples(unsigned short * Samples, int nSamples);
}

ALSASound::ALSASound() :
    audio(),
    SoundMode(0),
    stdinMode (0),
    UseLeft (TRUE),
    UseRight (TRUE),
    LogDir(""),
    Loopback (FALSE),
    //Loopback (TRUE),

    CaptureDevice("plughw:0,0"),
    PlaybackDevice("plughw:0,0"),
    CaptureDevices (CaptureDevice),
    PlaybackDevices (CaptureDevice),
    CaptureIndex (0),
    PlayBackIndex (0),

    Ticks (0),
    LastNow (0),
    MaxAvail(0),

    logfile{NULL,NULL,NULL},
    LogName{"ARDOPDebug", "ARDOPException", "ARDOPSession"},
    statslogfile (NULL),
    PortString (NULL),
    playhandle (NULL),
    rechandle (NULL),

    m_playchannels (2),
    m_recchannels (2),

    Savedplaychannels (2),
    SavedCaptureRate (0),
    SavedPlaybackRate (0),

    CaptureNames{ "" },
    PlaybackNames{ "" },

    PlaybackCount (0),
    CaptureCount (0),
    PriorSize (0),

    Index (0),				// DMA Buffer being used 0 or 1
    inIndex (0),				// DMA Buffer being used 0 or 1

    DMARunning (FALSE),		// Used to start DMA on first write
    Leds{0},
    PKTLEDTimer(0),
    min(0), max(0),
    lastlevelreport(0), lastlevelGUI(0),
#ifdef __ARM_ARCH
    gpioReg(MAP_FAILED)
#endif
    CurrentLevel(0)		// Peak from current samples
{

};

void ALSASound::Sleep(int mS)
{
    usleep(mS * 1000);
    return;
}

void ALSASound::printtick(char * msg)
{
    Debugprintf("%s %i", msg, Now - LastNow);
    LastNow = Now;
}

unsigned int ALSASound::getTicks()
{
    struct timespec tp;

    clock_gettime(CLOCK_MONOTONIC, &tp);
    return (tp.tv_sec - time_start.tv_sec) * 1000 + (tp.tv_nsec - time_start.tv_nsec) / 1000000;
}

void ALSASound::PlatformSleep(int mS)
{
    Sleep(mS);
}

void ALSASound::sigterm_handler(int n)
{
    UNUSED(n);

    printf("terminating on SIGTERM\n");
    Closing = TRUE;
}

void ALSASound::sigint_handler(int n)
{
    UNUSED(n);

    printf("terminating on SIGINT\n");
    Closing = TRUE;
}

void ALSASound::platformInit()
{
    struct sigaction act;

//	Sleep(1000);	// Give LinBPQ time to complete init if exec'ed by linbpq

    // Get Time Reference

    clock_gettime(CLOCK_MONOTONIC, &time_start);
    LastNow = getTicks();

    // Trap signals

    memset (&act, '\0', sizeof(act));

    act.sa_handler = &sigint_handler;
    if (sigaction(SIGINT, &act, NULL) < 0)
        perror ("SIGINT");

    act.sa_handler = &sigterm_handler;
    if (sigaction(SIGTERM, &act, NULL) < 0)
        perror ("SIGTERM");

    act.sa_handler = SIG_IGN;

    if (sigaction(SIGHUP, &act, NULL) < 0)
        perror ("SIGHUP");

    if (sigaction(SIGPIPE, &act, NULL) < 0)
        perror ("SIGPIPE");
}

void ALSASound::txSleep(int mS)
{
    // called while waiting for next TX buffer or to delay response.
    // Run background processes

    // called while waiting for next TX buffer. Run background processes

    while (mS > 50)
    {
        PollReceivedSamples();			// discard any received samples

        Sleep(50);
        mS -= 50;
    }

    Sleep(mS);

    PollReceivedSamples();			// discard any received samples
}

int ALSASound::CheckifLoaded()
{
    // Prevent CTRL/C from closing the TNC
    // (This causes problems if the TNC is started by LinBPQ)

    signal(SIGHUP, SIG_IGN);
    signal(SIGINT, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);

    return TRUE;
}

int ALSASound::GetOutputDeviceCollection()
{
    // Get all the suitable devices and put in a list for GetNext to return

    snd_ctl_t *handle= NULL;
    snd_pcm_t *pcm= NULL;
    snd_ctl_card_info_t *info;
    snd_pcm_info_t *pcminfo;
    snd_pcm_hw_params_t *pars;
    snd_pcm_format_mask_t *fmask;
    char NameString[256];

    Debugprintf("Playback Devices\n");

    CloseSoundCard();

    // free old struct if called again

//	while (PlaybackCount)
//	{
//		PlaybackCount--;
//		free(PlaybackNames[PlaybackCount]);
//	}

//	if (PlaybackNames)
//		free(PlaybackNames);

    PlaybackCount = 0;

    //	Get Device List  from ALSA

    snd_ctl_card_info_alloca(&info);
    snd_pcm_info_alloca(&pcminfo);
    snd_pcm_hw_params_alloca(&pars);
    snd_pcm_format_mask_alloca(&fmask);

    char hwdev[80];
    unsigned min, max, ratemin, ratemax;
    int card, err, dev, nsubd;
    snd_pcm_stream_t stream = SND_PCM_STREAM_PLAYBACK;

    card = -1;

    if (snd_card_next(&card) < 0)
    {
        Debugprintf("No Devices");
        return 0;
    }

    if (playhandle)
        snd_pcm_close(playhandle);

    playhandle = NULL;

    while (card >= 0)
    {
        sprintf(hwdev, "hw:%d", card);
        err = snd_ctl_open(&handle, hwdev, 0);
        err = snd_ctl_card_info(handle, info);

        Debugprintf("Card %d, ID `%s', name `%s'", card, snd_ctl_card_info_get_id(info),
                snd_ctl_card_info_get_name(info));


        dev = -1;

        if(snd_ctl_pcm_next_device(handle, &dev) < 0)
        {
            // Card has no devices

            snd_ctl_close(handle);
            goto nextcard;
        }

        while (dev >= 0)
        {
            snd_pcm_info_set_device(pcminfo, dev);
            snd_pcm_info_set_subdevice(pcminfo, 0);
            snd_pcm_info_set_stream(pcminfo, stream);

            err = snd_ctl_pcm_info(handle, pcminfo);


            if (err == -ENOENT)
                goto nextdevice;

            nsubd = snd_pcm_info_get_subdevices_count(pcminfo);

            Debugprintf("  Device hw:%d,%d ID `%s', name `%s', %d subdevices (%d available)",
                card, dev, snd_pcm_info_get_id(pcminfo), snd_pcm_info_get_name(pcminfo),
                nsubd, snd_pcm_info_get_subdevices_avail(pcminfo));

            sprintf(hwdev, "hw:%d,%d", card, dev);

            err = snd_pcm_open(&pcm, hwdev, stream, SND_PCM_NONBLOCK);

            if (err)
            {
                Debugprintf("Error %d opening output device", err);
                goto nextdevice;
            }

            //	Get parameters for this device

            err = snd_pcm_hw_params_any(pcm, pars);

            snd_pcm_hw_params_get_channels_min(pars, &min);
            snd_pcm_hw_params_get_channels_max(pars, &max);

            snd_pcm_hw_params_get_rate_min(pars, &ratemin, NULL);
            snd_pcm_hw_params_get_rate_max(pars, &ratemax, NULL);

            if( min == max )
                if( min == 1 )
                    Debugprintf("    1 channel,  sampling rate %u..%u Hz", ratemin, ratemax);
                else
                    Debugprintf("    %d channels,  sampling rate %u..%u Hz", min, ratemin, ratemax);
            else
                Debugprintf("    %u..%u channels, sampling rate %u..%u Hz", min, max, ratemin, ratemax);

            // Add device to list

            sprintf(NameString, "hw:%d,%d %s(%s)", card, dev,
                snd_pcm_info_get_name(pcminfo), snd_ctl_card_info_get_name(info));

            strcpy(PlaybackNames[PlaybackCount++], NameString);

            snd_pcm_close(pcm);
            pcm= NULL;

nextdevice:
            if (snd_ctl_pcm_next_device(handle, &dev) < 0)
                break;
        }
        snd_ctl_close(handle);

nextcard:

        Debugprintf("");

        if (snd_card_next(&card) < 0)		// No more cards
            break;
    }

    return PlaybackCount;
}


int ALSASound::GetInputDeviceCollection()
{
    // Get all the suitable devices and put in a list for GetNext to return

    snd_ctl_t *handle= NULL;
    snd_pcm_t *pcm= NULL;
    snd_ctl_card_info_t *info;
    snd_pcm_info_t *pcminfo;
    snd_pcm_hw_params_t *pars;
    snd_pcm_format_mask_t *fmask;
    char NameString[256];

    Debugprintf("Capture Devices\n");

    CaptureCount = 0;

    //	Get Device List  from ALSA

    snd_ctl_card_info_alloca(&info);
    snd_pcm_info_alloca(&pcminfo);
    snd_pcm_hw_params_alloca(&pars);
    snd_pcm_format_mask_alloca(&fmask);

    char hwdev[80];
    unsigned min, max, ratemin, ratemax;
    int card, err, dev, nsubd;
    snd_pcm_stream_t stream = SND_PCM_STREAM_CAPTURE;

    card = -1;

    if(snd_card_next(&card) < 0)
    {
        Debugprintf("No Devices");
        return 0;
    }

    if (rechandle)
        snd_pcm_close(rechandle);

    rechandle = NULL;

    while(card >= 0)
    {
        sprintf(hwdev, "hw:%d", card);
        err = snd_ctl_open(&handle, hwdev, 0);
        err = snd_ctl_card_info(handle, info);

        Debugprintf("Card %d, ID `%s', name `%s'", card, snd_ctl_card_info_get_id(info),
                snd_ctl_card_info_get_name(info));

        dev = -1;

        if (snd_ctl_pcm_next_device(handle, &dev) < 0)		// No Devicdes
        {
            snd_ctl_close(handle);
            goto nextcard;
        }

        while(dev >= 0)
        {
            snd_pcm_info_set_device(pcminfo, dev);
            snd_pcm_info_set_subdevice(pcminfo, 0);
            snd_pcm_info_set_stream(pcminfo, stream);
            err= snd_ctl_pcm_info(handle, pcminfo);

            if (err == -ENOENT)
                goto nextdevice;

            nsubd= snd_pcm_info_get_subdevices_count(pcminfo);
            Debugprintf("  Device hw:%d,%d ID `%s', name `%s', %d subdevices (%d available)",
                card, dev, snd_pcm_info_get_id(pcminfo), snd_pcm_info_get_name(pcminfo),
                nsubd, snd_pcm_info_get_subdevices_avail(pcminfo));

            sprintf(hwdev, "hw:%d,%d", card, dev);

            err = snd_pcm_open(&pcm, hwdev, stream, SND_PCM_NONBLOCK);

            if (err)
            {
                Debugprintf("Error %d opening input device", err);
                goto nextdevice;
            }

            err = snd_pcm_hw_params_any(pcm, pars);

            snd_pcm_hw_params_get_channels_min(pars, &min);
            snd_pcm_hw_params_get_channels_max(pars, &max);
            snd_pcm_hw_params_get_rate_min(pars, &ratemin, NULL);
            snd_pcm_hw_params_get_rate_max(pars, &ratemax, NULL);

            if( min == max )
                if( min == 1 )
                    Debugprintf("    1 channel,  sampling rate %u..%u Hz", ratemin, ratemax);
                else
                    Debugprintf("    %d channels,  sampling rate %u..%u Hz", min, ratemin, ratemax);
            else
                Debugprintf("    %u..%u channels, sampling rate %u..%u Hz", min, max, ratemin, ratemax);

            sprintf(NameString, "hw:%d,%d %s(%s)", card, dev,
                snd_pcm_info_get_name(pcminfo), snd_ctl_card_info_get_name(info));

//			Debugprintf("%s", NameString);

            strcpy(CaptureNames[CaptureCount++], NameString);

            snd_pcm_close(pcm);
            pcm= NULL;

nextdevice:
            if (snd_ctl_pcm_next_device(handle, &dev) < 0)
                break;
        }
        snd_ctl_close(handle);
nextcard:

        Debugprintf("");
        if (snd_card_next(&card) < 0 )
            break;
    }

    strcpy(CaptureNames[CaptureCount++], "stdin");

    return CaptureCount;
}

int ALSASound::OpenSoundPlayback(char * PlaybackDevice, int m_sampleRate, int channels, int Report)
{
    int err = 0;

    char buf1[100];
    char * ptr;

    if (playhandle)
    {
        snd_pcm_close(playhandle);
        playhandle = NULL;
    }

    strcpy(SavedPlaybackDevice, PlaybackDevice);	// Saved so we can reopen in error recovery
    SavedPlaybackRate = m_sampleRate;

    sprintf(buf1, "plug%s", PlaybackDevice);

    if (Report)
        Debugprintf("Real Device %s", buf1);


    ptr = strchr(buf1, ' ');
    if (ptr) *ptr = 0;				// Get Device part of name

    snd_pcm_hw_params_t *hw_params;

    if ((err = snd_pcm_open(&playhandle, buf1, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK)) < 0)
    {
        Debugprintf("cannot open playback audio device %s (%s)",  buf1, snd_strerror(err));
        return false;
    }

    if ((err = snd_pcm_hw_params_malloc (&hw_params)) < 0)
    {
        Debugprintf("cannot allocate hardware parameter structure (%s)", snd_strerror(err));
        return false;
    }

    if ((err = snd_pcm_hw_params_any (playhandle, hw_params)) < 0) {
        Debugprintf("cannot initialize hardware parameter structure (%s)", snd_strerror(err));
        return false;
    }

    if ((err = snd_pcm_hw_params_set_access (playhandle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
        Debugprintf("cannot set playback access type (%s)", snd_strerror (err));
        return false;
    }
    if ((err = snd_pcm_hw_params_set_format (playhandle, hw_params, SND_PCM_FORMAT_S16_LE)) < 0) {
        Debugprintf("cannot setplayback  sample format (%s)", snd_strerror(err));
        return false;
    }

    if ((err = snd_pcm_hw_params_set_rate (playhandle, hw_params, m_sampleRate, 0)) < 0) {
        Debugprintf("cannot set playback sample rate (%s)", snd_strerror(err));
        return false;
    }

    // Initial call has channels set to 1. Subequent ones set to what worked last time

    channels = 2;

    if ((err = snd_pcm_hw_params_set_channels (playhandle, hw_params, channels)) < 0)
    {
        Debugprintf("cannot set play channel count to %d (%s)", channels, snd_strerror(err));

        if (channels == 2)
            return false;				// Shouldn't happen as should have worked before

        channels = 2;

        if ((err = snd_pcm_hw_params_set_channels (playhandle, hw_params, 2)) < 0)
        {
            Debugprintf("cannot play set channel count to 2 (%s)", snd_strerror(err));
            return false;
        }
    }

    if (Report)
        Debugprintf("Play using %d channels", channels);

    if ((err = snd_pcm_hw_params (playhandle, hw_params)) < 0)
    {
        Debugprintf("cannot set parameters (%s)", snd_strerror(err));
        return false;
    }

    snd_pcm_hw_params_free(hw_params);

    if ((err = snd_pcm_prepare (playhandle)) < 0)
    {
        Debugprintf("cannot prepare audio interface for use (%s)", snd_strerror(err));
        return false;
    }

    Savedplaychannels = m_playchannels = channels;

    MaxAvail = snd_pcm_avail_update(playhandle);

    if (Report)
        Debugprintf("Playback Buffer Size %d", (int)MaxAvail);

    return true;
}

int ALSASound::OpenSoundCapture(char * CaptureDevice, int m_sampleRate, int Report)
{
    int err = 0;

    char buf1[100];
    char * ptr;
    snd_pcm_hw_params_t *hw_params;

    if (strcmp(CaptureDevice, "stdin") == 0)
    {
        stdinMode = 1;

        Debugprintf("Input from stdin");
        return TRUE;
    }

    if (rechandle)
    {
        snd_pcm_close(rechandle);
        rechandle = NULL;
    }

    strcpy(SavedCaptureDevice, CaptureDevice);	// Saved so we can reopen in error recovery
    SavedCaptureRate = m_sampleRate;

    sprintf(buf1, "plug%s", CaptureDevice);

    if (Report)
        Debugprintf("Real Device %s", buf1);

    ptr = strchr(buf1, ' ');
    if (ptr) *ptr = 0;				// Get Device part of name

    if ((err = snd_pcm_open (&rechandle, buf1, SND_PCM_STREAM_CAPTURE, 0)) < 0) {
        Debugprintf("cannot open capture audio device %s (%s)",  buf1, snd_strerror(err));
        return false;
    }

    if ((err = snd_pcm_hw_params_malloc (&hw_params)) < 0) {
        Debugprintf("cannot allocate capture hardware parameter structure (%s)", snd_strerror(err));
        return false;
    }

    if ((err = snd_pcm_hw_params_any (rechandle, hw_params)) < 0) {
        Debugprintf("cannot initialize capture hardware parameter structure (%s)", snd_strerror(err));
        return false;
    }

    if ((err = snd_pcm_hw_params_set_access (rechandle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
        Debugprintf("cannot set capture access type (%s)", snd_strerror (err));
        return false;
    }
    if ((err = snd_pcm_hw_params_set_format (rechandle, hw_params, SND_PCM_FORMAT_S16_LE)) < 0) {
        Debugprintf("cannot set capture sample format (%s)", snd_strerror(err));
        return false;
    }

    if ((err = snd_pcm_hw_params_set_rate (rechandle, hw_params, m_sampleRate, 0)) < 0) {
        Debugprintf("cannot set capture sample rate (%s)", snd_strerror(err));
        return false;
    }

    m_recchannels = 2;

    if ((err = snd_pcm_hw_params_set_channels(rechandle, hw_params, m_recchannels)) < 0)
    {
        Debugprintf("cannot set rec channel count to 2 (%s)", snd_strerror(err));

        m_recchannels = 1;

        if ((err = snd_pcm_hw_params_set_channels(rechandle, hw_params, 1)) < 0)
        {
            Debugprintf("cannot set rec channel count to 1 (%s)", snd_strerror(err));
            return false;
        }
        if (Report)
            Debugprintf("Record channel count set to 1");
    }
    else
        if (Report)
            Debugprintf("Record channel count set to 2");

    /*
    {
    unsigned int val = 0;
    unsigned int dir = 0, frames = 0;


    snd_pcm_hw_params_get_channels(rechandle, &val);
    printf("channels = %d\n", val);

    snd_pcm_hw_params_get_rate(rechandle, &val, &dir);
    printf("rate = %d bps\n", val);

    snd_pcm_hw_params_get_period_time(rechandle, &val, &dir);
    printf("period time = %d us\n", val);

    snd_pcm_hw_params_get_period_size(rechandle, &frames, &dir);
    printf("period size = %d frames\n", (int)frames);

    snd_pcm_hw_params_get_buffer_time(rechandle, &val, &dir);
    printf("buffer time = %d us\n", val);

    snd_pcm_hw_params_get_buffer_size(rechandle, (snd_pcm_uframes_t *)&val);
    printf("buffer size = %d frames\n", val);

    snd_pcm_hw_params_get_periods(rechandle, &val, &dir);
    printf("periods per buffer = %d frames\n", val);
    }
    */

    if ((err = snd_pcm_hw_params (rechandle, hw_params)) < 0)
    {
        // Try setting some more params Have to reinit params

        snd_pcm_hw_params_any(rechandle, hw_params);
        snd_pcm_hw_params_set_access(rechandle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
        snd_pcm_hw_params_set_format(rechandle, hw_params, SND_PCM_FORMAT_S16_LE);
        snd_pcm_hw_params_set_rate(rechandle, hw_params, m_sampleRate, 0);
        snd_pcm_hw_params_set_channels(rechandle, hw_params, m_recchannels);

        err = snd_pcm_hw_params_set_buffer_size(rechandle, hw_params, 65536);

        if (err)
            Debugprintf("cannot set buffer size (%s)", snd_strerror(err));

        err = snd_pcm_hw_params_set_period_size(rechandle, hw_params, (snd_pcm_uframes_t) { 1024 }, (int) { 0 });

        if (err)
            Debugprintf("cannot set period size (%s)", snd_strerror(err));

        if ((err = snd_pcm_hw_params(rechandle, hw_params)) < 0)
        {
            Debugprintf("cannot set parameters (%s)", snd_strerror(err));
            return false;
        }
    }

    snd_pcm_hw_params_free(hw_params);

    if ((err = snd_pcm_prepare (rechandle)) < 0) {
        Debugprintf("cannot prepare audio interface for use (%s)", snd_strerror(err));
        return FALSE;
    }

    if (Report)
        Debugprintf("Capture using %d channels", m_recchannels);

    int i;
    short buf[256];

    for (i = 0; i < 10; ++i)
    {
        if ((err = snd_pcm_readi (rechandle, buf, 128)) != 128)
        {
            Debugprintf("read from audio interface failed (%s)", snd_strerror (err));
        }
    }

//	Debugprintf("Read got %d", err);

    return TRUE;
}

int ALSASound::OpenSoundCard(char * CaptureDevice, char * PlaybackDevice, int c_sampleRate, int p_sampleRate, int Report)
{
    int Channels = 1;

    if (Report)
        Debugprintf("Opening Playback Device %s Rate %d", PlaybackDevice, p_sampleRate);

//	if (UseLeft == 0 || UseRight == 0)
    Channels = 2;						// L or R implies stereo

    if (OpenSoundPlayback(PlaybackDevice, p_sampleRate, Channels, Report))
    {
#ifdef SHARECAPTURE

        // Close playback device so it can be shared

        if (playhandle)
        {
            snd_pcm_close(playhandle);
            playhandle = NULL;
        }
#endif
        if (Report)
            Debugprintf("Opening Capture Device %s Rate %d", CaptureDevice, c_sampleRate);
        return OpenSoundCapture(CaptureDevice, c_sampleRate, Report);
    }
    else
        return false;
}



int ALSASound::CloseSoundCard()
{
    if (rechandle)
    {
        snd_pcm_close(rechandle);
        rechandle = NULL;
    }

    if (playhandle)
    {
        snd_pcm_close(playhandle);
        playhandle = NULL;
    }
    return 0;
}


int ALSASound::SoundCardWrite(short * input, int nSamples)
{
    unsigned int ret;
    snd_pcm_sframes_t avail; // , maxavail;

    if (playhandle == NULL)
        return 0;

    //	Stop Capture

    if (rechandle)
    {
        snd_pcm_close(rechandle);
        rechandle = NULL;
    }

    avail = snd_pcm_avail_update(playhandle);
//	Debugprintf("avail before play returned %d", (int)avail);

    if (avail < 0)
    {
        if (avail != -32)
            Debugprintf("Playback Avail Recovering from %d ..", (int)avail);
        snd_pcm_recover(playhandle, avail, 1);

        avail = snd_pcm_avail_update(playhandle);

        if (avail < 0)
            Debugprintf("avail play after recovery returned %d", (int)avail);
    }

//	maxavail = avail;

//	Debugprintf("Tosend %d Avail %d", nSamples, (int)avail);

    while (avail < nSamples || (MaxAvail - avail) > 12000)				// Limit to 1 sec of audio
    {
        txSleep(10);
        avail = snd_pcm_avail_update(playhandle);
//		Debugprintf("After Sleep Tosend %d Avail %d", nSamples, (int)avail);
    }

    ret = PackSamplesAndSend(input, nSamples);

    return ret;
}

int ALSASound::PackSamplesAndSend(short * input, int nSamples)
{
    unsigned short samples[256000];
    int ret;

    ret = snd_pcm_writei(playhandle, input, nSamples);

    if (ret < 0)
    {
//		Debugprintf("Write Recovering from %d ..", ret);
        snd_pcm_recover(playhandle, ret, 1);
        ret = snd_pcm_writei(playhandle, samples, nSamples);
//		Debugprintf("Write after recovery returned %d", ret);
    }

    snd_pcm_avail_update(playhandle);
    return ret;

}
/*
int xSoundCardClearInput()
{
    short samples[65536];
    int n;
    int ret;
    int avail;

    if (rechandle == NULL)
        return 0;

    // Clear queue

    avail = snd_pcm_avail_update(rechandle);

    if (avail < 0)
    {
        Debugprintf("Discard Recovering from %d ..", avail);
        if (rechandle)
        {
            snd_pcm_close(rechandle);
            rechandle = NULL;
        }
        OpenSoundCapture(SavedCaptureDevice, SavedCaptureRate, NULL);
        avail = snd_pcm_avail_update(rechandle);
    }

    while (avail)
    {
        if (avail > 65536)
            avail = 65536;

            ret = snd_pcm_readi(rechandle, samples, avail);
//			Debugprintf("Discarded %d samples from card", ret);
            avail = snd_pcm_avail_update(rechandle);

//			Debugprintf("Discarding %d samples from card", avail);
    }
    return 0;
}
*/

int ALSASound::SoundCardRead(short * input, int nSamples)
{
    short samples[65536];
    int n;
    int ret;
    int avail;

    if (SoundMode == 1)		// OSS
    {
        ret = audio.read(samples, nSamples);
    }
    else if (SoundMode == 2)// Pulse
    {
        ret = pulse_read(samples, nSamples);
    }
    else
    {
        if (rechandle == NULL)
            return 0;

        avail = snd_pcm_avail_update(rechandle);

        if (avail < 0)
        {
            Debugprintf("avail Recovering from %d ..", avail);
            if (rechandle)
            {
                snd_pcm_close(rechandle);
                rechandle = NULL;
            }

            OpenSoundCapture(SavedCaptureDevice, SavedCaptureRate, 0);
            //		snd_pcm_recover(rechandle, avail, 0);
            avail = snd_pcm_avail_update(rechandle);
            Debugprintf("After avail recovery %d ..", avail);
        }

        if (avail < nSamples)
            return 0;

        //	Debugprintf("ALSARead available %d", avail);

        ret = snd_pcm_readi(rechandle, samples, nSamples);

        if (ret < 0)
        {
            Debugprintf("RX Error %d", ret);
            //		snd_pcm_recover(rechandle, avail, 0);
            if (rechandle)
            {
                snd_pcm_close(rechandle);
                rechandle = NULL;
            }

            OpenSoundCapture(SavedCaptureDevice, SavedCaptureRate, 0);
            //		snd_pcm_recover(rechandle, avail, 0);
            avail = snd_pcm_avail_update(rechandle);
            Debugprintf("After Read recovery Avail %d ..", avail);

            return 0;
        }
    }


    if (ret < nSamples)
        return 0;

    if (m_recchannels == 1)
    {
        for (n = 0; n < ret; n++)
        {
            *(input++) = samples[n];
            *(input++) = samples[n];		// Duplicate
        }
    }
    else
    {
        for (n = 0; n < ret * 2; n++)		// return all
        {
            *(input++) = samples[n];
        }
    }

    return ret;
}

unsigned short * ALSASound::SendtoCard(unsigned short * buf, int n)
{
    if (Loopback)
    {
        // Loop back   to decode for testing

        ProcessNewSamples(buf, 1200);		// signed
    }

    if (SoundMode == 1)			// OSS
        audio.write((short *)buf, n);
    else if (SoundMode == 2)	// Pulse
        pulse_write((short *)buf, n);
    else
    {
        if (playhandle)
            SoundCardWrite((short *)buf, n);

        //	txSleep(10);				// Run buckground while waiting
    }

    Index = !Index;
    return &buffer[Index][0];
}

void ALSASound::GetSoundDevices()
{
    if (SoundMode == 0)
    {
        GetInputDeviceCollection();
        GetOutputDeviceCollection();
    }
    else if (SoundMode == 1)
    {
        PlaybackCount = 3;

        strcpy(&PlaybackNames[0][0], "/dev/dsp0");
        strcpy(&PlaybackNames[1][0], "/dev/dsp1");
        strcpy(&PlaybackNames[2][0], "/dev/dsp2");

        CaptureCount = 3;

        strcpy(&CaptureNames[0][0], "/dev/dsp0");
        strcpy(&CaptureNames[1][0], "/dev/dsp1");
        strcpy(&CaptureNames[2][0], "/dev/dsp2");
    }
    else if (SoundMode == 2)
    {
        // Pulse

        listpulse();
    }
}

int ALSASound::InitSound(BOOL Quiet)
{
    GetSoundDevices();

    switch (SoundMode)
    {
    case 0:				// ALSA

        if (!OpenSoundCard(CaptureDevice, PlaybackDevice, 12000, 12000, Quiet))
            return FALSE;

        break;

    case 1:				// OSS

        if (!audio.open(CaptureDevice, PlaybackDevice))
            return FALSE;

        break;

    case 2:				// PulseAudio

        if (!pulse_audio_open(CaptureDevice, PlaybackDevice))
            return FALSE;

        break;

    }

    printf("InitSound %s %s\n", CaptureDevice, PlaybackDevice);

    DMABuffer = SoundInit();
    return TRUE;
}

void ALSASound::PollReceivedSamples()
{
    // Process any captured samples
    // Ideally call at least every 100 mS, more than 200 will loose data

    int bytes;
#ifdef TXSILENCE
    SendSilence();			// send silence (attempt to fix CM delay issue)
#endif

    if (stdinMode)
    {
        // will block if no input. May get less, in which case wait a bit then try to read rest

        // rtl_udp outputs mono samples

        short input[1200];
        short * ptr1, *ptr2;
        int n = 20; // Max Wait

        bytes = read(STDIN_FILENO, input, ReceiveSize * 2);		// 4 = Stereo 2 bytes per sample

        while (bytes < ReceiveSize * 2 && n--)
        {
            Sleep(50);	//mS
            bytes += read(STDIN_FILENO, &input[bytes / 2], (ReceiveSize * 2) - bytes);
        }

        // if still not enough, too bad!

        if (bytes != ReceiveSize * 2)
            Debugprintf("Short Read %d", bytes);

        // convert to stereo

        ptr1 = input;
        ptr2 = (short *)inbuffer;
        n = ReceiveSize;

        while (n--)
        {
            *ptr2++ = *ptr1;
            *ptr2++ = *ptr1++;
        }
    }
    else
        bytes = SoundCardRead((short *)inbuffer, ReceiveSize);	 // returns ReceiveSize or none

    if (bytes > 0)
    {
        short * ptr = (short *)inbuffer;
        int i;

        for (i = 0; i < ReceiveSize; i++)
        {
            if (*(ptr) < min)
                min = *ptr;
            else if (*(ptr) > max)
                max = *ptr;
            ptr++;
        }


        CurrentLevel = ((max - min) * 75) /32768;	// Scale to 150 max

        if ((Now - lastlevelGUI) > 2000)	// 2 Secs
        {
                lastlevelGUI = Now;

            if ((Now - lastlevelreport) > 10000)	// 10 Secs
            {
                char HostCmd[64];
                lastlevelreport = Now;

                sprintf(HostCmd, "INPUTPEAKS %d %d", min, max);

                Debugprintf("Input peaks = %d, %d", min, max);
            }
            min = max = 0;							// Every 2 secs
        }

        ProcessNewSamples(inbuffer, ReceiveSize);
    }
}

void ALSASound::StopCapture()
{
    Capturing = FALSE;

#ifdef SHARECAPTURE

    // Stopcapture is only called when we are about to transmit, so use it to open plaback device. We don't keep
    // it open all the time to facilitate sharing.

    OpenSoundPlayback(SavedPlaybackDevice, SavedPlaybackRate, Savedplaychannels, NULL);
#endif
}

void ALSASound::StartCapture()
{
    Capturing = TRUE;

//	Debugprintf("Start Capture");
}

void ALSASound::CloseSound()
{
    switch (SoundMode)
    {
    case 0:				// ALSA

        CloseSoundCard();
        return;

    case 1:				// OSS

        audio.close();
        return;

    case 2:				// PulseAudio

        pulse_audio_close();
        return;
    }
}

unsigned short * ALSASound::SoundInit()
{
    Index = 0;
    return &buffer[0][0];
}

//	Called at end of transmission

void ALSASound::SoundFlush()
{
    // Append Trailer then send remaining samples

    snd_pcm_status_t *status = NULL;
    int err, res;
    int lastavail = 0;

    if (Loopback)
        ProcessNewSamples(&buffer[Index][0], Number);

    SendtoCard(&buffer[Index][0], Number);

    // Wait for tx to complete

    Debugprintf("Flush Soundmode = %d", SoundMode);

    if (SoundMode == 0)		// ALSA
    {
        usleep(100000);

        while (1 && playhandle)
        {
            snd_pcm_sframes_t avail = snd_pcm_avail_update(playhandle);

            //		Debugprintf("Waiting for complete. Avail %d Max %d", avail, MaxAvail);

            snd_pcm_status_alloca(&status);					// alloca allocates once per function, does not need a free

//			Debugprintf("Waiting for complete. Avail %d Max %d last %d", avail, MaxAvail, lastavail);

            if ((err = snd_pcm_status(playhandle, status)) != 0)
            {
                Debugprintf("snd_pcm_status() failed: %s", snd_strerror(err));
                break;
            }

            res = snd_pcm_status_get_state(status);

            //		Debugprintf("PCM Status = %d", res);

            if (res != SND_PCM_STATE_RUNNING || lastavail == avail)			// If sound system is not running then it needs data
//			if (res != SND_PCM_STATE_RUNNING)				// If sound system is not running then it needs data
    //		if (MaxAvail - avail < 100)
            {
                // Send complete - Restart Capture

                OpenSoundCapture(SavedCaptureDevice, SavedCaptureRate, 0);
                break;
            }
            lastavail = avail;
            usleep(50000);
        }
        // I think we should turn round the link here. I dont see the point in
        // waiting for MainPoll

#ifdef SHARECAPTURE
        if (playhandle)
        {
            snd_pcm_close(playhandle);
            playhandle = NULL;
        }
#endif
    }
    else if (SoundMode == 1)
    {
        audio.flush();
    }
    else if (SoundMode == 2)
    {
        pulse_flush();
    }

    SoundIsPlaying = FALSE;

    Number = 0;

    memset(buffer, 0, sizeof(buffer));
    DMABuffer = &buffer[0][0];

#ifdef TXSILENCE
    SendtoCard(&buffer[0][0], 1200);			// Start sending silence (attempt to fix CM delay issue)
#endif

    StartCapture();
    return;
}

#ifdef TXSILENCE

// send silence (attempt to fix CM delay issue)


void ALSASound::SendSilence()
{
    short buffer[2400];

    snd_pcm_sframes_t Avail = snd_pcm_avail_update(playhandle);

    if ((MaxAvail - Avail) < 1200)
    {
        // Keep at least 100 ms of audio in buffer

//		printtick("Silence");

        memset(buffer, 0, sizeof(buffer));
        SendtoCard(buffer, 1200);			// Start sending silence (attempt to fix CM delay issue)
    }
}

#endif

int ALSASound::stricmp(const unsigned char * pStr1, const unsigned char *pStr2)
{
    unsigned char c1, c2;
    int  v;

    if (pStr1 == NULL)
    {
        if (pStr2)
            Debugprintf("stricmp called with NULL 1st param - 2nd %s ", pStr2);
        else
            Debugprintf("stricmp called with two NULL params");

        return 1;
    }


    do {
        c1 = *pStr1++;
        c2 = *pStr2++;
        /* The casts are necessary when pStr1 is shorter & char is signed */
        v = tolower(c1) - tolower(c2);
    } while ((v == 0) && (c1 != '\0') && (c2 != '\0') );

    return v;
}

// move this to its own module
// GPIO access stuff for PTT on PI

#ifdef __ARM_ARCH

/*
   tiny_gpio.c
   2016-04-30
   Public Domain
*/

/* gpio modes. */

void ALSASound::gpioSetMode(unsigned gpio, unsigned mode)
{
   int reg, shift;

   reg   =  gpio/10;
   shift = (gpio%10) * 3;

   gpioReg[reg] = (gpioReg[reg] & ~(7<<shift)) | (mode<<shift);
}

int ALSASound::gpioGetMode(unsigned gpio)
{
   int reg, shift;

   reg   =  gpio/10;
   shift = (gpio%10) * 3;

   return (*(gpioReg + reg) >> shift) & 7;
}

/* Values for pull-ups/downs off, pull-down and pull-up. */

void ALSASound::gpioSetPullUpDown(unsigned gpio, unsigned pud)
{
   *(gpioReg + GPPUD) = pud;

   usleep(20);

   *(gpioReg + GPPUDCLK0 + PI_BANK) = PI_BIT;

   usleep(20);

   *(gpioReg + GPPUD) = 0;

   *(gpioReg + GPPUDCLK0 + PI_BANK) = 0;
}

int ALSASound::gpioRead(unsigned gpio)
{
   if ((*(gpioReg + GPLEV0 + PI_BANK) & PI_BIT) != 0) return 1;
   else                                         return 0;
}
void ALSASound::gpioWrite(unsigned gpio, unsigned level)
{
   if (level == 0)
       *(gpioReg + GPCLR0 + PI_BANK) = PI_BIT;
   else
       *(gpioReg + GPSET0 + PI_BANK) = PI_BIT;
}

void ALSASound::gpioTrigger(unsigned gpio, unsigned pulseLen, unsigned level)
{
   if (level == 0) *(gpioReg + GPCLR0 + PI_BANK) = PI_BIT;
   else            *(gpioReg + GPSET0 + PI_BANK) = PI_BIT;

   usleep(pulseLen);

   if (level != 0) *(gpioReg + GPCLR0 + PI_BANK) = PI_BIT;
   else            *(gpioReg + GPSET0 + PI_BANK) = PI_BIT;
}

/* Bit (1<<x) will be set if gpio x is high. */

uint32_t ALSASound::gpioReadBank1(void) { return (*(gpioReg + GPLEV0)); }
uint32_t ALSASound::gpioReadBank2(void) { return (*(gpioReg + GPLEV1)); }

/* To clear gpio x bit or in (1<<x). */

void ALSASound::gpioClearBank1(uint32_t bits) { *(gpioReg + GPCLR0) = bits; }
void ALSASound::gpioClearBank2(uint32_t bits) { *(gpioReg + GPCLR1) = bits; }

/* To set gpio x bit or in (1<<x). */

void ALSASound::gpioSetBank1(uint32_t bits) { *(gpioReg + GPSET0) = bits; }
void ALSASound::gpioSetBank2(uint32_t bits) { *(gpioReg + GPSET1) = bits; }

unsigned ALSASound::gpioHardwareRevision(void)
{
   static unsigned rev = 0;

   FILE * filp;
   char buf[512];
   char term;
   int chars=4; /* number of chars in revision string */

   if (rev) return rev;

   piModel = 0;

   filp = fopen ("/proc/cpuinfo", "r");

   if (filp != NULL)
   {
      while (fgets(buf, sizeof(buf), filp) != NULL)
      {
         if (piModel == 0)
         {
            if (!strncasecmp("model name", buf, 10))
            {
               if (strstr (buf, "ARMv6") != NULL)
               {
                  piModel = 1;
                  chars = 4;
               }
               else if (strstr (buf, "ARMv7") != NULL)
               {
                  piModel = 2;
                  chars = 6;
               }
               else if (strstr (buf, "ARMv8") != NULL)
               {
                  piModel = 2;
                  chars = 6;
               }
            }
         }

         if (!strncasecmp("revision", buf, 8))
         {
            if (sscanf(buf+strlen(buf)-(chars+1),
               "%x%c", &rev, &term) == 2)
            {
               if (term != '\n') rev = 0;
            }
         }
      }

      fclose(filp);
   }
   return rev;
}

int ALSASound::gpioInitialise(void)
{
   int fd;

   piRev = gpioHardwareRevision(); /* sets piModel and piRev */

   fd = open("/dev/gpiomem", O_RDWR | O_SYNC) ;

   if (fd < 0)
   {
      fprintf(stderr, "failed to open /dev/gpiomem\n");
      return -1;
   }

   gpioReg = (uint32_t *)mmap(NULL, 0xB4, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);

   close(fd);

   if (gpioReg == MAP_FAILED)
   {
      fprintf(stderr, "Bad, mmap failed\n");
      return -1;
   }
   return 0;
}

#endif

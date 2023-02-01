#include "soundmodemcom.h"
#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <stdarg.h>
#include <fcntl.h>
#include <errno.h>
#include "UZ7HOStuff.h"

const struct speed_struct
{
    int	user_speed;
    speed_t termios_speed;
} speed_table[] = {
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

SoundModemCOM::SoundModemCOM() :
    Leds{0},
    PKTLEDTimer(0)
{

}

VOID SoundModemCOM::COMSetDTR(HANDLE fd)
{
    int status;

    ioctl(fd, TIOCMGET, &status);
    status |= TIOCM_DTR;
    ioctl(fd, TIOCMSET, &status);
}

VOID SoundModemCOM::COMClearDTR(HANDLE fd)
{
    int status;

    ioctl(fd, TIOCMGET, &status);
    status &= ~TIOCM_DTR;
    ioctl(fd, TIOCMSET, &status);
}

VOID SoundModemCOM::COMSetRTS(HANDLE fd)
{
    int status;

    if (ioctl(fd, TIOCMGET, &status) == -1)
        perror("COMSetRTS PTT TIOCMGET");
    status |= TIOCM_RTS;
    if (ioctl(fd, TIOCMSET, &status) == -1)
        perror("COMSetRTS PTT TIOCMSET");
}

VOID SoundModemCOM::COMClearRTS(HANDLE fd)
{
    int status;

    if (ioctl(fd, TIOCMGET, &status) == -1)
        perror("COMClearRTS PTT TIOCMGET");
    status &= ~TIOCM_RTS;
    if (ioctl(fd, TIOCMSET, &status) == -1)
        perror("COMClearRTS PTT TIOCMSET");

}

HANDLE SoundModemCOM::OpenCOMPort(char * Port, int speed, BOOL SetDTR, BOOL SetRTS, BOOL Quiet, int Stopbits)
{
    char buf[101]; // this is sizeof(fulldev) + sizeof("  could not be opened")

    //	Linux Version.

    int fd;
    u_long param = 1;
    struct termios term;
    struct speed_struct *s;

    char fulldev[80];

    UNUSED(Stopbits);

    sprintf(fulldev, "/dev/%s", Port);

    printf("%s\n", fulldev);

    if ((fd = open(fulldev, O_RDWR | O_NDELAY)) == -1)
    {
            if (Quiet == 0)
            {
                    perror("Com Open Failed");
                    sprintf(buf, " %s could not be opened", (char *)fulldev);
                    Debugprintf(buf);
            }
            return 0;
    }


    // Validate Speed Param
    for (s = (struct speed_struct *)speed_table; s->user_speed != -1; s++)
    {
        if (s->user_speed == speed)
            break;
    }

    if (s->user_speed == -1)
    {
        fprintf(stderr, "tty_speed: invalid speed %d", speed);
        return FALSE;
    }

    if (tcgetattr(fd, &term) == -1)
    {
        perror("tty_speed: tcgetattr");
        return FALSE;
    }

    cfmakeraw(&term);
    cfsetispeed(&term, s->termios_speed);
    cfsetospeed(&term, s->termios_speed);

    if (tcsetattr(fd, TCSANOW, &term) == -1)
    {
        perror("tty_speed: tcsetattr");
        return FALSE;
    }

    ioctl(fd, FIONBIO, &param);

    Debugprintf("Port %s fd %d", fulldev, fd);

    if (SetDTR)
        COMSetDTR(fd);
    else
        COMClearDTR(fd);

    if (SetRTS)
        COMSetRTS(fd);
    else
        COMClearRTS(fd);

    return fd;
}

BOOL SoundModemCOM::WriteCOMBlock(HANDLE fd, char * Block, int BytesToWrite)
{
    //	Some systems seem to have a very small max write size

    int ToSend = BytesToWrite;
    int Sent = 0, ret;

    while (ToSend)
    {
        ret = write(fd, &Block[Sent], ToSend);

        if (ret >= ToSend)
            return TRUE;

//		perror("WriteCOM");

        if (ret == -1)
        {
            if (errno != 11 && errno != 35)					// Would Block
                return FALSE;

            usleep(10000);
            ret = 0;
        }

        Sent += ret;
        ToSend -= ret;
    }
    return TRUE;
}

VOID SoundModemCOM::CloseCOMPort(HANDLE fd)
{
    close(fd);
}


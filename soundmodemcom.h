#ifndef SOUNDMODEMCOM_H
#define SOUNDMODEMCOM_H
#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <stdarg.h>
#include <fcntl.h>
#include <errno.h>

#define BOOL int
#define VOID void
#define HANDLE int

class SoundModemCOM
{
public:
    SoundModemCOM();
    VOID COMSetDTR(HANDLE fd);
    VOID COMClearDTR(HANDLE fd);
    VOID COMSetRTS(HANDLE fd);
    VOID COMClearRTS(HANDLE fd);
    HANDLE OpenCOMPort(char * Port, int speed, BOOL SetDTR, BOOL SetRTS, BOOL Quiet, int Stopbits);
    BOOL WriteCOMBlock(HANDLE fd, char * Block, int BytesToWrite);
    VOID CloseCOMPort(HANDLE fd);

private:
    char Leds[8]= {0};
    unsigned int PKTLEDTimer = 0;
};

#endif // SOUNDMODEMCOM_H

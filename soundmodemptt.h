#ifndef SOUNDMODEMPTT_H
#define SOUNDMODEMPTT_H
#include "UZ7HOStuff.h"

#include "soundmodemcom.h"

class SoundModemPTT
{
public:
    SoundModemPTT();
    void DecodeCM108(char * ptr);
    char * strlop(char * buf, char delim);
    void OpenPTTPort();
    void ClosePTTPort();
    void CM108_set_ptt(int PTTState);
    void RadioPTT(int snd_ch, BOOL PTTState);
    char * ShortDateTime();
 #ifdef NOTDEF
    int RSEncode(UCHAR * bytToRS, UCHAR * RSBytes, int DataLen, int RSLen);
    BOOL RSDecode(UCHAR * bytRcv, int Length, int CheckLen, BOOL * blnRSOK);
    void ProcessPktFrame(int snd_ch, UCHAR * Data, int frameLen);
#endif
private:
    int hPTTDevice;
    char PTTPort[80];			// Port for Hardware PTT - may be same as control port.
    int PTTBAUD;
    int PTTMode;			// PTT Control Flags.
    char PTTOnString[128];
    char PTTOffString[128];
    UCHAR PTTOnCmd[64];
    UCHAR PTTOnCmdLen;
    UCHAR PTTOffCmd[64];
    UCHAR PTTOffCmdLen;
    int pttGPIOPin;			// Default
    int pttGPIOPinR;
    BOOL pttGPIOInvert;
    BOOL useGPIO;
    BOOL gotGPIO;
    int HamLibPort;
    char HamLibHost[32];
    char CM108Addr[80];
    int VID;
    int PID;
    char * CM108Device;
    char ShortDT[9];
    SoundModemCOM smCom;
};

#endif // SOUNDMODEMPTT_H



#include "soundmodemptt.h"
#include "UZ7HOStuff.h"
#include "hidapi.h"
#include "ecc.h"

namespace stdio {
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
};

extern "C" {
extern void HAMLIBSetPTT(int PTTState);
}


SoundModemPTT::SoundModemPTT() :
    hPTTDevice(0),
    PTTPort(""),			// Port for Hardware PTT - may be same as control port.
    PTTBAUD(19200),
    PTTMode(PTTRTS),			// PTT Control Flags.
    PTTOnString(""),
    PTTOffString(""),
    PTTOnCmdLen(0),
    PTTOffCmdLen(0),
    pttGPIOPin(17),			// Default
    pttGPIOPinR(17),
    pttGPIOInvert(FALSE),
    useGPIO(FALSE),
    gotGPIO(FALSE),
    HamLibPort(4532),
    HamLibHost("192.168.1.14"),
    CM108Addr(""),
    VID(0),
    PID(0),
    CM108Device(NULL),
    ShortDT("HH:MM:SS"),
    smCom()
{

}

// PTT Stuff

    //CM108 Code

void SoundModemPTT::DecodeCM108(char * ptr)
{
    // Called if Device Name or PTT = Param is CM108

#ifdef WIN32

    // Next Param is VID and PID - 0xd8c:0x8 or Full device name
    // On Windows device name is very long and difficult to find, so
    //	easier to use VID/PID, but allow device in case more than one needed

    char * next;
    long VID = 0, PID = 0;
    char product[256] = "Unknown";

    struct hid_device_info *devs, *cur_dev;
    const char *path_to_open = NULL;
    hid_device *handle = NULL;

    if (strlen(ptr) > 16)
        CM108Device = _strdup(ptr);
    else
    {
        VID = strtol(ptr, &next, 0);
        if (next)
            PID = strtol(++next, &next, 0);

        // Look for Device

        devs = hid_enumerate((unsigned short)VID, (unsigned short)PID);
        cur_dev = devs;

        while (cur_dev)
        {
            if (cur_dev->product_string)
                wcstombs(product, cur_dev->product_string, 255);

            Debugprintf("HID Device %s VID %X PID %X", product, cur_dev->vendor_id, cur_dev->product_id);
            if (cur_dev->vendor_id == VID && cur_dev->product_id == PID)
            {
                path_to_open = cur_dev->path;
                break;
            }
            cur_dev = cur_dev->next;
        }

        if (path_to_open)
        {
            handle = hid_open_path(path_to_open);

            if (handle)
            {
                hid_close(handle);
                CM108Device = _strdup(path_to_open);
            }
            else
            {
                Debugprintf("Unable to open CM108 device %x %x", VID, PID);
            }
        }
        else
            Debugprintf("Couldn't find CM108 device %x %x", VID, PID);

        hid_free_enumeration(devs);
    }
#else

    // Linux - Next Param HID Device, eg /dev/hidraw0

    CM108Device = _strdup(ptr);
#endif
}

char * SoundModemPTT::strlop(char * buf, char delim)
{
    // Terminate buf at delim, and return rest of string

    char * ptr = strchr(buf, delim);

    if (ptr == NULL) return NULL;

    *(ptr)++ = 0;
    return ptr;
}

void SoundModemPTT::OpenPTTPort()
{
    PTTMode &= ~PTTCM108;
    PTTMode &= ~PTTHAMLIB;

    if (PTTPort[0] && strcmp(PTTPort, "None") != 0)
    {
        if (PTTMode == PTTCAT)
        {
            // convert config strings from Hex

            char * ptr1 = PTTOffString;
            UCHAR * ptr2 = PTTOffCmd;
            char c;
            int val;

            while ((c = *(ptr1++)))
            {
                val = c - 0x30;
                if (val > 15) val -= 7;
                val <<= 4;
                c = *(ptr1++) - 0x30;
                if (c > 15) c -= 7;
                val |= c;
                *(ptr2++) = val;
            }

            PTTOffCmdLen = ptr2 - PTTOffCmd;

            ptr1 = PTTOnString;
            ptr2 = PTTOnCmd;

            while ((c = *(ptr1++)))
            {
                val = c - 0x30;
                if (val > 15) val -= 7;
                val <<= 4;
                c = *(ptr1++) - 0x30;
                if (c > 15) c -= 7;
                val |= c;
                *(ptr2++) = val;
            }

            PTTOnCmdLen = ptr2 - PTTOnCmd;
        }

        if (stricmp((UCHAR *)PTTPort, (UCHAR *)"GPIO") == 0)
        {
            // Initialise GPIO for PTT if available

#ifdef __ARM_ARCH

            if (gpioInitialise() == 0)
            {
                printf("GPIO interface for PTT available\n");
                gotGPIO = TRUE;

                SetupGPIOPTT();
            }
            else
                printf("Couldn't initialise GPIO interface for PTT\n");

#else
            printf("GPIO interface for PTT not available on this platform\n");
#endif

        }
        else if (stricmp((UCHAR *)PTTPort, (UCHAR *)"CM108") == 0)
        {
            DecodeCM108(CM108Addr);
            PTTMode |= PTTCM108;
        }

        else if (stricmp((UCHAR *)PTTPort, (UCHAR *)"HAMLIB") == 0)
        {
            PTTMode |= PTTHAMLIB;
            HAMLIBSetPTT(0);			// to open port
            return;
        }

        else		//  Not GPIO
        {
            hPTTDevice = smCom.OpenCOMPort(PTTPort, PTTBAUD, FALSE, FALSE, FALSE, 0);
        }
    }
}

void SoundModemPTT::ClosePTTPort()
{
    CloseCOMPort(hPTTDevice);
    hPTTDevice = 0;
}

void SoundModemPTT::CM108_set_ptt(int PTTState)
{
    char io[5];
    hid_device *handle; UNUSED(handle);
    int n;

    io[0] = 0;
    io[1] = 0;
    io[2] = 1 << (3 - 1);
    io[3] = PTTState << (3 - 1);
    io[4] = 0;

    if (CM108Device == NULL)
        return;

#ifdef WIN32
    handle = hid_open_path(CM108Device);

    if (!handle) {
        printf("unable to open device\n");
        return;
    }

    n = hid_write(handle, io, 5);
    if (n < 0)
    {
        printf("Unable to write()\n");
        printf("Error: %ls\n", hid_error(handle));
    }

    hid_close(handle);

#else

    int fd;

    fd = open(CM108Device, O_WRONLY);

    if (fd == -1)
    {
        printf("Could not open %s for write, errno=%d\n", CM108Device, errno);
        return;
    }

    io[0] = 0;
    io[1] = 0;
    io[2] = 1 << (3 - 1);
    io[3] = PTTState << (3 - 1);
    io[4] = 0;

    n = write(fd, io, 5);
    if (n != 5)
    {
        printf("Write to %s failed, n=%d, errno=%d\n", CM108Device, n, errno);
    }

    close(fd);
#endif
    return;

}



void SoundModemPTT::RadioPTT(int snd_ch, BOOL PTTState)
{
#ifdef __ARM_ARCH
    if (useGPIO)
    {
        if (DualPTT && modemtoSoundLR[snd_ch] == 1)
            gpioWrite(pttGPIOPinR, (pttGPIOInvert ? (1 - PTTState) : (PTTState)));
        else
            gpioWrite(pttGPIOPin, (pttGPIOInvert ? (1 - PTTState) : (PTTState)));

        return;
    }

#endif

    if ((PTTMode & PTTCM108))
    {
        CM108_set_ptt(PTTState);
        return;
    }

    if ((PTTMode & PTTHAMLIB))
    {
        HAMLIBSetPTT(PTTState);
        return;
    }
    if (hPTTDevice == 0)
        return;

    if ((PTTMode & PTTCAT))
    {
        if (PTTState)
            smCom.WriteCOMBlock(hPTTDevice, (char *)PTTOnCmd, PTTOnCmdLen);
        else
            smCom.WriteCOMBlock(hPTTDevice, (char *)PTTOffCmd, PTTOffCmdLen);

        return;
    }

    if (DualPTT && modemtoSoundLR[snd_ch] == 1)		// use DTR
    {
        if (PTTState)
            smCom.COMSetDTR(hPTTDevice);
        else
            smCom.COMClearDTR(hPTTDevice);
    }
    else
    {
        if ((PTTMode & PTTRTS))
        {
            if (PTTState)
                smCom.COMSetRTS(hPTTDevice);
            else
                smCom.COMClearRTS(hPTTDevice);
        }
    }

}

char * SoundModemPTT::ShortDateTime()
{
    struct tm * tm;
    time_t NOW = time(NULL);

    tm = gmtime(&NOW);

    sprintf(ShortDT, "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
    return ShortDT;
}

#ifdef NOTDEF
// Reed Solomon Stuff
// This doesn't belong here


int NPAR = -1;	// Number of Parity Bytes - used in RS Code

int xMaxErrors = 0;

int RSEncode(UCHAR * bytToRS, UCHAR * RSBytes, int DataLen, int RSLen)
{
    // This just returns the Parity Bytes. I don't see the point
    // in copying the message about

    unsigned char Padded[256];		// The padded Data

    int Length = DataLen + RSLen;	// Final Length of packet
    int PadLength = 255 - Length;	// Padding bytes needed for shortened RS codes

    //	subroutine to do the RS encode. For full length and shortend RS codes up to 8 bit symbols (mm = 8)

    if (NPAR != RSLen)		// Changed RS Len, so recalc constants;
    {
        NPAR = RSLen;
        xMaxErrors = NPAR / 2;
        initialize_ecc();
    }

    // Copy the supplied data to end of data array.

    memset(Padded, 0, PadLength);
    memcpy(&Padded[PadLength], bytToRS, DataLen);

    encode_data(Padded, 255 - RSLen, RSBytes);

    return RSLen;
}

//	Main RS decode function

extern int index_of[];
extern int recd[];
extern int Corrected[256];
extern int tt;		//  number of errors that can be corrected
extern int kk;		// Info Symbols

extern BOOL blnErrorsCorrected;


BOOL RSDecode(UCHAR * bytRcv, int Length, int CheckLen, BOOL * blnRSOK)
{


    // Using a modified version of Henry Minsky's code

    //Copyright Henry Minsky (hqm@alum.mit.edu) 1991-2009

    // Rick's Implementation processes the byte array in reverse. and also
    //	has the check bytes in the opposite order. I've modified the encoder
    //	to allow for this, but so far haven't found a way to mske the decoder
    //	work, so I have to reverse the data and checksum to decode G8BPQ Nov 2015

    //	returns TRUE if was ok or correction succeeded, FALSE if correction impossible

    UCHAR intTemp[256];				// WOrk Area to pass to Decoder
    int i;
    UCHAR * ptr2 = intTemp;
    UCHAR * ptr1 = &bytRcv[Length - CheckLen - 1]; // Last Byte of Data

    int DataLen = Length - CheckLen;
    int PadLength = 255 - Length;		// Padding bytes needed for shortened RS codes

    *blnRSOK = FALSE;

    if (Length > 255 || Length < (1 + CheckLen))		//Too long or too short
        return FALSE;

    if (NPAR != CheckLen)		// Changed RS Len, so recalc constants;
    {
        NPAR = CheckLen;
        xMaxErrors = NPAR / 2;

        initialize_ecc();
    }


    //	We reverse the data while zero padding it to speed things up

    //	We Need (Data Reversed) (Zero Padding) (Checkbytes Reversed)

    // Reverse Data

    for (i = 0; i < DataLen; i++)
    {
        *(ptr2++) = *(ptr1--);
    }

    //	Clear padding

    memset(ptr2, 0, PadLength);

    ptr2 += PadLength;

    // Error Bits

    ptr1 = &bytRcv[Length - 1];			// End of check bytes

    for (i = 0; i < CheckLen; i++)
    {
        *(ptr2++) = *(ptr1--);
    }

    decode_data(intTemp, 255);

    // check if syndrome is all zeros

    if (check_syndrome() == 0)
    {
        // RS ok, so no need to correct

        *blnRSOK = TRUE;
        return TRUE;		// No Need to Correct
    }

    if (correct_errors_erasures(intTemp, 255, 0, 0) == 0) // Dont support erasures at the momnet

        // Uncorrectable

        return FALSE;

    // Data has been corrected, so need to reverse again

    ptr1 = &intTemp[DataLen - 1];
    ptr2 = bytRcv; // Last Byte of Data

    for (i = 0; i < DataLen; i++)
    {
        *(ptr2++) = *(ptr1--);
    }

    // ?? Do we need to return the check bytes ??

    // Yes, so we can redo RS Check on supposedly connected frame

    ptr1 = &intTemp[254];	// End of Check Bytes

    for (i = 0; i < CheckLen; i++)
    {
        *(ptr2++) = *(ptr1--);
    }

    return TRUE;
}


extern TStringList detect_list[5];
extern TStringList detect_list_c[5];

void ProcessPktFrame(int snd_ch, UCHAR * Data, int frameLen)
{
    string * pkt = newString();

    stringAdd(pkt, Data, frameLen + 2);			// 2 for crc (not actually there)

    analiz_frame(snd_ch, pkt, "ARDOP", 1);

}
#endif

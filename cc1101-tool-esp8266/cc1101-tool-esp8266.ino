//
// CC1101 interactive terminal tool
// allows for sending / receiving data over serial port
// on selected radio channel, modulation, ..
//
// (C) Adam Loboda '2023 , adam.loboda@wp.pl
//  
// based on great SmartRC library by Little_S@tan
// Please download ZIP from 
// https://github.com/LSatan/SmartRC-CC1101-Driver-Lib
// and attach it as ZIP library for Arduino
//
// Also uses Arduino Command Line interpreter by Edgar Bonet
// from https://gist.github.com/edgar-bonet/607b387260388be77e96
//
// This code will ONLY work with ESP8266 board
//

#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

#define CCBUFFERSIZE 64
#define RECORDINGBUFFERSIZE 4096   // Buffer for recording the frames
#define EPROMSIZE 4096              // Size of EEPROM in your Arduino chip. For  ESP8266 size is 4096
#define BUF_LENGTH 128             // Buffer for the incoming command.

// named constants (replacing magic numbers scattered through the code)
#define MAX_PAYLOAD 60             // max RF payload bytes per frame
#define MAX_HEXCHARS 120           // max hex input chars (2 * MAX_PAYLOAD)
#define HEXDUMP_CHUNK 32           // bytes per chunk in RAW hex dumps
#define SCAN_RSSI_MARK (-75)       // dBm threshold to consider a signal present
#define RSSI_NONE (-100)           // sentinel "no signal" rssi value

// ---- WiFi Access Point configuration (edit to taste) ----
#define AP_SSID     "cc1101"
#define AP_PASSWORD "cc1101"          // >= 8 chars for WPA2; "" for an open AP
IPAddress apIP(192, 168, 1, 100);
IPAddress apGateway(192, 168, 1, 1);
IPAddress apSubnet(255, 255, 255, 0);

ESP8266WebServer server(80);

// Command output target. Defaults to Serial; temporarily repointed at a
// String sink while a command runs from the web (/cmd).
Print* out = &Serial;

// Current base frequency, tracked here because the SmartRC library has no
// getMHZ() accessor. Updated by cc1101initialize() and the setmhz handler.
float currentFreq = 433.92;

// defining PINs set for ESP8266 - WEMOS D1 MINI module
byte sck = 14;     // GPIO 14
byte miso = 12;  // GPIO 12
byte mosi = 13;  // GPIO 13
byte ss = 15;      // GPIO 15
int gdo0 = 5;     // GPIO 5
int gdo2 = 4;     // GPIO 4


// position in big recording buffer
int bigrecordingbufferpos = 0; 

// number of frames in big recording buffer
int framesinbigrecordingbuffer = 0; 

// CLI activity: exactly one mode active at a time.
enum Mode { MODE_IDLE, MODE_RX, MODE_JAM, MODE_REC, MODE_CHAT, MODE_SCAN, MODE_SNIFF, MODE_BRUTE };
Mode activeMode = MODE_IDLE;

// scan mode state (serviced one frequency step per loop pass)
float scanFrom = 0, scanTo = 0, scanCursor = 0;
float scanBestFreq = 0;  int scanBestRssi = -100;  long scanCompare = 0;

// human-readable name for /status
static const char* modeName(void) {
    switch (activeMode) {
        case MODE_RX: return "rx";        case MODE_JAM: return "jam";
        case MODE_REC: return "rec";      case MODE_CHAT: return "chat";
        case MODE_SCAN: return "scan";    case MODE_SNIFF: return "sniff";
        case MODE_BRUTE: return "brute";  default: return "idle";
    }
}

static bool do_echo = true;

// buffer for receiving  CC1101
byte ccreceivingbuffer[CCBUFFERSIZE] = {0};

// buffer for sending  CC1101
byte ccsendingbuffer[CCBUFFERSIZE] = {0};

// buffer for recording and replaying of many frames
byte bigrecordingbuffer[RECORDINGBUFFERSIZE] = {0};

// buffer for hex to ascii conversions
byte textbuffer[BUF_LENGTH];


// Forward declarations. Arduino's auto-prototype generator mangles the
// return type of static functions on this sketch (producing broken
// prototypes and bogus #line directives that fail to compile), so we
// declare every function explicitly to suppress that generation.
void asciitohex(byte *ascii_ptr, byte *hex_ptr, int len);
void hextoascii(byte *ascii_ptr, byte *hex_ptr, int len);
static void cc1101initialize(void);
static void zeroRecordingBuffer(void);
static void enterRawMode(bool tx);
static void exitRawMode(bool tx);
static void dumpBufferHex(int start, int count);
static bool ingestHex(char *in, byte *out, int *outlen);
static const char* modeName(void);
static void exec(char *cmdline);
static void serviceActiveMode(void);
void setup();
void loop();
static void startAP(void);
static void setupWebServer(void);
static void handleRoot(void);
static void handleCmd(void);
static void handleStatus(void);


// convert bytes in table to string with hex numbers
void asciitohex(byte *ascii_ptr, byte *hex_ptr,int len)
{
    byte i,j,k;
    for(i = 0; i < len; i++)
    {
      // high byte first
      j = ascii_ptr[i] / 16;
      if (j>9) 
         { k = j - 10 + 65; }
      else 
         { k = j + 48; }
      hex_ptr[2*i] = k ;
      // low byte second
      j = ascii_ptr[i] % 16;
      if (j>9) 
         { k = j - 10 + 65; }
      else
         { k = j + 48; }
      hex_ptr[(2*i)+1] = k ;
    };
    // terminate right after the last written nibble (i == len here)
    hex_ptr[2*len] = '\0' ;
}


// convert string with hex numbers to array of bytes
 void  hextoascii(byte *ascii_ptr, byte *hex_ptr,int len)
{
    byte i,j;
    for(i = 0; i < (len/2); i++)
     {
     // start from zero so invalid nibbles leave a defined 0, not garbage
     ascii_ptr[i] = 0;
     j = hex_ptr[i*2];
     if ((j>47) && (j<58))  ascii_ptr[i] = (j - 48) * 16;
     if ((j>64) && (j<71))  ascii_ptr[i] = (j - 55) * 16;
     if ((j>96) && (j<103)) ascii_ptr[i] = (j - 87) * 16;
     j = hex_ptr[i*2+1];
     if ((j>47) && (j<58))  ascii_ptr[i] += (j - 48);
     if ((j>64) && (j<71))  ascii_ptr[i] += (j - 55);
     if ((j>96) && (j<103)) ascii_ptr[i] += (j - 87);
     };
    ascii_ptr[i] = '\0' ;
}

// Initialize CC1101 board with default settings, you may change your preferences here
static void cc1101initialize(void)
{
    // initializing library with custom pins selected
     ELECHOUSE_cc1101.setSpiPin(sck, miso, mosi, ss);
     ELECHOUSE_cc1101.setGDO(gdo0, gdo2);

    // Main part to tune CC1101 with proper frequency, modulation and encoding    
    ELECHOUSE_cc1101.Init();                // must be set to initialize the cc1101!
    ELECHOUSE_cc1101.setGDO0(gdo0);         // set lib internal gdo pin (gdo0). Gdo2 not use for this example.
    ELECHOUSE_cc1101.setCCMode(1);          // set config for internal transmission mode. value 0 is for RAW recording/replaying
    ELECHOUSE_cc1101.setModulation(2);      // set modulation mode. 0 = 2-FSK, 1 = GFSK, 2 = ASK/OOK, 3 = 4-FSK, 4 = MSK.
    ELECHOUSE_cc1101.setMHZ(433.92);        // Here you can set your basic frequency. The lib calculates the frequency automatically (default = 433.92).The cc1101 can: 300-348 MHZ, 387-464MHZ and 779-928MHZ. Read More info from datasheet.
    ELECHOUSE_cc1101.setDeviation(47.60);   // Set the Frequency deviation in kHz. Value from 1.58 to 380.85. Default is 47.60 kHz.
    ELECHOUSE_cc1101.setChannel(0);         // Set the Channelnumber from 0 to 255. Default is cahnnel 0.
    ELECHOUSE_cc1101.setChsp(199.95);       // The channel spacing is multiplied by the channel number CHAN and added to the base frequency in kHz. Value from 25.39 to 405.45. Default is 199.95 kHz.
    ELECHOUSE_cc1101.setRxBW(812.50);       // Set the Receive Bandwidth in kHz. Value from 58.03 to 812.50. Default is 812.50 kHz.
    ELECHOUSE_cc1101.setDRate(9.6);         // Set the Data Rate in kBaud. Value from 0.02 to 1621.83. Default is 99.97 kBaud!
    ELECHOUSE_cc1101.setPA(10);             // Set TxPower. The following settings are possible depending on the frequency band.  (-30  -20  -15  -10  -6    0    5    7    10   11   12) Default is max!
    ELECHOUSE_cc1101.setSyncMode(2);        // Combined sync-word qualifier mode. 0 = No preamble/sync. 1 = 16 sync word bits detected. 2 = 16/16 sync word bits detected. 3 = 30/32 sync word bits detected. 4 = No preamble/sync, carrier-sense above threshold. 5 = 15/16 + carrier-sense above threshold. 6 = 16/16 + carrier-sense above threshold. 7 = 30/32 + carrier-sense above threshold.
    ELECHOUSE_cc1101.setSyncWord(211, 145); // Set sync word. Must be the same for the transmitter and receiver. Default is 211,145 (Syncword high, Syncword low)
    ELECHOUSE_cc1101.setAdrChk(0);          // Controls address check configuration of received packages. 0 = No address check. 1 = Address check, no broadcast. 2 = Address check and 0 (0x00) broadcast. 3 = Address check and 0 (0x00) and 255 (0xFF) broadcast.
    ELECHOUSE_cc1101.setAddr(0);            // Address used for packet filtration. Optional broadcast addresses are 0 (0x00) and 255 (0xFF).
    ELECHOUSE_cc1101.setWhiteData(0);       // Turn data whitening on / off. 0 = Whitening off. 1 = Whitening on.
    ELECHOUSE_cc1101.setPktFormat(0);       // Format of RX and TX data. 0 = Normal mode, use FIFOs for RX and TX. 1 = Synchronous serial mode, Data in on GDO0 and data out on either of the GDOx pins. 2 = Random TX mode; sends random data using PN9 generator. Used for test. Works as normal mode, setting 0 (00), in RX. 3 = Asynchronous serial mode, Data in on GDO0 and data out on either of the GDOx pins.
    ELECHOUSE_cc1101.setLengthConfig(1);    // 0 = Fixed packet length mode. 1 = Variable packet length mode. 2 = Infinite packet length mode. 3 = Reserved
    ELECHOUSE_cc1101.setPacketLength(0);    // Indicates the packet length when fixed packet length mode is enabled. If variable packet length mode is used, this value indicates the maximum packet length allowed.
    ELECHOUSE_cc1101.setCrc(0);             // 1 = CRC calculation in TX and CRC check in RX enabled. 0 = CRC disabled for TX and RX.
    ELECHOUSE_cc1101.setCRC_AF(0);          // Enable automatic flush of RX FIFO when CRC is not OK. This requires that only one packet is in the RXIFIFO and that packet length is limited to the RX FIFO size.
    ELECHOUSE_cc1101.setDcFilterOff(0);     // Disable digital DC blocking filter before demodulator. Only for data rates <= 250 kBaud The recommended IF frequency changes when the DC blocking is disabled. 1 = Disable (current optimized). 0 = Enable (better sensitivity).
    ELECHOUSE_cc1101.setManchester(0);      // Enables Manchester encoding/decoding. 0 = Disable. 1 = Enable.
    ELECHOUSE_cc1101.setFEC(0);             // Enable Forward Error Correction (FEC) with interleaving for packet payload (Only supported for fixed packet length mode. 0 = Disable. 1 = Enable.
    ELECHOUSE_cc1101.setPRE(0);             // Sets the minimum number of preamble bytes to be transmitted. Values: 0 : 2, 1 : 3, 2 : 4, 3 : 6, 4 : 8, 5 : 12, 6 : 16, 7 : 24
    ELECHOUSE_cc1101.setPQT(0);             // Preamble quality estimator threshold. The preamble quality estimator increases an internal counter by one each time a bit is received that is different from the previous bit, and decreases the counter by 8 each time a bit is received that is the same as the last bit. A threshold of 4*PQT for this counter is used to gate sync word detection. When PQT=0 a sync word is always accepted.
    ELECHOUSE_cc1101.setAppendStatus(0);    // When enabled, two status bytes will be appended to the payload of the packet. The status bytes contain RSSI and LQI values, as well as CRC OK.
}


// ---- shared helpers (extracted from repeated command code) ----

// zero the whole recording buffer and rewind its pointers
static void zeroRecordingBuffer(void)
{
    memset(bigrecordingbuffer, 0, RECORDINGBUFFERSIZE);
    bigrecordingbufferpos = 0;
    framesinbigrecordingbuffer = 0;
}

// put CC1101 into async (bit-banged GDO0) mode for RAW record/replay
static void enterRawMode(bool tx)
{
    ELECHOUSE_cc1101.setCCMode(0);
    ELECHOUSE_cc1101.setPktFormat(3);
    if (tx) ELECHOUSE_cc1101.SetTx(); else ELECHOUSE_cc1101.SetRx();
}

// restore CC1101 to normal packet mode after RAW record/replay
static void exitRawMode(bool tx)
{
    ELECHOUSE_cc1101.setCCMode(1);
    ELECHOUSE_cc1101.setPktFormat(0);
    if (tx) ELECHOUSE_cc1101.SetTx(); else ELECHOUSE_cc1101.SetRx();
}

// dump 'count' bytes of the recording buffer as hex, in HEXDUMP_CHUNK chunks
static void dumpBufferHex(int start, int count)
{
    for (int i = start; i < start + count; i += HEXDUMP_CHUNK)
       {
         asciitohex(&bigrecordingbuffer[i], textbuffer, HEXDUMP_CHUNK);
         out->print((char *)textbuffer);
         ESP.wdtFeed();
         yield();
       }
}

// parse a hex string (max MAX_HEXCHARS chars, even length) into out[],
// storing the resulting byte count in *outlen; returns false on bad input
static bool ingestHex(char *in, byte *out, int *outlen)
{
    int n = strlen(in);
    if ((n <= 0) || (n > MAX_HEXCHARS) || (n & 1)) return false;
    hextoascii(out, (byte *)in, n);
    *outlen = n / 2;
    return true;
}


// Execute a complete CC1101 command.

static void exec(char *cmdline)
{ 
        
    char *command = strsep(&cmdline, " ");
    int setting, setting2, len;
    byte j, k;
    uint32_t brute, poweroftwo;
    float settingf1;

  // identification of the command & actions
      
    if (strcmp_P(command, PSTR("help")) == 0) {
        out->println(F(
          "setmodulation <mode> : Set modulation mode. 0 = 2-FSK, 1 = GFSK, 2 = ASK/OOK, 3 = 4-FSK, 4 = MSK.\r\n\r\n"
          "setmhz <frequency>   : Here you can set your basic frequency. default = 433.92).The cc1101 can: 300-348 MHZ, 387-464MHZ and 779-928MHZ.\r\n\r\n"
          "setdeviation <deviation> : Set the Frequency deviation in kHz. Value from 1.58 to 380.85.\r\n\r\n"
          "setchannel <channel> : Set the Channelnumber from 0 to 255. Default is cahnnel 0.\r\n\r\n"
          "setchsp <spacing>  :  The channel spacing is multiplied by the channel number CHAN and added to the base frequency in kHz. Value from 25.39 to 405.45. \r\n\r\n"
          "setrxbw <Receive bndwth> : Set the Receive Bandwidth in kHz. Value from 58.03 to 812.50. \r\n\r\n"
          "setdrate <datarate> : Set the Data Rate in kBaud. Value from 0.02 to 1621.83.\r\n\r\n"
          "setpa <power value> : Set RF transmission power. The following settings are possible depending on the frequency band.  (-30  -20  -15  -10  -6    0    5    7    10   11   12) Default is max!\r\n\r\n"
          "setsyncmode  <sync mode> : Combined sync-word qualifier mode. 0 = No preamble/sync. 1 = 16 sync word bits detected. 2 = 16/16 sync word bits detected. 3 = 30/32 sync word bits detected. 4 = No preamble/sync, carrier-sense above threshold. 5 = 15/16 + carrier-sense above threshold. 6 = 16/16 + carrier-sense above threshold. 7 = 30/32 + carrier-sense above threshold.\r\n"
         ));
          yield();       
        out->println(F(
          "setsyncword <decimal LOW, decimal HIGH> : Set sync word. Must be the same for the transmitter and receiver. (Syncword high, Syncword low) Default is 211,145\r\n\r\n"
          "setadrchk <address chk> : Controls address check configuration of received packages. 0 = No address check. 1 = Address check, no broadcast. 2 = Address check and 0 (0x00) broadcast. 3 = Address check and 0 (0x00) and 255 (0xFF) broadcast.\r\n\r\n"
          "setaddr <address> : Address used for packet filtration. Optional broadcast addresses are 0 (0x00) and 255 (0xFF).\r\n\r\n"
          "setwhitedata <whitening> : Turn data whitening on / off. 0 = Whitening off. 1 = Whitening on.\r\n\r\n"
          "setpktformat <pktformat> : Format of RX and TX data. 0 = Normal mode, use FIFOs for RX and TX. 1 = Synchronous serial mode, Data in on GDO0 and data out on either of the GDOx pins. 2 = Random TX mode; sends random data using PN9 generator.  3 = Asynchronous serial mode\r\n\r\n"
          "setlengthconfig <mode> : Set packet Length mode : 0 = Fixed packet length mode. 1 = Variable packet length mode. 2 = Infinite packet length mode. 3 = Reserved \r\n\r\n"
          "setpacketlength <mode> : Indicates the packet length when fixed packet length mode is enabled. If variable packet length mode is used, this value indicates the maximum packet length allowed.\r\n\r\n"
          "setcrc <mode> : Switches on/of CRC calculation and check. 1 = CRC calculation in TX and CRC check in RX enabled. 0 = CRC disabled for TX and RX.\r\n\r\n"
          "setcrcaf <mode> : Enable automatic flush of RX FIFO when CRC is not OK. This requires that only one packet is in the RXIFIFO and that packet length is limited to the RX FIFO size.\r\n"
         ));
          yield();
        out->println(F(
          "setdcfilteroff <mode> : Disable digital DC blocking filter before demodulator. Only for data rates <= 250 kBaud The recommended IF frequency changes when the DC blocking is disabled. 1 = Disable (current optimized). 0 = Enable (better sensitivity).\r\n\r\n"
          "setmanchester <mode> : Enables Manchester encoding/decoding. 0 = Disable. 1 = Enable.\r\n\r\n"
          "setfec <mode> : Enable Forward Error Correction (FEC) with interleaving for packet payload (Only supported for fixed packet length mode. 0 = Disable. 1 = Enable.\r\n\r\n"
          "setpre <mode> : Sets the minimum number of preamble bytes to be transmitted. Values: 0 : 2, 1 : 3, 2 : 4, 3 : 6, 4 : 8, 5 : 12, 6 : 16, 7 : 24\r\n\r\n"
          "setpqt <mode> : Preamble quality estimator threshold. \r\n\r\n"
          "setappendstatus <mode> : When enabled, two status bytes will be appended to the payload of the packet. The status bytes contain RSSI and LQI values, as well as CRC OK.\r\n\r\n"
          "getrssi : Display quality information about last received frames over RF\r\n\r\n"
          "scan <start> <stop> : Scan frequency range for the highest signal.\r\n\r\n"         
          "chat :  Enable chat mode between many devices. No exit available, disconnect device to quit\r\n"
         ));
          yield();
         out->println(F(
          "rx : Sniffer. Enable or disable printing of received RF packets on serial terminal.\r\n\r\n"
          "tx <hex-vals> : Send packet of max 60 bytes <hex values> over RF\r\n\r\n"
          "jam : Enable or disable continous jamming on selected band.\r\n\r\n"
          "brute <microseconds> <number-of-bits> : Brute force garage gate with <nb-of-bits> keyword where symbol time is <usec>.\r\n\r\n"
          "rec : Enable or disable recording frames in the buffer.\r\n\r\n"
          "add <hex-vals> : Manually add single frame payload (max 64 hex values) to the buffer so it can be replayed\r\n\r\n"
          "show : Show content of recording buffer\r\n\r\n"
          "flush : Clear the recording buffer\r\n\r\n"
          "play <N> : Replay 0 = all frames or N-th recorded frame previously stored in the buffer.\r\n\r\n"
          "rxraw <microseconds> : Sniffs radio by sampling with <microsecond> interval and prints received bytes in hex.\r\n\r\n"
          "recraw <microseconds> : Recording RAW RF data with <microsecond> sampling interval.\r\n"
            ));
          yield();
        out->println(F(
          "addraw <hex-vals> : Manually add chunks (max 60 hex values) to the buffer so they can be further replayed.\r\n\r\n"        
          "showraw : Showing content of recording buffer in RAW format.\r\n\r\n"
          "playraw <microseconds> : Replaying previously recorded RAW RF data with <microsecond> sampling interval.\r\n\r\n"
          "showbit : Showing content of recording buffer in RAW format as a stream of bits.\r\n\r\n"
          "save : Store recording buffer content in non-volatile memory\r\n\r\n"
          "load : Load the content from non-volatile memory to the recording buffer\r\n\r\n"
          "echo <mode> : Enable or disable Echo on serial terminal. 1 = enabled, 0 = disabled\r\n\r\n"
          "x : Stops jamming/receiving/recording packets.\r\n\r\n"
          "init : Restarts CC1101 board with default parameters\r\n\r\n"
            ));
          yield();

    // Handling SETMODULATION command 
    } else if (strcmp_P(command, PSTR("setmodulation")) == 0) {
        setting = atoi(cmdline);
        ELECHOUSE_cc1101.setModulation(setting);
        out->print(F("\r\nModulation: "));
        if (setting == 0) { out->print(F("2-FSK")); }
        else if (setting == 1) { out->print(F("GFSK")); }
        else if (setting == 2) { out->print(F("ASK/OOK")); }
        else if (setting == 3) { out->print(F("4-FSK")); }
        else if (setting == 4) { out->print(F("MSK")); };  
        out->print(F(" \r\n"));
        yield();

    // Handling SETMHZ command 
    } else if (strcmp_P(command, PSTR("setmhz")) == 0) {
        settingf1 = atof(cmdline);
        ELECHOUSE_cc1101.setMHZ(settingf1);
        currentFreq = settingf1;
        out->print(F("\r\nFrequency: "));
        out->print(settingf1);
        out->print(F(" MHz\r\n"));
        yield();
        
    // Handling SETDEVIATION command 
    } else if (strcmp_P(command, PSTR("setdeviation")) == 0) {
        settingf1 = atof(cmdline);
        ELECHOUSE_cc1101.setDeviation(settingf1);
        out->print(F("\r\nDeviation: "));
        out->print(settingf1);
        out->print(F(" KHz\r\n"));        
        yield();

    // Handling SETCHANNEL command       
    } else if (strcmp_P(command, PSTR("setchannel")) == 0) {
        setting = atoi(cmdline);
        ELECHOUSE_cc1101.setChannel(setting);
        out->print(F("\r\nChannel:"));
        out->print(setting);
        out->print(F("\r\n"));        
        yield();

    // Handling SETCHSP command 
    } else if (strcmp_P(command, PSTR("setchsp")) == 0) {
        settingf1 = atof(cmdline);
        ELECHOUSE_cc1101.setChsp(settingf1);
        out->print(F("\r\nChann spacing: "));
        out->print(settingf1);
        out->print(F(" kHz\r\n"));  
        yield();

    // Handling SETRXBW command         
    } else if (strcmp_P(command, PSTR("setrxbw")) == 0) {
        settingf1 = atof(cmdline);
        ELECHOUSE_cc1101.setRxBW(settingf1);
        out->print(F("\r\nRX bandwidth: "));
        out->print(settingf1);
        out->print(F(" kHz \r\n"));  
        yield();

    // Handling SETDRATE command         
    } else if (strcmp_P(command, PSTR("setdrate")) == 0) {
        settingf1 = atof(cmdline);
        ELECHOUSE_cc1101.setDRate(settingf1);
        out->print(F("\r\nDatarate: "));
        out->print(settingf1);
        out->print(F(" kbaud\r\n"));  
        yield();

    // Handling SETPA command         
    } else if (strcmp_P(command, PSTR("setpa")) == 0) {
        setting = atoi(cmdline);
        ELECHOUSE_cc1101.setPA(setting);
        out->print(F("\r\nTX PWR: "));
        out->print(setting);
        out->print(F(" dBm\r\n"));  
        yield();
        
    // Handling SETSYNCMODE command         
    } else if (strcmp_P(command, PSTR("setsyncmode")) == 0) {
        int setting = atoi(cmdline);
        ELECHOUSE_cc1101.setSyncMode(setting);
        out->print(F("\r\nSynchronization: "));
        if (setting == 0) { out->print(F("No preamble")); }
        else if (setting == 1) { out->print(F("16 sync bits")); }
        else if (setting == 2) { out->print(F("16/16 sync bits")); }
        else if (setting == 3) { out->print(F("30/32 sync bits")); }
        else if (setting == 4) { out->print(F("No preamble/sync, carrier-sense")); }
        else if (setting == 5) { out->print(F("15/16 + carrier-sense")); }
        else if (setting == 6) { out->print(F("16/16 + carrier-sense")); }
        else if (setting == 7) { out->print(F("30/32 + carrier-sense")); };
        out->print(F("\r\n"));  
        yield();
        
    // Handling SETSYNCWORD command         
    } else if (strcmp_P(command, PSTR("setsyncword")) == 0) {
        setting = atoi(strsep(&cmdline, " "));
        setting2 = atoi(cmdline);
        // args are entered LOW then HIGH; setSyncWord takes (high, low)
        ELECHOUSE_cc1101.setSyncWord(setting2, setting);
        out->print(F("\r\nSynchronization:\r\n"));
        out->print(F("high = "));
        out->print(setting2);
        out->print(F("\r\nlow = "));
        out->print(setting);
        out->print(F("\r\n"));
        yield();
    
    // Handling SETADRCHK command         
    } else if (strcmp_P(command, PSTR("setadrchk")) == 0) {
        setting = atoi(cmdline);
        ELECHOUSE_cc1101.setAdrChk(setting);
        out->print(F("\r\nAddress checking:"));
        if (setting == 0) { out->print(F("No adr chk")); }
        else if (setting == 1) { out->print(F("Adr chk, no bcast")); }
        else if (setting == 2) { out->print(F("Adr chk and 0 bcast")); }
        else if (setting == 3) { out->print(F("Adr chk and 0 and FF bcast")); };
        out->print(F("\r\n"));  
        yield();
        
    // Handling SETADDR command         
    } else if (strcmp_P(command, PSTR("setaddr")) == 0) {
        setting = atoi(cmdline);
        ELECHOUSE_cc1101.setAddr(setting);
        out->print(F("\r\nAddress: "));
        out->print(setting);
        out->print(F("\r\n"));  
        yield();

    // Handling SETWHITEDATA command         
    } else if (strcmp_P(command, PSTR("setwhitedata")) == 0) {
        setting = atoi(cmdline);
        ELECHOUSE_cc1101.setWhiteData(setting);
        out->print(F("\r\nWhitening "));
        if (setting == 0) { out->print(F("OFF")); }
        else if (setting == 1) { out->print(F("ON")); }
        out->print(F("\r\n"));  
        yield();
        
    // Handling SETPKTFORMAT command         
    } else if (strcmp_P(command, PSTR("setpktformat")) == 0) {
        setting = atoi(cmdline);
        ELECHOUSE_cc1101.setPktFormat(setting);
        out->print(F("\r\nPacket format: "));
        if (setting == 0) { out->print(F("Normal mode")); }
        else if (setting == 1) { out->print(F("Synchronous serial mode")); }
        else if (setting == 2) { out->print(F("Random TX mode")); }
        else if (setting == 3) { out->print(F("Asynchronous serial mode")); };
        out->print(F("\r\n"));  
        yield();
  
    // Handling SETLENGTHCONFIG command         
    } else if (strcmp_P(command, PSTR("setlengthconfig")) == 0) {
        setting = atoi(cmdline);
        ELECHOUSE_cc1101.setLengthConfig(setting);
        out->print(F("\r\nPkt length mode: "));
        if (setting == 0) { out->print(F("Fixed")); }
        else if (setting == 1) { out->print(F("Variable")); }
        else if (setting == 2) { out->print(F("Infinite")); }
        else if (setting == 3) { out->print(F("Reserved")); };
        out->print(F("\r\n"));  
  
    // Handling SETPACKETLENGTH command         
    } else if (strcmp_P(command, PSTR("setpacketlength")) == 0) {
        setting = atoi(cmdline);
        ELECHOUSE_cc1101.setPacketLength(setting);
        out->print(F("\r\nPkt length: "));
        out->print(setting);
        out->print(F(" bytes\r\n"));  
        yield();
        
    // Handling SETCRC command         
    } else if (strcmp_P(command, PSTR("setcrc")) == 0) {
        setting = atoi(cmdline);
        ELECHOUSE_cc1101.setCrc(setting);
        out->print(F("\r\nCRC checking: "));
        if (setting == 0) { out->print(F("Disabled")); }
        else if (setting == 1) { out->print(F("Enabled")); };
        out->print(F("\r\n")); 
        yield();
        
    // Handling SETCRCAF command         
    } else if (strcmp_P(command, PSTR("setcrcaf")) == 0) {
        setting = atoi(cmdline);
        ELECHOUSE_cc1101.setCRC_AF(setting);
        out->print(F("\r\nCRC Autoflush: "));
        if (setting == 0) { out->print(F("Disabled")); }
        else if (setting == 1) { out->print(F("Enabled")); };
         out->print(F("\r\n")); 
        
    // Handling SETDCFILTEROFF command         
     } else if (strcmp_P(command, PSTR("setdcfilteroff")) == 0) {
        setting = atoi(cmdline);
        ELECHOUSE_cc1101.setDcFilterOff(setting);
        out->print(F("\r\nDC filter: "));
        if (setting == 0) { out->print(F("Enabled")); }
        else if (setting == 1) { out->print(F("Disabled")); };
        out->print(F("\r\n")); 
        yield();

    // Handling SETMANCHESTER command         
     } else if (strcmp_P(command, PSTR("setmanchester")) == 0) {
        setting = atoi(cmdline);
        ELECHOUSE_cc1101.setManchester(setting);
        out->print(F("\r\nManchester coding: "));
        if (setting == 0) { out->print(F("Disabled")); }
        else if (setting == 1) { out->print(F("Enabled")); };
        out->print(F("\r\n")); 
        yield();

    // Handling SETFEC command         
     } else if (strcmp_P(command, PSTR("setfec")) == 0) {
        setting = atoi(cmdline);
        ELECHOUSE_cc1101.setFEC(setting);
        out->print(F("\r\nForward Error Correction: "));
        if (setting == 0) { out->print(F("Disabled")); }
        else if (setting == 1) { out->print(F("Enabled")); };
        out->print(F("\r\n")); 
        yield();
        
    // Handling SETPRE command         
     } else if (strcmp_P(command, PSTR("setpre")) == 0) {
        setting = atoi(cmdline);
        ELECHOUSE_cc1101.setPRE(setting);
        out->print(F("\r\nMinimum preamble bytes:"));
        out->print(setting);
        out->print(F(" means 0 = 2 bytes, 1 = 3b, 2 = 4b, 3 = 6b, 4 = 8b, 5 = 12b, 6 = 16b, 7 = 24 bytes\r\n")); 
        out->print(F("\r\n")); 
        yield();

  
    // Handling SETPQT command         
      } else if (strcmp_P(command, PSTR("setpqt")) == 0) {
        setting = atoi(cmdline);
        ELECHOUSE_cc1101.setPQT(setting);
        out->print(F("\r\nPQT: "));
        out->print(setting);
        out->print(F("\r\n")); 
        yield();

    // Handling SETAPPENDSTATUS command         
       } else if (strcmp_P(command, PSTR("setappendstatus")) == 0) {
        setting = atoi(cmdline);
        ELECHOUSE_cc1101.setAppendStatus(setting);
        out->print(F("\r\nStatus bytes appending: "));
        if (setting == 0) { out->print(F("Enabled")); }
        else if (setting == 1) { out->print(F("Disabled")); };
        out->print(F("\r\n")); 
        yield();

    // Handling GETRSSI command         
      } else if (strcmp_P(command, PSTR("getrssi")) == 0) {
        //Rssi Level in dBm
        out->print(F("Rssi: "));
        out->println(ELECHOUSE_cc1101.getRssi());
        //Link Quality Indicator
        out->print(F(" LQI: "));
        out->println(ELECHOUSE_cc1101.getLqi());        
        out->print(F("\r\n")); 
        yield();


    // Handling SCAN command - frequency scanner by Little S@tan !
    } else if (strcmp_P(command, PSTR("scan")) == 0) {
        scanFrom = atof(strsep(&cmdline, " "));
        scanTo   = atof(cmdline);
        out->print(F("\r\nScanning frequency range from : "));
        out->print(scanFrom);
        out->print(F(" MHz to "));
        out->print(scanTo);
        out->print(F(" MHz, press any key for stop or wait...\r\n"));
        ELECHOUSE_cc1101.Init();
        ELECHOUSE_cc1101.setRxBW(58);
        ELECHOUSE_cc1101.SetRx();
        scanCursor = scanFrom;
        scanBestFreq = 0; scanBestRssi = RSSI_NONE; scanCompare = 0;
        activeMode = MODE_SCAN;


    // handling SAVE command
    } else if (strcmp_P(command, PSTR("save")) == 0) {
        //start saving recording buffer content into EEPROM non-volatile memory 
        out->print(F("\r\nSaving recording buffer content into the non-volatile memory...\r\n"));
        
        for (setting=0; setting<EPROMSIZE ; setting++)  
           {  // copying byte after byte from SRAM to EEPROM
            EEPROM.write(setting, bigrecordingbuffer[setting] );
           };
        // commit the writes (flash-simulated EEPROM needs this)
        EEPROM.commit();
        // print confirmation
        out->print(F("\r\nSaving complete.\r\n\r\n"));
        yield();
        
                 
    // handling LOAD command
    } else if (strcmp_P(command, PSTR("load")) == 0) {
        // flush the recording buffer and rewind its pointers first
        zeroRecordingBuffer();
        //start loading EEPROM non-volatile memory content into recording buffer
        out->print(F("\r\nLoading content from the non-volatile memory into the recording buffer...\r\n"));
        
        for (setting=0; setting<EPROMSIZE ; setting++)
           { // copying byte after byte from EEPROM to SRAM
            bigrecordingbuffer[setting] = EEPROM.read(setting);
           }
        // reconstruct the frame count by walking the length-prefixed frames,
        // otherwise 'show'/'play' would see 0 frames and do nothing.
        // (RAW captures have no framing - use 'showraw'/'playraw' for those.)
        framesinbigrecordingbuffer = 0;
        for (int p = 0; p < EPROMSIZE; )
           {
            int flen = bigrecordingbuffer[p];
            if ((flen <= 0) || (flen > 60) || (p + 1 + flen > EPROMSIZE)) break;
            framesinbigrecordingbuffer++;
            p += 1 + flen;
           }
        out->print(F("\r\nLoading complete. Enter 'show' or 'showraw' to see the buffer content.\r\n\r\n"));
        yield();
                  


    // Handling RX command         
       } else if (strcmp_P(command, PSTR("rx")) == 0) {
        out->print(F("\r\nReceiving and printing RF packet changed to "));
        if (activeMode == MODE_RX) { activeMode = MODE_IDLE; out->print(F("Disabled")); }
        else { ELECHOUSE_cc1101.SetRx(); out->print(F("Enabled")); activeMode = MODE_RX; }
        out->print(F("\r\n"));
        yield();


    // Handling CHAT command
       } else if (strcmp_P(command, PSTR("chat")) == 0) {
        out->print(F("\r\nEntering chat mode:\r\n\r\n"));
        activeMode = MODE_CHAT;
        yield();


    // Handling JAM command
       } else if (strcmp_P(command, PSTR("jam")) == 0) {
        out->print(F("\r\nJamming changed to "));
        if (activeMode == MODE_JAM) { out->print(F("Disabled")); activeMode = MODE_IDLE; }
        else { out->print(F("Enabled")); activeMode = MODE_JAM; }
        out->print(F("\r\n"));
        yield();
    
    // handling BRUTE command
    } else if (strcmp_P(command, PSTR("brute")) == 0) {
      
        // take interval period for sampling
        setting = atoi(strsep(&cmdline, " "));
        // take number of bits for brute forcing
        setting2 = atoi(cmdline);
        // calculate power of 2 upon setting (guard the shift: 1..16 bits;
        // counters are 32-bit so 2^16 fits and the loop runs to completion)
        if (setting2 > 16) setting2 = 16;
        poweroftwo = (setting2 > 0) ? (1UL << setting2) : 0;

        if ((setting>0) && (setting2>0))
        {
        // setup async mode on CC1101 and go into TX mode with GDO0 processing
        enterRawMode(true);

        //start playing RF with setting GDO0 bit state with bitbanging
        out->print(F("\r\nStarting Brute Forcing press any key to stop...\r\n"));
        pinMode(gdo0, OUTPUT);

        for (brute = 0; brute < poweroftwo ; brute++)  
           { 
           for(int k = 0; k <  5; k++)  // sending 5 times each code
             {
             for(int j = (setting2 - 1); j > -1; j--)  // j bits in a value brute
               {
                 digitalWrite(gdo0, bitRead(brute, j)); // Set GDO0 according to actual brute force value
                 delayMicroseconds(setting);            // delay for selected sampling interval
               }; // end of J loop
               // watchdog
               yield(); 
               // feed the watchdog in ESP8266
               ESP.wdtFeed();                    
             };  // end of K loop
             // checking if key pressed
             if (Serial.available()) break;
           };           

        out->print(F("\r\nBrute forcing complete.\r\n\r\n"));

        // setting normal pkt format again
        exitRawMode(true);
        } // end of IF

        else { out->print(F("Wrong parameters.\r\n")); };

  

    // Handling TX command         
       } else if (strcmp_P(command, PSTR("tx")) == 0) {
        // convert hex array to set of bytes
        if ( ingestHex(cmdline, ccsendingbuffer, &len) )
        {
                ccsendingbuffer[len] = 0x00;
                out->print(F("\r\nTransmitting RF packets.\r\n"));
                // send these data to radio over CC1101
                ELECHOUSE_cc1101.SendData(ccsendingbuffer, (byte)len);
                // echo back the frame that was sent
                asciitohex(ccsendingbuffer, textbuffer, len);
                out->print(F("Sent frame: "));
                out->print((char *)textbuffer);
                out->print(F("\r\n")); }
         else { out->print(F("Wrong parameters.\r\n")); };
        yield();



    // handling RECRAW command
    } else if (strcmp_P(command, PSTR("recraw")) == 0) {
        // take interval period for samplink
        setting = atoi(cmdline);
        if (setting>0)
        {
        // setup async mode on CC1101 with GDO0 pin processing
        enterRawMode(false);

        //start recording to the buffer with bitbanging of GDO0 pin state
        out->print(F("\r\nWaiting for radio signal to start RAW recording...\r\n"));
        pinMode(gdo0, INPUT);

        // prime the GDO0 read (some boards emit noise at the start)
        setting2 = digitalRead(gdo0);
        delayMicroseconds(1000);

        // wait (feeding the watchdog) until the RF line goes high
        while ( digitalRead(gdo0) == LOW )
                {  yield();
                   // feed the watchdog in ESP8266
                   ESP.wdtFeed();
                };

        //start recording to the buffer with bitbanging of GDO0 pin state
        out->print(F("\r\nStarting RAW recording to the buffer...\r\n"));
        pinMode(gdo0, INPUT);

        // start recording RF signal
        for (int i=0; i<RECORDINGBUFFERSIZE ; i++)
           { 
             byte receivedbyte = 0;
             for(int j=7; j > -1; j--)  // 8 bits in a byte
               {
                 bitWrite(receivedbyte, j, digitalRead(gdo0)); // Capture GDO0 state into the byte
                 delayMicroseconds(setting);                   // delay for selected sampling interval
               }; 
                 // store the output into recording buffer
             bigrecordingbuffer[i] = receivedbyte;
             // feed the watchdog in ESP8266
             ESP.wdtFeed();
           }

        out->print(F("\r\nRecording RAW data complete.\r\n\r\n"));
        // setting normal pkt format again
        exitRawMode(false);
        // feed the watchdog
        ESP.wdtFeed();
        // needed for ESP8266
        yield();
        }
        else { out->print(F("Wrong parameters.\r\n")); };

   // handling RXRAW command - sniffer
    } else if (strcmp_P(command, PSTR("rxraw")) == 0) {
        // take interval period for samplink
        setting = atoi(cmdline);
        if (setting>0)
        {
        // setup async mode on CC1101 with GDO0 pin processing
        enterRawMode(false);
        //start recording to the buffer with bitbanging of GDO0 pin state
        out->print(F("\r\nSniffer enabled...\r\n"));
        pinMode(gdo0, INPUT);

       // Any received char over Serial port stops printing  RF received bytes
        while (!Serial.available())
           {  
             
             // we have to use the buffer not to introduce delays
             for (int i=0; i<RECORDINGBUFFERSIZE ; i++)  
                { 
                  byte receivedbyte = 0;
                  for(int j=7; j > -1; j--)  // 8 bits in a byte
                    {
                       bitWrite(receivedbyte, j, digitalRead(gdo0));  // Capture GDO0 state into the byte
                       delayMicroseconds(setting);                    // delay for selected sampling interval
                    }; 
                    // store the output into recording buffer
                    bigrecordingbuffer[i] = receivedbyte;
                    // feed the watchdog
                    ESP.wdtFeed();
                  }; 
             // feed the watchdog
             ESP.wdtFeed();
             // needed for ESP8266
             yield();

             // when buffer full print the output to serial port
             dumpBufferHex(0, RECORDINGBUFFERSIZE);

           }; // end of While loop

        out->print(F("\r\nStopping the sniffer.\r\n\r\n"));

        // setting normal pkt format again
        exitRawMode(false);
        }
        else { out->print(F("Wrong parameters.\r\n")); };


    // handling PLAYRAW command
    } else if (strcmp_P(command, PSTR("playraw")) == 0) {
        // take interval period for sampling
        setting = atoi(cmdline);
        if (setting>0)
        {
        // setup async mode on CC1101 and go into TX mode with GDO0 processing
        enterRawMode(true);
        //start replaying GDO0 bit state from data in the buffer with bitbanging
        out->print(F("\r\nReplaying RAW data from the buffer...\r\n"));
        pinMode(gdo0, OUTPUT);

        // start RF replay
        for (int i=1; i<RECORDINGBUFFERSIZE ; i++)
           { 
             byte receivedbyte = bigrecordingbuffer[i];
             for(int j=7; j > -1; j--)  // 8 bits in a byte
               {
                 digitalWrite(gdo0, bitRead(receivedbyte, j)); // Set GDO0 according to recorded byte
                 delayMicroseconds(setting);                      // delay for selected sampling interval
               }; 
              // feed the watchdog
              ESP.wdtFeed();
           };

        out->print(F("\r\nReplaying RAW data complete.\r\n\r\n"));
        // setting normal pkt format again
        exitRawMode(true);
        // feed the watchdog
        ESP.wdtFeed();
        // needed for ESP8266
        yield();
        }
        else { out->print(F("Wrong parameters.\r\n")); };

    // handling SHOWRAW command
    } else if (strcmp_P(command, PSTR("showraw")) == 0) {
    // show the content of recorded RAW signal as hex numbers
       out->print(F("\r\nRecorded RAW data:\r\n"));
       dumpBufferHex(0, RECORDINGBUFFERSIZE);
       out->print(F("\r\n\r\n"));
       // feed the watchdog
       ESP.wdtFeed();
       // needed for ESP8266
       yield();
      


    // handling SHOWBIT command
    } else if (strcmp_P(command, PSTR("showbit")) == 0) {
    // show the content of recorded RAW signal as hex numbers
       out->print(F("\r\nRecorded RAW data as bit stream:\r\n"));
       for (int i = 0; i < RECORDINGBUFFERSIZE ; i = i + 32)  
           {        // first convert to hex numbers
                    asciitohex((byte *)&bigrecordingbuffer[i], (byte *)textbuffer,  32);
                    // now decode as binary and print
                    for (setting = 0; setting < 64 ; setting++)
                        {
                        setting2 = textbuffer[setting];
                        switch( setting2 )
                              {
                              case '0':
                              out->print(F("____"));
                              break;
   
                              case '1':
                              out->print(F("___-"));
                              break;
   
                              case '2':
                              out->print(F("__-_"));
                              break;

                              case '3':
                              out->print(F("__--"));
                              break;

                              case '4':
                              out->print(F("_-__"));
                              break;

                              case '5':
                              out->print(F("_-_-"));
                              break;

                              case '6':
                              out->print(F("_--_"));
                              break;

                              case '7':
                              out->print(F("_---"));
                              break;

                              case '8':
                              out->print(F("-___"));
                              break;

                              case '9':
                              out->print(F("-__-"));
                              break;

                              case 'A':
                              out->print(F("-_-_"));
                              break;

                              case 'B':
                              out->print(F("-_--"));
                              break;

                              case 'C':
                              out->print(F("--__"));
                              break;

                              case 'D':
                              out->print(F("--_-"));
                              break;

                              case 'E':
                              out->print(F("---_"));
                              break;

                              case 'F':
                              out->print(F("----"));
                              break;
                              
                              }; // end of switch
                              
                        }; // end of for
              // feed the watchdog
              ESP.wdtFeed();
              // needed for ESP8266   
              yield();      
   
              } // end of for
              out->print(F("\r\n\r\n"));


    // Handling ADDRAW command         
       } else if (strcmp_P(command, PSTR("addraw")) == 0) {
        // getting hex numbers - the content of the  frame
        // convert hex array to set of bytes
        if ( ingestHex(cmdline, textbuffer, &len) )
        {
                // check if the frame fits into the buffer and store it
                if (( bigrecordingbufferpos + len) < RECORDINGBUFFERSIZE)
                     { // copy current frame and increase pointer for next frames
                      memcpy(&bigrecordingbuffer[bigrecordingbufferpos], &textbuffer, len );
                      // increase position in big recording buffer for next frame
                      bigrecordingbufferpos = bigrecordingbufferpos + len; 
                      out->print(F("\r\nChunk added to recording buffer\r\n\r\n"));
                    }   
               else                  
                   {   
                     out->print(F("\r\nBuffer is full. The frame does not fit.\r\n "));
                   };
        }  
        else { out->print(F("Wrong parameters.\r\n")); };
        // needed for ESP8266   
        yield();      


        
    // Handling REC command         
    } else if (strcmp_P(command, PSTR("rec")) == 0) {
        out->print(F("\r\nRecording mode set to "));
        if (activeMode == MODE_REC) {
            out->print(F("Disabled")); bigrecordingbufferpos = 0; activeMode = MODE_IDLE;
        } else {
            ELECHOUSE_cc1101.SetRx(); out->print(F("Enabled"));
            zeroRecordingBuffer();
            activeMode = MODE_REC;
        }
        out->print(F("\r\n"));
        yield();
 

    // Handling PLAY command         
       } else if (strcmp_P(command, PSTR("play")) == 0) {
        setting = atoi(strsep(&cmdline, " "));
        // if number of played frames is 0 it means play all frames
        if ((setting >= 0) && (setting <= framesinbigrecordingbuffer))
        {
          out->print(F("\r\nReplaying recorded frames.\r\n "));
          // rewind recording buffer position to the beginning
          bigrecordingbufferpos = 0;
          if (framesinbigrecordingbuffer >0)
          {

            // start reading and sending frames from the buffer : FIFO
            for (int i=1; i<=framesinbigrecordingbuffer ; i++)  
               { 
                 // read length of the recorded frame first from the buffer
                 len = bigrecordingbuffer[bigrecordingbufferpos];
                 if ( ((len<=60) and (len>0)) and ((i == setting) or (setting == 0))  )
                 { 
                    // take next frame from the buffer  for replay
                    memcpy(ccsendingbuffer, &bigrecordingbuffer[bigrecordingbufferpos + 1], len );      
                    // send these data to radio over CC1101
                    ELECHOUSE_cc1101.SendData(ccsendingbuffer, (byte)len);
                 };
                  // increase position to the buffer and check exception
                  bigrecordingbufferpos = bigrecordingbufferpos + 1 + len;
                  if ( bigrecordingbufferpos > RECORDINGBUFFERSIZE) break;
                  // feed the watchdog per frame (a big replay must not starve it)
                  ESP.wdtFeed();
                  // needed for ESP8266
                  yield();
               };

             }; // end of IF framesinrecordingbuffer
        
          // rewind buffer position
          bigrecordingbufferpos = 0;
          out->print(F("Done.\r\n"));       
        }
         else { out->print(F("Wrong parameters.\r\n")); };


    // Handling ADD command         
       } else if (strcmp_P(command, PSTR("add")) == 0) {
        // getting hex numbers - the content of the  frame
        // convert hex array to set of bytes
        if ( ingestHex(cmdline, textbuffer, &len) )
        {
                // check if the frame fits into the buffer and store it
                if (( bigrecordingbufferpos + len + 1) < RECORDINGBUFFERSIZE)
                     { // put info about number of bytes
                      bigrecordingbuffer[bigrecordingbufferpos] = len; 
                      bigrecordingbufferpos++;
                      // next - copy current frame and increase 
                      memcpy(&bigrecordingbuffer[bigrecordingbufferpos], &textbuffer, len );
                      // increase position in big recording buffer for next frame
                      bigrecordingbufferpos = bigrecordingbufferpos + len; 
                      // increase counter of frames stored
                      framesinbigrecordingbuffer++;
                      out->print(F("\r\nAdded frame number "));
                      out->print(framesinbigrecordingbuffer);
                      out->print(F("\r\n"));                  
                    }   
               else                  
                   {   
                     out->print(F("\r\nBuffer is full. The frame does not fit.\r\n "));
                   };
        }  
        else { out->print(F("Wrong parameters.\r\n")); };
        // needed for ESP8266   
        yield();      
       

    // Handling SHOW command         
       } else if (strcmp_P(command, PSTR("show")) == 0) {
         if (framesinbigrecordingbuffer>0)
        {
          out->print(F("\r\nFrames stored in the recording buffer:\r\n "));
          // rewind recording buffer position to the beginning
          bigrecordingbufferpos = 0;
          // start reading and sending frames from the buffer : FIFO
          for (setting=1; setting<=framesinbigrecordingbuffer; setting++)  
               { 
                 // read length of the recorded frame first from the buffer
                 len = bigrecordingbuffer[bigrecordingbufferpos];
                 if ((len<=60) and (len>0))
                 { 
                    // take next frame from the buffer  for replay
                    // flush textbuffer
                    for (setting2 = 0; setting2 < BUF_LENGTH; setting2++)
                        { textbuffer[setting2] = 0; };           
                    asciitohex(&bigrecordingbuffer[bigrecordingbufferpos + 1], textbuffer,  len);
                    out->print(F("\r\nFrame "));
                    out->print(setting);
                    out->print(F(" : "));                     
                    out->print((char *)textbuffer);
                    out->print(F("\r\n"));
                 };
                    // increase position to the buffer and check exception
                    bigrecordingbufferpos = bigrecordingbufferpos + 1 + len;
                    if ( bigrecordingbufferpos > RECORDINGBUFFERSIZE) break;
                    // feed the watchdog
                    ESP.wdtFeed();
                 // 
               };
          // rewind buffer position
          // bigrecordingbufferpos = 0;
          out->print(F("\r\n")); 
        }
         else { out->print(F("Wrong parameters.\r\n")); };
        // needed for ESP8266   
        yield();      


    // Handling FLUSH command         
    } else if (strcmp_P(command, PSTR("flush")) == 0) {
        // flush the recording buffer and rewind its pointers
        zeroRecordingBuffer();
        out->print(F("\r\nRecording buffer cleared.\r\n"));
        // needed for ESP8266   
        yield();      
          
       
    // Handling ECHO command         
    } else if (strcmp_P(command, PSTR("echo")) == 0) {
        do_echo = atoi(cmdline);

    // Handling X command         
    // command 'x' stops jamming, receiveing, recording...
    } else if (strcmp_P(command, PSTR("x")) == 0) {
        activeMode = MODE_IDLE;
        out->print(F("\r\n"));
        // needed for ESP8266
        yield();

    // Handling INIT command         
    // command 'init' initializes board with default settings
    } else if (strcmp_P(command, PSTR("init")) == 0) {
        // init cc1101
        cc1101initialize();
        // give feedback
        out->print(F("CC1101 initialized\r\n"));
          
    } else {
        out->print(F("Error: Unknown command: "));
        out->println(command);
        // needed for ESP8266
        yield();
    }
}


// Advance whichever long-running mode is active by one small slice.
// Called every loop() pass. Keeps the web server responsive.
static void serviceActiveMode(void)
{
    if (activeMode == MODE_SCAN) {
        ELECHOUSE_cc1101.setMHZ(scanCursor);
        int rssi = ELECHOUSE_cc1101.getRssi();
        if (rssi > SCAN_RSSI_MARK && rssi > scanBestRssi) {
            scanBestRssi = rssi; scanBestFreq = scanCursor;
        }
        scanCursor += 0.01;
        if (scanCursor > scanTo) {
            scanCursor = scanFrom;
            if (scanBestRssi > SCAN_RSSI_MARK) {
                long fr = scanBestFreq * 100;
                if (fr == scanCompare) {
                    out->print(F("\r\nSignal found at  "));
                    out->print(F("Freq: "));  out->print(scanBestFreq);
                    out->print(F(" Rssi: ")); out->println(scanBestRssi);
                    scanBestRssi = RSSI_NONE; scanCompare = 0; scanBestFreq = 0;
                } else {
                    scanCompare = scanBestFreq * 100;
                    scanCursor = scanBestFreq - 0.10;
                    scanBestFreq = 0; scanBestRssi = RSSI_NONE;
                }
            }
        }
    }
}


void setup() {

     // initialize USB Serial Port CDC
     Serial.begin(115200);

     Serial.println(F("CC1101 terminal tool connected, use 'help' for list of commands...\n\r"));
     Serial.println(F("(C) Adam Loboda 2023\n\r  "));

     Serial.println();  // print CRLF

    // Init flash-simulated EEPROM (ESP8266)
     EEPROM.begin(EPROMSIZE);

     // seed the PRNG once for the 'jam' random payloads
     randomSeed(analogRead(0));

     // initialize CC1101 module with preffered parameters
     cc1101initialize();

      if (ELECHOUSE_cc1101.getCC1101()) {  // Check the CC1101 Spi connection.
      Serial.println(F("cc1101 initialized. Connection OK\n\r"));
      } else {
      Serial.println(F("cc1101 connection error! check the wiring.\n\r"));
      };
    
      // setup variables
     bigrecordingbufferpos = 0;

     // Enable the ESP8266 software watchdog as a safety net
     ESP.wdtEnable(5000);

     // bring up WiFi AP and the web control panel
     startAP();
     setupWebServer();
}


void loop() {

  // index for serial port characters
  int i = 0;

   // feed the watchdog in ESP8266
   ESP.wdtFeed();

   // service pending HTTP requests
   server.handleClient();

   // any serial input stops a running background mode (preserves old UX)
   if (activeMode == MODE_SCAN || activeMode == MODE_SNIFF || activeMode == MODE_BRUTE) {
       if (Serial.available()) { activeMode = MODE_IDLE; }
   }

    /* Process incoming commands. */
    while (Serial.available()) {
        static char buffer[BUF_LENGTH];
        static int length = 0;


    // handling CHAT MODE     
    if (activeMode == MODE_CHAT)
       {
            
            // clear serial port buffer index
            i = 0;

            // something was received over serial port put it into radio sending buffer
            while (Serial.available() and (i<(CCBUFFERSIZE-1)) ) 
             {

              // read single character from Serial port         
              ccsendingbuffer[i] = Serial.read();

              // also put it as ECHO back to serial port
              Serial.write(ccsendingbuffer[i]);

              // if CR was received add also LF character and display it on Serial port
              if (ccsendingbuffer[i] == 0x0d )
                  {  
                    Serial.write( 0x0a );
                    i++;
                    ccsendingbuffer[i] = 0x0a;
                  }
              //
              
              // increase CC1101 TX buffer position
              i++;
             };

            // clamp: a trailing CR appends an extra LF and can push i past the
            // last index, so keep the NUL write inside ccsendingbuffer[]
            if (i > (CCBUFFERSIZE - 1)) i = CCBUFFERSIZE - 1;
            // put NULL at the end of CC transmission buffer
            ccsendingbuffer[i] = '\0';

            // send these data to radio over CC1101
            ELECHOUSE_cc1101.SendData((char *)ccsendingbuffer);

            // feed the watchdog
            ESP.wdtFeed();
            // needed for ESP8266   
            yield();      
                           
       }
    // handling CLI commands processing
    else
      {   
        int data = Serial.read();
        if (data == '\b' || data == '\177') {  // BS and DEL
            if (length) {
                length--;
                if (do_echo) Serial.write("\b \b");
              // feed the watchdog
              ESP.wdtFeed();
            }
        }
        else if (data == '\r' || data == '\n' ) {
            if (do_echo) Serial.write("\r\n");    // output CRLF
            buffer[length] = '\0';
            if (length) exec(buffer);
            length = 0;
            // feed the watchdog
            ESP.wdtFeed();

        }
        else if (length < BUF_LENGTH - 1) {
            buffer[length++] = data;
            if (do_echo) Serial.write(data);
            // feed the watchdog
            ESP.wdtFeed();
        }
       };  
      // end of handling CLI processing
        
    };

  /* Process RF received packets */
   
   //Checks whether something has been received.
  if (ELECHOUSE_cc1101.CheckReceiveFlag() &&
      (activeMode == MODE_RX || activeMode == MODE_REC || activeMode == MODE_CHAT) )
      {

       //CRC Check. If "setCrc(false)" crc returns always OK!
       if (ELECHOUSE_cc1101.CheckCRC())
          { 
            // feed the watchdog
            ESP.wdtFeed();

            //Get received Data and calculate length
            int len = ELECHOUSE_cc1101.ReceiveData(ccreceivingbuffer);

            // Actions for CHAT MODE
            if ( (activeMode == MODE_CHAT) && (len < CCBUFFERSIZE ) )
               {
                // put NULL at the end of char buffer
                ccreceivingbuffer[len] = '\0';
                //Print received in char format.
                Serial.print((char *) ccreceivingbuffer);
                // feed the watchdog
                // ESP.wdtFeed();
                // needed for ESP8266   
                yield();      

               };  // end of handling Chat mode

            // Actions for RECEIVNG MODE
            if ( (activeMode == MODE_RX) && (len < CCBUFFERSIZE ) )
               {
                   // put NULL at the end of char buffer
                   ccreceivingbuffer[len] = '\0';
                   // flush textbuffer
                   for (int i = 0; i < BUF_LENGTH; i++)
                    { textbuffer[i] = 0; };
                   
                   //Print received packet as set of hex values directly 
                   // not to loose any data in buffer
                   // asciitohex((byte *)ccreceivingbuffer, (byte *)textbuffer,  len);
                   asciitohex(ccreceivingbuffer, textbuffer,  len);
                   Serial.print((char *)textbuffer);
                   // set RX  mode again
                   ELECHOUSE_cc1101.SetRx();
                   // feed the watchdog
                   ESP.wdtFeed();
                   // needed for ESP8266   
                   yield();                        
                };   // end of handling receiving mode 

            // Actions for RECORDING MODE               
            if ( (activeMode == MODE_REC) && (len < CCBUFFERSIZE ) )
               { 
                // copy the frame from receiving buffer for replay - only if it fits
                if (( bigrecordingbufferpos + len + 1) < RECORDINGBUFFERSIZE) 
                     {
                      // put info about number of bytes
                      bigrecordingbuffer[bigrecordingbufferpos] = len; 
                      bigrecordingbufferpos++;
                      // next - copy current frame and increase 
                      memcpy(&bigrecordingbuffer[bigrecordingbufferpos], ccreceivingbuffer, len );
                      // increase position in big recording buffer for next frame
                      bigrecordingbufferpos = bigrecordingbufferpos + len; 
                      // increase counter of frames stored
                      framesinbigrecordingbuffer++;
                      // set RX  mode again
                      ELECHOUSE_cc1101.SetRx();
                      // feed the watchdog
                      ESP.wdtFeed();
                      // needed for ESP8266   
                      yield();                            
                     }
                     
                else {
                    Serial.print(F("Recording buffer full! Stopping..\r\nFrames stored: "));
                    Serial.print(framesinbigrecordingbuffer); 
                    Serial.print(F("\r\n"));
                    bigrecordingbufferpos = 0;
                    activeMode = MODE_IDLE;
                    // feed the watchdog
                    ESP.wdtFeed();
                    // needed for ESP8266   
                    yield();                          
                     };
                
               };   // end of handling frame recording mode 
 
          };   // end of CRC check IF


      };   // end of Check receive flag if

      // if jamming mode activate continously send something over RF...
      if ( activeMode == MODE_JAM )
      {
        // populate cc1101 sending buffer with random values (PRNG seeded in setup)
        for (i = 0; i<MAX_PAYLOAD; i++)
           { ccsendingbuffer[i] = (byte)random(255);
             // feed the watchdog
             ESP.wdtFeed();
             // needed for ESP8266
             yield();
           };
        // send these data to radio over CC1101
        ELECHOUSE_cc1101.SendData(ccsendingbuffer,MAX_PAYLOAD);
        // feed the watchdog
        ESP.wdtFeed();
        // needed for ESP8266   
        yield();              
      };

   // advance any active long-running mode by one slice
   serviceActiveMode();

   // give control for other procedures in ESP8266
   yield();
 
 
}  // end of main LOOP

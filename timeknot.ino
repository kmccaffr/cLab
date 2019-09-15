/**************************************************************************************************
 * File Name      : timeknot.ino
 * Author         : Kyle McCaffrey
 * Version        : 1.10
 * Date           : 8/01/2019
 * Description:   : The main section of code handles FTP data write/read to a server, file lookup,
 *                  and playback/recording features. Additionally, an accelerometer and RGB LED
 *                  are used to provide or receive feedback.
 *
 * ===============================================================================================
 * Referenced Authors: Adafruit (Example Code), Particle - Peekay123 (Ported FTP Code)
 *************************************************************************************************/

/*********************************************************
*                  Included headers                      *
*********************************************************/
#include "Adafruit_VS1053_Photon.h"
#include "sd-card-library-photon-compat.h"
#include <Adafruit_Sensor.h>
#include <Adafruit_ADXL345_U.h>
#include "math.h"

/*********************************************************
*                  Define pin constants                  *
*********************************************************/

// SPI communication pins
#define PHOTON_SCK      A3
#define PHOTON_MISO     A4
#define PHOTON_MOSI     A5

// VS1053 data pins
#define VS1053_RESET    D6      // VS1053 reset pin (output)
#define VS1053_CS       A2      // VS1053 chip select pin (output)
#define VS1053_DCS      D5      // VS1053 Data/command select pin (output)
#define CARDCS          D3      // Card chip select pin
#define DREQ            WKP     // VS1053 Data request, ideally an Interrupt pin

// Switch state pins
#define SP3REC          D4      // For recording audio messsages and writing to FTP server
#define SP3PLAY         D7      // For playback of audio
#define SP3DATA         DAC     // For buffering of FTP server and receiving data

// Button input variables
#define BUTTON_1        3        // Record(REC) / Play and Pause (PLAY) / Manually check data (DATA)
#define BUTTON_2        4        // Write FTP Recording and Reload Codec (REC) / Forward (PLAY)
#define BUTTON_3        5        // Backward (PLAY) / Cancel (REC)
#define BUTTON_4        6        // Volume Up (PLAY)
#define BUTTON_5        7        // Volume Down (PLAY)

// RGB LED pins
#define LED_R           D2      // Red LED in sensor
#define LED_G           D1      // Green LED in sensor
#define LED_B           D0      // Blue LED in sensor

// ADXL pins
#define ADXL_CS         A0      // SPI chip-select pin for accelerometer

// Define bitshifting(or casting) for recording
#define _BV(bit) (1 << (bit))

/*********************************************************
*            Create objects/ Global Variables            *
*********************************************************/

// Create VS1053 object for playback/recording/accessing microSD
Adafruit_VS1053_FilePlayer musicPlayer = Adafruit_VS1053_FilePlayer(PHOTON_MOSI, PHOTON_MISO,
                                                                    PHOTON_SCK, VS1053_RESET,
                                                                    VS1053_CS, VS1053_DCS,
                                                                    DREQ, CARDCS);

// Create accelerometer object
Adafruit_ADXL345_Unified accel = Adafruit_ADXL345_Unified(PHOTON_SCK, PHOTON_MISO, PHOTON_MOSI, ADXL_CS, 12345);

// Define FTPSTATE
bool FTPSTATE;

// IP Address of drivehq server
IPAddress server( 66, 220, 9, 50 );

// FTP server information
String userFTP = "###";
String passFTP = "###";
String downloadFolderFTP = "###";
String uploadFolderFTP = "###";

// File list size
const int FILELISTSIZE = 50;

// Define tcp client commands object
TCPClient client;
// Define tcp client data object
TCPClient dclient;

// Char buffer for data input/output
char outBuf[128];
char outCount;

// Global define of FTP ported functions
byte doFTP();
byte eRcv();
void efail();

// 8.3 format filename
String fileList[FILELISTSIZE]; // FTP list output
String sdList[FILELISTSIZE]; // SD list output
char fileName[13];
File root; // Passthrough for root directory
File fh; // FTP

// SD file creation date structure for update function
sdFatDate dateStorage;
// FTP server file upload date
sdFatDate serverFileDate;

// Playback variables
bool playing = false;
uint8_t buttonState, prevButtonState; // Play debounce variables
int leftVolume, rightVolume;

// Recording variables
File recording;  // the file we will save our recording to
#define RECBUFFSIZE 64  // 64 or 128 bytes.
uint8_t recording_buffer[RECBUFFSIZE];
uint8_t isRecording = false; // Initial declaring of recording check

// Vancouver timezone with DST offset
int timeZoneValue = -7;

// Unix Time Thresholds
time_t torontoToVancouver = 75600;
time_t vancouverToToronto = 10800;
time_t weekThreshold = 604800;
// Timer variables
time_t current;
// Declare constants for directory time
time_t tval;
// Update timer variable
time_t updateTimer;
// Update timer variable LED
time_t updateTimerLED;
// Schedule update timer
time_t scheduleTimer;
// Accelerometer timer
time_t accelResetTimer;
// File Record Name
char fileREC[13];
// Record flag variable;
bool recSend = false;
// Playing iterator
int currentlyPlaying = 0;
// Initial play state SD setup
bool playStateInit = false;
// Playback GPIO
bool playGPIO = false;
// Accelerometer variables
float xBias = 0;
float yBias = 0;
float zBias = 0;
float triggerCompare = 15;
bool accelError = false;

int ledUpdateMinute = 15;
int dataUpdateMinute = 20;
int motionUpdateMinute = 1;
int scheduleUpdate = 1;

bool artifactMotion = false; // This is to simplify the artifacts (Motion, LED, Weekly)
bool artifactAmbient = false;
bool artifactWeekly = false;

bool accelReset = false;
bool newMessage = false;

bool newToOldSort = true;
bool activeMessage[FILELISTSIZE]; // List of whether a message has been played
int playSchedule[FILELISTSIZE]; // Corresponds to either Morning 1 (>=05:00 - <12:00), Afternoon 2 (>=12:00 - <18:00), Evening 3 (>= 18:00 - 23:59), Late 4 (00:00 - <05:00)
String activeFiles[FILELISTSIZE]; // Associated files for newMessage
String playableTracks[FILELISTSIZE];

bool WIFIOFFLINE = false; // Can change to allow offline capability
// SYSTEM_THREAD(ENABLED); // REMOVE THE COMMENT LINES IF YOU WANT TO USE OFFLINE - THIS NEEDS TO BE DEFINED

/*********************************************************
*                  Initial Setup Function                *
*********************************************************/
void setup()
{
// Set local time zone for MCU
Time.zone(timeZoneValue);
int j = 0; // Error iterator

// Serial communication at 9600 baud rate for debug
Serial.begin(9600);


for (j=0; j<FILELISTSIZE; j++) { // Prepare led and schedule arrays

    activeMessage[j] = false;
    playSchedule[j] = 0;
    activeFiles[j] = "";
}


// LED
pinMode(LED_R, OUTPUT);
pinMode(LED_G, OUTPUT);
pinMode(LED_B, OUTPUT);
digitalWrite(LED_R, HIGH);
digitalWrite(LED_B, HIGH);
digitalWrite(LED_G, HIGH);

// Initialize the music player
Serial.println("Trying music player");
while (!musicPlayer.begin())
{
    Serial.println("VS1053 not found");
    delay(3000);
    j += 1;
    if (j >= 5)
    {
      break;
      ledColor(100, 255, 177); // Error
    }
}

j = 0;

Serial.println("Trying SD");
// Initialization of SD card
while(!SD.begin(PHOTON_MOSI,PHOTON_MISO,PHOTON_SCK,CARDCS))
{
    Serial.println("SD init fail");
    delay(3000);
    j += 1;
    if (j >= 5)
    {
      break;
      ledColor(100, 255, 177); // Error
    }
}

leftVolume = 20;
rightVolume = 20;

/************************************************************* MOTION ARTIFACT CHECK */
if (artifactMotion)
{

delay(2000); // Time buffer for accelerometer to power on
int i = 0;
/* Initialise the sensor */
while(!accel.begin())
{
  if (accel.begin()) {
    break;
    accelError = false;
  }
  else {
    i++;
    delay(500);
  }
  if(i > 4) {
    break;
    accelError = true;
    ledColor(100, 255, 177); // Error
  }
  /* There was a problem detecting the ADXL345 ... check your connections */
  Serial.println("Ooops, no ADXL345 detected ... Check your wiring!");
}
/* Set the range to whatever is appropriate for your project */
accel.setRange(ADXL345_RANGE_2_G);
Serial.println("");
float xVal = 0.0;
float yVal = 0.0;
float zVal = 0.0;
i = 0;
for (i = 0; i<500; i++){
  xVal = xVal + accel.getX() * ADXL345_MG2G_MULTIPLIER;
  yVal = yVal + accel.getY() * ADXL345_MG2G_MULTIPLIER;
  zVal = zVal + accel.getZ() * ADXL345_MG2G_MULTIPLIER;
}
xBias = xVal/500.0;
yBias = yVal/500.0;
zBias = zVal/500.0;
Serial.print("XBIAS: "); Serial.println(xBias);
Serial.print("YBIAS: "); Serial.println( yBias);
Serial.print("ZBIAS: "); Serial.println(zBias);
}

/************************************************************* MOTION ARTIFACT CHECK */

// Set volume for left, right channels. lower numbers == louder volume!
musicPlayer.setVolume(leftVolume, rightVolume);

// WKP interrupt pin for playback
if(!musicPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT)) {
  Serial.println("DREQ is not functioning");
}  // DREQ int

// Declare playback button states
buttonState = LOW;
prevButtonState = LOW;

// Setting state switch pin mode
pinMode(SP3REC, INPUT_PULLUP);
pinMode(SP3DATA, INPUT_PULLUP);
pinMode(SP3PLAY, INPUT_PULLUP);

// Setting VS1053 GPIO pin mode
musicPlayer.GPIO_pinMode(BUTTON_1, INPUT);
musicPlayer.GPIO_pinMode(BUTTON_2, INPUT);
musicPlayer.GPIO_pinMode(BUTTON_3, INPUT);
musicPlayer.GPIO_pinMode(BUTTON_4, INPUT);
musicPlayer.GPIO_pinMode(BUTTON_5, INPUT);

/************************************************************* MOTION ARTIFACT CHECK */
if (artifactMotion)
{
pinMode(ADXL_CS, OUTPUT);
}
/************************************************************* MOTION ARTIFACT CHECK */
delay(5000);

Serial.println("Sound Test");
// Setup is complete
musicPlayer.sineTest(0x44, 500);

time_t tempTime = Time.now();
// Check update timer
updateTimerLED = tempTime; // LED
updateTimer = tempTime; // Data
accelResetTimer = tempTime;
scheduleTimer = tempTime;
}

/*********************************************************
*                 Continous Loop Function                *
*********************************************************/
void loop()
{
  // Main update time
  time_t currentTime = Time.now();
// LED update
/************************************************************* AMBIENT ARTIFACT CHECK */
// LED update

 if (Time.minute(currentTime - updateTimerLED) > ledUpdateMinute) {

   newMessage = false; // Reset

   int i = 0;
   for (i=0; i<FILELISTSIZE; i++)
   {
     if (activeMessage[i] == true)
     {
       newMessage = true;
       break;
     }
   }

   if (newMessage && (!recSend && !isRecording && !musicPlayer.playingMusic) )
   {
     ledColor(100, 100, 100); // ON
   }
   else if (!newMessage && (!recSend && !isRecording && !musicPlayer.playingMusic))
   {
     ledColor(255, 255, 255); // OFF
   }
   // ledNotification();
   updateTimerLED = currentTime;
 }

/************************************************************* AMBIENT ARTIFACT CHECK */

// Recording mode
if (!digitalRead(SP3REC))
{

  if (newMessage && (!recSend && !isRecording && !musicPlayer.playingMusic) )
  {
    ledColor(100, 100, 100); // ON
  }
  else if (!newMessage && (!recSend && !isRecording && !musicPlayer.playingMusic))
  {
    ledColor(255, 255, 255); // OFF
  }

  if (recSend)
  {
    ledColor(255, 255, 250); // Faint blue
  }

  FTPSTATE = true;
  playStateInit = false;
  if (!isRecording && musicPlayer.GPIO_digitalRead(BUTTON_1)) {
    Serial.println("Begin recording");
    isRecording = true;
    delay(1000);

    // Randomized number value since checking the ftp directory contents would take too long
    int randVal = random(0, 10000);
    String numberAdjusted = "REB";
    if ( randVal >= 1000 ) {
      numberAdjusted = numberAdjusted + "0" + String(randVal) + ".OGG";
    }
    if ( randVal >= 100 && randVal <= 999) {
      numberAdjusted = numberAdjusted + "00" + String(randVal) + ".OGG";
    }
    if ( randVal >= 10 && randVal <= 99) {
      numberAdjusted = numberAdjusted + "000" + String(randVal) + ".OGG";
    }
    if (randVal >= 0 && randVal <= 9) {
      numberAdjusted = numberAdjusted + "0000" + String(randVal) + ".OGG";
    }
    strcpy(fileREC, numberAdjusted);


    Serial.print("Recording to "); Serial.println(fileREC);
     SdFile::dateTimeCallback(DateTime);
    recording = SD.open(fileREC, FILE_WRITE);
    if (! recording) {
       Serial.println("Couldn't open file to record!");
      // while (1);
    }
    musicPlayer.prepareRecordOgg("codec/v16k1q05.img");
    musicPlayer.startRecordOgg(false); // use microphone (for linein, pass in 'false')
    delay(1000);
    ledColor(200, 255, 255); // Red
  }
  if (isRecording)
    saveRecordedData(isRecording);
  if (isRecording && musicPlayer.GPIO_digitalRead(BUTTON_1)) {
    Serial.println("End recording");
    delay(1000);
    musicPlayer.stopRecordOgg();
    isRecording = false;
    // flush all the data!
    saveRecordedData(isRecording);
    // close it up
    recording.close();
    recSend = true;
    ledColor(255, 255, 250); // Faintly Blue
    musicPlayer.sineTest(0x44, 500);
    delay(1000);
  }

  bool checkForInternet = false;

  if (WIFIOFFLINE)
  {
    checkForInternet = true;
  }

  if (!checkForInternet)
  {
  if (recSend && musicPlayer.GPIO_digitalRead(BUTTON_2)) // FTP send
    {
      delay(1000);
      doFTP(fileREC);
      recSend = false;
      SD.remove(fileREC);
      ledColor(255, 200, 255); // Green
      delay(500);
      ledColor(250, 250, 250); // Dimmly white
    }
  }
  else
  {
    if (WiFi.connecting() == false)
    {
      if (recSend && musicPlayer.GPIO_digitalRead(BUTTON_2)) // FTP send
        {
          delay(1000);
          doFTP(fileREC);
          recSend = false;
          SD.remove(fileREC);
          ledColor(255, 200, 255); // Green
          delay(500);
          ledColor(250, 250, 250); // Dimmly white
        }
    }
  }

  if (recSend && musicPlayer.GPIO_digitalRead(BUTTON_3))  // Cancel recording
  {
    delay(1000);
    recSend = false;
    SD.remove(fileREC);
    ledColor(255, 255, 200); // Blue
    delay(500);
    ledColor(250, 250, 250); // Dimmly white
  }
}

// FTP data download
else if (!digitalRead(SP3DATA))
{
  // LED NOTIFICATION
  if (newMessage)
  {
    ledColor(100, 100, 100); // Brighter White
  }

  bool checkForInternet = false;

  if (WIFIOFFLINE)
  {
    checkForInternet = true;
  }

  if (!checkForInternet)
  {

  FTPSTATE = false;
  playStateInit = false;
  if (Time.minute((currentTime - updateTimer)) > dataUpdateMinute) // Update files
  {
    Serial.println("File Management");
    ledColor(255, 200, 200); // Cyan
    /************************************************************* WEEKLY ARTIFACT CHECK */
    if (artifactWeekly)
    {
    fileManagement();
    }
    /************************************************************* WEEKLY ARTIFACT CHECK */

    Serial.println("Directory List");
    directoryList();
    delay(1000);
    Serial.println("Start Data");
    int i = 0;
    int j = 0;

    for (i=0; i < FILELISTSIZE; i++)
    {
      activeFiles[i] = "";
    }

    i = 0;

    for (i = 0; i < FILELISTSIZE; i++) {
      char buffer[13];
      fileList[i].toCharArray(buffer, 13);
      Serial.println("Check SD exists");
      Serial.println(fileList[i]);
      if (!SD.exists(buffer) && fileList[i] != "") {
        time_t check;
        Serial.println("directory date");
       if( !directoryDate(fileList[i])) {
         Serial.println("Error");
       }
        delay(500);
        check = tmConvert_t(serverFileDate.year, serverFileDate.month, serverFileDate.day, serverFileDate.hour, serverFileDate.minute, 00);
        time_t newTime = currentTime;
        newTime = tmConvert_t(Time.year(newTime), Time.month(newTime), Time.day(newTime), Time.hour(newTime), Time.minute(newTime), 00);
        Serial.println(abs(newTime-check));

         if (abs(newTime - check) >= torontoToVancouver && abs(newTime - check) < weekThreshold) {

           char bufferVal[13];
           fileList[i].toCharArray(bufferVal, 13);

           activeFiles[j] = fileList[i];
           j = j+1;

           Serial.println("Going to Download");
           delay(1500);
           doFTP(bufferVal);
           Serial.println("Downloaded");
           delay(1500); // To provide buffer between connections
         }
       }
     }
    updateTimer = currentTime;
    directorySort();
  }
}
else
{
  if (WiFi.connecting() == false)
  {
    FTPSTATE = false;
    playStateInit = false;
    if (Time.minute((currentTime - updateTimer)) > dataUpdateMinute) // Update files
    {
      Serial.println("File Management");
      ledColor(255, 200, 200); // Cyan
      /************************************************************* WEEKLY ARTIFACT CHECK */
      if (artifactWeekly)
      {
      fileManagement();
      }
      /************************************************************* WEEKLY ARTIFACT CHECK */

      Serial.println("Directory List");
      directoryList();
      delay(1000);
      Serial.println("Start Data");
      int i = 0;
      int j = 0;

      for (i=0; i < FILELISTSIZE; i++)
      {
        activeFiles[i] = "";
      }

      i = 0;

      for (i = 0; i < FILELISTSIZE; i++) {
        char buffer[13];
        fileList[i].toCharArray(buffer, 13);
        Serial.println("Check SD exists");
        Serial.println(fileList[i]);
        if (!SD.exists(buffer) && fileList[i] != "") {
          time_t check;
          Serial.println("directory date");
         if( !directoryDate(fileList[i])) {
           Serial.println("Error");
         }
          delay(500);
          check = tmConvert_t(serverFileDate.year, serverFileDate.month, serverFileDate.day, serverFileDate.hour, serverFileDate.minute, 00);
          time_t newTime = currentTime;
          newTime = tmConvert_t(Time.year(newTime), Time.month(newTime), Time.day(newTime), Time.hour(newTime), Time.minute(newTime), 00);
          Serial.println(abs(newTime-check));

           if (abs(newTime - check) >= torontoToVancouver && abs(newTime - check) < weekThreshold) {

             char bufferVal[13];
             fileList[i].toCharArray(bufferVal, 13);

             activeFiles[j] = fileList[i];
             j = j+1;

             Serial.println("Going to Download");
             delay(1500);
             doFTP(bufferVal);
             Serial.println("Downloaded");
             delay(1500); // To provide buffer between connections
           }
         }
       }
      updateTimer = currentTime;
      directorySort();
    }
  }
}
}

else if (!digitalRead(SP3PLAY))
{
  if (newMessage && (!recSend && !isRecording && !musicPlayer.playingMusic) )
  {
    ledColor(100, 100, 100);
  }
  else if (!newMessage && (!recSend && !isRecording && !musicPlayer.playingMusic))
  {
    ledColor(255, 255, 255); // OFF
  }

  // Init when in play state
  if (playStateInit == false) {
    playStateInit = true;
    // Sort directory
    directorySort();
    delay(2000);
    int iter = 0;

    for (iter=0; iter<FILELISTSIZE; iter++)
      {
         playableTracks[iter] = "";
      }

    if (Time.hour(currentTime) >= 5 && Time.hour(currentTime) < 12) // Morning
    {
      int iter1 = 0;
      int iter2 = 0;
      int breakVal = 0;

      for (iter1 = 0; iter1<FILELISTSIZE; iter1++)
      {
         for (iter2 = breakVal; iter2 < FILELISTSIZE; iter2++)
         {
           if (playSchedule[iter2] == 1)
           {
             playableTracks[iter1] = sdList[iter2];
             breakVal = iter2 + 1;
             break;
           }
         }
      }
    }
    else if (Time.hour(currentTime) >= 12 && Time.hour(currentTime) < 18) // Afternoon
    {
      int iter1 = 0;
      int iter2 = 0;
      int breakVal = 0;

      for (iter1 = 0; iter1<FILELISTSIZE; iter1++)
      {
         for (iter2 = breakVal; iter2 < FILELISTSIZE; iter2++)
         {
           if (playSchedule[iter2] == 2)
           {
             playableTracks[iter1] = sdList[iter2];
             breakVal = iter2 + 1;
             break;
           }
         }
      }
    }
    else if (Time.hour(currentTime) >= 18 && Time.hour(currentTime) <= 23) // Evening
    {
      int iter1 = 0;
      int iter2 = 0;
      int breakVal = 0;

      for (iter1 = 0; iter1<FILELISTSIZE; iter1++)
      {
         for (iter2 = breakVal; iter2 < FILELISTSIZE; iter2++)
         {
           if (playSchedule[iter2] == 3)
           {
             playableTracks[iter1] = sdList[iter2];
             breakVal = iter2 + 1;
             break;
           }
         }
      }
    }
    else if (Time.hour(currentTime) >= 0 && Time.hour(currentTime) < 5) // Late
    {
      int iter1 = 0;
      int iter2 = 0;
      int breakVal = 0;

      for (iter1 = 0; iter1<FILELISTSIZE; iter1++)
      {
         for (iter2 = breakVal; iter2 < FILELISTSIZE; iter2++)
         {
           if (playSchedule[iter2] == 4)
           {
             playableTracks[iter1] = sdList[iter2];
             breakVal = iter2 + 1;
             break;
           }
         }
      }
    }

    int iter4 = 0;

    for (iter4 = 0; iter4 < FILELISTSIZE; iter4 ++)
    {
      if (playableTracks[iter4] != "")
      {
      Serial.println(playableTracks[iter4]);
      }
    }


 }

if (Time.minute(currentTime - scheduleTimer) > scheduleUpdate )
{
  int iter = 0;

  for (iter=0; iter<FILELISTSIZE; iter++)
    {
       playableTracks[iter] = "";
    }

  if (Time.hour(currentTime) >= 5 && Time.hour(currentTime) < 12) // Morning
  {
    int iter1 = 0;
    int iter2 = 0;
    int breakVal = 0;

    for (iter1 = 0; iter1<FILELISTSIZE; iter1++)
    {
       for (iter2 = breakVal; iter2 < FILELISTSIZE; iter2++)
       {
         if (playSchedule[iter2] == 1)
         {
           playableTracks[iter1] = sdList[iter2];
           breakVal = iter2 + 1;
           break;
         }
       }
    }
  }
  else if (Time.hour(currentTime) >= 12 && Time.hour(currentTime) < 18) // Afternoon
  {
    int iter1 = 0;
    int iter2 = 0;
    int breakVal = 0;

    for (iter1 = 0; iter1<FILELISTSIZE; iter1++)
    {
       for (iter2 = breakVal; iter2 < FILELISTSIZE; iter2++)
       {
         if (playSchedule[iter2] == 2)
         {
           playableTracks[iter1] = sdList[iter2];
           breakVal = iter2 + 1;
           break;
         }
       }
    }
  }
  else if (Time.hour(currentTime) >= 18 && Time.hour(currentTime) <= 23) // Evening
  {
    int iter1 = 0;
    int iter2 = 0;
    int breakVal = 0;

    for (iter1 = 0; iter1<FILELISTSIZE; iter1++)
    {
       for (iter2 = breakVal; iter2 < FILELISTSIZE; iter2++)
       {
         if (playSchedule[iter2] == 3)
         {
           playableTracks[iter1] = sdList[iter2];
           breakVal = iter2 + 1;
           break;
         }
       }
    }
  }
  else if (Time.hour(currentTime) >= 0 && Time.hour(currentTime) < 5) // Late
  {
    int iter1 = 0;
    int iter2 = 0;
    int breakVal = 0;

    for (iter1 = 0; iter1<FILELISTSIZE; iter1++)
    {
       for (iter2 = breakVal; iter2 < FILELISTSIZE; iter2++)
       {
         if (playSchedule[iter2] == 4)
         {
           playableTracks[iter1] = sdList[iter2];
           breakVal = iter2 + 1;
           break;
         }
       }
    }
  }
}



bool volUp;
bool volDown;
bool nextTrack;
bool prevTrack;
/************************************************************* MOTION ARTIFACT CHECK */
if (artifactMotion)
{
if (!accelError) {

    // Motion trigger autoplay function
    if (musicPlayer.playingMusic) {
      accelResetTimer = currentTime;
      accelReset = false;
    }
    else {
      if (Time.minute(currentTime - accelResetTimer) > (motionUpdateMinute)) { // Offset by a minute to allow it to catch up
        Serial.println("Loading Motion");
        accelReset = true;
        if (Time.minute(currentTime - accelResetTimer) > (motionUpdateMinute + 0.3)) {
          Serial.println("Waiting");
      if (motionTrigger() && accelReset) {
        Serial.println("Trig");
         char bufferTrigger[13];
         sdList[0].toCharArray(bufferTrigger, 13);
         if (! musicPlayer.startPlayingFile(bufferTrigger)) {
         Serial.println("Could not open file");
        }
        else {
          Serial.println("Auto-play");
          accelReset = false;
          accelResetTimer = currentTime;
        }
      }
    //     Serial.println(F("Started playing"));
      }
    }
    }

     if (!accelReset) {
    // Playback and Pause Section
      delay(1000); // Buffer for audio playback stability
      uint16_t byteVal = musicPlayer.GPIO_digitalRead();
      playGPIO = byteVal & _BV(BUTTON_1);//musicPlayer.GPIO_digitalRead(BUTTON_1);
      volUp = byteVal & _BV(BUTTON_4); //musicPlayer.GPIO_digitalRead(BUTTON_4);
      volDown = byteVal & _BV(BUTTON_5);//musicPlayer.GPIO_digitalRead(BUTTON_5);
      nextTrack = byteVal & _BV(BUTTON_2);//musicPlayer.GPIO_digitalRead(BUTTON_2);
      prevTrack = byteVal & _BV(BUTTON_3);//musicPlayer.GPIO_digitalRead(BUTTON_3);
    }
    else {
      playGPIO = false;
      volUp = false;
      volDown = false;
      nextTrack = false;
      prevTrack = false;
    }
}
else
{
     delay(1000);
     uint16_t byteVal = musicPlayer.GPIO_digitalRead();
     playGPIO = byteVal & _BV(BUTTON_1);//musicPlayer.GPIO_digitalRead(BUTTON_1);
     volUp = byteVal & _BV(BUTTON_4); //musicPlayer.GPIO_digitalRead(BUTTON_4);
     volDown = byteVal & _BV(BUTTON_5);//musicPlayer.GPIO_digitalRead(BUTTON_5);
     nextTrack = byteVal & _BV(BUTTON_2);//musicPlayer.GPIO_digitalRead(BUTTON_2);
     prevTrack = byteVal & _BV(BUTTON_3);//musicPlayer.GPIO_digitalRead(BUTTON_3);
}
}
else // If artifact motion or an initialize error occurs, revert to original update check
{
  delay(1000);
  uint16_t byteVal = musicPlayer.GPIO_digitalRead();
  playGPIO = byteVal & _BV(BUTTON_1);//musicPlayer.GPIO_digitalRead(BUTTON_1);
  volUp = byteVal & _BV(BUTTON_4); //musicPlayer.GPIO_digitalRead(BUTTON_4);
  volDown = byteVal & _BV(BUTTON_5);//musicPlayer.GPIO_digitalRead(BUTTON_5);
  nextTrack = byteVal & _BV(BUTTON_2);//musicPlayer.GPIO_digitalRead(BUTTON_2);
  prevTrack = byteVal & _BV(BUTTON_3);//musicPlayer.GPIO_digitalRead(BUTTON_3);
}
/************************************************************* MOTION ARTIFACT CHECK */

  if (!musicPlayer.playingMusic)
  {
    if (playGPIO)
    {
      delay(500);
      if (playableTracks[currentlyPlaying] != "") {
      Serial.println("Play");
      char buffer[13];
      playableTracks[currentlyPlaying].toCharArray(buffer, 13);
      if (! musicPlayer.startPlayingFile(buffer)) {
        Serial.println("Could not open file");
      }
      Serial.println(F("Started playing"));
      ledColor(255, 200, 255); // Green
      int i =0;
      for (i=0; i<FILELISTSIZE; i++)
      {
        if (sdList[i] == playableTracks[currentlyPlaying])
        {
          activeMessage[i] = false;
          break;
        }
      }
    }
    }
  }
  else
  {
    if (playGPIO)
    {
      delay(500);
      Serial.println("Stop");
      musicPlayer.stopPlaying();
      ledColor(250, 250, 250); // Dimmly White
    }
  }


  if (volUp) { // Volume Up
   if (leftVolume >= 5) {
     leftVolume = leftVolume - 5;
     rightVolume = rightVolume - 5;
     musicPlayer.setVolume(leftVolume, rightVolume);
     ledColor(255, 200, 200); // Cyan
     delay(500);
     if (musicPlayer.playingMusic)
     {
       ledColor(255, 200, 255); // Green
     }
     else
     {
       ledColor(250, 250, 250); // Dimmly white
     }
     Serial.println("Vol Up");
   }
}

  if (volDown) { // Volume down
    if (leftVolume <= 55) {
      leftVolume = leftVolume + 5;
      rightVolume = rightVolume + 5;
      musicPlayer.setVolume(leftVolume, rightVolume);
      ledColor(255, 255, 200);
      delay(500);
      if (musicPlayer.playingMusic)
      {
        ledColor(255, 200, 255); // Green
      }
      else
      {
        ledColor(250, 250, 250); // Dimmly white
      }
      Serial.println("Vol Down");
    }
  }

if (nextTrack) { // Next track
    if (playableTracks[currentlyPlaying + 1] != "") {
      currentlyPlaying = currentlyPlaying + 1;
      delay(500);
      musicPlayer.stopPlaying();
      ledColor(200,227, 255); // Orange
      delay(500);
      ledColor(250, 250, 250); // Dimmly white
      Serial.println("Next Track");
    }
}

if (prevTrack) { // Prev track
    if (currentlyPlaying > 0) {
      currentlyPlaying = currentlyPlaying - 1;
      delay(500);
      musicPlayer.stopPlaying();
      ledColor(200,200, 255); // Yellow
      delay(500);
      ledColor(250, 250, 250); // Dimmly white
      Serial.println("Previous Track");
    }
}

}
}

/*********************************************************
*                Recording State Functions               *
*********************************************************/
uint16_t saveRecordedData(boolean isrecord)
{

// Initialize bytes written
uint16_t written = 0;

// Read how many words are waiting for us
uint16_t wordswaiting = musicPlayer.recordedWordsWaiting();

// try to process 256 words (512 bytes) at a time, for best speed
while (wordswaiting > 256)
{
    for (int x=0; x < 512/RECBUFFSIZE; x++)  // Fill buffer
    {
        for (uint16_t addr=0; addr < RECBUFFSIZE; addr+=2)
        {
            uint16_t t = musicPlayer.recordedReadWord();
            recording_buffer[addr] = t >> 8;
            recording_buffer[addr+1] = t;
        }
        if (! recording.write(recording_buffer, RECBUFFSIZE))
        {
            Serial.print("Couldn't write "); Serial.println(RECBUFFSIZE);
          //  while (1);
        }
    }
    // Flush 512 bytes at a time
    recording.flush();
    written += 256;
    wordswaiting -= 256;
}

wordswaiting = musicPlayer.recordedWordsWaiting();

if (!isrecord)
{
    Serial.print(wordswaiting); Serial.println(" remaining");
    // Wrapping up the recording!
    uint16_t addr = 0;
    for (int x=0; x < wordswaiting-1; x++)
    {
      // Fill the buffer
        uint16_t t = musicPlayer.recordedReadWord();
        recording_buffer[addr] = t >> 8;
        recording_buffer[addr+1] = t;
        if (addr > RECBUFFSIZE)
        {
            if (! recording.write(recording_buffer, RECBUFFSIZE))
            {
                Serial.println("Couldn't write!");
            //    while (1);
            }
            recording.flush();
            addr = 0;
        }
    }
    if (addr != 0)
    {
        if (!recording.write(recording_buffer, addr))
        {
            Serial.println("Couldn't write!");
        }
        written += addr;
    }
    musicPlayer.sciRead(VS1053_SCI_AICTRL3);
    if (! (musicPlayer.sciRead(VS1053_SCI_AICTRL3) & _BV(2)))
    {
       recording.write(musicPlayer.recordedReadWord() & 0xFF);
       written++;
    }
    recording.flush();
  }

return written;
}

/*********************************************************
*                 Playback State Functions               *
*********************************************************/

uint8_t debounceRead(int pin)
{
  uint8_t pinState = musicPlayer.GPIO_digitalRead(pin);
  uint32_t timeout = millis();
  while (millis() < timeout+10)
  {
    if (musicPlayer.GPIO_digitalRead(pin) != pinState)
    {
      pinState = musicPlayer.GPIO_digitalRead(pin);
      timeout = millis();
    }
  }

  return pinState;
}

/*********************************************************
*                   FTP Server Functions                 *
*********************************************************/
byte doFTP(char fileFTP[13]) // upload/download files via FTP
{
if ( FTPSTATE ) {
  fh = SD.open(fileFTP,FILE_READ);
}
else if (!FTPSTATE) {
  SD.remove(fileFTP);
  SdFile::dateTimeCallback(ServerTime);
  fh = SD.open(fileFTP,FILE_WRITE);
}

  if(!fh)
  {
    Serial.println("SD open fail");
    return 0;
  }

if (!FTPSTATE) {
  if(!fh.seek(0))
  {
    Serial.println("Rewind fail");
    fh.close();
    return 0;
  }
}

  Serial.println("SD opened");

    if (client.connect(server, 21)){
    Serial.println("Command connected");
  }
  else {
    fh.close();
    Serial.println("Command connection failed");
    return 0;
  }


  if(!eRcv()) return 0;
  client.println("USER " + userFTP);
  if(!eRcv()) return 0;
  client.println("PASS " + passFTP);
  if(!eRcv()) return 0;
  if (FTPSTATE) {
  client.println("CWD " + uploadFolderFTP);
  }
  else {
  client.println("CWD " + downloadFolderFTP);
  }
  if(!eRcv()) return 0;
  client.println("SYST");
  if(!eRcv()) return 0;
  client.println("Type I");
  if(!eRcv()) return 0;
  client.println("PASV");
  if(!eRcv()) return 0;

  char *tStr = strtok(outBuf,"(,");
  int array_pasv[6];
  for ( int i = 0; i < 6; i++) {
    tStr = strtok(NULL,"(,");
    array_pasv[i] = atoi(tStr);
    if(tStr == NULL)
    {
      Serial.println("Bad PASV Answer");
    }
  }

  unsigned int hiPort,loPort;

  hiPort = array_pasv[4] << 8;
  loPort = array_pasv[5] & 255;

  Serial.print("Data port: ");
  hiPort = hiPort | loPort;
  Serial.println(hiPort);

  if (dclient.connect(server,hiPort)) {
    Serial.println("Data connected");
  }
  else {
    Serial.println("Data connection failed");
    client.stop();
    fh.close();
    return 0;
  }

if (FTPSTATE) {
  client.print("STOR ");
  client.println(fileFTP);
}
else {
  client.print("RETR ");
  client.println(fileFTP);
}

  if(!eRcv())
  {
    dclient.stop();
    return 0;
  }

if (FTPSTATE) {
  Serial.println("Writing");

  byte clientBuf[64];
  int clientCount = 0;

  while(fh.available())
  {
    clientBuf[clientCount] = fh.read();
    clientCount++;

    if(clientCount > 63)
    {
      dclient.write(clientBuf,64);
      clientCount = 0;
      delay(2);
    }
  }

  if(clientCount > 0) dclient.write(clientBuf,clientCount);
}
else {
  while(dclient.connected())
  {
    while(dclient.available())
    {
      char c = dclient.read();
      fh.write(c);
    }
  }
}

  dclient.stop();
  Serial.println("Data disconnected");
  client.stop();
  Serial.println("Command disconnected");

  fh.close();
  Serial.println("SD closed");
  return 1;
}

byte eRcv()
{
  byte respCode;
  byte thisByte;

  while(!client.available()) Spark.process();
  respCode = client.peek();
  outCount = 0;

  while(client.available())
  {
    thisByte = client.read();
    Serial.write(thisByte);

    if(outCount < 127)
    {
      outBuf[outCount] = thisByte;
      outCount++;
      outBuf[outCount] = 0;
    }
  }

  if(respCode >= '4')
  {
    efail();
    return 0;
  }

  return 1;
}


void efail()
{
  byte thisByte = 0;

  client.println("QUIT");

  while(!client.available()) Spark.process();
  while(client.available())
  {
    thisByte = client.read();
    Serial.write(thisByte);
  }

  client.stop();
  Serial.println("Command disconnected");
  fh.close();
  Serial.println("SD closed");
}

/*********************************************************
*                Directory and Date Functions            *
*********************************************************/
byte directoryDate(String fileInput) // Return server file upload date
{
 // Connect to FTP client over port 21
    if (client.connect(server, 21)){
    Serial.println("Command connected");
  }
  else {
    Serial.println("Command connection failed");
    return 0;
  }

 // Client commands for login/interaction mode
  if(!eRcv()) return 0;
  client.println("USER " + userFTP);
  if(!eRcv()) return 0;
  client.println("PASS " + passFTP);
  if(!eRcv()) return 0;
  client.println("CWD " + downloadFolderFTP);
  if(!eRcv()) return 0;
  client.println("SYST");
  if(!eRcv()) return 0;
  client.println("Type ascii");
  if(!eRcv()) return 0;
  client.println("PASV");
  if(!eRcv()) return 0;

  char *tStr = strtok(outBuf,"(,");
  int array_pasv[6];
  for ( int i = 0; i < 6; i++) {
    tStr = strtok(NULL,"(,");
    array_pasv[i] = atoi(tStr);
    if(tStr == NULL)
    {
      Serial.println("Bad PASV Answer");
    }
  }

  unsigned int hiPort,loPort;

  hiPort = array_pasv[4] << 8;
  loPort = array_pasv[5] & 255;

  Serial.print("Data port: ");
  hiPort = hiPort | loPort;
  Serial.println(hiPort);

  if (dclient.connect(server,hiPort)) {
    Serial.println("Data connected");
  }
  else {
    Serial.println("Data connection failed");
    client.stop();
    return 0;
  }

  // Sending list command to server for date of file
  byte testBuffer[65];
  byte newBuffer[13];
  client.println("LIST " + fileInput);

  // Reading buffer values
  if(!eRcv()) return 0;
  dclient.read(testBuffer, 65);

  // Formatting date
   int i = 0;
   for (i=39; i<51; i++) {
     newBuffer[i-39] = testBuffer[i];
   }
   String val = String((char*)newBuffer);
   val = val.remove(12);
   String monthInput = val.substring(0, 3);
   int Day = (val.substring(4, 6)).toInt();
   int Hour = (val.substring(7, 9)).toInt();
   int Minute = (val.substring(10, 12)).toInt();
   int Month;

   // Switch statement for month values
   if (monthInput == "Jan") {
          Month = 1; }
   else if (monthInput == "Feb") {
          Month = 2; }
   else if (monthInput == "Mar") {
          Month = 3; }
   else if (monthInput == "Apr") {
          Month = 4; }
   else if (monthInput == "May") {
          Month = 5; }
   else if (monthInput == "Jun") {
          Month = 6; }
   else if (monthInput == "Jul") {
          Month = 7; }
   else if (monthInput == "Aug") {
          Month = 8; }
   else if (monthInput == "Sep") {
          Month = 9; }
   else if (monthInput == "Oct") {
          Month = 10; }
   else if (monthInput == "Nov") {
          Month = 11; }
   else if (monthInput == "Dec") {
          Month = 12; }

   tval = tmConvert_t(2019, Month, Day, Hour, Minute, 00); // UNIX formatting
   serverFileDate.month = Time.month(tval); // Update with timezone
   serverFileDate.day = Time.day(tval);
   serverFileDate.year = 2019;
   serverFileDate.hour = Time.hour(tval);
   serverFileDate.minute = Time.minute(tval);
   serverFileDate.second = 00;

  if(!eRcv())
  {
    dclient.stop();
    return 0;
  }

  dclient.stop();
  Serial.println("Data disconnected");
  client.stop();
  Serial.println("Command disconnected");


  return 1;
}

byte directoryList()
{
    int i = 0;
    for (i=0; i<FILELISTSIZE; i++) {
      fileList[i] = "";
    }

    if (client.connect(server, 21)){
    Serial.println("Command connected");
  }
  else {
    Serial.println("Command connection failed");
    return 0;
  }

  if(!eRcv()) return 0;
  client.println("USER " + userFTP);
  if(!eRcv()) return 0;
  client.println("PASS " + passFTP);
  if(!eRcv()) return 0;
  client.println("CWD " + downloadFolderFTP);
  if(!eRcv()) return 0;
  client.println("SYST");
  if(!eRcv()) return 0;
  client.println("Type ascii"); // Text type FTP return
  if(!eRcv()) return 0;
  client.println("PASV");
  if(!eRcv()) return 0;

  char *tStr = strtok(outBuf,"(,");
  int array_pasv[6];
  for ( int i = 0; i < 6; i++) {
    tStr = strtok(NULL,"(,");
    array_pasv[i] = atoi(tStr);
    if(tStr == NULL)
    {
      Serial.println("Bad PASV Answer");
    }
  }

  unsigned int hiPort,loPort;

  hiPort = array_pasv[4] << 8;
  loPort = array_pasv[5] & 255;

  Serial.print("Data port: ");
  hiPort = hiPort | loPort;
  Serial.println(hiPort);

  if (dclient.connect(server,hiPort)) {
    Serial.println("Data connected");
  }
  else {
    Serial.println("Data connection failed");
    client.stop();
    return 0;
  }


  Serial.println("Testing NLST Command");
  byte testBuffer2[1000];

  client.println("NLST");
  if(!eRcv()) return 0;

  dclient.read(testBuffer2, 1000);
  String val = String((char*)testBuffer2);
  val = val.trim();

  i = 0;
  for (i=0; i < (val.length()/13); i++) {
    // file array limit
    if (i == FILELISTSIZE) {
      break;
    }
    if (i == (val.length()/13 -1)) {
    fileList[i] = (val.substring(i*13, (i*13) + 14)).trim();
    }
    else {
      fileList[i] = (val.substring(i*13, (i*13) + 13)).trim();
    }
  }


  if(!eRcv())
  {
    dclient.stop();
    return 0;
  }


  dclient.stop();
  Serial.println("Data disconnected");
  client.stop();
  Serial.println("Command disconnected");

  return 1;
}

time_t tmConvert_t(int YYYY, byte MM, byte DD, byte hh, byte mm, byte ss)  // inlined for speed
{
  struct tm t;
  t.tm_year = YYYY-1900;
  t.tm_mon = MM - 1;
  t.tm_mday = DD;
  t.tm_hour = hh;
  t.tm_min = mm;
  t.tm_sec = ss;
  t.tm_isdst = 0;  // not used
  time_t t_of_day = mktime(&t);
  return t_of_day;
}

void printDirectory(File dir, String printBuffer[FILELISTSIZE]) {

  int z = 0;

  while (true) {

    File entry =  dir.openNextFile();
    if (! entry) { // No more files
      // Attemption to use rewind function to fix errors
  //     dir.rewindDirectory();
      break;
    }

   if (entry.isDirectory()) {

   }
   else if (String(entry.name()).startsWith("REA")) {
     if (z >= FILELISTSIZE) {
//       entry.close();
       break;
     }
    printBuffer[z] = String(entry.name());
    Serial.println(printBuffer[z]);
    z++;
   }
    entry.close();
  }

 Serial.println("End of Directory Sort");
}

void fileManagement()
{
// Initialize iteration variable
int i = 0;
// Initial sort
directorySort();
// Check current unix time
current = Time.now();
current = tmConvert_t(Time.year(current), Time.month(current), Time.day(current), Time.hour(current), Time.minute(current), 00);
// Threshold value
time_t thresholdUNIX = 604800; // Unix time difference for one week

Serial.println("Start Management Loop");
for (i = 0; i < FILELISTSIZE; i++)
{
    if (sdList[i] != "")
    {
        // Update dataStorage struct with values based on file
        SD.fileDate(&dateStorage, sdList[i]);
        // Convert SD fat date output to unix time
        tval = tmConvert_t(dateStorage.year, dateStorage.month, dateStorage.day, dateStorage.hour, dateStorage.minute, 00);
        time_t difference = abs(current - tval);
        char bufferInput[13];
        (sdList[i]).toCharArray(bufferInput, 13);
        if (SD.exists(bufferInput) && difference > thresholdUNIX )
        {
          SD.remove(bufferInput);
          sdList[i] = "";
        }
  }
}
directorySort();
Serial.println("End of Management");
}

// Directory sort function for aligning oldest to new based on creation date
void directorySort()
{
  String sdBuffer[FILELISTSIZE]; // print directory debug
  // Open root directory
  root = SD.open("/");
  // Rewind directory for root, since it points to last referenced position
  // SD.fileDate function will shift the position if you don't rewind
  root.rewindDirectory();
  // Create list of FILELISTSIZE most recent files
  printDirectory(root, sdBuffer);

// Initialize values
String sortingList[FILELISTSIZE];
bool activeSorting[FILELISTSIZE];

int j = 0;

for (j = 0; j <FILELISTSIZE; j++){
  sdList[j] = sdBuffer[j];
}

j = 0; // Iterator
int k = 0; // Iterator
time_t val = 0; // Storage value
int breakVal = -1; // For corresponding newest creation date to file
int stopVar = 0; // For checking when files stop


Serial.println("Start sorting"); // Debugging check

 // For checking where the list is empty
for (j = 0; j < FILELISTSIZE; j++)
{
  if (sdList[j] == "")
  {
      stopVar = j;
      break;
  }
}

// Serial.println(stopVar); // Debugging check

j = 0; // Reset iterator

// Checks to see if first list item is empty or not
if (sdList[0] != "")
{
  // Main loop iterator for creating switch list
  for (j = 0; j < FILELISTSIZE; j++)
  {

      val = 0; // Resetting time_t storage variable

      // Inner loop iterator for comparing creation time of entire sdList
      for (k = 0; k < FILELISTSIZE; k++)
      {
           // Checks to see whether the file is empty or not, can change to != ""
           if (sdList[k].startsWith("REA"))
           {
               // Calls fileDate function for SD object to pass through a files creation time to dateStorage pointer
               SD.fileDate(&dateStorage, sdList[k]);
               // Formats the creation time for simple comparison
               tval = tmConvert_t(dateStorage.year, dateStorage.month, dateStorage.day, dateStorage.hour, dateStorage.minute, 00);
               // Serial.println(tval); // Debugging
               // Serial.println("Check" + sdList[k]);

               // Comparator of greatest tmConvert value (newest)
               if (tval >= val)
               {
                  val = tval;
                  breakVal = k; // Array indices that corresponds to greatest tmConvert
                  //          Serial.println(breakVal);
                  //          Serial.println("Tval" + sdList[k]);

               }
            }
      }
      // Check whether the next sd file is empty
      if (sdList[breakVal] != "")
      {
  //    Serial.println(j); // Debugging info
  //    Serial.println(breakVal);
  //    Serial.println(sdList[breakVal]);
      sortingList[j] = sdList[breakVal]; // Passing through to sorting list for storage
      activeSorting[j] = activeMessage[breakVal];
      sdList[breakVal] = ""; // Remove file that was passed through for doing next set of comparisons
    }
  }

   Serial.println("Beginning rearrange");


   j = 0; // Resetting iterator

   // Sorting and passing through oldest to newest
   for (j = 0; j<51; j++)
   {
       // Check whether the iterator is less than our empty value indicator
      if (j < (stopVar))
      {
        sdList[j] = sortingList[(stopVar-1)-j]; // Pass through in reverse manner with -1 offset for starting at zero
        activeMessage[j] = activeSorting[(stopVar-1)-j];
//        Serial.println(sdList[j]);
      }
      // Check if the iterator is greater or starting at the empty value indicator
      if (j >= (stopVar))
      {
        sdList[j] = ""; // Resolve to empty if not already
        activeMessage[j] = false;
      }
   }
//   Serial.println(stopVar);
   Serial.println("Done Old sort");

   if (newToOldSort)
   {
         String switchBuf[FILELISTSIZE];
         bool switchActive[FILELISTSIZE];

         int bufVal = 0;
         int iterator = 0;

         for (iterator = 0; iterator<FILELISTSIZE; iterator++)
         {
           switchBuf[iterator] = sdList[iterator];
           switchActive[iterator] = activeMessage[iterator];
         }

         iterator = 0;

         for (iterator = 0; iterator < FILELISTSIZE; iterator++)
         {
            if (sdList[iterator] == "")
            {
              bufVal = iterator;
              Serial.println("Break Val");
              break;
            }
         }

         iterator = 0;

         if (bufVal != 0) {
         // Sorting and passing through oldest to newest
         for (iterator = 0; iterator<FILELISTSIZE; iterator++)
         {
             // Check whether the iterator is less than our empty value indicator
            if (iterator < (bufVal))
            {
              sdList[iterator] = switchBuf[(bufVal-1)-iterator]; // Pass through in reverse manner with -1 offset for starting at zero
              activeMessage[iterator] = switchActive[(bufVal-1)-iterator];
      //        Serial.println(sdList[j]);
            }
            // Check if the iterator is greater or starting at the empty value indicator
            if (iterator >= (bufVal))
            {
              sdList[iterator] = ""; // Resolve to empty if not already
              activeMessage[iterator] = false;
            }
         }
         Serial.println("Start new to old");
       }

         Serial.println("End of new to old");

         // Active Message Update

         int i = 0;
         j = 0;
         for (i=0; i<FILELISTSIZE; i++)
         {
           if (activeFiles[i] == "")
           {
             break;
           }
              for (j = 0; j < FILELISTSIZE; j++)
              {
                if (sdList[j] == activeFiles[i])
                {
                  activeMessage[j] = true;
                  break;
                }
              }
         }

   }

   j = 0;

   for (j=0; j<FILELISTSIZE; j++)
   {
        if (sdList[j] != "")
          {

            SD.fileDate(&dateStorage, sdList[j]);
            // Formats the creation time for simple comparison
           //  tval = tmConvert_t(dateStorage.year, dateStorage.month, dateStorage.day, dateStorage.hour, dateStorage.minute, 00);
            //Serial.println(tval);
            //Serial.println(Time.hour(tval));
            int hour = dateStorage.hour;
            Serial.println(hour);
            Serial.println(sdList[j]);
            //Morning 1 (>=05:00 - <12:00), Afternoon 2 (>=12:00 - <18:00), Evening 3 (>= 18:00 - 23:59), Late 4 (00:00 - <05:00)
            if (hour >= 5 && hour < 12)
            {
              playSchedule[j] = 1;
            }
            else if (hour >= 12 && hour < 18)
            {
              playSchedule[j] = 2;
            }
            else if (hour >= 18 && hour <= 23)
            {
              playSchedule[j] = 3;
            }
            else if (hour >= 0 && hour < 5)
            {
              playSchedule[j] = 4;
            }
          }
   }

}
}

void ServerTime(uint16_t* date, uint16_t* timeVal) {

  // return date using FAT_DATE macro to format fields
  *date = FAT_DATE(2019, Time.month(tval), Time.day(tval));

  // return time using FAT_TIME macro to format fields
  *timeVal = FAT_TIME(Time.hour(tval), Time.minute(tval), 00);
}

void DateTime(uint16_t* date, uint16_t* timeVal) {

  // Create variable of type time_t to store UTC time offset by timezone
  time_t current = Time.now();

  // return date using FAT_DATE macro to format fields
  *date = FAT_DATE(Time.year(current), Time.month(current), Time.day(current));

  // return time using FAT_TIME macro to format fields
  *timeVal = FAT_TIME(Time.hour(current), Time.minute(current), Time.second(current));
}

// LED update function for resetting after changing values
void ledNotification()
{
  // Sort directory
  directorySort();
  // Acquire creation time for oldest
  time_t ledTIME = Time.now();
  ledTIME = tmConvert_t(Time.year(ledTIME), Time.month(ledTIME), Time.day(ledTIME), Time.hour(ledTIME), Time.minute(ledTIME), 00);
  if (sdList[0] == "") {
    analogWrite(LED_R, 255);
    analogWrite(LED_B, 250);
    analogWrite(LED_G, 255);
  }
  else {
    SD.fileDate(&dateStorage, sdList[0]);
    // Converting time to UNIX with zone offset for comparison
    tval = tmConvert_t(dateStorage.year, dateStorage.month, dateStorage.day, dateStorage.hour, dateStorage.minute, 00);
    time_t difference = abs(ledTIME - tval);
//     Serial.println(difference);
    // Checks to see if difference is greater than one week (shouldn't be but might during testing)
    if (difference > 604800) {
      analogWrite(LED_R, 255);
      analogWrite(LED_B, 250);
      analogWrite(LED_G, 255);
    }
    else {
      // Scales based on 255(no light):0 (max light) - difference of one week would be 604800 (1*255: no light)
      int writeVal = (difference/604800)*255;
         analogWrite(LED_R, writeVal);
         analogWrite(LED_G, writeVal);
         analogWrite(LED_B, writeVal);
    }
  }
}

bool motionTrigger()
{
float xVal = accel.getX() * ADXL345_MG2G_MULTIPLIER; //0.004
float yVal = accel.getY() * ADXL345_MG2G_MULTIPLIER;
float zVal = accel.getZ() * ADXL345_MG2G_MULTIPLIER;
float triggerVector = sqrt( pow(xVal - xBias, 2) + pow(yVal - yBias, 2) + pow(zVal - zBias, 2));
  triggerVector = triggerVector * 100;
  if (triggerVector > triggerCompare) {
    Serial.println("Motion!");
    return true;
  }
  else
  {
    Serial.println("No Motion");
    return false;
  }
}

// Simple LED write function, input values inverse (0 brightest : 255 off)
void ledColor(int red, int green, int blue)
{
  analogWrite(LED_R, red);
  analogWrite(LED_G, green);
  analogWrite(LED_B, blue);
}

/*
LED COLOUR LIST:

Green (255, 200, 255) - Not too bright?
Orange(200,227, 255)
Yellow(200,200,255)
Cyan(255, 200, 200)
Blue(255, 255, 200)
Red (200, 255, 255)
White(200, 200, 200)

*/

/*
Connects all MIDI devices.

- Midronome: default HW Serial (RX, TX)
- Looper: SW Serial (9, 10)
- Looper: SW Serial (11, 12)

The program sequentially starts every Synth program and (TODO) starts/stops/play looper channels every 4 beats.


TODO:

- add LCD, SD, and ENCODER interface
- in S_LOAD/LOADED: load and parse txt files ()
- figure out data structure (map)?
- try one simple song (Kids)

*/
#include <SdFat.h>
#include <EncoderButton.h>
#include <LiquidCrystal_I2C.h>
#include <MIDI.h>
#include <SoftwareSerial.h>

#define LOOPER_MIDI_RX 9
#define LOOPER_MIDI_TX 10
#define SYNTH_MIDI_RX 11
#define SYNTH_MIDI_TX 12

#define midiChannelSynth 3
#define midiChannelLooper 4

//------------------------------------------------------------------------------
// SD Card configuration
#define SD_FAT_TYPE 0
// #define error(s) sd.errorHalt(&Serial, F(s))
// Try max SPI clock for an SD. Reduce SPI_CLOCK if errors occur.
#define SPI_CLOCK SD_SCK_MHZ(50)

// // Try to select the best SD card configuration.
#if HAS_SDIO_CLASS
#define SD_CONFIG SdioConfig(FIFO_SDIO)
#elif ENABLE_DEDICATED_SPI
#define SD_CONFIG SdSpiConfig(SD_CS_PIN, DEDICATED_SPI, SPI_CLOCK)
#else  // HAS_SDIO_CLASS
#define SD_CONFIG SdSpiConfig(SD_CS_PIN, SHARED_SPI, SPI_CLOCK)
#endif  // HAS_SDIO_CLASS

const uint8_t SD_CS_PIN = 4;

//-----------------------------------------------------------------------------
// Program variables
static enum { S_LOAD, S_SONG_LOADED, S_WAIT_TO_START, S_PLAYING } state = S_LOAD;
// static enum { SEQ, MID } synthMode = SEQ;

// data loading
SdFat sd;
File rootDir;
File songDir;
size_t size = 20;
char filename[20];

char songList[10][20];
uint8_t numSongs = 0;
int8_t currentSong = 0;
uint8_t firstSongDisplayed = 0;
uint8_t lastSongDisplayed = 3;
const uint8_t numSongsDisplayed = 4;
// static enum { S_LOAD_SONG, S_WAIT_TO_START, S_PLAY } state = S_LOAD_SONG;

// Looper commands can be: R1-R2-S1-S2-C1-C2-SA-CA
// E.g. 33-R1 --> Rec/Play/Dub Channel 1 at beat 33
uint16_t looperBeats[20] = {0};
char looperCmds[20][3] = {""};
uint8_t numLooperCmds = 0;

// Synth commands can be: P (Play) or S (Stop)
// E.g. 33-129-P --> Play Preset 129 at beat 33 OR 127-0-S --> Stop at beat 127 (the second entry is ignored)
uint16_t synthBeats[20] = {0};
uint16_t synthPresets[20] = {0};
char synthCmds[20][2] = {""};
uint8_t numSynthCmds = 0;

// clock count
// uint16_t estimatedBPM = 0;
// long elapsedClockTime;
// long previousClockTime;
long timeLedOn = 0;
bool isLedOn = false;
const uint8_t ledDurationMs = 100;

bool songStarted = false;
bool printedStart = false;
uint8_t ticksCount = 0; // 0-24 ticks
uint8_t currentPreQuarter = 0;
uint16_t currentSongQuarter = 0;
uint16_t waitForQuarters = 4;

int stopAt = 16;
int currentProgram = 0;

//------------------------------------------------------------------------------
// LCD configuration
const int pinInput1 = A1;  
const int pinInput2 = A0;
const int pinButton = 5;

EncoderButton myEnc(pinInput1, pinInput2, pinButton);
LiquidCrystal_I2C lcd(0x27,20,4);

bool lcdIsOn = true;

//------------------------------------------------------------------------------
// MIDI configuration
using Transport = MIDI_NAMESPACE::SerialMIDI<SoftwareSerial>;

// SYNTH
SoftwareSerial serialSynth = SoftwareSerial(SYNTH_MIDI_RX, SYNTH_MIDI_TX);
Transport serialMIDISynth(serialSynth);
MIDI_NAMESPACE::MidiInterface<Transport> MIDI_SYNTH((Transport&)serialMIDISynth);

// LOOPER
SoftwareSerial serialLooper = SoftwareSerial(LOOPER_MIDI_RX, LOOPER_MIDI_TX);
Transport serialMIDILooper(serialLooper);
MIDI_NAMESPACE::MidiInterface<Transport> MIDI_LOOPER((Transport&)serialMIDILooper);

// MIDRONOME - MIDI default RX, TX
MIDI_CREATE_DEFAULT_INSTANCE();

const uint8_t beatLed = 7;

//----------------------------------------
// Data loading
// Encoder
void onClicked(EncoderButton& eb) {
  if (lcdIsOn)
    {
      lcd.noBacklight();
      lcdIsOn = false;
    }
  else
  {
    lcd.backlight();
    lcdIsOn = true;
  }
}

void onEncoder(EncoderButton& eb) {
  int increment = eb.increment();
  currentSong = currentSong + increment;
  displaySongs();
}

// Loaders and String helpers
int endsWith(const char *str, const char *suffix)
{
    if (!str || !suffix)
        return 0;
    size_t lenstr = strlen(str);
    size_t lensuffix = strlen(suffix);
    if (lensuffix >  lenstr)
        return 0;
    return strncmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
}

bool startsWith(const char *pre, const char *str)
{
    size_t lenpre = strlen(pre),
           lenstr = strlen(str);
    return lenstr < lenpre ? false : memcmp(pre, str, lenpre) == 0;
}

void loadSongList(){
  // Open root directory
  if (!rootDir.open("/")) 
  {
    //Serial.println("Root dir open failed");
  }
  else
  {
    // Open files and store names them
    while (songDir.openNext(&rootDir, O_RDONLY)) 
    {
      songDir.getName(filename, size);
      if (!(filename[0] == '.'))
      {
        if (songDir.isDir()) {
          File fileSub;
          songList[numSongs][0] = '\0';
          strncpy(songList[numSongs], filename, strlen(filename)); // fix this
          //Serial.print("Song name: ");
          //Serial.print(songList[numSongs]);
          numSongs++;

          Serial.write('/');
          //Serial.println();
          while (fileSub.openNext(&songDir, O_RDONLY))
          {
            fileSub.getName(filename, size);
            //Serial.print("  ");
            //Serial.print(filename);
            //Serial.println();
            fileSub.close();
          }
          
        }
      }
      songDir.close();
    }
    rootDir.close();
  } 
}

void displaySongs()
{
  // delete cursors
  lcd.clear();
  // for (int row=0; row<numSongsDisplayed; row++)
  // {
  //   lcd.setCursor(0, row);
  //   lcd.print(" ");
  // }
  //Serial.print("Current pre: ");
  //Serial.print(currentSong);
  //Serial.print(" - First pre: ");
  //Serial.print(firstSongDisplayed);
  //Serial.print(" - Last pre: ");
  //Serial.println(lastSongDisplayed);
  if (currentSong < 0)
  {
    //Serial.println("Current song negative");
    currentSong = numSongs - 1;
    firstSongDisplayed = currentSong - numSongsDisplayed + 1;
    lastSongDisplayed = currentSong;
  }
  else if (currentSong > numSongs - 1)
  {
    //Serial.println("Current song greater than num songs");
    currentSong = 0;
    firstSongDisplayed = 0;
    lastSongDisplayed = numSongsDisplayed - 1;
  }
  else if (currentSong > lastSongDisplayed)
  {
    //Serial.println("Current song greater than last displayed");
    lastSongDisplayed = currentSong;
    firstSongDisplayed = lastSongDisplayed - numSongsDisplayed + 1;
  }
  else if (currentSong < firstSongDisplayed)
  {
    //Serial.println("Current song less than first displayed");
    firstSongDisplayed = currentSong;
    lastSongDisplayed = firstSongDisplayed + numSongsDisplayed - 1;
  }

  //Serial.print("Current post: ");
  //Serial.print(currentSong);
  //Serial.print(" - First post: ");
  //Serial.print(firstSongDisplayed);
  //Serial.print(" - Last post: ");
  //Serial.println(lastSongDisplayed);

  // Display titles
  for (int row=firstSongDisplayed; row<=lastSongDisplayed; row++)
  {
      uint8_t real_row = row - firstSongDisplayed;
      lcd.setCursor(2, real_row);
      lcd.print(songList[row]);
  }
    
  // Display cursor
  lcd.setCursor(0, currentSong - firstSongDisplayed);
  lcd.print(">");
}

void loadSong(const char* songName){
  if (!rootDir.open("/")) {
    //Serial.println("Root open failed");
  }
  else
  {
    File file;
    if (!songDir.open(songName)) {
      //Serial.println("Song folder open failed");
    } 

    if (file.open(&songDir, "looper.txt", O_RDONLY))
    {
      // parse looper
      //Serial.println("Parsing looper");
      char currentCmd[6]; // max size is 5 + NULL character
      currentCmd[0] = '\0';
      char currentChar[2];
      uint16_t currentBeat = 0;
      int data;
      if (file.fileSize() > 0)
      {
        bool isEmpty = false;
        while (!isEmpty) 
        {
          data = file.read();
          if ((char(data) == ',') || (data == -1))
          {
            /* 
            In this case, the command is complete and needs to be parsed.
            Commands for the looper are always in this format:
            {beat}-{cmd}
            */
            const char delimiter[2] = "-";
            char *looperCmd, beat_str;
            int beat;
            
            /* get the first token */
            beat = atoi(strtok(currentCmd, delimiter));
            looperCmd = strtok(NULL, delimiter);

            looperBeats[currentBeat] = beat;
            strncpy(looperCmds[currentBeat], looperCmd, 3);

            currentBeat++;
            // reset
            currentCmd[0] = '\0';
            if (data < 0)
            {
              // exit the loop
              isEmpty = true;
              //Serial.println("End of file looper.txt");
            }
              
          }
          else
          {
            currentChar[0] = char(data);
            currentChar[1] = '\0';
            strcat(currentCmd, currentChar);
          }
        }
      }
      numLooperCmds = currentBeat;
      file.close();
    }
    if (file.open(&songDir, "synth.txt", O_RDONLY))
    {
      // parse synth
      //Serial.println("Parsing synth");

      char currentCmd[20]; // max size is 5 + NULL character
      currentCmd[0] = '\0';
      char currentChar[2];
      uint16_t currentBeat = 0;

      // TODO fix this!
      int data;
      if (file.fileSize() > 0)
      {
        bool isEmpty = false;
        while (!isEmpty) 
        {
          data = file.read();
          // //Serial.println(char(data));

          if ((char(data) == ',') || (data == -1))
          {
            /* 
            In this case, the command is complete and needs to be parsed.
            Commands for the looper are always in this format:
            {beat}-{cmd}
            */
            const char delimiter[2] = "-";
            char * synthCmd;
            uint8_t synthProgram;
            int beat, preset;
            
            // First token is always the beat, second is the Preset, third is rthe Cmd
            beat = atoi(strtok(currentCmd, delimiter));
            synthProgram = atoi(strtok(NULL, delimiter));
            synthCmd = strtok(NULL, delimiter);

            synthBeats[currentBeat] = beat;
            synthPresets[currentBeat] = synthProgram;
            strncpy(synthCmds[currentBeat], synthCmd, 3);
            currentBeat++;
            // reset
            currentCmd[0] = '\0';
            if (data < 0)
            {
              // exit the loop
              isEmpty = true;
              //Serial.println("End of file synth.txt");
            }
          }
          else
          {
            currentChar[0] = char(data);
            currentChar[1] = '\0';
            strcat(currentCmd, currentChar);
          }
        }
      }
      numSynthCmds = currentBeat;
      file.close();
    }
    // file.close();
    songDir.close();
    rootDir.close();
  }
}

//----------------------------------------
// Synth controls
void synthChangeProgram(int programNumber)
{
  int bankMSB, bankLSB;
  bankMSB = programNumber / 127;
  bankLSB = programNumber % 127;
  //Serial.print("Change Preset to ");
  //Serial.println(bankLSB);

  MIDI_SYNTH.sendControlChange(0, bankMSB, midiChannelSynth);
  MIDI_SYNTH.sendProgramChange(bankLSB, midiChannelSynth);
}

void synthStopAll()
{
  MIDI_SYNTH.sendControlChange(120, 127, midiChannelSynth);
}

//----------------------------------------
// Looper controls
void looperRecPlay1(){
  //MIDI_LOOPER.sendControlChange(3, 127, midiChannelLooper);
}
void looperRecPlay2(){
  //MIDI_LOOPER.sendControlChange(22, 127, midiChannelLooper);
}
void looperStop1(){
  //MIDI_LOOPER.sendControlChange(9, 127, midiChannelLooper);
}
void looperStop2(){
  //MIDI_LOOPER.sendControlChange(23, 127, midiChannelLooper);
}
void looperClear1(){
  //MIDI_LOOPER.sendControlChange(14, 127, midiChannelLooper);
}
void looperClear2(){
  //MIDI_LOOPER.sendControlChange(24, 127, midiChannelLooper);
}
void looperStopAll(){
  //MIDI_LOOPER.sendControlChange(29, 127, midiChannelLooper);
}
void looperClearAll(){
  //MIDI_LOOPER.sendControlChange(30, 127, midiChannelLooper);
}
void looperParallelSerial(){
  //MIDI_LOOPER.sendControlChange(85, 127, midiChannelLooper);
}


//----------------------------------------
// Handles
void handleClock()
{
  MIDI_SYNTH.sendClock();
  //MIDI_LOOPER.sendClock();

  ticksCount++;

  if (ticksCount == 24)
  {
    // elapsedClockTime = millis() - previousClockTime;
    // previousClockTime = millis();
    //Serial.print("Clock received - time from previous: ");
    //Serial.print(elapsedClockTime);
    // estimatedBPM = int(60 / (float(elapsedClockTime) / 1000.));
    //Serial.print(" ms. Estimated BPM = ");
    //Serial.print(estimatedBPM);
    digitalWrite(beatLed, HIGH);
    isLedOn = true;
    timeLedOn = millis();
    ticksCount = 0;
    if (state == S_WAIT_TO_START)
    {
      //Serial.print(" Current Quarter PRE = ");
      //Serial.println(currentPreQuarter);
      currentPreQuarter++;
    }
    else if (state == S_PLAYING)
    {
      //Serial.print(" Current Quarter SONG = ");
      //Serial.println(currentSongQuarter);
      currentSongQuarter++;
    }
  }
}

void handleStart()
{
  // The start makes the program start counting
  if (state == S_SONG_LOADED)
  {
    state = S_WAIT_TO_START;
  }
}

void handleStop()
{
  synthStopAll();
  looperStopAll();
  looperClearAll();
  state = S_LOAD;
  currentPreQuarter = 0;
  currentSongQuarter = 0;
}


void setup()
{
  Serial.begin(9600);
  while (! Serial) {}
  delay(1000);

  pinMode(beatLed, OUTPUT);

  // Initialize MIDI.
  MIDI.begin(MIDI_CHANNEL_OMNI);
  //MIDI_LOOPER.begin();
  MIDI_SYNTH.begin();
  MIDI.setHandleClock(handleClock);
  MIDI.setHandleStart(handleStart);
  MIDI.setHandleStop(handleStop);

  // Initialize LCD and Encoder.
  lcd.init(); // LCD driver initialization
  lcd.backlight(); // Open the backlight
  lcd.clear();
  myEnc.setClickHandler(onClicked);
  myEnc.setEncoderHandler(onEncoder);

  // Initialize the SD and load files
  if (!sd.begin(SD_CONFIG)) {
    sd.initErrorHalt(&Serial);
  }

  // Load songs
  loadSongList();

  if (numSongs > 0)
  {
    //Serial.println("");
    //Serial.print("Songs loaded! Found ");
    //Serial.print(numSongs);
    //Serial.println(" songs.");
    displaySongs();
  }
  else
  {

  }
    //Serial.println("Failed to load songs");
}


void loop()
{
  // Beat LED
  if ((isLedOn) && ((millis() - timeLedOn) > ledDurationMs))
  {
    digitalWrite(beatLed, LOW);
    isLedOn = false;
  }
  // Read MIDI
  if (MIDI.read()){}

  switch (state)
  {
    case S_LOAD:
      //load current song
      loadSong(currentSong);
      state = S_SONG_LOADED;
      break;
    case S_SONG_LOADED:
      //wait for start (handled by handleStart) or reload song if user input
      myEnc.update();
      break;

    case S_WAIT_TO_START:
      if (currentPreQuarter >= waitForQuarters)
      {
        //Serial.print(currentPreQuarter);
        //Serial.println(" --> S_PLAYING");
        state = S_PLAYING;
        currentPreQuarter = 0;
      }
      break;
    case S_PLAYING:
      // Here we have to control the synth and the looper based on the currentSongQuarter (or play the MIDI file)
      break;
  }

    // if (current == stopAt)
    // {
    //   MIDI_SYNTH.sendStop();
    //   printedStart = false;
    //   //Serial.println("Stop");
    //   current = 0;
    //   int bankMSB = program / 127;
    //   int programLSB = program % 127;
    //   //Serial.print("Change Preset to ");
    //   //Serial.println(programLSB);

    //   MIDI_SYNTH.sendControlChange(0, bankMSB, midiChannelSynth);
    //   MIDI_SYNTH.sendProgramChange(programLSB, midiChannelSynth);
    //   program++;
    //   if (program > 255)
    //     program = 0;
    // }
}

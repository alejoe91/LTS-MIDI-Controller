/*
Connects all MIDI devices.

- Midronome: default HW Serial (RX, TX)
- Looper: SW Serial (9, 10)
- Looper: SW Serial (11, 12)

The program sequentially starts every Synth program and (TODO) starts/stops/play looper channels every 4 beats.


TODO:

- add MIDI song

Maybe buy Arduino 2 or Mega?

*/
#include <SdFat.h>
#include <EncoderButton.h>
#include <LiquidCrystal_I2C.h>
#include <MIDI.h>
#include <SoftwareSerial.h>

// Use these for optional defines
#define SYNTH 0
#define LOOPER 0

#if SYNTH
  #define midiChannelSynth 3
  #define SYNTH_MIDI_RX 11
  #define SYNTH_MIDI_TX 12
#endif

#if LOOPER
  #define midiChannelLooper 4
  #define LOOPER_MIDI_RX 9
  #define LOOPER_MIDI_TX 10
#endif

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
#if LOOPER
  uint16_t looperBeats[20] = {0};
  char looperCmds[20][3] = {""};
  uint8_t numLooperCmds = 0;
  uint8_t lastLooperCmdIndex = 0;
#endif

// Synth commands can be: P (Play) or S (Stop)
// E.g. 33-129-P --> Play Preset 129 at beat 33 OR 127-0-S --> Stop at beat 127 (the second entry is ignored)
#if SYNTH
  uint16_t synthBeats[20] = {0};
  uint16_t synthPresets[20] = {0};
  char synthCmds[20][2] = {""};
  uint8_t numSynthCmds = 0;
  uint8_t lastSynthCmdIndex = 0;
  uint16_t currentProgram = 0;
#endif

// clock count
// uint16_t estimatedBPM = 0;
// long elapsedClockTime;
// long previousClockTime;
long timeLedIdleOn = 0;
long timeLedSongOn = 0;
bool isLedIdleOn = false;
bool isLedSongOn = false;
const uint8_t ledDurationMs = 100;

bool songStarted = false;
bool printedStart = false;
uint8_t ticksCount = 0; // 0-24 ticks
uint8_t currentPreQuarter = 0;
uint16_t currentSongQuarter = 0;
uint16_t waitForQuarters = 8;
uint16_t fakeSongLen = 8;

//------------------------------------------------------------------------------
// LCD configuration
const int pinInput1 = A0;  
const int pinInput2 = A1;
const int pinButton = 5;

EncoderButton myEnc(pinInput1, pinInput2, pinButton);
LiquidCrystal_I2C lcd(0x27,20,4);

bool lcdIsOn = true;

//------------------------------------------------------------------------------
// MIDI configuration
using Transport = MIDI_NAMESPACE::SerialMIDI<SoftwareSerial>;

// SYNTH
#if SYNTH
  SoftwareSerial serialSynth = SoftwareSerial(SYNTH_MIDI_RX, SYNTH_MIDI_TX);
  Transport serialMIDISynth(serialSynth);
  MIDI_NAMESPACE::MidiInterface<Transport> MIDI_SYNTH((Transport&)serialMIDISynth);
#endif

// LOOPER
#if LOOPER
  SoftwareSerial serialLooper = SoftwareSerial(LOOPER_MIDI_RX, LOOPER_MIDI_TX);
  Transport serialMIDILooper(serialLooper);
  MIDI_NAMESPACE::MidiInterface<Transport> MIDI_LOOPER((Transport&)serialMIDILooper);
#endif

// MIDRONOME - MIDI default RX, TX
MIDI_CREATE_DEFAULT_INSTANCE();

const uint8_t beatIdleLed = 6;
const uint8_t beatSongLed = 7;

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
  state = S_LOAD;
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
          numSongs++;

          Serial.write('/');
          while (fileSub.openNext(&songDir, O_RDONLY))
          {
            fileSub.getName(filename, size);
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
  // Serial.print("Current pre: ");
  // Serial.print(currentSong);
  // Serial.print(" - First pre: ");
  // Serial.print(firstSongDisplayed);
  // Serial.print(" - Last pre: ");
  // Serial.println(lastSongDisplayed);
  if (currentSong < 0)
  {
    // Serial.println("Current song negative");
    currentSong = numSongs - 1;
    firstSongDisplayed = currentSong - numSongsDisplayed + 1;
    lastSongDisplayed = currentSong;
  }
  else if (currentSong > numSongs - 1)
  {
    // Serial.println("Current song greater than num songs");
    currentSong = 0;
    firstSongDisplayed = 0;
    lastSongDisplayed = numSongsDisplayed - 1;
  }
  else if (currentSong > lastSongDisplayed)
  {
    // Serial.println("Current song greater than last displayed");
    lastSongDisplayed = currentSong;
    firstSongDisplayed = lastSongDisplayed - numSongsDisplayed + 1;
  }
  else if (currentSong < firstSongDisplayed)
  {
    // Serial.println("Current song less than first displayed");
    firstSongDisplayed = currentSong;
    lastSongDisplayed = firstSongDisplayed + numSongsDisplayed - 1;
  }

  Serial.print("Current post: ");
  Serial.print(currentSong);
  Serial.print(" - First post: ");
  Serial.print(firstSongDisplayed);
  Serial.print(" - Last post: ");
  Serial.println(lastSongDisplayed);

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
    Serial.println("Root open failed");
  }
  else
  {
    File file;
    Serial.print("Opening song ");
    Serial.println(songName);

    if (!(songDir.open(&rootDir, songName, O_RDONLY)))
    {
      Serial.println("Song folder open failed");
    }
    else
    {
      #if LOOPER
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
    #endif

    #if SYNTH
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
    #endif
    songDir.close();
    Serial.println("Song loaded!");
    }
    rootDir.close();
  }
}

//----------------------------------------
// Synth controls
#if SYNTH
  void synthChangeProgram(int programNumber)
  {
    int bankMSB, bankLSB;
    bankMSB = programNumber / 127;
    bankLSB = programNumber % 127;

    MIDI_SYNTH.sendControlChange(0, bankMSB, midiChannelSynth);
    MIDI_SYNTH.sendProgramChange(bankLSB, midiChannelSynth);
  }

  void synthStopAll()
  {
      MIDI_SYNTH.sendControlChange(120, 127, midiChannelSynth);
  }
#endif



//----------------------------------------
// Looper controls
#if LOOPER
  void looperRecPlay1(){
    MIDI_LOOPER.sendControlChange(3, 127, midiChannelLooper);
  }
  void looperRecPlay2(){
    MIDI_LOOPER.sendControlChange(22, 127, midiChannelLooper);
  }
  void looperStop1(){
    MIDI_LOOPER.sendControlChange(9, 127, midiChannelLooper);
  }
  void looperStop2(){
    MIDI_LOOPER.sendControlChange(23, 127, midiChannelLooper);
  }
  void looperClear1(){
    MIDI_LOOPER.sendControlChange(14, 127, midiChannelLooper);
  }
  void looperClear2(){
    MIDI_LOOPER.sendControlChange(24, 127, midiChannelLooper);
  }
  void looperStopAll(){
    MIDI_LOOPER.sendControlChange(29, 127, midiChannelLooper);
  }
  void looperClearAll(){
    MIDI_LOOPER.sendControlChange(30, 127, midiChannelLooper);
  }
  void looperParallelSerial(){
    MIDI_LOOPER.sendControlChange(85, 127, midiChannelLooper);
  }
#endif

void songFinished()
{
  #if SYNTH
    synthStopAll();
  #endif
  #if LOOPER
    looperStopAll();
    looperClearAll();
  #endif
  Serial.println("Song finished");
  state = S_SONG_LOADED;
}


//----------------------------------------
// Handles
void handleClock()
{
  // TODO check if start comes before or after clock (in case move at the end)
  #if SYNTH
    MIDI_SYNTH.sendClock();
  #endif
  #if LOOPER
    MIDI_LOOPER.sendClock();
  #endif

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
    if (state == S_PLAYING)
    {
      digitalWrite(beatSongLed, HIGH);
      isLedSongOn = true;
      timeLedSongOn = millis();
    }
    else
    {
      digitalWrite(beatIdleLed, HIGH);
      isLedIdleOn = true;
      timeLedIdleOn = millis();
    }

    ticksCount = 0;
    if (state == S_WAIT_TO_START)
    {
      // Serial.print(" Current Quarter PRE = ");
      // Serial.println(currentPreQuarter);
      currentPreQuarter++;
    }
    else if (state == S_PLAYING)
    {
      //Serial.print(" Current Quarter SONG = ");
      //Serial.println(currentSongQuarter);
      currentSongQuarter++;
      #if SYNTH
        for (int i=lastSynthCmdIndex; i<numSynthCmds; i++)
        {
          if (synthBeats[i] == currentSongQuarter)
          {
            Serial.print("Scmd @: ");
            Serial.print(synthBeats[i]);

            if (strcmp(synthCmds[i], "P") == 0) // Change program and play
            {
              // Serial.print("Play -  ");
              // Serial.println(synthPresets[i]);
              MIDI_SYNTH.sendStop();
              if (currentProgram != synthPresets[i])
              {
                synthChangeProgram(synthPresets[i]);
                currentProgram = synthPresets[i];
              }
              MIDI_SYNTH.sendStart();
            }
            else if (strcmp(synthCmds[i], "C") == 0) // Change program 
            {
              // Serial.print("Change -  ");
              // Serial.println(synthPresets[i]);
              if (currentProgram != synthPresets[i])
              {
                synthChangeProgram(synthPresets[i]);
                currentProgram = synthPresets[i];
              }
            }
            else if (strcmp(synthCmds[i], "S") == 0) // Stop
            {
              // Serial.println("Stop");
              MIDI_SYNTH.sendStop();
            }
          }
          else if (synthBeats[i] > currentSongQuarter)
          {
            lastSynthCmdIndex = i;
            break;
          }
        }
      #endif
      #if LOOPER
        for (int i=lastLooperCmdIndex; i<numLooperCmds; i++)
        {
          if (looperBeats[i] == currentSongQuarter)
          {
            if (strcmp(looperCmds[i], "R1") == 0) // Rec/Play/Dub 1
            {
              looperRecPlay1();
            }
            else if (strcmp(looperCmds[i], "R2") == 0) // Rec/Play/Dub 1
            {
              looperRecPlay2();
            }
            else if (strcmp(looperCmds[i], "S1") == 0) // Stop 1
            {
              looperStop1();
            }
            else if (strcmp(looperCmds[i], "S2") == 0) // Stop2
            {
              looperStop2();
            }
            else if (strcmp(looperCmds[i], "C1") == 0) // Clear 1
            {
              looperClear1();
            }
            else if (strcmp(looperCmds[i], "C2") == 0) // Clear 2
            {
              looperClear2();
            }
            else if (strcmp(looperCmds[i], "SA") == 0) // Stop All
            {
              looperStopAll();
            }
            else if (strcmp(looperCmds[i], "CA") == 0) // Clear All
            {
              looperClearAll();
            }
            else if (strcmp(looperCmds[i], "PS") == 0) // Parallel/Serial Toggle
            {
              looperParallelSerial();
            }
          }
          else if (looperBeats[i] > currentSongQuarter)
          {
            lastLooperCmdIndex = i;
            break;
          }
        }
      #endif
      #if SYNTH && LOOPER
        if ((currentSongQuarter > looperBeats[numLooperCmds - 1]) && (currentSongQuarter > synthBeats[numSynthCmds - 1]))
        {
          songFinished();
        }
      #elif SYNTH
        if (currentSongQuarter > synthBeats[numSynthCmds - 1])
        {
          songFinished();
        }
      #elif LOOPER
        if (currentSongQuarter > looperBeats[numLooperCmds - 1])
        {
          songFinished();
        }
      #else
        if (currentSongQuarter >= fakeSongLen)
        {
          songFinished();
        }
      #endif
    }
  }
}

void handleStart()
{
  // The start makes the program start counting
  if (state == S_SONG_LOADED)
  {
    Serial.println("Start! Waiting ");
    state = S_WAIT_TO_START;
    digitalWrite(beatSongLed, HIGH);
  }
}

void handleStop()
{
  #if SYNTH
    synthStopAll();
  #endif
  #if LOOPER
    looperStopAll();
    looperClearAll();
  #endif
  Serial.println("Stop! Reset");
  state = S_LOAD;
  currentPreQuarter = 0;
  currentSongQuarter = 0;
}


void setup()
{
  // Serial.begin(9600);
  // while (! Serial) {}
  delay(1000);

  pinMode(beatIdleLed, OUTPUT);
  pinMode(beatSongLed, OUTPUT);

  // Initialize MIDI.
  MIDI.begin(MIDI_CHANNEL_OMNI);
  MIDI.setHandleClock(handleClock);
  MIDI.setHandleStart(handleStart);
  MIDI.setHandleStop(handleStop);

  #if LOOPER
    MIDI_LOOPER.begin();
  #endif
  #if SYNTH
    MIDI_SYNTH.begin();
  #endif

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
    // Serial.println("");
    // Serial.print("Songs loaded! Found ");
    // Serial.print(numSongs);
    // Serial.println(" songs.");
    displaySongs();
  }
  else
  {
    // Serial.println("Failed to load songs");
  }
    
}


void loop()
{
  // Beat LED
  if ((isLedIdleOn) && ((millis() - timeLedIdleOn) > ledDurationMs))
  {
    digitalWrite(beatIdleLed, LOW);
    isLedIdleOn = false;
  }
  else if ((isLedSongOn) && ((millis() - timeLedSongOn) > ledDurationMs))
  {
    digitalWrite(beatSongLed, LOW);
    isLedSongOn = false;
  }
  // Read MIDI
  if (MIDI.read()){}

  switch (state)
  {
    case S_LOAD:
      //load current song
      loadSong(songList[currentSong]);
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
        Serial.println("Play");
        state = S_PLAYING;
        currentPreQuarter = 0;
        digitalWrite(beatSongLed, LOW);
      }
      break;
    case S_PLAYING:
      // Here we have to control the synth and the looper based on the currentSongQuarter (or play the MIDI file)
      break;
  }
}

/*
Connects all MIDI devices.

TODO: 

- check why hold turns on
- Add settings:
    - synth mode
    - looper mode (on/off)
    - something else
- DOCS
*/
#include <SdFat.h>
#include <EncoderButton.h>
#include <LiquidCrystal_I2C.h>
#include <MIDI.h>
#include <MD_MIDIFile.h>
#include <SoftwareSerial.h>

#define MIDRO_MIDI_RX 12
#define MIDRO_MIDI_TX 11

#define midiChannelLooper 4
#define LOOPER_MIDI_RX 5
#define LOOPER_MIDI_TX 4
// #define LOOPER_MIDI_RX 7
// #define LOOPER_MIDI_TX 6


#define midiChannelSynth 3
#define SYNTH_MIDI_RX 7
#define SYNTH_MIDI_TX 6
// #define SYNTH_MIDI_RX 3
// #define SYNTH_MIDI_TX 2

#define CUSTOM_MIDI_RX 3
#define CUSTOM_MIDI_TX 2


#define TICKS_PER_QUARTER_CLK 24

//------------------------------------------------------------------------------
// SD Card configuration
#define SD_FAT_TYPE 0
// #define error(s) sd.errorHalt(&Serial, F(s))
// Try max SPI clock for an SD. Reduce SPI_CLOCK if errors occur.
#define SPI_CLOCK SD_SCK_MHZ(50)


const uint8_t SD_CS_PIN = 53;

//-----------------------------------------------------------------------------
// Program variables
static enum { S_LOAD, S_SETTINGS, S_SONG_LOADED, S_WAIT_TO_START, S_PLAYING } state;
static enum { FULL, PARTIAL, OFF } synthMode = FULL;

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

// Looper commands can be: R1-R2-S1-S2-C1-C2-SA-CA
// E.g. 33-R1 --> Rec/Play/Dub Channel 1 at beat 33
uint16_t looperBeats[20] = {0};
char looperCmds[20][3] = {""};
uint8_t numLooperCmds = 0;
uint8_t lastLooperCmdIndex = 0;


// Synth commands can be: P (Play) or S (Stop)
// E.g. 33-129-P --> Play Preset 129 at beat 33 OR 127-0-S --> Stop at beat 127 (the second entry is ignored)
#define ADVANCE_PLAY 2
#define ADVANCE_PRESET 4

uint16_t synthBeats[50] = {0};
uint16_t synthPresets[50] = {0};
char synthCmds[50][2] = {""};
uint8_t numSynthCmds = 0;
uint8_t lastSynthCmdIndex = 0;
uint16_t currentProgram = 0;
bool hasMidiFile = false;
bool startMidi = false;

uint16_t ticksPerQuarterMidi = 0;
uint8_t numTracks = 0;

MD_MIDIFile SMF;
uint8_t note, velocity, channel;
int tickMultiplier;


// clock count
uint16_t estimatedBPM = 0;
long elapsedClockTime;
long previousClockTime;
long timeLedIdleOn = 0;
long timeLedSongOn = 0;
bool isLedIdleOn = false;
bool isLedSongOn = false;
const uint8_t ledDurationMs = 100;


bool songStarted = false;
bool printedStart = false;
uint8_t ticksCount = 0; // 0-TICKS_PER_QUARTER_CLK ticks
uint8_t previousTicksCount = 0; // 0-TICKS_PER_QUARTER_CLK ticks
uint8_t currentPreQuarter = 0;
uint16_t currentSongQuarter = 0;
uint16_t waitForQuarters = 8;
uint16_t fakeSongLen = 8;

//------------------------------------------------------------------------------
// ENCODER configuration
const int encPinInput1 = A8;
const int encPinInput2 = A9;
const int encPinButton = 30;
long timeLastEncoder = 0;
const uint8_t minEncoderChangePeriod = 50;
EncoderButton myEnc(encPinInput1, encPinInput2, encPinButton);

//------------------------------------------------------------------------------
// LCD configuration
LiquidCrystal_I2C lcd(0x27,20,4);
bool lcdIsOn = true;

//------------------------------------------------------------------------------
// MIDI configuration
using Transport = MIDI_NAMESPACE::SerialMIDI<SoftwareSerial>;

// MIDRONOME
SoftwareSerial serialMidro = SoftwareSerial(MIDRO_MIDI_RX, MIDRO_MIDI_TX);
Transport serialMIDIMidro(serialMidro);
MIDI_NAMESPACE::MidiInterface<Transport> MIDI_MIDRO((Transport&)serialMIDIMidro);


// SYNTH
SoftwareSerial serialSynth = SoftwareSerial(SYNTH_MIDI_RX, SYNTH_MIDI_TX);
Transport serialMIDISynth(serialSynth);
MIDI_NAMESPACE::MidiInterface<Transport> MIDI_SYNTH((Transport&)serialMIDISynth);


// LOOPER
SoftwareSerial serialLooper = SoftwareSerial(LOOPER_MIDI_RX, LOOPER_MIDI_TX);
Transport serialMIDILooper(serialLooper);
MIDI_NAMESPACE::MidiInterface<Transport> MIDI_LOOPER((Transport&)serialMIDILooper);


const uint8_t beatIdleLed = 43;
const uint8_t beatSongLed = 42;
const uint8_t errLed = 41;

//----------------------------------------
// Data loading
// RESET
void onLongClick(EncoderButton& eb) {

  // RESET state
  digitalWrite(errLed, HIGH);
  lcd.noBacklight();
  delay(200);
  lcd.clear();
  lcd.backlight();
  lcd.setCursor(5, 1);
  lcd.print("RESET");
  delay(500);

  // Stop all MIDI
  songFinished();

  // Restart
  currentSong = 0;
  displaySongs();
  state = S_LOAD;
  digitalWrite(errLed, LOW);
}

// SETTINGS modes - toggle between S_SETTINGS and S_LOAD
void onDoubleClick(EncoderButton& eb) {
  if (state != S_SETTINGS)
  {
    state = S_SETTINGS;
    displaySettings();
  }
  else
  {
    displaySongs();
    state = S_LOAD;
  }
}


void onEncoder(EncoderButton& eb) {
  int increment = eb.increment();
  int numSynthModes = 3;

  if ((millis() - timeLastEncoder) > minEncoderChangePeriod)
  {
    if (state == S_SETTINGS)
    {
      synthMode = synthMode + increment;
      synthMode = synthMode % numSynthModes;
      displaySettings();
    }
    else
    {
      currentSong = currentSong + increment;
      displaySongs();
      state = S_LOAD;
    }
  }
  timeLastEncoder = millis();
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

void flashLedError(uint16_t delayMs, uint8_t numRep)
{
  for (int i=0; i<numRep; i++)
  {
    digitalWrite(errLed, LOW);
    delay(delayMs);
    digitalWrite(errLed, HIGH);
    delay(delayMs);
  }
  digitalWrite(errLed, LOW);
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
    Serial.println("Root dir open failed");
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

void displayOnWait()
{
  if (currentPreQuarter == 1)
  {
    lcd.clear();
    lcd.setCursor(2, 0);
    lcd.print(songList[currentSong]);
    lcd.setCursor(2, 2);
    lcd.print("Starting in ");
  }
  lcd.setCursor(15, 2);
  lcd.print(waitForQuarters - currentPreQuarter);
}

void displayOnPlay()
{
  lcd.setCursor(2, 2);
  lcd.print("              ");
  lcd.setCursor(2, 2);
  lcd.print("Playing!");

  if (numLooperCmds > 0)
  {
    lcd.setCursor(3, 3);
    lcd.print("L");
  }
  

  if (numSynthCmds > 0)
  {
    lcd.setCursor(9, 3);
    lcd.print("S");
  }
  if (hasMidiFile)
  {
    lcd.setCursor(15, 3);
    lcd.print("M");
  }
  
}

void displaySettings()
{
  // delete cursors
  lcd.clear();
  // Display cursor
  lcd.setCursor(0, 0);
  lcd.print("SYNTH MODE");
  lcd.setCursor(2, 1);
  lcd.print("Full");
  lcd.setCursor(2, 2);
  lcd.print("Partial");
  lcd.setCursor(2, 3);
  lcd.print("Off");

  // Display cursor
  lcd.setCursor(0, 1 + int(synthMode));
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
    Serial.print(currentSong + 1);
    Serial.print(" ");
    Serial.print(songName);
    Serial.print("...");

    if (!(songDir.open(&rootDir, songName, O_RDONLY)))
    {
      Serial.println("Song folder open failed");
    }
    else
    {
      numLooperCmds = 0;
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
    
    numSynthCmds = 0;
    // TODO: here take care of different synth modes
    if (file.open(&songDir, "synth.txt", O_RDONLY))
    {
      // parse synth
      //Serial.println("Parsing synth");

      char currentCmd[20]; // max size is 5 + NULL character
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
      for (int i=0; i<numSynthCmds; i++)
      {
        Serial.println("");
        Serial.print("Beat: ");
        Serial.print(synthBeats[i]);
        Serial.print(" - Preset: ");
        Serial.print(synthPresets[i]);
        Serial.print(" - CMD: ");
        Serial.println(synthCmds[i]);
      }
    }

    // try to load MIDI file
    hasMidiFile = false;

    // Restart SMF object
    SMF.close();
    // SMF.begin(&sd);

    char midiFileName[50];
    midiFileName[0] = '\0';
    strcat(midiFileName, songName);
    strcat(midiFileName, "/");
    strcat(midiFileName, "synth.mid");

    if (file.open(&songDir, "synth.mid", O_RDONLY))
    {
      file.close();

      int err = SMF.load(midiFileName);

      if (err == MD_MIDIFile::E_OK)
      {
        hasMidiFile = true;
        Serial.println("MIDI loaded!\n");
        Serial.print("\tNum. tracks: ");
        numTracks = SMF.getTrackCount();
        Serial.println(numTracks);
        Serial.print("\tTicks per quarter note: ");
        ticksPerQuarterMidi = SMF.getTicksPerQuarterNote();
        Serial.println(ticksPerQuarterMidi);
        Serial.print("\tTick multiplier: ");
        tickMultiplier = int(ticksPerQuarterMidi / TICKS_PER_QUARTER_CLK);
        Serial.println(tickMultiplier);
        Serial.print("\tMIDI format: ");
        Serial.println(SMF.getFormat());
      }
      else
      {
        // Flash error led 3 times
        flashLedError(200, 3);
      }
    }
    
    songDir.close();
    Serial.println("Song loaded!");
    }
    rootDir.close();
  }
}

//----------------------------------------
// Synth controls
void synthChangeProgram(int programNumber)
{
  int bankMSB, bankLSB;
  bankMSB = programNumber / 128;
  bankLSB = programNumber % 128;
  Serial.print("\tProg. Change: ");
  Serial.println(programNumber);

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


void songFinished()
{
  MIDI_SYNTH.sendStop();
  synthStopAll();
  lastSynthCmdIndex = 0;
  if (hasMidiFile)
    SMF.close();
  hasMidiFile = false;
  startMidi = false;
  
  looperStopAll();
  looperClearAll();
  lastLooperCmdIndex = 0;
  
  Serial.println("Song finished");
  state = S_LOAD;
  currentSongQuarter = 0;
  currentPreQuarter = 0;
  currentSong++;
  currentSong = currentSong % numSongs;
  displaySongs();
}


//----------------------------------------
// Handles
void handleClock()
{

  ticksCount++;
  // Propagate clock
  MIDI_SYNTH.sendClock();
  
  MIDI_LOOPER.sendClock();
  

  // Change preset 8 ticks before Play
  if (ticksCount == TICKS_PER_QUARTER_CLK - ADVANCE_PRESET)
  {
    if (state == S_PLAYING)
    {
        for (int i=lastSynthCmdIndex; i<numSynthCmds; i++)
          {
            if (synthBeats[i] == currentSongQuarter + 1)
            {
              uint16_t changeProgram = synthPresets[i] - 1; //0--based

              if (strcmp(synthCmds[i], "P") == 0) // Change program and play
              {
                Serial.print("Changing preset before Play -  ");
                Serial.println(synthPresets[i]);

                if (currentProgram != changeProgram)
                {
                  synthStopAll();
                  synthChangeProgram(changeProgram);
                  currentProgram = changeProgram;
                }
              }
              else if (strcmp(synthCmds[i], "C") == 0) // Stop
              {
                if (currentProgram != changeProgram)
                {
                  Serial.println("Change preset the actual clock");
                  synthStopAll();
                  synthChangeProgram(changeProgram);
                  currentProgram = changeProgram;
                }
              }
            }
            else if (synthBeats[i] > currentSongQuarter + 1)
              break;
          }
        
      }
    }
    // send play a couple of ticks before actual Quarter
    else if (ticksCount == TICKS_PER_QUARTER_CLK - ADVANCE_PLAY)
    {
      if (state == S_PLAYING)
      {
        for (int i=lastSynthCmdIndex; i<numSynthCmds; i++)
        {
          if (synthBeats[i] == currentSongQuarter + 1)
          {
            if (strcmp(synthCmds[i], "P") == 0) // Change program and play
            {
              Serial.println("Sending Play before Play");
              MIDI_SYNTH.sendStart();
            }
            else if (strcmp(synthCmds[i], "S") == 0) // Stop
            {
              Serial.println("Stop before the actual clock");
              MIDI_SYNTH.sendStop();
            }
          }
          else if (synthBeats[i] > currentSongQuarter + 1)
            break;
        }
        
        for (int i=lastLooperCmdIndex; i<numLooperCmds; i++)
        {
          if (looperBeats[i] == currentSongQuarter + 1)
          {
            // These looper commands are sent ADVANCE_PLAY ticks before the actual quarter
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
            else if (strcmp(looperCmds[i], "SA") == 0) // Stop All
            {
              looperStopAll();
            }
          }
          else if (looperBeats[i] > currentSongQuarter + 1)
            break;
        }
        
      }
    }

  if (ticksCount == TICKS_PER_QUARTER_CLK)
  {
    elapsedClockTime = millis() - previousClockTime;
    previousClockTime = millis();
    //Serial.print("Clock received - time from previous: ");
    //Serial.print(elapsedClockTime);
    estimatedBPM = int(60 / (float(elapsedClockTime) / 1000.));
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
      currentPreQuarter++;
      Serial.print("\tStart in... ");
      Serial.println(waitForQuarters - currentPreQuarter);
      displayOnWait();
    }
    else if (state == S_PLAYING)
    {
      // Serial.print("\tSong at... ");
      // Serial.println(currentSongQuarter);
      if (hasMidiFile)
      {
        startMidi = true;
        previousTicksCount = TICKS_PER_QUARTER_CLK - 1;
      }
      currentSongQuarter++;
      for (int i=lastSynthCmdIndex; i<numSynthCmds; i++)
      {
        if (synthBeats[i] == currentSongQuarter)
        {
          Serial.print("Synth CMD at: ");
          Serial.print(synthBeats[i]);

          if (strcmp(synthCmds[i], "P") == 0) // Change program and play
          {
            Serial.print(" Play -  ");
            Serial.println(synthPresets[i]);
            // Program change is handled ADVANCE_PRESET ticks in advance previous tick
            // Start is sent ADVANCE_PLAY ticks in advance
          }
          else if (strcmp(synthCmds[i], "C") == 0) // Change program 
          {
            // Change preset is sent ADVANCE_PLAY ticks in advance
            Serial.print(" Change -  ");
            Serial.println(synthPresets[i]);
          }
          else if (strcmp(synthCmds[i], "S") == 0) // Stop
          {
            // Stop is sent ADVANCE_PLAY ticks in advance
            Serial.println(" Stop!");
          }
        }
        else if (synthBeats[i] > currentSongQuarter)
        {
          lastSynthCmdIndex = i;
          break;
        }
      }
      
      for (int i=lastLooperCmdIndex; i<numLooperCmds; i++)
      {
        if (looperBeats[i] == currentSongQuarter)
        {
          Serial.print("Looper CMD at: ");
          Serial.print(looperBeats[i]);
          if (strcmp(looperCmds[i], "R1") == 0) // Rec/Play/Dub 1
          {
            Serial.println(" Rec/Play/Dub 1");
            // looperRecPlay1();
          }
          else if (strcmp(looperCmds[i], "R2") == 0) // Rec/Play/Dub 1
          {
            Serial.println(" Rec/Play/Dub 2");
            // looperRecPlay2();
          }
          else if (strcmp(looperCmds[i], "S1") == 0) // Stop 1
          {
            Serial.println(" Stop 1");
            // looperStop1();
          }
          else if (strcmp(looperCmds[i], "S2") == 0) // Stop2
          {
            Serial.println(" Stop 2");
            // looperStop2();
          }
          else if (strcmp(looperCmds[i], "C1") == 0) // Clear 1
          {
            Serial.println(" Clear 1");
            looperClear1();
          }
          else if (strcmp(looperCmds[i], "C2") == 0) // Clear 2
          {
            Serial.println(" Clear 2");
            looperClear2();
          }
          else if (strcmp(looperCmds[i], "SA") == 0) // Stop All
          {
            Serial.println(" Stop all");
            // looperStopAll();
          }
          else if (strcmp(looperCmds[i], "CA") == 0) // Clear All
          {
            Serial.println(" Clear all");
            looperClearAll();
          }
          else if (strcmp(looperCmds[i], "PS") == 0) // Parallel/Serial Toggle
          {
            Serial.println(" Paralllel / Serial toggle");
            looperParallelSerial();
          }
        }
        else if (looperBeats[i] > currentSongQuarter)
        {
          lastLooperCmdIndex = i;
          break;
        }
      }
      
      if ((currentSongQuarter > looperBeats[numLooperCmds - 1]) && (currentSongQuarter > synthBeats[numSynthCmds - 1]))
      {
        if (!hasMidiFile)
          songFinished();
        else if (SMF.isEOF())
          songFinished();
      }
    }
  }
}

void handleStart()
{
  // Reset click count!
  ticksCount = 0;
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
  MIDI_SYNTH.sendStop();
  synthStopAll();
  lastSynthCmdIndex = 0;
  if (hasMidiFile)
    SMF.close();
  hasMidiFile = false;
  startMidi = false;
  

  looperStopAll();
  looperClearAll();
  lastLooperCmdIndex = 0;

  
  Serial.println("Stop! Reset");
  digitalWrite(beatSongLed, LOW);
  state = S_LOAD;
  currentPreQuarter = 0;
  currentSongQuarter = 0;
  displaySongs();
}

//---------------------------------------------------------
// MIDI file functions
uint16_t tickClock(void)
// Return number of ticks since last call 
{
  int16_t ticks = ticksCount - previousTicksCount;
  if (ticks < 0)
    // Take care of negative values
    ticks = TICKS_PER_QUARTER_CLK + ticks;
  ticks = ticks * tickMultiplier;
  previousTicksCount = ticksCount;

  return ticks;
}


void midiCallback(midi_event *pev)
// Skip program change, and use all the rest
{
  if ((pev->data[0] >= 0x80) && (pev->data[0] <= 0xe0) && (pev->data[0] != 0xC0))
  {
    // serialSynth.write(pev->data[0] | pev->channel);
    serialSynth.write(pev->data[0] | midiChannelSynth - 1);
    serialSynth.write(&pev->data[1], pev->size-1);
  }
  else
    serialSynth.write(pev->data, pev->size);
}

void sysexCallback(sysex_event *pev)
// Called by the MIDIFile library when a system Exclusive (sysex) file event needs 
// to be processed through the midi communications interface. Most sysex events cannot 
// really be processed, so we just ignore it here.
// This callback is set up in the setup() function.
{}




void setup()
{
  Serial.begin(9600);
  while (! Serial) {}
  delay(1000);
  Serial.println("Initializing");


  Serial.println("LEDs");
  pinMode(beatIdleLed, OUTPUT);
  pinMode(beatSongLed, OUTPUT);
  pinMode(errLed, OUTPUT);

  // Initialize MIDI
  Serial.println("MIDI IN");
  MIDI_MIDRO.begin(MIDI_CHANNEL_OMNI);
  MIDI_MIDRO.setHandleClock(handleClock);
  MIDI_MIDRO.setHandleStart(handleStart);
  MIDI_MIDRO.setHandleStop(handleStop);
  
  Serial.println("MIDI OUT");
  MIDI_LOOPER.begin();
  MIDI_SYNTH.begin();
  

  // Initialize LCD and Encoder.
  Serial.println("LCD");
  lcd.init(); // LCD driver initialization
  lcd.backlight(); // Open the backlight
  lcd.clear();

  Serial.println("ENCODER");
  myEnc.setLongPressHandler(onLongClick);
  myEnc.setClickHandler(onDoubleClick);
  myEnc.setDoubleClickHandler(onDoubleClick);
  myEnc.setEncoderHandler(onEncoder);

  // Initialize the SD and load files
  //if (!sd.begin(SD_CONFIG)) {
  Serial.println("SD CARD");
  if (!sd.begin(SD_CS_PIN, SPI_FULL_SPEED))
  {
    Serial.println("Error initializing SD card");
    digitalWrite(errLed, HIGH);
    lcd.clear();
    lcd.setCursor(2, 1);
    lcd.print("SD Error: Reset!");
    sd.initErrorHalt(&Serial);
    // Keep this way until reset!
  }

  // Initialize MIDIFile
  Serial.println("MIDI FILE library");
  SMF.begin(&sd);
  SMF.setMidiHandler(midiCallback);
  SMF.setSysexHandler(sysexCallback);
  
  Serial.println("Done!");

  // Load songs
  loadSongList();

  if (numSongs > 0)
    displaySongs();
  else
    Serial.println("Failed to load songs");

  state = S_LOAD;
  Serial.println("Starting main loop");
  digitalWrite(errLed, LOW);
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
  if (MIDI_MIDRO.read()){}

  // Read Encoder
  myEnc.update();


  switch (state)
  {
    case S_LOAD:
      //load current song
      loadSong(songList[currentSong]);
      state = S_SONG_LOADED;
      break;
    case S_SONG_LOADED:
      //wait for start (handled by handleStart) or reload song if user input
      break;

    case S_WAIT_TO_START:
      // why? 
      if (currentPreQuarter >= waitForQuarters - 1)
      {
        Serial.println("Play");
        state = S_PLAYING;
        currentPreQuarter = 0;
        digitalWrite(beatSongLed, LOW);
        displayOnPlay();
      }
      break;
    case S_PLAYING:
      // Here we have to control the synth and the looper based on the currentSongQuarter (or play the MIDI file)
      if (hasMidiFile && startMidi)
      {
        uint32_t ticks = tickClock();
        if (ticks > 0)
        {
          SMF.processEvents(ticks);
          SMF.isEOF();
        }
      }
      break;
  }
}

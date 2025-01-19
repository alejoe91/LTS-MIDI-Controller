/*
Connects Midronome to an external MIDI devices chain for automation.

The system reads the available song automation MIDI files from the
SD card, allows the user to choose which song to play, and plays back
the MIDI once a PLAY event is received from the Midronome.
A STOP event stops all MIDI messages.

The Midronome MIDI input sets the tempo if connected, otherwise the internal tempo
of the MIDI is used (check!).

The output controls the MIDI devices:

For our setup:
- Channel 1 (configurable): UNO Synth Pro - IK Multimedia (notes + automation)
- Channel 3 (configurable): Ampero Stomp II - Hotone (automation)
- Channel 4 (not configurable): Ditto X4 Looper - TC (automation)


Synth modes:
FULL: Channel 1 + 2 are played
Partial: Channel 1 only is played
OFF: No channel 1 or 2 are played
**

*/
#include <math.h>

#include <SdFat.h>
#include <EncoderButton.h>
#include <MIDI.h>
#include <MD_MIDIFile.h>
#include <SoftwareSerial.h>
#include <LiquidCrystal_I2C.h>

#define MIDRO_MIDI_RX 12
#define MIDRO_MIDI_TX 11

#define OUT_MIDI_RX1 5
#define OUT_MIDI_TX1 4

#define OUT_MIDI_RX2 7
#define OUT_MIDI_TX2 6

#define midiChannelSynthOut 1

#define midiChannelSynth 1
#define midiChannelPedal 3
#define midiChannelLooper 4


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
static enum { S_WAIT_FOR_SYNC, S_LOAD, S_SETTINGS, S_SONG_LOADED, S_PRE_SONG, S_PLAYING } state;
// midiMode controls which channels are outputted to the MidiOut1 and MidiOut2
static enum { ALL_OFF, S_LP, ALL_ALL } midiMode = S_LP;

// data loading
SdFat sd;
File rootDir;
File nextFile;
File midiFile;
size_t size = 50;
char filename[50];

char songList[50][20];
uint16_t songTempos[50];
uint8_t numSongs = 0;
int8_t currentSong = 0;
uint8_t firstSongDisplayed = 0;
uint8_t lastSongDisplayed = 3;
const uint8_t numSongsDisplayed = 4;

bool startMidi = false;
bool acceptingSync = false;
bool startReceived = false;
bool midiPlay = true;
bool isMidronomeConnected = false;

MD_MIDIFile SMF;
uint8_t note, velocity, channel;
int tickMultiplier;
uint16_t ticksPerQuarterMidi = 0;
uint8_t numTracks = 0;
uint16_t midiTempo = 0;

// clock count
uint16_t currentSongTempo = 0;
uint16_t currentSongSignatureNumerator = 4;
uint8_t currentBeat = 0;
uint16_t oldTempo = 0;

long elapsedClockTime;
long previousClockTime;
long startingClockTime;
long elapsedTimeFromStart;
long elapsedClockTimeLoop;
long timeLedIdleOn = 0;
long timeLedBeatOn = 0;
long timeLedSongOn = 0;
long timeFromPlayStart;
long elapsedTimeFromPlay;
bool isLedIdleOn = false;
bool isLedBeatOn = false;
bool isLedSongOn = false;
const uint8_t ledDurationMs = 100;
const uint16_t ingoreStopsAfterPlayMs = 2000;


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

// OUT
SoftwareSerial serialOut1 = SoftwareSerial(OUT_MIDI_RX1, OUT_MIDI_TX1);
Transport serialMIDIOut1(serialOut1);
MIDI_NAMESPACE::MidiInterface<Transport> MIDI_OUT1((Transport&)serialMIDIOut1);

// OUT2
SoftwareSerial serialOut2 = SoftwareSerial(OUT_MIDI_RX2, OUT_MIDI_TX2);
Transport serialMIDIOut2(serialOut2);
MIDI_NAMESPACE::MidiInterface<Transport> MIDI_OUT2((Transport&)serialMIDIOut2);

const uint8_t beatIdleLed = 43;
const uint8_t beatSongLed = 42;
const uint8_t errLed = 41;

//----------------------------------------
// Data loading
// SYNC
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
  state = S_WAIT_FOR_SYNC;
  startingClockTime = millis();
  acceptingSync = false;
  Serial.println("AcceptingSync set to false");
  digitalWrite(errLed, LOW);
  displaySongs();
}

// SETTINGS modes - toggle between S_SETTINGS and S_LOAD
void onClick(EncoderButton& eb) {
  if (! isMidronomeConnected)
  {
    if (state == S_PLAYING)
    {
      // Stop all MIDI
      songFinished();
      // Restart
      currentSong = 0;
      displaySongs();
      state = S_LOAD;
      digitalWrite(beatSongLed, LOW);
    }
    else
    {
      currentPreQuarter = 1;
      displayOnWait();
      displayOnPlay();
      startMidi = true;
      state = S_PLAYING;
    }
  }
}

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
  int numMidiModes = 3;

  // for simplicity: only allow positive increments
  if (increment > 0)
  {
    if ((millis() - timeLastEncoder) > minEncoderChangePeriod)
    {
      if (state == S_SETTINGS)
      {
        midiMode = midiMode + increment;
        midiMode = midiMode % numMidiModes;
        displaySettings();
      }
      else
      {
        currentSong = currentSong + increment;
        adjustCurrentSong();
        state = S_LOAD;
      }
    }
    timeLastEncoder = millis();
  }
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
    Serial.println("Root dir open failed");
  }
  else
  {
    // Open files and store names them
    while (nextFile.openNext(&rootDir, O_RDONLY)) 
    {
      nextFile.getName(filename, size);
      if (!(filename[0] == '.') && (endsWith(filename, ".mid") || endsWith(filename, ".MID")))
      {
        Serial.println(filename);
        songList[numSongs][0] = '\0';
        strncpy(songList[numSongs], filename, strlen(filename) - 4); 
        numSongs++;
      }
      nextFile.close();
    }
    Serial.print("Loaded ");
    Serial.print(numSongs);
    Serial.println(" songs");
    rootDir.close();
  } 
}

void displayClockMode()
{
  // Display clock mode
  if (isMidronomeConnected)
    if (state == S_WAIT_FOR_SYNC)
    {
      lcd.clear();
      lcd.setCursor(0, 2);
      lcd.print("Press START to SYNC!");
    }
    else
    {
      lcd.setCursor(19, 0);
      lcd.print("S");
      lcd.setCursor(19, 3);
      lcd.print("E");
    }
  else
    lcd.print("I");
}

void adjustCurrentSong() {
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
}

void displaySongs()
{
  // delete cursors
  lcd.clear();

  // Adjust song index
  adjustCurrentSong();

  // Display titles
  for (int row=firstSongDisplayed; row<=lastSongDisplayed; row++)
  {
      uint8_t real_row = row - firstSongDisplayed;
      lcd.setCursor(1, real_row);
      lcd.print(songList[row]);
  }
    
  // Display cursor
  lcd.setCursor(0, currentSong - firstSongDisplayed);
  lcd.print(">");

  // Add Tempo
  lcd.setCursor(15, currentSong - firstSongDisplayed);
  lcd.print(" ");
  lcd.print(currentSongTempo);


  // Display clock mode
  displayClockMode();
}

void displayOnWait()
{
  if (currentPreQuarter == 1)
  {
    lcd.clear();
    lcd.setCursor(2, 0);
    lcd.print(songList[currentSong]);
    lcd.setCursor(2, 2);
    lcd.print("Starting in  8");
  }
  lcd.setCursor(15, 2);
  // Check this
  lcd.print(waitForQuarters - currentPreQuarter);

  // Display clock mode
  displayClockMode();
}

void displayOnPlay()
{
  lcd.setCursor(2, 2);
  lcd.print("              ");
  lcd.setCursor(2, 2);
  lcd.print("Playing!");

  // Display clock mode
  displayClockMode();

  // Midi MOde
  lcd.setCursor(0, 3);
  lcd.print("MIDI: ");

  switch (midiMode)
  {
  case ALL_OFF:
    lcd.print("A-O");
    break;
  case S_LP:
    lcd.print("S-LP");
    break;
  case ALL_ALL:
    lcd.print("A-A");
    break;
  }
}

// TODO
void displaySettings()
{
  // delete cursors
  lcd.clear();
  // Display cursor
  lcd.setCursor(0, 0);
  lcd.print("MIDI MODE");
  lcd.setCursor(2, 1);
  lcd.print("All-Off");
  lcd.setCursor(2, 2);
  lcd.print("Syn-LoopPed");
  lcd.setCursor(2, 3);
  lcd.print("All-All");

  // Display cursor
  lcd.setCursor(0, 1 + int(midiMode));
  lcd.print(">");
}

uint16_t loadSongTempo(const char* midiFileName)
{
  SMF.close();
  int err = SMF.load(midiFileName);
  // To get the tempo, we get the first event and stop the synth messages
  midiPlay = false;
  SMF.processEvents(0);
  midiPlay = true;

  uint16_t midiTempo = SMF.getTempo();
  SMF.close();

  return midiTempo;
}


uint16_t loadSongSignature(const char* midiFileName)
{
  SMF.close();
  int err = SMF.load(midiFileName);
  // To get the tempo, we get the first event and stop the synth messages
  midiPlay = false;
  SMF.processEvents(0);
  midiPlay = true;

  uint16_t midiSignature = SMF.getTimeSignature();
  SMF.close();

  return midiSignature;
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
  
    // Open midi file
    char midiFileName[30];;
    midiFileName[0] = '\0';
    strcpy(midiFileName, songName);
    strcat(midiFileName, ".mid");

    currentSongTempo = loadSongTempo(midiFileName);
    uint16_t songSignature = loadSongSignature(midiFileName);
    currentSongSignatureNumerator = songSignature >> 8;
    uint16_t denumerator = songSignature - (currentSongSignatureNumerator * 256);

    int err = SMF.load(midiFileName);
    if (err == MD_MIDIFile::E_OK)
    {
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
      Serial.print("\tTEMPO: ");
      Serial.println(currentSongTempo);
      Serial.print("\tSignature: ");
      Serial.print(currentSongSignatureNumerator);
      Serial.print("/");
      Serial.println(denumerator);
    }
    else
    {
      // Flash error led 3 times
      flashLedError(200, 3);
      Serial.println("MIDI file opening failed");
    }
    rootDir.close();
  }
}

//----------------------------------------
// STOP controls

void synthStopAll()
{
  MIDI_OUT1.sendControlChange(120, 127, midiChannelSynthOut);
  MIDI_OUT2.sendControlChange(120, 127, midiChannelSynthOut);
}
void looperStopAll(){
  MIDI_OUT1.sendControlChange(29, 127, midiChannelLooper);
  MIDI_OUT2.sendControlChange(29, 127, midiChannelLooper);
}
void looperClearAll(){
  MIDI_OUT1.sendControlChange(30, 127, midiChannelLooper);
  MIDI_OUT2.sendControlChange(30, 127, midiChannelLooper);
}

void songFinished()
{
  MIDI_OUT1.sendStop();
  MIDI_OUT2.sendStop();
  synthStopAll();
  SMF.close();
  startMidi = false;
  
  looperStopAll();
  looperClearAll();
  
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
  if (! isMidronomeConnected)
  {
    isMidronomeConnected = true;
    state = S_WAIT_FOR_SYNC;
    displaySongs();
  }

  // Propagate clock
  if (state != S_WAIT_FOR_SYNC)
  {
    MIDI_OUT1.sendClock();
    MIDI_OUT2.sendClock();
  }
  ticksCount++;

  if (ticksCount == TICKS_PER_QUARTER_CLK)
  {
    elapsedClockTime = millis() - previousClockTime;
    previousClockTime = millis();
    currentSongTempo = int(60 / (float(elapsedClockTime) / 1000.));
    currentBeat++;

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

      // Flash IDLE and BEAT song in pre
      if ((state == S_PRE_SONG))
      {
        digitalWrite(errLed, HIGH);
        isLedBeatOn = true;
        timeLedBeatOn = millis();
      }
    }

    if (currentBeat == currentSongSignatureNumerator)
    {
      digitalWrite(errLed, HIGH);
      isLedBeatOn = true;
      timeLedBeatOn = millis();
      currentBeat = 0;
    }

    ticksCount = 0;
    if (state == S_PRE_SONG)
    {
      Serial.print("\tStart in... ");
      Serial.println(waitForQuarters - currentPreQuarter);
      currentPreQuarter++;
      displayOnWait();
    }
    else if (state == S_PLAYING)
    {
      startMidi = true;
      previousTicksCount = TICKS_PER_QUARTER_CLK - 1;
      currentSongQuarter++;

      // Check if song has finished
      if (SMF.isEOF())
          songFinished();
    }
  }
}

void handleStart()
{
  // Reset click count!
  ticksCount = 0;
  currentBeat = 0;
  if (acceptingSync)
  {
    if (state == S_WAIT_FOR_SYNC)
    {
      {
        Serial.println("Press STOP to sync");
        startReceived = true;
        digitalWrite(errLed, HIGH);

        lcd.clear();
        lcd.setCursor(0, 2);
        lcd.print("Press STOP to SYNC!");
      }
    }
    else if (state == S_SONG_LOADED)
    {
      Serial.println("Start! Waiting ");
      state = S_PRE_SONG;
      digitalWrite(beatSongLed, HIGH);
    }
  }
}

void handleStop()
{
  if (acceptingSync)
  {
    if ((state == S_WAIT_FOR_SYNC) && startReceived)
    {
      Serial.println("Synced with MIDRONOME");
      digitalWrite(errLed, LOW);
      displaySongs();
      state = S_LOAD;
    }
    else
    {
      elapsedTimeFromPlay = millis() - timeFromPlayStart;
      if (elapsedTimeFromPlay > ingoreStopsAfterPlayMs)
      {
        MIDI_OUT1.sendStop();
        MIDI_OUT2.sendStop();
        synthStopAll();
        SMF.close();
        startMidi = false;
        
        looperStopAll();
        looperClearAll();
        
        Serial.println("Stop! Reset");
        digitalWrite(beatSongLed, LOW);
        state = S_LOAD;
        currentPreQuarter = 0;
        currentSongQuarter = 0;
        displaySongs();
      }
      else
        Serial.println("Ignored STOP signal: too close to PLAY start");
    }
  }
}

//---------------------------------------------------------
// MIDI file functions
uint16_t tickClockExternal(void)
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

void tickClockInternal(void)
// flash a LED to the beat
{
  int beatLed;
  if (state == S_PRE_SONG)
    beatLed = beatIdleLed;
  else
    beatLed = beatSongLed;

  static uint32_t lastBeatTime = 0;
  static boolean  inBeat = false;
  uint16_t  beatTime;

  beatTime = 60000/SMF.getTempo();    // msec/beat = ((60sec/min)*(1000 ms/sec))/(beats/min)

  if (!inBeat)
  {
    if ((millis() - lastBeatTime) >= beatTime)
    {
      lastBeatTime = millis();
      digitalWrite(beatLed, HIGH);
      inBeat = true;
    }
  }
  else
  {
    if ((millis() - lastBeatTime) >= 100)	// keep the flash on for 100ms only
    {
      digitalWrite(beatSongLed, LOW);
      inBeat = false;
    }
  }
}

void midiCallback(midi_event *pev)
{
  if (midiPlay)
  {
    if ((pev->data[0] >= 0x80) && (pev->data[0] <= 0xe0))
    {
      channel = pev->channel;

      if (channel == midiChannelSynth - 1)
      {
        // Always send to out 1
        serialOut1.write(pev->data[0] | channel);
        serialOut1.write(&pev->data[1], pev->size-1);
        // if ALL_ALL, also send to out 2
        if (midiMode == ALL_ALL)
        {
          serialOut2.write(pev->data[0] | channel);
          serialOut2.write(&pev->data[1], pev->size-1);
        }
      }
      else if ((channel == midiChannelLooper - 1) || (channel == midiChannelPedal - 1))
      {
        if (midiMode != ALL_OFF)
        {
          serialOut2.write(pev->data[0] | channel);
          serialOut2.write(&pev->data[1], pev->size-1);
        }
        if (midiMode != S_LP)
        {
          serialOut1.write(pev->data[0] | channel);
          serialOut1.write(&pev->data[1], pev->size-1);
        }
      }
    }
    else // Other MIDI events
    {
      serialOut1.write(pev->data, pev->size);
      serialOut2.write(pev->data, pev->size);
    }
  }
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
  
  Serial.println("MIDI OUTS");
  MIDI_OUT1.begin();
  MIDI_OUT2.begin();
  

  // Initialize LCD and Encoder.
  Serial.println("LCD");
  lcd.init(); // LCD driver initialization
  lcd.backlight(); // Open the backlight
  lcd.clear();

  Serial.println("ENCODER");
  myEnc.setLongPressHandler(onLongClick);
  myEnc.setClickHandler(onClick);
  myEnc.setDoubleClickHandler(onDoubleClick);
  myEnc.setMultiClickInterval(200);
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

  state = S_WAIT_FOR_SYNC;
  Serial.println("Starting main loop");
  digitalWrite(errLed, LOW);
  previousClockTime = 0;
  startingClockTime = millis();
}


void loop()
{
  if (! acceptingSync && isMidronomeConnected)
  {
    elapsedTimeFromStart = millis() - startingClockTime;
    Serial.print("Elapsed time from Start/Reset: ");
    Serial.println(elapsedTimeFromStart);
    if (elapsedTimeFromStart > 5000)
      {
        acceptingSync = true;
        Serial.println("Now accepting SYNC");
      }

  }

  elapsedClockTimeLoop = millis() - previousClockTime;

  if ((elapsedClockTimeLoop > 2000) && (isMidronomeConnected))
  {
    isMidronomeConnected = false;
    // No need to wait for Sync when Midronome is not connected
    state = S_LOAD;
    displaySongs();
  }
    

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
  if ((isLedBeatOn) && ((millis() - timeLedBeatOn) > ledDurationMs))
  {
    digitalWrite(errLed, LOW);
    isLedBeatOn = false;
  }
  // Read MIDI
  if (MIDI_MIDRO.read()){}

  // Read Encoder
  myEnc.update();


  switch (state)
  {
    case S_WAIT_FOR_SYNC:
      // handled by Start/Stop
      break;
    case S_LOAD:
      //load current song
      loadSong(songList[currentSong]);
      displaySongs();
      state = S_SONG_LOADED;
      break;
    case S_SONG_LOADED:
      //wait for start (handled by handleStart) or reload song if user input
      break;

    case S_PRE_SONG:
      if (currentPreQuarter >= waitForQuarters - 1)
      {
        Serial.println("Play");
        // prevent spurious stops after Play
        timeFromPlayStart = millis();
        state = S_PLAYING;
        currentPreQuarter = 0;
        digitalWrite(beatSongLed, LOW);
        displayOnPlay();
      }
      break;
    case S_PLAYING:
      // Here we have to control the synth and the looper based on the currentSongQuarter (or play the MIDI file)
      // TODO: in init, check if MIDRO is receiving, else use internal clock of song
      if (startMidi)
      {
        if (isMidronomeConnected)
        {
          uint32_t ticks = tickClockExternal();
          if (ticks > 0)
          {
            SMF.processEvents(ticks);
            SMF.isEOF();
          }
        }
        else
        {
          if (!SMF.isEOF())
          {
            if (SMF.getNextEvent())
              tickClockInternal();
          }
          else
            songFinished();
        }
      }
      break;
  }
}

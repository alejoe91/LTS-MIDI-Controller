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
- Channel 1 (configurable): UNO Synth Pro - IK Multimedia (notes + automation + clock)
- Channel 2 (configurable): AIRA S1 - Roland (notes + automation + clock)
- Channel 3 (configurable): Ampero Stomp II - Hotone (automation)
- Channel 4 (not configurable): Ditto X4 Looper - TC (automation)
**

*/
#include <math.h>

#include <SdFat.h>
#include <EncoderButton.h>
#include <MIDI.h>
#include <MD_MIDIFile.h>
#include <SoftwareSerial.h>
#include <LiquidCrystal_I2C.h>

// Midronome input moved to hardware Serial1 (RX1 = D19), jumpered from D12 on the
// shield. D12 must stay an input (never pinMode OUTPUT) since it's wired in parallel.
// #define MIDRO_MIDI_RX 12
// #define MIDRO_MIDI_TX 11

#define OUT_MIDI_RX1 5
#define OUT_MIDI_TX1 4

#define OUT_MIDI_RX2 3
#define OUT_MIDI_TX2 2

#define OUT_MIDI_RX3 7
#define OUT_MIDI_TX3 6


#define midiChannelUno 1
#define midiChannelAira 2
#define midiChannelUnoLV 5
#define midiChannelAiraLV 6
#define midiChannelPedal 3
#define midiChannelLooper 4
#define midiChannelMidro 12


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
static enum { S_WAIT_FOR_SYNC, S_LOAD, S_SETTINGS_LIVE, S_SETTINGS_OUT, S_SONG_LOADED, S_TEMPO_SET, S_START_SONG, S_PRE_SONG, S_PLAYING } state;
// liveMode controls the mapping between live channels (LV) of synths
static enum { PLAYBACK, LIVE } liveMode = PLAYBACK;
// midiMode controls which channels are outputted to the MidiOut1 and MidiOut2
static enum { ONLY_1, ONLY_2, BOTH } midiMode = ONLY_1;

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
uint16_t midiSongTempo = 0;
uint16_t currentSongTempo = 0;
const uint8_t numSongsDisplayed = 4;

bool startMidi = false;
bool acceptingSync = false;
bool start1Received = false;
bool stop1Received = false;
bool start2Received = false;

bool midiPlay = true;
bool isMidronomeConnected = true;

MD_MIDIFile SMF;
uint8_t note, velocity, channel;
int tickMultiplier;
uint16_t ticksPerQuarterMidi = 0;
uint8_t numTracks = 0;
uint16_t midiTempo = 0;

// clock count
const uint16_t waitForAcceptingSyncMs = 5000;
uint16_t currentSongSignatureNumerator = 4;
uint8_t currentBeat = 0;
uint16_t oldTempo = 0;
bool isQuarter = false;

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
bool updateCountdownDisplay = false;
bool pendingTempoSet = false;
uint32_t totalPulses = 0;
// When the last clock pulse actually arrived. Lets lost-pulse recovery tell a
// destroyed pulse apart from a Midronome that simply is not running.
uint32_t lastPulseMs = 0;
// Smoothed ms-per-pulse, used to reconstruct pulses lost during SD access.
// Outlier-rejected so a beat that spanned a load cannot poison the estimate.
float pulsePeriodEma = 0.0;
// Carried fractional pulse, so repeated sub-pulse corrections do not round away.
float pulseResidue = 0.0;
// Deferred lost-pulse guard, completed in loop(). Used for blocking work that
// can run from inside handleClock() (songFinished on EOF), where draining the
// MIDI input in place would re-enter MIDI_MIDRO.read() and corrupt the parser.
bool pulseGuardActive = false;
uint32_t pulseGuardT0 = 0;
uint32_t pulseGuardP0 = 0;
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
// Wait for the encoder to settle before hitting the SD card - see S_LOAD.
// Browsing runs at roughly one detent per 600-800ms, so this has to sit well
// above that or every song still loads and the deferral buys nothing.
const uint16_t songLoadSettleMs = 1500;
EncoderButton myEnc(encPinInput1, encPinInput2, encPinButton);

//------------------------------------------------------------------------------
// LCD configuration
LiquidCrystal_I2C lcd(0x27,20,4);
bool lcdIsOn = true;

//------------------------------------------------------------------------------
// MIDI configuration
using Transport = MIDI_NAMESPACE::SerialMIDI<SoftwareSerial>;

// MIDRONOME
// Hardware UART: bytes are received by the USART itself, so interrupt blackouts
// (SD access, SoftwareSerial writes) no longer drop incoming clock pulses.
MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI_MIDRO);
// SoftwareSerial serialMidro = SoftwareSerial(MIDRO_MIDI_RX, MIDRO_MIDI_TX);
// Transport serialMIDIMidro(serialMidro);
// MIDI_NAMESPACE::MidiInterface<Transport> MIDI_MIDRO((Transport&)serialMIDIMidro);

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
  songFinished(false);

  // Restart
  currentSong = 0;
  state = S_WAIT_FOR_SYNC;
  startingClockTime = millis();
  start1Received = false;
  stop1Received = false;
  start2Received = false;
  acceptingSync = false;
  Serial.println("AcceptingSync set to false");
  digitalWrite(errLed, LOW);
  lcd.clear();
  lcd.setCursor(0, 1);
  lcd.print("Press START1 to SYNC");
}

// SETTINGS modes - toggle between S_SETTINGS and S_LOAD
void onClick(EncoderButton& eb) {
  if ((state == S_SETTINGS_LIVE) || (state == S_SETTINGS_OUT))
  {
    // Exit settings
    displaySongs();
    state = S_LOAD;
  }
  else if (! isMidronomeConnected)
  {
    if (state == S_PLAYING)
    {
      // Stop all MIDI
      songFinished(false);
      // Restart
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
  else
    if (state == S_SONG_LOADED)
    {
      // Deferred to just after the next clock tick - see handleClock()
      pendingTempoSet = true;
      state = S_TEMPO_SET;
    }
    else if (state == S_TEMPO_SET)
      // Now start the song at next beat
      state = S_START_SONG;
    else if ((state == S_PRE_SONG) || (state == S_PLAYING))
      // Stop song
      songFinished(false);
}

void onDoubleClick(EncoderButton& eb) {
  if (state != S_SETTINGS_LIVE)
  {
    state = S_SETTINGS_LIVE;
    displaySettingsLive();
  }
  else
  {
    displaySongs();
    state = S_LOAD;
  }
}

void onTripleClick(EncoderButton& eb) {
  if (state != S_SETTINGS_OUT)
  {
    state = S_SETTINGS_OUT;
    displaySettingsOut();
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
  int numLiveModes = 2;

  // for simplicity: only allow positive increments
  // if (increment > 0)
  if (increment != 0)
  {
    if ((millis() - timeLastEncoder) > minEncoderChangePeriod)
    {
      if (state == S_SETTINGS_LIVE)
      {
        // liveMode = liveMode + increment;
        // liveMode = liveMode % numLiveModes;
        // C++ % keeps the sign, so add n back to stay non-negative when turning back
        liveMode = ((liveMode + increment) % numLiveModes + numLiveModes) % numLiveModes;
        displaySettingsLive();
      }
      else if (state == S_SETTINGS_OUT)
      {
        // midiMode = midiMode + increment;
        // midiMode = midiMode % numMidiModes;
        midiMode = ((midiMode + increment) % numMidiModes + numMidiModes) % numMidiModes;
        Serial.print("Midi mode: ");
        Serial.println(midiMode);
        displaySettingsOut();
      }
      else
      {
        currentSong = currentSong + increment;
        adjustCurrentSong();
        // Show the new selection straight away; the SD load itself is deferred
        // in S_LOAD until scrolling stops. Clear the tempo so the placeholder
        // shows instead of the previous song's value until the load lands.
        currentSongTempo = 0;
        displaySongs();
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

    // Sort song names alphanumerically (case-insensitive)
    char tmp[20];
    for (uint8_t i = 1; i < numSongs; i++)
    {
      strcpy(tmp, songList[i]);
      int8_t j = i - 1;
      while (j >= 0 && strcasecmp(songList[j], tmp) > 0)
      {
        strcpy(songList[j + 1], songList[j]);
        j--;
      }
      strcpy(songList[j + 1], tmp);
    }

    Serial.print("Loaded ");
    Serial.print(numSongs);
    Serial.println(" songs");
    rootDir.close();
  } 
}

void displayClockMode()
{
  // Display clock source: External (Midronome) with live tempo, or Internal
  if (isMidronomeConnected)
    {
      lcd.setCursor(16, 0);
      lcd.print(currentSongTempo);
      lcd.setCursor(19, 0);
      lcd.print("E");
    }
  else
    {
      lcd.setCursor(19, 0);
      lcd.print("I");
    }
}

void adjustCurrentSong() {
  if (currentSong < 0)
  {
    // Serial.println("Current song negative");
    currentSong = numSongs - 1;
    // firstSongDisplayed = currentSong - numSongsDisplayed + 1;
    // Guard short lists: firstSongDisplayed is unsigned and would underflow
    firstSongDisplayed = (numSongs > numSongsDisplayed) ? numSongs - numSongsDisplayed : 0;
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

  // Add Tempo. The SD load is deferred until scrolling stops, so the tempo is
  // unknown for the song under the cursor until then - show a placeholder
  // rather than the previously loaded song's value.
  lcd.setCursor(15, currentSong - firstSongDisplayed);
  if (currentSongTempo > 0)
  {
    lcd.print(" ");
    lcd.print(currentSongTempo);
  }
  else
    lcd.print(" ...");


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
  lcd.print("LIVE: ");

  if (liveMode == PLAYBACK)
    lcd.print("N");
  else
    lcd.print("Y");
  lcd.print(" - ");
  switch (midiMode)
  {
  case ONLY_1:
    lcd.print("O-1");
    break;
  case ONLY_2:
    lcd.print("O-2");
    break;
  case BOTH:
    lcd.print("O-1+2");
    break;
  }
}

void displaySettingsLive()
{
  // delete cursors
  lcd.clear();
  // Display cursor
  lcd.setCursor(0, 0);
  lcd.print("LIVE MODE");
  lcd.setCursor(2, 1);
  lcd.print("Playback");
  lcd.setCursor(2, 2);
  lcd.print("Live");
  // Display cursor
  lcd.setCursor(0, 1 + int(liveMode));
  lcd.print(">");
}

void displaySettingsOut()
{
  // delete cursors
  lcd.clear();
  // Display cursor
  lcd.setCursor(0, 0);
  lcd.print("MIDI OUT");
  lcd.setCursor(2, 1);
  lcd.print("1 only");
  lcd.setCursor(2, 2);
  lcd.print("2 only");
  lcd.setCursor(2, 3);
  lcd.print("1 and 2");


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

// Reconstruct clock pulses destroyed during SD access.
//
// SdFat masks interrupts across SPI transfers, and SoftwareSerial RX needs its
// pin-change ISR to fire within ~32us of a start bit or the byte is gone - so
// pulses are lost outright rather than merely delayed (measured ~5% across a
// load). Each lost pulse slides ticksCount/currentBeat permanently late, and
// only handleStart() ever re-anchors them, which is why a Midronome start plays
// tight and an encoder start does not.
//
// We know how long the load took and the current ms-per-pulse, so the pulses
// that should have arrived are recoverable. Advance the grid by the shortfall,
// carrying the fraction so repeated sub-pulse corrections do not round away.
void recoverLostPulses(uint32_t windowStartMs, uint32_t dtMs, uint32_t rxPulses)
{
  if ((pulsePeriodEma <= 0.0) || !isMidronomeConnected)
    return;

  // The clock must have been running when the window opened. A stopped
  // Midronome sends nothing, so a zero count there means nothing was lost -
  // "recovering" it invents pulses and pushes the grid early. This is exactly
  // what happens on the load right after the sync handshake's final STOP.
  // (lastPulseMs can be newer than windowStartMs if a pulse landed inside the
  // window; that proves the clock was alive, so only test the older case.)
  if (lastPulseMs < windowStartMs)
  {
    if (float(windowStartMs - lastPulseMs) > 3.0 * pulsePeriodEma)
      return;
  }

  float lost = (float(dtMs) / pulsePeriodEma) - float(rxPulses) + pulseResidue;
  if (lost < 0.0)
  {
    // Received at least as many as expected - keep the surplus as residue so it
    // offsets the next correction rather than rewinding the grid. Clamp at one
    // pulse: a genuine surplus is just integer quantisation, so anything larger
    // is measurement error and must not be carried, or it suppresses real
    // corrections later.
    pulseResidue = (lost < -1.0) ? -1.0 : lost;
    return;
  }

  int16_t whole = int16_t(lost);
  pulseResidue = lost - float(whole);

  // A correction beyond the RX buffer depth means the estimate is unreliable
  // (stalled clock, tempo jump); clamp rather than jump the grid wildly.
  if (whole > 64)
    whole = 64;

  for (int16_t i = 0; i < whole; i++)
  {
    ticksCount++;
    if (ticksCount == TICKS_PER_QUARTER_CLK)
    {
      ticksCount = 0;
      currentBeat++;
      if (currentBeat == currentSongSignatureNumerator)
        currentBeat = 0;
    }
  }

}

void loadSong(const char* songName){
  // Bracket the SD access so pulses it destroys can be reconstructed afterwards.
  uint32_t loadT0 = millis();
  uint32_t loadP0 = totalPulses;

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

    midiSongTempo = loadSongTempo(midiFileName);
    currentSongTempo = midiSongTempo;
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

  // Pulses that arrived during the load are still sitting in the RX buffer -
  // drain them first, so only the ones actually destroyed count as missing.
  while (MIDI_MIDRO.read()) {}
  recoverLostPulses(loadT0, millis() - loadT0, totalPulses - loadP0);
}

//----------------------------------------
// STOP controls

void synthStopAll()
{
  if (midiMode != ONLY_2)
  {
    MIDI_OUT1.sendControlChange(123, 0, midiChannelUno);
    MIDI_OUT1.sendControlChange(123, 0, midiChannelAira);
  }
  if (midiMode != ONLY_1)
  {
    MIDI_OUT2.sendControlChange(123, 0, midiChannelUno);
    MIDI_OUT2.sendControlChange(123, 0, midiChannelAira);
  }
}
void looperStopAll(){
  if (midiMode != ONLY_2)
    MIDI_OUT1.sendControlChange(29, 127, midiChannelLooper);
  if (midiMode != ONLY_1)
    MIDI_OUT2.sendControlChange(29, 127, midiChannelLooper);
}
void looperClearAll(){
  if (midiMode != ONLY_2)
    MIDI_OUT1.sendControlChange(30, 127, midiChannelLooper);
  if (midiMode != ONLY_1)
    MIDI_OUT2.sendControlChange(30, 127, midiChannelLooper);
}

void songFinished(bool nextSong)
{
  // The stop-all bursts below are blocking SoftwareSerial writes that destroy
  // incoming clock bytes. Open a guard; loop() closes it once it has drained.
  if (!pulseGuardActive)
  {
    pulseGuardT0 = millis();
    pulseGuardP0 = totalPulses;
    pulseGuardActive = true;
  }

  // if (midiMode != ONLY_2)
  //   MIDI_OUT1.sendStop();
  // if (midiMode != ONLY_1)
  //   MIDI_OUT2.sendStop();

  SMF.close();
  startMidi = false;

  synthStopAll();
  looperStopAll();
  looperClearAll();
  
  Serial.println("Song finished");
  state = S_LOAD;
  currentSongQuarter = 0;
  currentPreQuarter = 0;
  if (nextSong)
    currentSong++;
  currentSong = currentSong % numSongs;
  displaySongs();
}

void setMidroTempo()
{
  uint8_t tempo_MSB;
  uint8_t tempo_LSB;

  tempo_MSB = midiSongTempo / 128;
  tempo_LSB = midiSongTempo % 128;
  Serial.print("Tempo MSB: ");
  Serial.println(tempo_MSB);
  Serial.print("Tempo LSB: ");
  Serial.println(tempo_LSB);

  if (midiMode != ONLY_2)
  {
    MIDI_OUT1.sendControlChange(85, tempo_MSB, midiChannelMidro);
    MIDI_OUT1.sendControlChange(86, tempo_LSB, midiChannelMidro);
  }
  if (midiMode != ONLY_1)
  {
    MIDI_OUT2.sendControlChange(85, tempo_MSB, midiChannelMidro);
    MIDI_OUT2.sendControlChange(86, tempo_LSB, midiChannelMidro);
  }
}


//----------------------------------------
// Handles
void handleClock()
{
  if (! isMidronomeConnected)
  {
    isMidronomeConnected = true;
    state = S_WAIT_FOR_SYNC;
    lcd.clear();
    lcd.setCursor(0, 1);
    lcd.print("Press START1 to SYNC");
  }

  // Propagate clock.
  // S_TEMPO_SET is deliberately excluded: SoftwareSerial::write() blocks interrupts
  // for ~320us per byte, which drops incoming Midronome clock bytes and slips
  // ticksCount/currentBeat. That state can last indefinitely (waiting for the user's
  // second click), so forwarding there desyncs the grid before the count-in starts.
  if ((state == S_PLAYING) || (state == S_PRE_SONG))
  {
    if (midiMode != ONLY_2)
      MIDI_OUT1.sendClock();
    if (midiMode != ONLY_1)
      MIDI_OUT2.sendClock();
  }
  ticksCount++;
  totalPulses++;
  lastPulseMs = millis();

  // Send the tempo CCs here, right after a tick has been counted: these are
  // blocking SoftwareSerial writes (~2-4ms with interrupts off) and would drop
  // incoming clock bytes if sent at an arbitrary time from onClick(), slipping
  // ticksCount and offsetting every later beat.
  if (pendingTempoSet)
  {
    pendingTempoSet = false;
    setMidroTempo();
  }

  // Flash LEDs following beat
  if (ticksCount == TICKS_PER_QUARTER_CLK)
  {
    elapsedClockTime = millis() - previousClockTime;
    previousClockTime = millis();
    currentSongTempo = int(60 / (float(elapsedClockTime) / 1000.));
    currentBeat++;

    // Track ms-per-pulse for lost-pulse recovery. Reject beats more than 25%
    // off the running estimate: those are beats that spanned an SD load, and
    // folding them in would inflate the period and under-correct.
    {
      float beatMs = float(elapsedClockTime);
      if (beatMs > 0)
      {
        static uint8_t emaRejects = 0;
        float thisPeriod = beatMs / float(TICKS_PER_QUARTER_CLK);
        if (pulsePeriodEma <= 0.0)
        {
          pulsePeriodEma = thisPeriod;
          emaRejects = 0;
        }
        else if ((thisPeriod > pulsePeriodEma * 0.75) && (thisPeriod < pulsePeriodEma * 1.25))
        {
          pulsePeriodEma += (thisPeriod - pulsePeriodEma) * 0.125;
          emaRejects = 0;
        }
        else if (++emaRejects >= 8)
        {
          // Seeded on a bad beat (or the tempo genuinely jumped), so the gate is
          // now rejecting every good beat and the estimate is stuck. Re-seed.
          pulsePeriodEma = thisPeriod;
          pulseResidue = 0.0;
          emaRejects = 0;
        }
      }
    }

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

      // start song from button
      if (state == S_START_SONG)
      {
        state = S_PRE_SONG;
        currentPreQuarter = 0;
        digitalWrite(beatSongLed, HIGH);
      }
    }

    ticksCount = 0;
    if (state == S_PRE_SONG)
    {
      currentPreQuarter++;
      updateCountdownDisplay = true;
    }
    else if (state == S_PLAYING)
    {
      startMidi = true;
      previousTicksCount = TICKS_PER_QUARTER_CLK - 1;
      currentSongQuarter++;

      // Check if song has finished
      if (SMF.isEOF())
          songFinished(true);
    }
  }
}

void handleStart()
{
  // Reset click count
  ticksCount = 0;
  currentBeat = 0;
  currentPreQuarter = 0;
  // START re-anchors the grid outright, so a carried fractional correction from
  // before is stale - applying it later would offset a grid that is already correct.
  pulseResidue = 0.0;
  Serial.println("START received");
  if (acceptingSync)
  {
    if (state == S_WAIT_FOR_SYNC)
    {
      if (!start1Received) 
      {
        Serial.println("Press STOP1 to sync");
        lcd.clear();
        lcd.setCursor(0, 1);
        lcd.print("Press STOP1 to SYNC");
        start1Received = true;
        digitalWrite(errLed, HIGH);
      }
      else if (stop1Received)
      {
        Serial.println("Press STOP2 to sync");
        lcd.clear();
        lcd.setCursor(0, 1);
        lcd.print("Press STOP2 to SYNC");
        start2Received = true;
        digitalWrite(errLed, HIGH);
      }
    }
    else if ((state == S_SONG_LOADED) || (state == S_TEMPO_SET))
    {
      Serial.println("Start! Waiting ");
      state = S_PRE_SONG;
      currentPreQuarter = 1;
      updateCountdownDisplay = true;
      digitalWrite(beatSongLed, HIGH);
    }
  }
}

void handleStop()
{
  if (acceptingSync)
  {
    if (state == S_WAIT_FOR_SYNC)
    {
      if (start2Received)
      {
        Serial.println("Synced with MIDRONOME");
        digitalWrite(errLed, LOW);
        state = S_LOAD;
      }
      else if (start1Received)
      {
        Serial.println("Waiting for Start2");
        lcd.clear();
        lcd.setCursor(0, 1);
        lcd.print("Press START2 to SYNC");
        stop1Received = true;
      }
    }
    else
    {
      elapsedTimeFromPlay = millis() - timeFromPlayStart;
      if (elapsedTimeFromPlay > ingoreStopsAfterPlayMs)
        songFinished(false);
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

      // Remap channels if playback
      if (liveMode == PLAYBACK)
      {
        if (channel == midiChannelUnoLV - 1)
          channel = midiChannelUno - 1;
        if (channel == midiChannelAiraLV - 1)
          channel = midiChannelAira - 1;
      }

      if ((channel == midiChannelUno - 1) || (channel == midiChannelAira - 1) || (channel == midiChannelLooper - 1) || (channel == midiChannelPedal - 1))
      {
        // OUT 1
        if (midiMode != ONLY_2)
        {
          serialOut1.write(pev->data[0] | channel);
          serialOut1.write(&pev->data[1], pev->size-1);
        }
        // OUT 2
        if (midiMode != ONLY_1)
        {
          serialOut2.write(pev->data[0] | channel);
          serialOut2.write(&pev->data[1], pev->size-1);
        }
      }

      // serialOut1.write(pev->data[0] | channel);
      // serialOut1.write(&pev->data[1], pev->size-1);
    }
    else // Other MIDI events
    {
      if (midiMode != ONLY_2)
        serialOut1.write(pev->data, pev->size);
      if (midiMode != ONLY_1)
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

  Serial.println("MIDI OUTS");
  MIDI_OUT1.begin();
  MIDI_OUT2.begin();

  // Initialize MIDI
  Serial.println("MIDI IN");
  MIDI_MIDRO.begin(MIDI_CHANNEL_OMNI);
  MIDI_MIDRO.setHandleClock(handleClock);
  MIDI_MIDRO.setHandleStart(handleStart);
  MIDI_MIDRO.setHandleStop(handleStop);

  // serialMidro.listen();   // reclaim the RX slot after the outputs' begin() (not needed with Serial1)

  // Initialize LCD and Encoder.
  Serial.println("LCD");
  lcd.init(); // LCD driver initialization
  lcd.backlight(); // Open the backlight
  lcd.clear();

  Serial.println("ENCODER");
  myEnc.setLongPressHandler(onLongClick);
  myEnc.setClickHandler(onClick);
  myEnc.setDoubleClickHandler(onDoubleClick);
  myEnc.setTripleClickHandler(onTripleClick);
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

  if (numSongs == 0)
    Serial.println("Failed to load songs");

  state = S_WAIT_FOR_SYNC;
  lcd.clear();
  lcd.setCursor(0, 1);
  lcd.print("Press START1 to SYNC");

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
    // Serial.print("Elapsed time from Start/Reset: ");
    // Serial.println(elapsedTimeFromStart);
    if (elapsedTimeFromStart > waitForAcceptingSyncMs)
      {
        acceptingSync = true;
        Serial.println("");
        Serial.println("***Now accepting SYNC***");
        Serial.println("");
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
  // Read MIDI - drain the RX buffer each loop so a slow iteration
  // (SD load / LCD writes) doesn't overflow it and drop clock/Start bytes
  while (MIDI_MIDRO.read()) {}

  // Close a deferred pulse guard now that the buffer is drained.
  if (pulseGuardActive)
  {
    pulseGuardActive = false;
    recoverLostPulses(pulseGuardT0, millis() - pulseGuardT0, totalPulses - pulseGuardP0);
  }

  // Read Encoder
  myEnc.update();


  switch (state)
  {
    case S_WAIT_FOR_SYNC:
      // handled by Start/Stop
      break;
    case S_LOAD:
      // Defer the SD load until the encoder has settled. SdFat masks interrupts
      // during SPI access, which destroys incoming SoftwareSerial clock bytes
      // (~5% measured). Every lost pulse slides the beat grid permanently late,
      // and only handleStart() ever re-anchors it - so a button start inherits
      // the whole accumulated slip. Loading once per selection instead of once
      // per detent cuts that by the number of songs scrolled past.
      if ((millis() - timeLastEncoder) > songLoadSettleMs)
      {
        loadSong(songList[currentSong]);
        displaySongs();
        state = S_SONG_LOADED;
      }
      break;
    case S_SONG_LOADED:
      //wait for start (handled by handleStart) or reload song if user input
      break;
    case S_TEMPO_SET:
      break;
    case S_PRE_SONG:
      if (updateCountdownDisplay)
      {
        Serial.print("\tStart in... ");
        Serial.println(waitForQuarters - currentPreQuarter);
        displayOnWait();
        updateCountdownDisplay = false;
      }
      if (currentPreQuarter >= waitForQuarters)
      {
        Serial.println("Play");
        // prevent spurious stops after Play
        timeFromPlayStart = millis();
        displayOnPlay();
        state = S_PLAYING;
        currentPreQuarter = 0;
        digitalWrite(beatSongLed, LOW);
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
            songFinished(true);
        }
      }
      break;
  }
}

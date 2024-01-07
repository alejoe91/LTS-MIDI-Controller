// Test playing a succession of MIDI files from the SD card.
// Example program to demonstrate the use of the MIDFile library
// Just for fun light up a LED in time to the music.
//
// Hardware required:
//  SD card interface - change SD_SELECT for SPI comms
//  3 LEDs (optional) - to display current status and beat. 
//  Change pin definitions for specific hardware setup - defined below.

#include <SdFat.h>
#include <MD_MIDIFile.h>
#include <MIDI.h>

// Simple tutorial on how to receive and send MIDI messages.
// Here, when receiving any message on channel 4, the Arduino
// will blink a led and play back a note for 1 second.

#include <SoftwareSerial.h>

#define MIDRO_MIDI_RX 12
#define MIDRO_MIDI_TX 11

#define SYNTH_MIDI_RX 7
#define SYNTH_MIDI_TX 6

#define midiChannelSynth 3
#define midiChannelLooper 4


using Transport = MIDI_NAMESPACE::SerialMIDI<SoftwareSerial>;

SoftwareSerial serialMidro = SoftwareSerial(MIDRO_MIDI_RX, MIDRO_MIDI_TX);
Transport serialMIDIMidro(serialMidro);
MIDI_NAMESPACE::MidiInterface<Transport> MIDI_MIDRO((Transport&)serialMIDIMidro);

// SYNTH
SoftwareSerial serialSynth = SoftwareSerial(SYNTH_MIDI_RX, SYNTH_MIDI_TX);
Transport serialMIDISynth(serialSynth);
MIDI_NAMESPACE::MidiInterface<Transport> MIDI_SYNTH((Transport&)serialMIDISynth);


#define TICKS_PER_QUARTER_CLOCK 24

const int inChannelSynth = 3;

uint8_t lclBPM = 120;
uint32_t ticks;


#define USE_MIDI  1   // set to 1 to enable MIDI output, otherwise debug output

#if USE_MIDI // set up for direct MIDI serial output

#define DEBUG(s, x)
#define DEBUGX(s, x)
#define DEBUGS(s)
#define SERIAL_RATE 31250
// MIDI_CREATE_DEFAULT_INSTANCE();

#else // don't use MIDI to allow printing debug statements

#define DEBUG(s, x)  do { Serial.print(F(s)); Serial.print(x); } while(false)
#define DEBUGX(s, x) do { Serial.print(F(s)); Serial.print(F("0x")); Serial.print(x, HEX); } while(false)
#define DEBUGS(s)    do { Serial.print(F(s)); } while (false)
#define SERIAL_RATE 57600

#endif // USE_MIDI


// SD chip select pin for SPI comms.
// Default SD chip select is the SPI SS pin (10 on Uno, 53 on Mega).
const uint8_t SD_SELECT = 2;

// LED definitions for status and user indicators
const uint8_t READY_LED = 42;      // when finished
const uint8_t SMF_ERROR_LED = 44;  // SMF error
const uint8_t SD_ERROR_LED = 45;   // SD error
const uint8_t BEAT_LED = 43;       // toggles to the 'beat'

const uint16_t WAIT_DELAY = 2000; // ms

#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))

// The files in the tune list should be located on the SD card 
// or an error will occur opening the file and the next in the 
// list will be opened (skips errors).
const char tuneFolder[] = {"Bliss"};
const char *tuneList[] = {"synth.mid"};
// {
//   "LOOPDEMO.MID",  // simplest and shortest file
//   "BANDIT.MID",
//   "ELISE.MID",
//   "TWINKLE.MID",
//   "GANGNAM.MID",
//   "FUGUEGM.MID",
//   "POPCORN.MID",
//   "AIR.MID",
//   "PRDANCER.MID",
//   "MINUET.MID",
//   "FIRERAIN.MID",
//   "MOZART.MID",
//   "FERNANDO.MID",
//   "SONATAC.MID",
//   "SKYFALL.MID",
//   "XMAS.MID",
//   "GBROWN.MID",
//   "PROWLER.MID",
//   "IPANEMA.MID",
//   "JZBUMBLE.MID",
// };

// These don't play as they need more than 16 tracks but will run if MIDIFile.h is changed
//#define MIDI_FILE  "SYMPH9.MID"     // 29 tracks
//#define MIDI_FILE  "CHATCHOO.MID"   // 17 tracks
//#define MIDI_FILE  "STRIPPER.MID"   // 25 tracks

SDFAT	SD;
MD_MIDIFile SMF;

int note;
int velocity;
int channel;


int estimatedBPM = 0;
long elapsedClockTime;
long previousClockTime;
int ticksPerQuarterMidi = 0;
int tickMultiplier;

long timeLedOn = 0;
uint16_t tickCount = 0;
uint16_t previousTickCount = 0;
uint32_t tickTimeMicros = 0;


bool isLedOn = false;
int ledDurationMs = 100;
int waitForStart = 8;
int currentQuarter = 0;

void handleClock(){
    tickCount++;

    if (tickCount == TICKS_PER_QUARTER_CLOCK)
    {
      elapsedClockTime = millis() - previousClockTime;
      previousClockTime = millis();
      // Serial.print("Clock received - time from previous: ");
      // Serial.println(elapsedClockTime);
      estimatedBPM = int(60 / (float(elapsedClockTime) / 1000.));
      // Serial.print(" ms. Estimated BPM = ");
      // Serial.print(estimatedBPM);
      // Serial.print(" Current position: ");
      // Serial.println(currentQuarter);
      digitalWrite(BEAT_LED, HIGH);
      isLedOn = true;
      timeLedOn = millis();
      tickCount = 0;
      currentQuarter++;
    }
}

void midiCallback(midi_event *pev)
// Called by the MIDIFile library when a file event needs to be processed
// thru the midi communications interface.
// This callback is set up in the setup() function.
{
#if USE_MIDI
  // if ((pev->data[0] >= 0x80) && (pev->data[0] <= 0xe0))
  // {
  //   Serial.write(pev->data[0] | pev->channel);
  //   Serial.write(&pev->data[1], pev->size-1);
  // }
  // else
  //   Serial.write(pev->data, pev->size);
  // Serial.print("MIDI event: ");
  // Serial.println(pev->data[0]);
  if (pev->data[0] == 0x80)
  {
    // send not On
    note = pev->data[1];
    velocity = pev->data[2];
    channel = pev->channel + 1;

    // Serial.println("note on  - channel ");
    // Serial.print(channel);
    // Serial.print(" note ");
    // Serial.print(note);
    // Serial.print(" velocity ");
    // Serial.println(velocity); 
    MIDI_SYNTH.sendNoteOn(note, velocity, inChannelSynth);   
    // MIDI_SYNTH.sendNoteOn(note, velocity, channel);   
     
  }
  else if (pev->data[0] == 0x90)
  {
    // send not Off
    // send not On
    note = pev->data[1];
    velocity = pev->data[2];
    channel = pev->channel + 1;

    // Serial.println("note off - channel ");
    // Serial.print(channel);
    // Serial.print(" note ");
    // Serial.print(note);
    // Serial.print(" velocity ");
    // Serial.println(velocity);
    MIDI_SYNTH.sendNoteOff(note, velocity, inChannelSynth);
  }
#endif
  DEBUG("\n", millis());
  DEBUG("\tM T", pev->track);
  DEBUG(":  Ch ", pev->channel+1);
  DEBUGS(" Data");
  for (uint8_t i=0; i<pev->size; i++)
    DEBUGX(" ", pev->data[i]);
}

void sysexCallback(sysex_event *pev)
// Called by the MIDIFile library when a system Exclusive (sysex) file event needs 
// to be processed through the midi communications interface. Most sysex events cannot 
// really be processed, so we just ignore it here.
// This callback is set up in the setup() function.
{
  DEBUG("\nS T", pev->track);
  DEBUGS(": Data");
  for (uint8_t i=0; i<pev->size; i++)
    DEBUGX(" ", pev->data[i]);
}



// uint16_t tickClock(void)
// // Check if enough time has passed for a MIDI tick and work out how many!
// {
//   static uint32_t lastTickCheckTime, lastTickError;
//   uint8_t   ticks = 0;

//   uint32_t elapsedTime = lastTickError + micros() - lastTickCheckTime;
//   uint32_t tickTime = (60 * 1000000L) / (lclBPM * SMF.getTicksPerQuarterNote());  // microseconds per tick
//   // Serial.println(tickTime);
//   tickTime = (tickTime * 4) / (SMF.getTimeSignature() & 0xf); // Adjusted for time signature

//   if (elapsedTime >= tickTime)
//   {
//     ticks = elapsedTime/tickTime;
//     lastTickError = elapsedTime - (tickTime * ticks);
//     lastTickCheckTime = micros();   // save for next round of checks
//   }

//   return(ticks);
// }

uint16_t tickClock(void)
// Return number of ticks since last call 
{
  int16_t ticks = tickCount - previousTickCount;
  if (ticks < 0)
    // Take care of negative values
    ticks = 24 + ticks;
  ticks = ticks * tickMultiplier;
  previousTickCount = tickCount;

  return ticks;
}



void midiSilence(void)
// Turn everything off on every channel.
// Some midi files are badly behaved and leave notes hanging, so between songs turn
// off all the notes and sound
{
  // midi_event ev;

  // // All sound off
  // // When All Sound Off is received all oscillators will turn off, and their volume
  // // envelopes are set to zero as soon as possible.
  // ev.size = 0;
  // ev.data[ev.size++] = 0xb0;
  // ev.data[ev.size++] = 120;
  // ev.data[ev.size++] = 0;

  // for (ev.channel = 0; ev.channel < 16; ev.channel++)
  //   midiCallback(&ev);
  // MIDI.send

}

void setup(void)
{
  // Set up LED pins
  pinMode(READY_LED, OUTPUT);
  pinMode(SD_ERROR_LED, OUTPUT);
  pinMode(SMF_ERROR_LED, OUTPUT);
  pinMode(BEAT_LED, OUTPUT);

  // reset LEDs
  digitalWrite(READY_LED, LOW);
  digitalWrite(SD_ERROR_LED, LOW);
  digitalWrite(SMF_ERROR_LED, LOW);
  digitalWrite(BEAT_LED, LOW);
  
  Serial.begin(9600);
  while (!Serial) {};

  previousClockTime = millis();

  Serial.println("Waiting for MIDI messages!");


  #if USE_MIDI
    MIDI_MIDRO.begin(MIDI_CHANNEL_OMNI);
    MIDI_MIDRO.setHandleClock(handleClock);
    MIDI_SYNTH.begin();
  #endif

  // DEBUGS("\n[MidiFile Play List]");

  // Initialize SD
  if (!SD.begin(SD_SELECT, SPI_FULL_SPEED))
  {
    DEBUGS("\nSD init fail!");
    digitalWrite(SD_ERROR_LED, HIGH);
    Serial.println("SD error");
    while (true) ;
  }

  // Initialize MIDIFile
  SMF.begin(&SD);
  SMF.setMidiHandler(midiCallback);
  SMF.setSysexHandler(sysexCallback);

  MIDI_SYNTH.setHandleClock(handleClock);

  digitalWrite(READY_LED, HIGH);
}




void loop(void)
{
  static enum { S_IDLE, S_WAIT_TO_START, S_PLAYING, S_END, S_WAIT_BETWEEN } state = S_IDLE;
  static uint16_t currTune = ARRAY_SIZE(tuneList);
  static uint32_t timeStart;

  if ((isLedOn) && ((millis() - timeLedOn) > ledDurationMs))
    {
      digitalWrite(BEAT_LED, LOW);
      isLedOn = false;
    }
  if (MIDI_MIDRO.read())                    // If we have received a message
  {
  }

  switch (state)
  {
  case S_IDLE:    // now idle, set up the next tune
    {
      int err;

      DEBUGS("\nS_IDLE");

      digitalWrite(READY_LED, LOW);
      digitalWrite(SMF_ERROR_LED, LOW);

      currTune++;
      if (currTune >= ARRAY_SIZE(tuneList))
        currTune = 0;

      // use the next file name and play it
      DEBUG("\nFile: ", tuneList[currTune]);
      Serial.println(tuneFolder);
      Serial.println(tuneList[currTune]);

      SMF.setFileFolder(tuneFolder);
      err = SMF.load(tuneList[currTune]);

      if (err != MD_MIDIFile::E_OK)
      {
        DEBUG(" - SMF load Error ", err);
        digitalWrite(SMF_ERROR_LED, HIGH);
        timeStart = millis();
        state = S_WAIT_BETWEEN;
        DEBUGS("\nWAIT_BETWEEN");
        Serial.print("Error Loading ");
        Serial.println(err);
      }
      else
      {
        DEBUG("\nTempo: ", SMF.getTempo());
        DEBUG("\nTime Signature: ", SMF.getTimeSignature());
        DEBUG("\nTicks per quarter: ", SMF.getTicksPerQuarterNote());
        DEBUG("\n# Tracks: ", SMF.getTrackCount());

        Serial.println("S_WAIT_TO_START");
        state = S_WAIT_TO_START;
      }
      // SMF.setTicksPerQuarterNote(BEATS_PER_QUARTER);
      Serial.print("Ticks per quarter note: ");
      ticksPerQuarterMidi = SMF.getTicksPerQuarterNote();
      Serial.println(ticksPerQuarterMidi);
      Serial.print("Ticks time micros: ");
      tickTimeMicros = SMF.getTickTime();
      Serial.println(tickTimeMicros);
      Serial.print("Tick multiplier: ");
      tickMultiplier = int(ticksPerQuarterMidi / TICKS_PER_QUARTER_CLOCK);
      Serial.println(tickMultiplier);
      
    }
    break;
  case S_WAIT_TO_START:
    // Serial.println("Waiting");

    if (currentQuarter >= waitForStart)
    {
      Serial.print(currentQuarter);
      Serial.println(" --> S_PLAYING");
      state = S_PLAYING;
    }
    break;
  case S_PLAYING: // play the file
    DEBUG("\nS_PLAYING ", SMF.isEOF());
    // if (!SMF.isEOF())
    // { 
    uint32_t ticks = tickClock();
    // ticks = int(tickCount * ticksPerQuarterMidi / TICKS_PER_QUARTER_CLOCK);
    // Serial.println(ticks);


    if (ticks > 0)
    {
      // Serial.print("Playing: ticks ");
      // Serial.println(ticks);
      SMF.processEvents(ticks);  
      SMF.isEOF();
    }
    // Serial.print("End of file: ");
    // Serial.println(SMF.isEOF());
    // {
    //   if (SMF.getNextEvent())
    //     tickMetronome();
    // }
    if (SMF.isEOF())
    {
      Serial.println("Song ended");
      state = S_END;
    }
    break;

  case S_END:   // done with this one
    DEBUGS("\nS_END");
    SMF.close();
    midiSilence();
    timeStart = millis();
    state = S_WAIT_BETWEEN;
    DEBUGS("\nWAIT_BETWEEN");
    break;

  case S_WAIT_BETWEEN:    // signal finished with a dignified pause
    digitalWrite(READY_LED, HIGH);
    if (millis() - timeStart >= WAIT_DELAY)
      state = S_IDLE;
    break;

  default:
    state = S_IDLE;
    break;
  }
}
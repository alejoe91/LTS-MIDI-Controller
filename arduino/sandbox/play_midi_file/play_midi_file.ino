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

#include <SoftwareSerial.h>

#define SYNTH_MIDI_RX 7
#define SYNTH_MIDI_TX 6

#define midiChannelSynth 3
#define midiChannelLooper 4


using Transport = MIDI_NAMESPACE::SerialMIDI<SoftwareSerial>;

// SYNTH
SoftwareSerial serialSynth = SoftwareSerial(SYNTH_MIDI_RX, SYNTH_MIDI_TX);
Transport serialMIDISynth(serialSynth);
MIDI_NAMESPACE::MidiInterface<Transport> MIDI_SYNTH((Transport&)serialMIDISynth);


#define BEATS_PER_QUARTER 24

const int inChannelSynth = 3;
const int tempo = 0;

#define USE_MIDI  1   // set to 1 to enable MIDI output, otherwise debug output

#if USE_MIDI // set up for direct MIDI serial output

#define DEBUG(s, x)
#define DEBUGX(s, x)
#define DEBUGS(s)
#define SERIAL_RATE 31250
MIDI_CREATE_DEFAULT_INSTANCE();

#else // don't use MIDI to allow printing debug statements

#define DEBUG(s, x)  do { Serial.print(F(s)); Serial.print(x); } while(false)
#define DEBUGX(s, x) do { Serial.print(F(s)); Serial.print(F("0x")); Serial.print(x, HEX); } while(false)
#define DEBUGS(s)    do { Serial.print(F(s)); } while (false)
#define SERIAL_RATE 57600

#endif // USE_MIDI


// SD chip select pin for SPI comms.
// Default SD chip select is the SPI SS pin (10 on Uno, 53 on Mega).
// const uint8_t SD_SELECT = SS;
const uint8_t SD_SELECT = 53;


// LED definitions for status and user indicators
const uint8_t READY_LED = 43;      // when finished
const uint8_t SMF_ERROR_LED = 44;  // SMF error
const uint8_t SD_ERROR_LED = 45;   // SD error
const uint8_t BEAT_LED = 41;       // toggles to the 'beat'

const uint16_t WAIT_DELAY = 2000; // ms

#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))

// The files in the tune list should be located on the SD card 
// or an error will occur opening the file and the next in the 
// list will be opened (skips errors).
const char *tuneFolder = {"KnightsOfCydonia"};
const char *tuneList[] = {"synth.mid"};

SDFAT	SD;
MD_MIDIFile SMF;

int note;
int velocity;
int channel;

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
  if (pev->data[0] == 0x80)
  {
    // send not On
    note = pev->data[1];
    velocity = pev->data[2];
    channel = pev->channel + 1;

    // Serial.print("note on  - channel ");
    // Serial.println(channel);
    // Serial.print(" note ");
    // Serial.print(note);
    // Serial.print(" velocity ");
    // Serial.println(velocity); 
    MIDI_SYNTH.sendNoteOn(note, velocity, inChannelSynth);    
  }
  else if (pev->data[0] == 0x90)
  {
    // send not Off
    // send not On
    note = pev->data[1];
    velocity = pev->data[2];
    channel = pev->channel + 1;

    // Serial.print("note off - channel ");
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
  // // MIDI.send
  // MIDI_SYNTH.sendControlChange(120, 127, midiChannelSynth);
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

  #if USE_MIDI
    MIDI_SYNTH.begin();
  #endif

  Serial.println("\n[MidiFile Play List]");

  // Initialize SD
  if (!SD.begin(SD_SELECT, SPI_FULL_SPEED))
  {
    DEBUGS("\nSD init fail!");
    Serial.println("Error initializing SD card");
    digitalWrite(SD_ERROR_LED, HIGH);
    while (true) ;
  }

  // Initialize MIDIFile
  SMF.begin(&SD);
  SMF.setMidiHandler(midiCallback);
  SMF.setSysexHandler(sysexCallback);

  digitalWrite(READY_LED, HIGH);
}

void tickMetronome(void)
// flash a LED to the beat
{
  static uint32_t lastBeatTime = 0;
  static boolean  inBeat = false;
  uint16_t  beatTime;

  beatTime = 60000/SMF.getTempo();    // msec/beat = ((60sec/min)*(1000 ms/sec))/(beats/min)
  if (!inBeat)
  {
    if ((millis() - lastBeatTime) >= beatTime)
    {
      lastBeatTime = millis();
      digitalWrite(BEAT_LED, HIGH);
      inBeat = true;
    }
  }
  else
  {
    if ((millis() - lastBeatTime) >= 100)	// keep the flash on for 100ms only
    {
      digitalWrite(BEAT_LED, LOW);
      inBeat = false;
    }
  }
}

void loop(void)
{
  static enum { S_IDLE, S_PLAYING, S_END, S_WAIT_BETWEEN } state = S_IDLE;
  static uint16_t currTune = ARRAY_SIZE(tuneList);
  static uint32_t timeStart;

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
      SMF.setFileFolder(tuneFolder);

      err = SMF.load(tuneList[currTune]);
      if (err != MD_MIDIFile::E_OK)
      {
        Serial.println("Error loading");
        digitalWrite(SMF_ERROR_LED, HIGH);
        timeStart = millis();
        state = S_WAIT_BETWEEN;
      }
      else
      {
        Serial.println("Loaded correctly");
        Serial.print("\nTempo: ");
        if (tempo > 0)
          SMF.setTempo(tempo);
        Serial.println(SMF.getTempo());
        Serial.print("\nTime Signature: ");
        Serial.println(SMF.getTimeSignature());
        Serial.print("\nTicks per quarter: ");
        Serial.println(SMF.getTicksPerQuarterNote());
        Serial.print("\n# Tracks: ");
        Serial.println(SMF.getTrackCount());

        DEBUGS("\nS_PLAYING");
        state = S_PLAYING;
      }
    }
    break;

  case S_PLAYING: // play the file
    // DEBUG("\nS_PLAYING ", SMF.isEOF());
    if (!SMF.isEOF())
    {
      // Serial.println("Get next event");
      if (SMF.getNextEvent())
      {
        // Serial.println("Process next event");
        tickMetronome();
        // Serial.println("Done");
      }
        
    }
    else
      state = S_END;
    break;

  case S_END:   // done with this one
    // DEBUGS("\nS_END");
    Serial.println("EEEEEEEEEEEENNNNNNNNNNDDDDDDDDDDDDDDDDD");
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
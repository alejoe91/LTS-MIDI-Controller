#include <MIDI.h>
#include <SoftwareSerial.h>

#define DEFAULT_TX TX
#define DEFAULT_RX RX

#define DITTO_MIDI_RX 9
#define DITTO_MIDI_TX 10
#define SYNTH_MIDI_RX 11
#define SYNTH_MIDI_TX 12


// Simple tutorial on how to receive and send MIDI messages.
// Here, when receiving any message on channel 4, the Arduino
// will blink a led and play back a note for 1 second.

// MIDI_CREATE_DEFAULT_INSTANCE();


using Transport = MIDI_NAMESPACE::SerialMIDI<SoftwareSerial>;

SoftwareSerial serialSynth = SoftwareSerial(SYNTH_MIDI_RX, SYNTH_MIDI_TX);
Transport serialMIDISynth(serialSynth);
MIDI_NAMESPACE::MidiInterface<Transport> MIDI_SYNTH((Transport&)serialMIDISynth);

SoftwareSerial serialLooper = SoftwareSerial(DITTO_MIDI_RX, DITTO_MIDI_TX);
Transport serialMIDILooper(serialLooper);
MIDI_NAMESPACE::MidiInterface<Transport> MIDI_LOOPER((Transport&)serialMIDILooper);


MIDI_CREATE_DEFAULT_INSTANCE();

const int inChannelSynth = 3;
const int inChannelLooper = 4;
const int LED = 7;


uint16_t estimatedBPM = 0;
long elapsedClockTime;
long previousClockTime;

long timeLedOn = 0;
bool isLedOn = false;
bool printedStart = false;
int ledDurationMs = 100;
 

int clockCount = 0;
int startAt = 4;
int stopAt = 16;
int current = 0;
int program = 0;
int max_program = 255;

void handleClock()
{
  MIDI_SYNTH.sendClock();
  MIDI_LOOPER.sendClock();

  clockCount++;

  if (clockCount == 24)
  {
    elapsedClockTime = millis() - previousClockTime;
    previousClockTime = millis();
    Serial.print("Clock received - time from previous: ");
    Serial.print(elapsedClockTime);
    estimatedBPM = int(60 / (float(elapsedClockTime) / 1000.));
    Serial.print(" ms. Estimated BPM = ");
    Serial.print(estimatedBPM);
    Serial.print(" CurrentQuarter = ");
    Serial.println(current);
    digitalWrite(LED, HIGH);
    isLedOn = true;
    timeLedOn = millis();
    clockCount = 0;
    current++;
  }
}

void handleStart()
{
  MIDI_SYNTH.sendStart();
}

void handleStop()
{
  MIDI_SYNTH.sendStop();
}


void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    MIDI.begin(MIDI_CHANNEL_OMNI);
    MIDI_LOOPER.begin();
    MIDI_SYNTH.begin();                      // Launch MIDI and listen to channel 4
    Serial.begin(9600);
    while (! Serial) {}
    MIDI.setHandleClock(handleClock);
    MIDI.setHandleStart(handleStart);
    MIDI.setHandleStop(handleStop);
    current = 0;
}


void loop()
{
    if ((isLedOn) && ((millis() - timeLedOn) > ledDurationMs))
    {
      digitalWrite(LED, LOW);
      isLedOn = false;
    }
    if (MIDI.read())
    {

    }

    if (current == startAt)
    {
      MIDI_SYNTH.sendStart();
      if (!printedStart)
      {
        Serial.println("Start");
        printedStart = true;
      }
        
    }
    if (current == stopAt)
    {
      MIDI_SYNTH.sendStop();
      printedStart = false;
      Serial.println("Stop");
      current = 0;
      int bankMSB = program / 127;
      int programLSB = program % 127;
      Serial.print("Change Preset to ");
      Serial.println(programLSB);

      MIDI_SYNTH.sendControlChange(0, bankMSB, inChannelSynth);
      MIDI_SYNTH.sendProgramChange(programLSB, inChannelSynth);
      program++;
      if (program > 255)
        program = 0;
    }

    // if(currentMillis - prevMillis > interval) {
    //     // save the last time.
    //     prevMillis = currentMillis;
    //     MIDI_SYNTH.sendClock();
    //     clockCount++;
    //     if (clockCount == 24){
    //       Serial.print("Sent clock ");
    //       clockCount = 0;
    //       current++;
    //       Serial.println(current);
    //       if (current == startAt)
    //       {
    //         MIDI_SYNTH.sendStart();
    //         Serial.println("Start");
    //       }
    //       if (current == stopAt)
    //       {
    //         MIDI_SYNTH.sendStop();
    //         Serial.println("Stop");
    //         current = 0;
    //         int bankMSB = program / 127;
    //         int programLSB = program % 127;
    //         Serial.print("Change Preset to ");
    //         Serial.println(programLSB);

    //         MIDI_SYNTH.sendControlChange(0, bankMSB, inChannelSynth);
    //         MIDI_SYNTH.sendProgramChange(programLSB, inChannelSynth);
    //         program++;
    //       }
    //     }
        
    // }
}

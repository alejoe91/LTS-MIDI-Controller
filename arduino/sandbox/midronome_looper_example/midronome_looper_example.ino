#include <MIDI.h>

// Simple tutorial on how to receive and send MIDI messages.
// Here, when receiving any message on channel 4, the Arduino
// will blink a led and play back a note for 1 second.

MIDI_CREATE_DEFAULT_INSTANCE();

#define BEATS_PER_QUARTER 24

const int inChannelDitto = 4;
const int LED = 9;


int estimatedBPM = 0;
long elapsedClockTime;
long previousClockTime;

long timeLedOn = 0;
int clockCount = 0;
bool isLedOn = false;
int ledDurationMs = 100;


int clockCount = 0;
int startAt = 8;
int stopAt = 16;

int current = 0;
int songCounter = 0;

void handleClock(){
    MIDI.sendClock();
    clockCount++;

    if (clockCount == BEATS_PER_QUARTER)
    {
      elapsedClockTime = millis() - previousClockTime;
      previousClockTime = millis();
      Serial.print("Clock received - time from previous: ");
      Serial.print(elapsedClockTime);
      estimatedBPM = int(60 / (float(elapsedClockTime) / 1000.));
      Serial.print(" ms. Estimated BPM = ");
      Serial.println(estimatedBPM);
      digitalWrite(LED, HIGH);
      isLedOn = true;
      timeLedOn = millis();
      clockCount = 0;
    }
}

void handleStart(){

}

void handleStop(){}


void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    MIDI.begin(4);                      // Launch MIDI and listen to channel 4
    Serial.begin(9600);
    while (! Serial) {}
}

void loop()
{
    unsigned long currentMillis = millis();
    if(currentMillis - prevMillis > interval) {
        // save the last time.
        prevMillis = currentMillis;
        MIDI.sendClock();
        clockCount++;
        if (clockCount == 24){
          Serial.print("Sent clock ");
          clockCount = 0;
          current++;
          Serial.println(current);
          if (current == startAt)
          {
            MIDI.sendStart();
            Serial.println("Start");
          }
          if (current == stopAt)
          {
            MIDI.sendStop();
            Serial.println("Stop");
            current = 0;
            int bankMSB = 1;
            int program = 0;
            Serial.println("Change Preset");
            MIDI.sendControlChange(0, bankMSB, inChannelSynth);
            MIDI.sendProgramChange(program, inChannelSynth);
            //MIDI.sendControlChange(32, bankLSB, inChannelSynth);
          }
        }
        
    }
}

#include <MIDI.h>

// Simple tutorial on how to receive and send MIDI messages.
// Here, when receiving any message on channel 4, the Arduino
// will blink a led and play back a note for 1 second.

MIDI_CREATE_DEFAULT_INSTANCE();
//const int inChannelDitto = 4;

const int inChannelDitto = 3;


long bpm = 140;
long tempo = 1000/(bpm/60);
 
long prevMillis = 0;
long interval = tempo/24; 
int clockCount = 0;
int startAt = 8;
int stopAt = 16;
int current = 0;


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
          }
        }
        
    }
}

#include <MIDI.h>

// Simple tutorial on how to receive and send MIDI messages.
// Here, when receiving any message on channel 4, the Arduino
// will blink a led and play back a note for 1 second.

/*
DITTO x4 MIDI Spec
Responds to MIDI Channel 4 (hardcoded)

CC3 Looper 1 Rec/Dub/Start
CC9 Looper 1 Stop
CC14 Looper 1 Clear
CC15 Looper 1 Level
CC20 Looper 1 Hold to Store
CC21 Looper 1 Clear Backtrack
CC22 Looper 2 Rec/Dub/Start
CC23 Looper 2 Stop
CC24 Looper 2 Clear
CC25 Looper 2 Level
CC26 Looper 2 Hold to Store
CC27 Looper 2 Clear Backtrack
CC28 Decay “Level”
CC29 All Loops Stop
CC30 All Loops Clear
CC31 FX On/Off
CC85 Parallel/Serial Toggle

The FX respond to Prg change message 1 to 7
*/

MIDI_CREATE_DEFAULT_INSTANCE();
const int inChannelDitto = 4;

int initial_delay_ms = 10000;
int delay_ms = 5000;
int num_iter = 0;
int max_iter = 3;

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    MIDI.begin(4);                      // Launch MIDI and listen to channel 4
    Serial.begin(9600);
    while (! Serial) {}
}


void loop()
{
    if (num_iter <= max_iter)
    {
      delay(delay_ms);
      Serial.print("Starting control: iter ");
      Serial.print(num_iter);
      Serial.print(" out of ");
      Serial.print(max_iter);
      Serial.println("\n\n");
    }

    if (num_iter == 0)
      {
        // Rec Looper 1
      Serial.println("Rec Ch. 1");
      MIDI.sendControlChange(3, 127, inChannelDitto);
      delay(delay_ms);
      // Play Looper 1
      Serial.println("Play Ch. 1");
      MIDI.sendControlChange(3, 127, inChannelDitto);
      delay(delay_ms);
      // Stop Looper 1
      Serial.println("Stop Ch. 1");
      MIDI.sendControlChange(9, 127, inChannelDitto);
    
      // Rec Looper 2
      Serial.println("Rec Ch. 2");
      MIDI.sendControlChange(22, 127, inChannelDitto);
      delay(delay_ms);
      // Play Looper 2
      Serial.println("Play Ch. 2");
      MIDI.sendControlChange(22, 127, inChannelDitto);
      delay(delay_ms);
      // Stop Looper 2
      Serial.println("Stop Ch. 2");
      MIDI.sendControlChange(23, 127, inChannelDitto);

      num_iter++;
    }
    else if (num_iter < max_iter)
    {
      // Play Looper 1
      Serial.println("Play Ch. 1");
      MIDI.sendControlChange(3, 127, inChannelDitto);
      delay(delay_ms);
      // Stop Looper 1
      Serial.println("Stop Ch. 1");
      MIDI.sendControlChange(9, 127, inChannelDitto);

      // Play Looper 2
      Serial.println("Play Ch. 2");
      MIDI.sendControlChange(22, 127, inChannelDitto);
      delay(delay_ms);
      // Stop Looper 2
      Serial.println("Stop Ch. 2");
      MIDI.sendControlChange(23, 127, inChannelDitto);

      num_iter++;
    }
    else if (num_iter == max_iter)
    {
      Serial.println("Stop all");
      MIDI.sendControlChange(29, 127, inChannelDitto);
      Serial.println("Clear all");
      MIDI.sendControlChange(30, 127, inChannelDitto);
      num_iter++;
      Serial.println("Reset arduino to restart\n\n");
    }
}

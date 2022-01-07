# Pico CV Recorder

Using the Raspberry Pi Pico microcontroller to record voltages and then replay those voltages,
e.g. for use in a modular synthesizer.

Outputs and inputs for CV/Gate style modular patching.

Requires installing the
[Raspberry Pi Pico C/C++ SDK](https://datasheets.raspberrypi.org/pico/raspberry-pi-pico-c-sdk.pdf).

## Optional analog pots

This makes a nice sequencer when paired with analog pots.
Three of the GPIO pins are setup to address a 8 to 1 mux, stepping through each address
on each trigger.
It could be easily expanded to 16 or more steps by adding additional pots and mux chips.

## Modes

Hardware switch options:

- not quantized
- semitone quantized
- scale quantized (major, pentatonic, etc - define what you like in the code)
- external trigger
- record/playback

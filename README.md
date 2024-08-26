# Pico CV Recorder

Using the Raspberry Pi Pico microcontroller to record voltages and then replay those voltages,
e.g. for use in a modular synthesizer.

Outputs and inputs for CV/Gate style modular patching.

Requires installing the
[Raspberry Pi Pico C/C++ SDK](https://datasheets.raspberrypi.org/pico/raspberry-pi-pico-c-sdk.pdf).

# 16 Step Sequencer

![control panel for a modular synthesizer](/16_step_sequencer_panel.jpg)

## Hardware

Todo: add schematics to this repo.

## 16 step sequencer pots

Default source for the ADC is a set of analog potentiometers that can be set between 0 V and 3.3 V.

Which pot is read is controlled by CD4051 mux ICs. Each IC can address 8 pots. By using the inhibit pin as a chip select pin, many sets of 8 pots can be addressed.
The code is currently set up to read 16 pots.

## Panel connections

### Signal input/output

Modular synth systems can output voltages up +/-15 Vdc, so an appropriate front end has to map the system signal voltage range to the 3.3 V level of the ADC.

One solution is to shift the system 0 V to 1.65 V and scale down the input to fit into 0 to 3.3 V. This can be accomplished with op amps.

The output should perform the inverse function by shifting 1.65 V to 0 V and scaling up from 3.3 V to the system level.

When using the pots are a source, a separate output calibration may be desirable to control the sensitivity of the controls. Too sensitive means hard to tune for musical effect, especially if using it to control the frequency of an audio oscillator.

### Trigger input/output

Trigger levels must also be shifted from the system level down to 3.3 V on the way in, and boosted from 3.3 V to the system level on the way out.

## Switches

- quantize enable
- semitone quantized
- scale quantized (major, pentatonic, etc - define what you like in the code)
- record/playback

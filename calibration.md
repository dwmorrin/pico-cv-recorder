# Calibration Procedure

The module requires physical calibration of the analog front-end and back-end to ensure accurate 1 V/Octave tracking. The hardware utilizes four trimmers: two for the output stage (offset and scale) and two for the input stage (offset and scale).

Because the microcontroller acts as a bridge between the input and output, the output stage must be calibrated first. Once the output is accurate, the firmware can act as a digital pass-through to calibrate the input stage.

## Equipment Required

- Digital Multimeter (DMM) with millivolt accuracy.
- Precision CV source (e.g., a well-calibrated MIDI-to-CV converter or keyboard).
- Small flathead screwdriver for trimming.

## Part 1: Output Stage Calibration

This aligns the DAC output to true 0 V and ensures the mathematical 1 V/Octave steps translate accurately to the physical output.

1. **Initialize State:** Power the module and set the `SRC` switch to **Pot Mode**. Set the `QUANT` switch to **Chromatic** (center position).
2. **Set 0 V Reference:** Turn step 1's potentiometer to approximately its center position (50%). The internal quantizer will snap this to the nearest digital zero value.
3. **Adjust Output Offset:** Connect the DMM to the `CV OUT` jack. Adjust the **Output Offset** trimmer until the DMM reads exactly `0.000 V`.
4. **Set High Voltage Reference:** Turn the potentiometers clockwise until the output steps up to `0.5 V` for the low range, or `2.00 V` on the high range (about 75%).
5. **Adjust Output Scale:** Adjust the **Output Scale** trimmer until the DMM reads exactly `1.000 V` for every octave shifted (e.g., exactly `3.000 V` or `4.000 V`).
6. **Iterate:** Because scale and offset in op-amp stages can interact slightly, return the pot to center, verify the `0.000 V` offset, and repeat the process until both measurements are stable.

## Part 2: Input Stage Calibration

This aligns incoming CV signals to the hardware center of the ADC and ensures external 1 V/Octave signals map correctly to the internal digital scaling.

1. **Initialize State:** Set the `SRC` switch to **CV Mode**. Set the `QUANT` switch to **Unquantized** (down position) so the firmware acts as a direct 1:1 digital pass-through.
2. **Set 0 V Reference:** Patch a precision `0.000 V` signal from the external CV source into the `CV IN` jack.
3. **Adjust Input Offset:** Monitor the `CV OUT` jack with the DMM. Adjust the **Input Offset** trimmer until the DMM reads exactly `0.000 V`.
4. **Set High Voltage Reference:** Change the external CV source to output exactly `+4.000 V`.
5. **Adjust Input Scale:** Adjust the **Input Scale** trimmer until the `CV OUT` jack reads exactly `+4.000 V` on the DMM.
6. **Iterate:** Return the external source to `0.000 V`, verify the offset, and repeat the calibration until both the 0 V center and the 1 V/Octave tracking are precise.

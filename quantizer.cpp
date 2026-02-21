#include "quantizer.h"

// Lookup tables to snap any of the 12 chromatic semitones to the nearest valid scale degree
const int snap_major[12] = {0, 0, 2, 2, 4, 5, 5, 7, 7, 9, 9, 11};
const int snap_pentatonic[12] = {0, 0, 2, 2, 4, 4, 7, 7, 7, 9, 9, 0}; // 11 wraps to 0 (next octave)

// Helper: Converts a deviation in ADC steps to a continuous semitone count
int32_t steps_to_semitones(int32_t steps)
{
  // Multiply by 12 first to preserve precision before dividing by the octave size
  // We add (MAGIC_NUMBER_OCTAVE / 2) to properly round to nearest instead of truncating
  if (steps >= 0)
    return ((steps * 12) + (MAGIC_NUMBER_OCTAVE / 2)) / MAGIC_NUMBER_OCTAVE;
  else
    return ((steps * 12) - (MAGIC_NUMBER_OCTAVE / 2)) / MAGIC_NUMBER_OCTAVE;
}

// Helper: Converts a semitone count back to ADC steps
int32_t semitones_to_steps(int32_t semitones)
{
  return (semitones * MAGIC_NUMBER_OCTAVE) / 12;
}

// Helper: Snaps a raw ADC value to a specific scale lookup table
uint16_t snap_to_scale(uint16_t adc_value, const int snap_array[])
{
  int32_t deviation = (int32_t)adc_value - ADC_CENTER;
  int32_t semitones = steps_to_semitones(deviation);

  // Handle C++ negative modulo behavior for octaves below 0V
  int32_t octave = semitones >= 0 ? semitones / 12 : (semitones - 11) / 12;
  int32_t note = semitones - (octave * 12);

  int32_t snapped_note = snap_array[note];

  // Pentatonic edge case: wrapping from major 7th up to the root of the next octave
  if (note == 11 && snapped_note == 0)
  {
    octave++;
  }

  int32_t final_semitones = (octave * 12) + snapped_note;
  int32_t final_steps = semitones_to_steps(final_semitones);

  return (uint16_t)(ADC_CENTER + final_steps);
}

// --- PUBLIC API ---

uint16_t semitone_quantize(uint16_t adc_value)
{
  int32_t deviation = (int32_t)adc_value - ADC_CENTER;
  int32_t semitones = steps_to_semitones(deviation);
  int32_t quantized_steps = semitones_to_steps(semitones);

  return (uint16_t)(ADC_CENTER + quantized_steps);
}

uint16_t quantize_scale_major(uint16_t adc_value)
{
  return snap_to_scale(adc_value, snap_major);
}

uint16_t quantize_scale_pentatonic(uint16_t adc_value)
{
  return snap_to_scale(adc_value, snap_pentatonic);
}

uint16_t scale_pot_value(uint16_t raw_adc, PotRange current_range)
{
  uint32_t target_max_steps = 0;

  switch (current_range)
  {
  case RANGE_1_OCTAVE:
    target_max_steps = MAGIC_NUMBER_OCTAVE; // +/- 0.5 octaves
    break;
  case RANGE_2_OCTAVES:
    target_max_steps = MAGIC_NUMBER_OCTAVE * 2; // +/- 1 octave
    break;
  case RANGE_5_OCTAVES:
    target_max_steps = MAGIC_NUMBER_OCTAVE * 5; // +/- 2.5 octaves
    break;
  default:
    target_max_steps = ADC_MAX_VALUE;
    break;
  }

  int32_t deviation = (int32_t)raw_adc - ADC_CENTER;
  int32_t scaled_deviation = (deviation * (int32_t)target_max_steps) / ADC_MAX_VALUE;

  return (uint16_t)(ADC_CENTER + scaled_deviation);
}
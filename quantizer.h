#pragma once
#include "stdint.h"
#include "pico/stdlib.h"

// The new hardware-accurate magic numbers for a +/- 10V expander
#define ADC_MAX_VALUE 4095
#define ADC_CENTER 2048
#define MAGIC_NUMBER_OCTAVE 205 // 4095 steps / 20V span = 204.8 steps per 1V

#define SIZE_OF_ARRAY(x) (sizeof(x) / sizeof((x)[0]))

enum PotRange
{
  RANGE_1_OCTAVE,
  RANGE_2_OCTAVES,
  RANGE_5_OCTAVES,
  RANGE_MAX
};

// Core quantization functions
uint16_t semitone_quantize(uint16_t adc_value);
uint16_t quantize_scale_major(uint16_t adc_value);
uint16_t quantize_scale_pentatonic(uint16_t adc_value);

// Pot scaling
uint16_t scale_pot_value(uint16_t raw_adc, PotRange current_range);
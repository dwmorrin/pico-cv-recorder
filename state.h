#pragma once
#include "pico/stdlib.h"
#include "quantizer.h" // Needed for the PotRange enum

#define MEMORY_LENGTH 16

struct SequencerState
{
  // Hardware interrupt flags (must be volatile)
  volatile bool trigger_pending = false;
  volatile bool mode_toggle_pending = false;
  volatile bool range_toggle_pending = false;

  // Application state
  uint tempo_delay_ms = 500;
  bool recording = true;
  bool external_trigger = false;
  bool quantize_enabled = false;
  bool quantize_full_range = false;
  bool quantize_pentatonic = false;
  bool pot_mode = false;
  PotRange pot_range = RANGE_1_OCTAVE; // Default to 1 octave

  // Sequence memory
  uint16_t memory[MEMORY_LENGTH] = {0};
  int memory_index = 0;

  // Timer tracking
  alarm_id_t internal_clock_alarm = 0;
};

extern SequencerState state;
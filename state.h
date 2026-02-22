#pragma once
#include "pico/stdlib.h"
#include "quantizer.h"

#define MEMORY_LENGTH 16

struct SequencerState
{
  // Hardware interrupt flags
  volatile bool trigger_pending = false;
  volatile bool mode_toggle_pending = false;
  volatile bool scale_toggle_pending = false; // Triggered by long-press
  volatile bool range_toggle_pending = false;
  volatile uint64_t mode_button_press_time = 0;

  // Application state
  uint tempo_delay_ms = 500;
  bool recording = true;
  bool external_trigger = false;

  // UI State
  bool pot_mode = false;
  int quantize_mode = 0; // 0=Unquantized, 1=Chromatic, 2=Scale
  PotRange pot_range = RANGE_2_OCTAVES;
  MusicalScale active_scale = SCALE_MAJOR;

  // Sequence memory
  uint16_t memory[MEMORY_LENGTH] = {0};
  int memory_index = 0;

  // Timer tracking
  alarm_id_t internal_clock_alarm = 0;
};

extern SequencerState state;
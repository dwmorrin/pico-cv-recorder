#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"

#define I2C_PORT i2c0

/*
  Adafruit MCP4725 DAC breakout:
    GPIO 4 (pin 6) -> SDA (pin 4)
    GPIO 5 (pin 7) -> SCL (pin 3)
    3.3 V (pin 36) -> VDD (pin 1)
    GND (pin 38)   -> GND (pin 2)

  Trigger button:
    GPIO 16 (pin 21) -> N.O. switch to 3.3 V
  Trigger input (high pulse):
    GPIO 18 (pin 24)
  Mode button:
    GPIO 17 (pin 22) -> N.O. switch to 3.3 V
  Mode input (high pulse):
    GPIO 19 (pin 25)

  Tempo control: (using internal timer for playback)
    Potentiometer:
      CCW   -> 3.3 V (pin 36) (slowest tempo)
      WIPER -> GPIO 27 (pin 32) (ADC)
      CW    -> GND (pin 2) (fastest tempo)
  
  Voltage-to-be-sampled input:
    GPIO 26 (pin 31)
  
  Trigger output: (pulse high on playback)
    GPIO 15 (pin 20)
*/

// tempo constants
const uint SLOW_MS = 3e3; // seconds = 1/3 Hz = 20 bpm
const uint FAST_MS = 142; // milliseconds = 7 Hz = 420 bpm
const uint TEMPO_READ_DELAY = 100; // milliseconds
static struct repeating_timer tempoTimer;

// ADC constants
// pins can be 26-29, inputs are 0-3, respectively
const uint CV_IN_PIN = 26;
const uint CV_IN = 0;
const uint TEMPO_IN_PIN = 27;
const uint TEMPO_IN= 1;

// button debounce time in milliseconds
const uint DEBOUNCE_MS = 20;

// default address of MCP4725 DAC
static int addr = 0x62;
const uint SDA_PIN = 4;
const uint SCL_PIN = 5;
const uint IC2_HZ = 400e3;

// led output
const uint LED_PIN = 25;

// trigger output
const uint TRIG_OUT_PIN = 15;

// controls
const uint TRIG_BUTTON_PIN = 16;
const uint MODE_BUTTON_PIN = 17;
const uint TRIG_PULSE_PIN = 18;
const uint MODE_PULSE_PIN = 19;

// memory
#define MEMORY_LENGTH 16
uint16_t memory[MEMORY_LENGTH] = {0};
static int memoryIndex = 0;

// global state
volatile uint tempoDelayMs = 500; // milliseconds; tempo pot updates
volatile bool triggered = false;
volatile bool modeToggled = false;
static bool recording = false;
static alarm_id_t internalClockAlarmId = 0;

static void mcp4725_write(uint value) {
  uint8_t data[] = {0x40, value / 16, (value % 16) << 4};
  i2c_write_blocking(I2C_PORT, addr, data, 3, false);
}

void onTrigger();
int64_t beatTrigger(alarm_id_t, void*);

int64_t beatAnticipate(alarm_id_t id, void* user_data) {
  gpio_put(LED_PIN, false);
  internalClockAlarmId = add_alarm_in_ms(tempoDelayMs, &beatTrigger, 0, true);
  return 0;
}

int64_t beatTrigger(alarm_id_t id, void* user_data) {
  onTrigger();
  gpio_put(LED_PIN, true);
  if (!recording)
    internalClockAlarmId = add_alarm_in_ms(tempoDelayMs, &beatAnticipate, 0, true);
  return 0;
}

int64_t pinOff(alarm_id_t id, void* user_data) {
  uint pin = (uint)user_data;
  gpio_put(pin, false);
  return 0;
}

void onPulse(uint);
void onEdge(uint, uint32_t);

void enableInput(uint pin) {
  gpio_set_irq_enabled_with_callback(pin, GPIO_IRQ_EDGE_RISE, true, &onEdge);
}

void disableInput(uint pin) {
  gpio_set_irq_enabled(pin, GPIO_IRQ_EDGE_RISE, false);
}

// debouncing: edge calls this, checks if button held high, runs trig or resets interrupt
int64_t checkTrigger(alarm_id_t id, void* user_data) {
  uint gpio = (uint) user_data;
  if (gpio_get(gpio)) onPulse(gpio);
  else 
    enableInput(gpio);
  return 0;
}

// debouncing: immediately react for pulse inputs, debounce manually pressed buttons
void onEdge(uint gpio, uint32_t events) {
  switch (gpio) {
    case TRIG_PULSE_PIN:
    case MODE_PULSE_PIN:
      // no debounce
      onPulse(gpio);
      break;
    case TRIG_BUTTON_PIN:
    case MODE_BUTTON_PIN:
      // software debounce
      disableInput(gpio);
      add_alarm_in_ms(DEBOUNCE_MS, &checkTrigger, (void*) gpio, true);
      break;
  }
}

// debouncing: this runs post-debounce check
void onPulse(uint gpio) {
  switch (gpio) {
    case TRIG_BUTTON_PIN:
    case TRIG_PULSE_PIN:
      triggered = true;
      if (gpio == TRIG_BUTTON_PIN) enableInput(gpio);
      break;
    case MODE_BUTTON_PIN:
    case MODE_PULSE_PIN:
      modeToggled = true;
      if (gpio == MODE_BUTTON_PIN) enableInput(gpio);
      break;
  }
}

void resetInternalClock() {
  if (internalClockAlarmId) cancel_alarm(internalClockAlarmId);
  gpio_put(LED_PIN, 0);
  if (!recording) internalClockAlarmId = add_alarm_in_ms(0, &beatTrigger, 0, true);
}

// update tempo potentiometer reading; probably not quite right yet with the reset
bool updateTempoDelay(repeating_timer_t* rt) {
  adc_select_input(TEMPO_IN);
  uint16_t tempoRaw = adc_read(); // 0 to 4096
  // divide desired tempo by two; two delays per beat
  uint newTempoDelayMs = ((SLOW_MS - FAST_MS) * tempoRaw / 4096 + FAST_MS)/2;
  if (newTempoDelayMs > tempoDelayMs + 20 || newTempoDelayMs < tempoDelayMs - 20) {
    tempoDelayMs = newTempoDelayMs;
  }
  return true;
}

void onTrigger() {
  if (recording) {
    gpio_put(LED_PIN, true);
    add_alarm_in_ms(20, &pinOff, (void*) LED_PIN, true);
    adc_select_input(CV_IN);
    memory[memoryIndex] = adc_read();
  }
  mcp4725_write(memory[memoryIndex]);
  gpio_put(TRIG_OUT_PIN, true);
  add_alarm_in_ms(10, &pinOff, (void*) TRIG_OUT_PIN, true);
  memoryIndex = (memoryIndex + 1) % MEMORY_LENGTH;
  triggered = false;
}

void onModeToggle() {
  recording = !recording;
  resetInternalClock();
  modeToggled = false;
}

int main() {
  stdio_init_all();
  
  // register front panel controls with callbacks
  enableInput(TRIG_BUTTON_PIN);
  enableInput(MODE_BUTTON_PIN);
  enableInput(TRIG_PULSE_PIN);
  enableInput(MODE_PULSE_PIN);

  // i2c setup for DAC
  i2c_init(I2C_PORT, IC2_HZ);
  gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
  gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
  gpio_pull_up(SDA_PIN);
  gpio_pull_up(SCL_PIN);

  // ADC setup
  adc_init();
  adc_gpio_init(CV_IN_PIN);
  adc_gpio_init(TEMPO_IN_PIN);

  // trigger output
  gpio_init(TRIG_OUT_PIN);
  gpio_set_dir(TRIG_OUT_PIN, GPIO_OUT);
  gpio_put(TRIG_OUT_PIN, false);

  // tempo indicator LED
  gpio_init(LED_PIN);
  gpio_set_dir(LED_PIN, GPIO_OUT);
  resetInternalClock();

  add_repeating_timer_ms(TEMPO_READ_DELAY, &updateTempoDelay, 0, &tempoTimer);

  while (true) {
    if (triggered) onTrigger();
    if (modeToggled) onModeToggle();
  };
}
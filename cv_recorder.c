#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"

#define I2C_PORT i2c0

/*
  Connections from Raspberry Pi Pico to Adafruit MCP4725 DAC breakout:
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
*/

// button debounce time in milliseconds
const uint DEBOUNCE_MS = 20;

// default address of MCP4725 DAC
static int addr = 0x62;
const uint LED_PIN = 25;
const uint TRIG_BUTTON_PIN = 16;
const uint MODE_BUTTON_PIN = 17;
const uint TRIG_PULSE_PIN = 18;
const uint MODE_PULSE_PIN = 19;
#define MEMORY_LENGTH 16
uint16_t memory[MEMORY_LENGTH] = {0};
static int memoryIndex = 0;
static bool recording = true;
static alarm_id_t ledAlarmId = 0;

static void mcp4725_write(uint value) {
  uint8_t data[] = {0x40, value / 16, (value % 16) << 4};
  i2c_write_blocking(I2C_PORT, addr, data, 3, false);
}

// LED indicates mode: solid for playback, blinking for recording
int64_t ledOn(alarm_id_t, void*);

int64_t ledOff(alarm_id_t id, void* user_data) {
  gpio_put(LED_PIN, 0);
  ledAlarmId = add_alarm_in_ms(250, &ledOn, 0, true);
  return 0;
}

int64_t ledOn(alarm_id_t id, void* user_data) {
  gpio_put(LED_PIN, 1);
  if (recording)
    ledAlarmId = add_alarm_in_ms(250, &ledOff, 0, true);
  return 0;
}

void onTrigger(uint);
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
  if (gpio_get(gpio)) onTrigger(gpio);
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
      onTrigger(gpio);
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
void onTrigger(uint gpio) {
  switch (gpio) {
    case TRIG_BUTTON_PIN:
    case TRIG_PULSE_PIN:
      if (recording) memory[memoryIndex] = adc_read();
      else mcp4725_write(memory[memoryIndex]);
      printf("%02d: %d\n", memoryIndex, memory[memoryIndex]);
      memoryIndex = (memoryIndex + 1) % MEMORY_LENGTH;
      if (gpio == TRIG_BUTTON_PIN) enableInput(gpio);
      break;
    case MODE_BUTTON_PIN:
    case MODE_PULSE_PIN:
      recording = !recording;
      // reset indicator LED
      cancel_alarm(ledAlarmId);
      gpio_put(LED_PIN, 1);
      if (recording) ledAlarmId = add_alarm_in_ms(250, &ledOff, 0, true);
      printf("Mode: %s\n", recording ? "recording" : "playing");
      if (gpio == MODE_BUTTON_PIN) enableInput(gpio);
      break;
  }
}

int main() {
  stdio_init_all();
  
  // register front panel controls with callbacks
  enableInput(TRIG_BUTTON_PIN);
  enableInput(MODE_BUTTON_PIN);
  enableInput(TRIG_PULSE_PIN);
  enableInput(MODE_PULSE_PIN);

  // i2c setup for DAC
  i2c_init(I2C_PORT, 400 * 1e3);
  gpio_set_function(4, GPIO_FUNC_I2C);
  gpio_set_function(5, GPIO_FUNC_I2C);
  gpio_pull_up(4);
  gpio_pull_up(5);

  // ADC setup
  adc_init();
  // init GPIO pin for ADC: high impedance, disable all dig functions
  adc_gpio_init(26); // 26, 27, 28, or 29
  // select ADC input (matching what was init'd for GPIO)
  adc_select_input(0);

  // basic indicator LED
  gpio_init(LED_PIN);
  gpio_set_dir(LED_PIN, GPIO_OUT);
  ledOn(0, 0); // starts blinking to indicate record mode

  while (1);
}
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

  Trigger input:
    GPIO 16 (pin 21) -> N.O. switch to 3.3 V
  Mode input:
    GPIO 17 (pin 22) -> N.O. switch to 3.3 V
*/

// default address of MCP4725 DAC
static int addr = 0x62;
const uint LED_PIN = 25;
const uint TRIG_IN_PIN = 16;
const uint REC_PLAY_PIN = 17;
#define MEMORY_LENGTH 16
uint16_t memory[MEMORY_LENGTH] = {0};
static int memoryIndex = 0;
static bool recording = true;

static void mcp4725_write(uint value) {
  uint8_t data[] = {0x40, value / 16, (value % 16) << 4};
  i2c_write_blocking(I2C_PORT, addr, data, 3, false);
}

void onTrigger(uint);
void onEdge(uint, uint32_t);

// debouncing: edge calls this, checks if button held high, runs trig or resets interrupt
int64_t checkTrigger(alarm_id_t id, void* user_data) {
  uint gpio = (uint) user_data;
  if (gpio_get(gpio)) onTrigger(gpio);
  else 
    gpio_set_irq_enabled_with_callback(gpio, GPIO_IRQ_EDGE_RISE, true, &onEdge);
  return 0;
}

// debouncing: edge interrupt calls this, disables interrupt and checks if held high
void onEdge(uint gpio, uint32_t events) {
  gpio_set_irq_enabled(gpio, GPIO_IRQ_EDGE_RISE, false);
  add_alarm_in_ms(20, &checkTrigger, (void*) gpio, true);
}

// debouncing: this runs post-debounce check
void onTrigger(uint gpio) {
  switch (gpio) {
    case TRIG_IN_PIN:
      if (recording) memory[memoryIndex] = adc_read();
      else mcp4725_write(memory[memoryIndex]);
      printf("%02d: %d\n", memoryIndex, memory[memoryIndex]);
      memoryIndex = (memoryIndex + 1) % MEMORY_LENGTH;
      break;
    case REC_PLAY_PIN:
      recording = !recording;
      printf("Mode: %s\n", recording ? "recording" : "playing");
      break;
  }
  // turn interrupt back on
  gpio_set_irq_enabled_with_callback(gpio, GPIO_IRQ_EDGE_RISE, true, &onEdge);
}

int main() {
  stdio_init_all();
  
  // basic indicator LED
  gpio_init(LED_PIN);
  gpio_set_dir(LED_PIN, GPIO_OUT);
  gpio_put(LED_PIN, 1);

  // register front panel controls with callbacks
  gpio_set_irq_enabled_with_callback(TRIG_IN_PIN, GPIO_IRQ_EDGE_RISE, true, &onEdge);
  gpio_set_irq_enabled_with_callback(REC_PLAY_PIN, GPIO_IRQ_EDGE_RISE, true, &onEdge);

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

  while (1);
}
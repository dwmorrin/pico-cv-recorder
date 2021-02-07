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
    GPIO 16 (pin 21) -> N.O. switch (other side connected to 3.3 V)
      or other source of high logic voltage
*/

// default address of MCP4725 DAC
static int addr = 0x62;
const uint LED_PIN = 25;
const uint TRIG_IN_PIN = 16;

static void mcp4725_write(uint value) {
  uint8_t data[] = {0x40, value / 16, (value % 16) << 4};
  i2c_write_blocking(I2C_PORT, addr, data, 3, false);
}

void onTrigger(void);
void onEdge(uint, uint32_t);

// debouncing: edge calls this, checks if button held high, runs trig or resets interrupt
int64_t checkTrigger(alarm_id_t id, void* user_data) {
  if (gpio_get(TRIG_IN_PIN)) onTrigger();
  else 
    gpio_set_irq_enabled_with_callback(TRIG_IN_PIN, GPIO_IRQ_EDGE_RISE, true, &onEdge);
  return 0;
}

// debouncing: edge interrupt calls this, disables interrupt and checks if held high
void onEdge(uint gpio, uint32_t events) {
  gpio_set_irq_enabled(TRIG_IN_PIN, GPIO_IRQ_EDGE_RISE, false);
  add_alarm_in_ms(20, &checkTrigger, 0, true);
}

// debouncing: this runs post-debounce check
void onTrigger() {
  uint i = 1000;
  // 12-bit conversion, calibrate for V_max
  const float conversion_factor = 3.3f / (1 << 12);
  printf("writing to DAC: %d\n", i);
  mcp4725_write(i);
  uint16_t reading = adc_read();
  printf("reading from ADC: 0x%03x, voltage: %f V\n", reading, reading * conversion_factor);
  // turn interrupt back on
  gpio_set_irq_enabled_with_callback(TRIG_IN_PIN, GPIO_IRQ_EDGE_RISE, true, &onEdge);
}

int main() {
  stdio_init_all();
  
  // basic indicator LED
  gpio_init(LED_PIN);
  gpio_set_dir(LED_PIN, GPIO_OUT);
  gpio_put(LED_PIN, 1);

  // trigger input
  gpio_set_irq_enabled_with_callback(TRIG_IN_PIN, GPIO_IRQ_EDGE_RISE, true, &onEdge);

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
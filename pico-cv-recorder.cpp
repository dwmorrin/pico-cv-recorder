#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"

#define SIZE_OF_ARRAY(x) (sizeof(x) / sizeof(x[0]))

//**** QUANTIZE ****
// TODO: move this to a separate file

// this magic number depends on the DAC's voltage reference
// and any analog processing after the DAC
// this is for a 3.3V reference with a gain of 7/3.3 (about 2.12)
// to expand the 0-3.3V range to a 0-7V range.
#define MAGIC_NUMBER_SEMITONE 49

uint semitone_quantize(uint adc_value)
{
    // parentheses matter: we want integer division before multiplication
    return MAGIC_NUMBER_SEMITONE * (adc_value / MAGIC_NUMBER_SEMITONE);
}

#define ADC_MAX_VALUE 4095
#define NUMBER_OF_BUCKETS 12

// divide up the range of the ADC into NUMBER_OF_BUCKETS buckets
int adc_to_bucket(int adc_value)
{
    return adc_value / (ADC_MAX_VALUE / NUMBER_OF_BUCKETS);
}

int quantize_to_index_of_scale(int index, const int scale[], const int scale_size)
{
    int octave = index / scale_size;
    int i = index % scale_size;
    return octave * MAGIC_NUMBER_SEMITONE * 12 + scale[i] * MAGIC_NUMBER_SEMITONE;
}

#define quantize_scale(index, scale) quantize_to_index_of_scale(index, scale, SIZE_OF_ARRAY(scale))

const int scale_major[] = {0, 2, 4, 5, 7, 9, 11};
const int scale_pentatonic[] = {0, 2, 4, 7, 9};

//**** END QUANTIZE ****

/*
 **** HARDWARE NOTES ****

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
  Ext/Int trigger switch:
    GPIO 11 (pin 15)
  Tempo control: (using internal timer for playback)
    Potentiometer:
      CCW   -> 3.3 V (pin 36) (slowest tempo)
      WIPER -> GPIO 27 (pin 32) (ADC)
      CW    -> GND (pin 2) (fastest tempo)

  Voltage-to-be-sampled input:
    GPIO 26 (pin 31)

  Trigger output: (pulse high on playback)
    GPIO 15 (pin 20)

  optional potentiometer addressing:
    For 8 pot boards, 3 bits for pot address, plus a bit per board
    (no extra bit if just one board)
    GPIO 6, 7, 8 (pins 9, 10, 11) -> A0, A1, A2

  Quantize mode:
    Floors ADC value to 1V/Octave semitones (1/12 V ~= 0.08333)
    Quantize function depends on DAC power supply
    High = quantize.
    GPIO 10 (pin 14)

  Quantize range & scale:
    Range: full range or bucketed
      High = full range
      GPIO 0 (pin 1)
    Scale: select scale
      High = pentatonic
      GPIO 1 (pin 2)
*/
enum GPIO_PINS // hardware pin #
{
    QUANTIZE_RANGE_PIN = 0, // 1
    QUANTIZE_SCALE_PIN = 1, // 2
    DAC_SDA_PIN = 4,        // 6
    DAC_SCL_PIN = 5,        // 7
    POT_ADDR_0_PIN = 6,     // 9
    POT_ADDR_1_PIN = 7,     // 10
    POT_ADDR_2_PIN = 8,     // 11
    QUANTIZE_PIN = 10,      // 14
    EXT_TRIG_EN_PIN = 11,   // 15
    POT_INH_0 = 12,         // 16
    POT_INH_1 = 13,         // 17
    TRIG_OUT_PIN = 15,      // 20
    TRIG_BUTTON_PIN = 16,   // 21
    MODE_BUTTON_PIN = 17,   // 22
    TRIG_PULSE_PIN = 18,    // 24
    MODE_PULSE_PIN = 19,    // 25
    REC_LED_PIN = 20,       // 26
    LED_PIN = 25,           // built-in LED
    CV_IN_PIN = 26,         // 31
    TEMPO_IN_PIN = 27,      // 32
};

// tempo constants
const uint SLOW_MS = 3e3;          // seconds = 1/3 Hz = 20 bpm
const uint FAST_MS = 142;          // milliseconds = 7 Hz = 420 bpm
const uint TEMPO_READ_DELAY = 100; // milliseconds
static struct repeating_timer tempoTimer;

enum ADC_INPUTS
{
    ADC_IN_CV = 0,
    ADC_IN_TEMPO = 1,
};

// memory
#define MEMORY_LENGTH 16
uint16_t memory[MEMORY_LENGTH] = {0};
volatile int memoryIndex = 0;

// global state
volatile uint tempoDelayMs = 500; // milliseconds; tempo pot updates
volatile bool triggered = false;
volatile bool modeToggled = false;
volatile bool quantize = false;
volatile bool quantize_full_range = false;
volatile bool quantize_pentatonic = false;
volatile bool externalTrigger = false;
volatile bool recording = true;
volatile alarm_id_t internalClockAlarmId = 0;

#define I2C_PORT i2c0
#define DAC_ADDR 0x62

static void dac_write(uint16_t value)
{
    uint8_t data[] = {
        0x40,
        static_cast<uint8_t>(value / 16),
        static_cast<uint8_t>((value % 16) << 4)};
    i2c_write_blocking(I2C_PORT, DAC_ADDR, data, 3, false);
}

void onTrigger();
int64_t beatTrigger(alarm_id_t, void *);

void pot_address_setup()
{
    gpio_init(POT_ADDR_0_PIN);
    gpio_set_dir(POT_ADDR_0_PIN, GPIO_OUT);
    gpio_init(POT_ADDR_1_PIN);
    gpio_set_dir(POT_ADDR_1_PIN, GPIO_OUT);
    gpio_init(POT_ADDR_2_PIN);
    gpio_set_dir(POT_ADDR_2_PIN, GPIO_OUT);
    gpio_init(POT_INH_0);
    gpio_set_dir(POT_INH_0, GPIO_OUT);
    gpio_put(POT_INH_0, false);
    gpio_init(POT_INH_1);
    gpio_set_dir(POT_INH_1, GPIO_OUT);
    gpio_put(POT_INH_1, true);
}

void set_pot_address()
{
    gpio_put(POT_ADDR_0_PIN, memoryIndex & 0x01);
    gpio_put(POT_ADDR_1_PIN, memoryIndex & 0x02);
    gpio_put(POT_ADDR_2_PIN, memoryIndex & 0x04);
    // values 0-7 we want to inhibit first column
    // values 8-15 we want to inhibit second column
    bool inhibit0 = memoryIndex & 0x08;
    gpio_put(POT_INH_0, inhibit0);
    gpio_put(POT_INH_1, !inhibit0);
}

int64_t beatAnticipate(alarm_id_t id, void *user_data)
{
    gpio_put(LED_PIN, false);
    internalClockAlarmId = add_alarm_in_ms(tempoDelayMs, &beatTrigger, 0, true);
    return 0;
}

int64_t beatTrigger(alarm_id_t id, void *user_data)
{
    onTrigger();
    gpio_put(LED_PIN, true);
    if (!externalTrigger)
        internalClockAlarmId = add_alarm_in_ms(tempoDelayMs, &beatAnticipate, 0, true);
    return 0;
}

int64_t pinOff(alarm_id_t id, void *user_data)
{
    uint pin = (uint)user_data;
    gpio_put(pin, false);
    return 0;
}

void onPulse(uint);
void onEdge(uint, uint32_t);

void enableInput(uint pin)
{
    gpio_set_irq_enabled_with_callback(pin, GPIO_IRQ_EDGE_RISE, true, &onEdge);
}

void disableInput(uint pin)
{
    gpio_set_irq_enabled(pin, GPIO_IRQ_EDGE_RISE, false);
}

// debouncing: edge calls this, checks if button held high, runs trig or resets interrupt
int64_t checkTrigger(alarm_id_t id, void *user_data)
{
    uint gpio = (uint)user_data;
    if (gpio_get(gpio))
        onPulse(gpio);
    else
        enableInput(gpio);
    return 0;
}

// debouncing: immediately react for pulse inputs, debounce manually pressed buttons
void onEdge(uint gpio, uint32_t events)
{
    switch (gpio)
    {
    case TRIG_PULSE_PIN:
    case MODE_PULSE_PIN:
        // no debounce
        onPulse(gpio);
        break;
    case TRIG_BUTTON_PIN:
    case MODE_BUTTON_PIN:
        // software debounce
        disableInput(gpio);
        add_alarm_in_ms(20, &checkTrigger, (void *)gpio, true);
        break;
    }
}

// debouncing: this runs post-debounce check
void onPulse(uint gpio)
{
    switch (gpio)
    {
    case TRIG_BUTTON_PIN:
    case TRIG_PULSE_PIN:
        triggered = true;
        if (gpio == TRIG_BUTTON_PIN)
            enableInput(gpio);
        break;
    case MODE_BUTTON_PIN:
    case MODE_PULSE_PIN:
        modeToggled = true;
        if (gpio == MODE_BUTTON_PIN)
            enableInput(gpio);
        break;
    }
}

void resetInternalClock()
{
    if (internalClockAlarmId)
        cancel_alarm(internalClockAlarmId);
    gpio_put(LED_PIN, 0);
    if (!externalTrigger)
        internalClockAlarmId = add_alarm_in_ms(0, &beatTrigger, 0, true);
}

// update tempo potentiometer reading; probably not quite right yet with the reset
bool updateTempoDelay(repeating_timer_t *rt)
{
    adc_select_input(ADC_IN_TEMPO);
    uint16_t tempoRaw = 4095 - adc_read(); // reversed to correct wiring error in pot!
    // divide desired tempo by two; two delays per beat
    uint newTempoDelayMs = ((SLOW_MS - FAST_MS) * tempoRaw / 4096 + FAST_MS) / 2;
    if (newTempoDelayMs > tempoDelayMs + 20 || newTempoDelayMs < tempoDelayMs - 20)
    {
        tempoDelayMs = newTempoDelayMs;
    }
    return true;
}

// continuation of onTrigger after pot mux has had time to update
int64_t postAddressUpdate(alarm_id_t id, void *user_data)
{
    adc_select_input(ADC_IN_CV);
    uint16_t sample = adc_read();
    // write to memory if recording
    if (recording)
    {
        gpio_put(LED_PIN, true);
        if (externalTrigger)
            add_alarm_in_ms(20, &pinOff, (void *)LED_PIN, true);
        memory[memoryIndex] = sample;
    }
    // write to DAC
    if (quantize)
    {
        if (quantize_full_range)
            dac_write(semitone_quantize(memory[memoryIndex]));
        else
        {
            int bucket = adc_to_bucket(memory[memoryIndex]);
            int quantized_value;
            if (quantize_pentatonic)
                quantized_value = quantize_scale(bucket, scale_pentatonic);
            else
                quantized_value = quantize_scale(bucket, scale_major);
            dac_write(quantized_value);
        }
    }
    else
        dac_write(memory[memoryIndex]);
    // trigger output
    gpio_put(TRIG_OUT_PIN, true);
    add_alarm_in_ms(10, &pinOff, (void *)TRIG_OUT_PIN, true);
    // update state
    triggered = false;
    if (++memoryIndex == MEMORY_LENGTH)
        memoryIndex = 0;
    return 0;
}

void onTrigger()
{
    set_pot_address();
    add_alarm_in_us(1, &postAddressUpdate, 0, true);
}

void onRecPlayToggle()
{
    recording = !recording;
    gpio_put(REC_LED_PIN, recording);
    resetInternalClock();
    modeToggled = false;
}

int main()
{
    // register front panel controls with callbacks
    enableInput(TRIG_BUTTON_PIN);
    enableInput(MODE_BUTTON_PIN);
    enableInput(TRIG_PULSE_PIN);
    enableInput(MODE_PULSE_PIN);
    gpio_init(QUANTIZE_PIN);
    gpio_set_dir(QUANTIZE_PIN, GPIO_IN);
    gpio_init(QUANTIZE_RANGE_PIN);
    gpio_set_dir(QUANTIZE_RANGE_PIN, GPIO_IN);
    gpio_init(QUANTIZE_SCALE_PIN);
    gpio_set_dir(QUANTIZE_SCALE_PIN, GPIO_IN);
    gpio_init(EXT_TRIG_EN_PIN);
    gpio_set_dir(EXT_TRIG_EN_PIN, GPIO_IN);

    // pot board addressing
    pot_address_setup();

    // i2c setup for DAC
    i2c_init(I2C_PORT, 400e3);
    gpio_set_function(DAC_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(DAC_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(DAC_SDA_PIN);
    gpio_pull_up(DAC_SCL_PIN);

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

    gpio_init(REC_LED_PIN);
    gpio_set_dir(REC_LED_PIN, GPIO_OUT);
    gpio_put(REC_LED_PIN, true);

    add_repeating_timer_ms(TEMPO_READ_DELAY, &updateTempoDelay, 0, &tempoTimer);

    while (true)
    {
        // check for external trigger event
        if (triggered)
            onTrigger();
        // check for rec/play event
        if (modeToggled)
            onRecPlayToggle();
        // check state of hardware latched switches
        quantize = gpio_get(QUANTIZE_PIN);
        quantize_full_range = gpio_get(QUANTIZE_RANGE_PIN);
        quantize_pentatonic = gpio_get(QUANTIZE_SCALE_PIN);
        bool extTrigEn = gpio_get(EXT_TRIG_EN_PIN);
        if (extTrigEn != externalTrigger)
        {
            externalTrigger = extTrigEn;
            resetInternalClock();
        }
    }
}
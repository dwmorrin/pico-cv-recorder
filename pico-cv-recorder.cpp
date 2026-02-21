#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/timer.h"
#include "state.h"
#include "quantizer.h"

SequencerState state;

enum GPIO_PINS
{
    QUANTIZE_RANGE_PIN = 0,
    QUANTIZE_SCALE_PIN = 1,
    POT_MODE_PIN = 2,
    DAC_SDA_PIN = 4,
    DAC_SCL_PIN = 5,
    POT_ADDR_0_PIN = 6,
    POT_ADDR_1_PIN = 7,
    POT_ADDR_2_PIN = 8,
    QUANTIZE_PIN = 10,
    EXT_TRIG_EN_PIN = 11,
    POT_INH_0 = 12,
    POT_INH_1 = 13,
    TRIG_OUT_PIN = 15,
    TRIG_BUTTON_PIN = 16,
    MODE_BUTTON_PIN = 17,
    TRIG_PULSE_PIN = 18,
    MODE_PULSE_PIN = 19,
    REC_LED_PIN = 20,
    LED_PIN = 25,
    CV_IN_PIN = 26,
    TEMPO_IN_PIN = 27,
};

const uint SLOW_MS = 3e3;
const uint FAST_MS = 142;
const uint TEMPO_READ_DELAY = 100;
static struct repeating_timer tempoTimer;

enum ADC_INPUTS
{
    ADC_IN_CV = 0,
    ADC_IN_TEMPO = 1,
};

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
    gpio_put(POT_ADDR_0_PIN, state.memory_index & 0x01);
    gpio_put(POT_ADDR_1_PIN, state.memory_index & 0x02);
    gpio_put(POT_ADDR_2_PIN, state.memory_index & 0x04);
    bool inhibit0 = state.memory_index & 0x08;
    gpio_put(POT_INH_0, inhibit0);
    gpio_put(POT_INH_1, !inhibit0);
}

// --- FORWARD DECLARATIONS ---
void enableInput(uint pin);
void disableInput(uint pin);
int64_t beatTrigger(alarm_id_t, void *);

// --- ALARMS & TIMERS ---
int64_t pinOff(alarm_id_t id, void *user_data)
{
    uint pin = (uint)user_data;
    gpio_put(pin, false);
    return 0;
}

int64_t beatAnticipate(alarm_id_t id, void *user_data)
{
    gpio_put(LED_PIN, false);
    state.internal_clock_alarm = add_alarm_in_ms(state.tempo_delay_ms, &beatTrigger, 0, true);
    return 0;
}

int64_t beatTrigger(alarm_id_t id, void *user_data)
{
    state.trigger_pending = true; // Flag for the main loop
    gpio_put(LED_PIN, true);
    if (!state.external_trigger)
        state.internal_clock_alarm = add_alarm_in_ms(state.tempo_delay_ms, &beatAnticipate, 0, true);
    return 0;
}

void resetInternalClock()
{
    if (state.internal_clock_alarm)
        cancel_alarm(state.internal_clock_alarm);
    gpio_put(LED_PIN, 0);
    if (!state.external_trigger)
        state.internal_clock_alarm = add_alarm_in_ms(0, &beatTrigger, 0, true);
}

bool updateTempoDelay(repeating_timer_t *rt)
{
    adc_select_input(ADC_IN_TEMPO);
    uint16_t tempoRaw = 4095 - adc_read();
    uint newTempoDelayMs = ((SLOW_MS - FAST_MS) * tempoRaw / 4096 + FAST_MS) / 2;
    if (newTempoDelayMs > state.tempo_delay_ms + 20 || newTempoDelayMs < state.tempo_delay_ms - 20)
    {
        state.tempo_delay_ms = newTempoDelayMs;
    }
    return true;
}

// --- INTERRUPTS ---
void onPulse(uint gpio)
{
    switch (gpio)
    {
    case TRIG_BUTTON_PIN:
        // Hijack the button if we are in pot mode
        if (state.pot_mode)
        {
            state.range_toggle_pending = true;
        }
        else
        {
            state.trigger_pending = true;
        }
        enableInput(gpio);
        break;
    case TRIG_PULSE_PIN:
        state.trigger_pending = true;
        break;
    case MODE_BUTTON_PIN:
        state.mode_toggle_pending = true;
        enableInput(gpio);
        break;
    case MODE_PULSE_PIN:
        state.mode_toggle_pending = true;
        break;
    }
}

int64_t checkTrigger(alarm_id_t id, void *user_data)
{
    uint gpio = (uint)user_data;
    if (gpio_get(gpio))
        onPulse(gpio);
    else
        enableInput(gpio);
    return 0;
}

void onEdge(uint gpio, uint32_t events)
{
    switch (gpio)
    {
    case TRIG_PULSE_PIN:
    case MODE_PULSE_PIN:
        onPulse(gpio);
        break;
    case TRIG_BUTTON_PIN:
    case MODE_BUTTON_PIN:
        disableInput(gpio);
        add_alarm_in_ms(20, &checkTrigger, (void *)gpio, true);
        break;
    }
}

void enableInput(uint pin)
{
    gpio_set_irq_enabled_with_callback(pin, GPIO_IRQ_EDGE_RISE, true, &onEdge);
}

void disableInput(uint pin)
{
    gpio_set_irq_enabled(pin, GPIO_IRQ_EDGE_RISE, false);
}

// --- MAIN EXECUTION LOGIC ---
void processStep()
{
    // Set address and wait for CD4051 to settle
    set_pot_address();
    busy_wait_us(50);

    // Read ADC
    adc_select_input(ADC_IN_CV);
    uint16_t sample = adc_read();

    // Apply math if in Pot Mode
    if (state.pot_mode)
    {
        sample = scale_pot_value(sample, state.pot_range);
    }

    // Memory writing
    if (state.recording)
    {
        gpio_put(LED_PIN, true);
        if (state.external_trigger)
            add_alarm_in_ms(20, &pinOff, (void *)LED_PIN, true);
        state.memory[state.memory_index] = sample;
    }

    // Output Quantization
    uint16_t output_value = state.memory[state.memory_index];
    if (state.quantize_enabled)
    {
        if (state.quantize_full_range)
            output_value = semitone_quantize(output_value);
        else
        {
            output_value = state.quantize_pentatonic ? quantize_scale_pentatonic(output_value) : quantize_scale_major(output_value);
        }
    }

    // I2C Write
    dac_write(output_value);

    // Fire trigger output
    gpio_put(TRIG_OUT_PIN, true);
    add_alarm_in_ms(10, &pinOff, (void *)TRIG_OUT_PIN, true);

    // Advance Sequence
    if (++state.memory_index == MEMORY_LENGTH)
        state.memory_index = 0;
}

int main()
{
    // Register front panel controls with callbacks
    enableInput(TRIG_BUTTON_PIN);
    enableInput(MODE_BUTTON_PIN);
    disableInput(TRIG_PULSE_PIN);
    enableInput(MODE_PULSE_PIN);

    gpio_init(QUANTIZE_PIN);
    gpio_set_dir(QUANTIZE_PIN, GPIO_IN);
    gpio_init(QUANTIZE_RANGE_PIN);
    gpio_set_dir(QUANTIZE_RANGE_PIN, GPIO_IN);
    gpio_init(QUANTIZE_SCALE_PIN);
    gpio_set_dir(QUANTIZE_SCALE_PIN, GPIO_IN);
    gpio_init(EXT_TRIG_EN_PIN);
    gpio_set_dir(EXT_TRIG_EN_PIN, GPIO_IN);

    // Init new DPDT switch pin
    gpio_init(POT_MODE_PIN);
    gpio_set_dir(POT_MODE_PIN, GPIO_IN);
    gpio_pull_up(POT_MODE_PIN);

    // Pot board addressing
    pot_address_setup();

    // I2C setup for DAC
    i2c_init(I2C_PORT, 400e3);
    gpio_set_function(DAC_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(DAC_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(DAC_SDA_PIN);
    gpio_pull_up(DAC_SCL_PIN);

    // ADC setup
    adc_init();
    adc_gpio_init(CV_IN_PIN);
    adc_gpio_init(TEMPO_IN_PIN);

    // Trigger output
    gpio_init(TRIG_OUT_PIN);
    gpio_set_dir(TRIG_OUT_PIN, GPIO_OUT);
    gpio_put(TRIG_OUT_PIN, false);

    // LEDs
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    gpio_init(REC_LED_PIN);
    gpio_set_dir(REC_LED_PIN, GPIO_OUT);
    gpio_put(REC_LED_PIN, true);

    resetInternalClock();
    add_repeating_timer_ms(TEMPO_READ_DELAY, &updateTempoDelay, 0, &tempoTimer);

    while (true)
    {
        // Execute pending triggers
        if (state.trigger_pending)
        {
            state.trigger_pending = false;
            processStep();
        }

        // Handle Record/Play mode toggle
        if (state.mode_toggle_pending)
        {
            state.mode_toggle_pending = false;
            state.recording = !state.recording;
            gpio_put(REC_LED_PIN, state.recording);
            resetInternalClock();
        }

        // Handle hidden button range cycling (debug feature)
        if (state.range_toggle_pending)
        {
            state.range_toggle_pending = false;
            int next_range = (state.pot_range + 1) % RANGE_MAX;
            state.pot_range = static_cast<PotRange>(next_range);
        }

        // Continuously poll latched hardware switches
        state.quantize_enabled = gpio_get(QUANTIZE_PIN);
        state.quantize_full_range = gpio_get(QUANTIZE_RANGE_PIN);
        state.quantize_pentatonic = gpio_get(QUANTIZE_SCALE_PIN);

        // Assume pulling the DPDT switch low means we are in Pot Mode
        state.pot_mode = !gpio_get(POT_MODE_PIN);

        bool extTrigEn = gpio_get(EXT_TRIG_EN_PIN);
        if (extTrigEn != state.external_trigger)
        {
            state.external_trigger = extTrigEn;
            if (state.external_trigger)
                enableInput(TRIG_PULSE_PIN);
            else
                disableInput(TRIG_PULSE_PIN);
            resetInternalClock();
        }
    }
}
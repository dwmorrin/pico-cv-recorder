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
    QUANT_UP_PIN = 0,   // SP3T
    QUANT_DOWN_PIN = 1, // SP3T
    POT_MODE_PIN = 2,   // DPDT POT/CV switch
    DAC_SDA_PIN = 4,
    DAC_SCL_PIN = 5,
    POT_ADDR_0_PIN = 6,
    POT_ADDR_1_PIN = 7,
    POT_ADDR_2_PIN = 8,
    RANGE_DOWN_PIN = 10, // SP3T
    EXT_TRIG_EN_PIN = 11,
    POT_INH_0 = 12,
    POT_INH_1 = 13,
    RANGE_UP_PIN = 14, // SP3T
    TRIG_OUT_PIN = 15,
    TRIG_BUTTON_PIN = 16, // Internal hidden button
    MODE_BUTTON_PIN = 17, // REC/PLAY
    TRIG_PULSE_PIN = 18,
    MODE_PULSE_PIN = 19,
    LED_R_PIN = 20, // RGB LED
    LED_G_PIN = 21, // RGB LED
    LED_B_PIN = 22, // RGB LED
    LED_PIN = 25,   // Built-in
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
void enableDualEdgeInput(uint pin);
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
    state.trigger_pending = true;
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

// Helper function to keep onEdge clean
void handleModeButtonEdge(uint32_t events)
{
    if (events & GPIO_IRQ_EDGE_RISE)
    {
        state.mode_button_press_time = time_us_64();
    }
    else if (events & GPIO_IRQ_EDGE_FALL)
    {
        uint64_t duration = time_us_64() - state.mode_button_press_time;
        if (duration > 600000)
        { // 600ms long press
            state.scale_toggle_pending = true;
        }
        else if (duration > 20000)
        { // 20ms debounce for short press
            state.mode_toggle_pending = true;
        }
    }
}

void onPulse(uint gpio)
{
    switch (gpio)
    {
    case TRIG_BUTTON_PIN:
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
        disableInput(gpio);
        add_alarm_in_ms(20, &checkTrigger, (void *)gpio, true);
        break;
    case MODE_BUTTON_PIN:
        handleModeButtonEdge(events);
        break;
    }
}

void enableInput(uint pin)
{
    gpio_set_irq_enabled_with_callback(pin, GPIO_IRQ_EDGE_RISE, true, &onEdge);
}

void enableDualEdgeInput(uint pin)
{
    gpio_set_irq_enabled_with_callback(pin, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &onEdge);
}

void disableInput(uint pin)
{
    gpio_set_irq_enabled(pin, 0, false); // Clear both edge flags
}

// --- MAIN EXECUTION LOGIC ---
void processStep()
{
    // 1. Set address and allow analog frontend to settle
    set_pot_address();
    busy_wait_us(50); // Increased RC settling time

    // 2. Read ADC
    adc_select_input(ADC_IN_CV);
    uint16_t sample = adc_read();

    // 3. Apply math if in Pot Mode
    if (state.pot_mode)
    {
        sample = scale_pot_value(sample, state.pot_range);
    }

    // 4. Memory writing
    if (state.recording)
    {
        gpio_put(LED_PIN, true);
        if (state.external_trigger)
            add_alarm_in_ms(20, &pinOff, (void *)LED_PIN, true);
        state.memory[state.memory_index] = sample;
    }

    // 5. Output Quantization Route
    uint16_t output_value = state.memory[state.memory_index];

    if (state.quantize_mode == 1)
    {
        output_value = semitone_quantize(output_value);
    }
    else if (state.quantize_mode == 2)
    {
        switch (state.active_scale)
        {
        case SCALE_MAJOR:
            output_value = quantize_scale_major(output_value);
            break;
        case SCALE_PENTATONIC:
            output_value = quantize_scale_pentatonic(output_value);
            break;
        case SCALE_MINOR:
            output_value = quantize_scale_minor(output_value);
            break;
        default:
            break;
        }
    }

    // 6. I2C Write safely in main loop
    dac_write(output_value);

    // 7. Fire trigger output
    gpio_put(TRIG_OUT_PIN, true);
    add_alarm_in_ms(10, &pinOff, (void *)TRIG_OUT_PIN, true);

    // 8. Advance Sequence
    if (++state.memory_index == MEMORY_LENGTH)
        state.memory_index = 0;
}

int main()
{
    // Inputs
    enableInput(TRIG_BUTTON_PIN);
    disableInput(TRIG_PULSE_PIN);
    enableInput(MODE_PULSE_PIN);
    enableDualEdgeInput(MODE_BUTTON_PIN); // Dual-edge for long press

    // Setup toggle switch pins with pull-ups
    gpio_init(QUANT_UP_PIN);
    gpio_set_dir(QUANT_UP_PIN, GPIO_IN);
    gpio_pull_up(QUANT_UP_PIN);

    gpio_init(QUANT_DOWN_PIN);
    gpio_set_dir(QUANT_DOWN_PIN, GPIO_IN);
    gpio_pull_up(QUANT_DOWN_PIN);

    gpio_init(RANGE_UP_PIN);
    gpio_set_dir(RANGE_UP_PIN, GPIO_IN);
    gpio_pull_up(RANGE_UP_PIN);

    gpio_init(RANGE_DOWN_PIN);
    gpio_set_dir(RANGE_DOWN_PIN, GPIO_IN);
    gpio_pull_up(RANGE_DOWN_PIN);

    gpio_init(POT_MODE_PIN);
    gpio_set_dir(POT_MODE_PIN, GPIO_IN);
    gpio_pull_up(POT_MODE_PIN);

    gpio_init(EXT_TRIG_EN_PIN);
    gpio_set_dir(EXT_TRIG_EN_PIN, GPIO_IN);

    // Setup Pot board addressing
    pot_address_setup();

    // Setup I2C for DAC
    i2c_init(I2C_PORT, 400e3);
    gpio_set_function(DAC_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(DAC_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(DAC_SDA_PIN);
    gpio_pull_up(DAC_SCL_PIN);

    // Setup ADC
    adc_init();
    adc_gpio_init(CV_IN_PIN);
    adc_gpio_init(TEMPO_IN_PIN);

    // Setup Outputs
    gpio_init(TRIG_OUT_PIN);
    gpio_set_dir(TRIG_OUT_PIN, GPIO_OUT);
    gpio_put(TRIG_OUT_PIN, false);

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    // Setup RGB LED, active LOW
    gpio_init(LED_R_PIN);
    gpio_set_dir(LED_R_PIN, GPIO_OUT);
    gpio_put(LED_R_PIN, true);
    gpio_init(LED_G_PIN);
    gpio_set_dir(LED_G_PIN, GPIO_OUT);
    gpio_put(LED_G_PIN, true);
    gpio_init(LED_B_PIN);
    gpio_set_dir(LED_B_PIN, GPIO_OUT);
    gpio_put(LED_B_PIN, true);

    resetInternalClock();
    add_repeating_timer_ms(TEMPO_READ_DELAY, &updateTempoDelay, 0, &tempoTimer);

    while (true)
    {
        // 1. Execute pending triggers
        if (state.trigger_pending)
        {
            state.trigger_pending = false;
            processStep();
        }

        // 2. Handle Record/Play mode toggle (Short Press)
        if (state.mode_toggle_pending)
        {
            state.mode_toggle_pending = false;
            state.recording = !state.recording;
            resetInternalClock();
        }

        // 3. Handle Scale Cycling (Long Press)
        if (state.scale_toggle_pending)
        {
            state.scale_toggle_pending = false;
            int next_scale = (state.active_scale + 1) % SCALE_MAX;
            state.active_scale = static_cast<MusicalScale>(next_scale);
        }

        // 4. Update RGB LED Status
        if (state.recording)
        {
            gpio_put(LED_R_PIN, state.active_scale != SCALE_MAJOR);
            gpio_put(LED_B_PIN, state.active_scale != SCALE_PENTATONIC);
            gpio_put(LED_G_PIN, state.active_scale != SCALE_MINOR);
        }
        else
        {
            gpio_put(LED_R_PIN, true);
            gpio_put(LED_G_PIN, true);
            gpio_put(LED_B_PIN, true);
        }

        // 5. Handle hidden button range cycling (debug feature fallback)
        if (state.range_toggle_pending)
        {
            state.range_toggle_pending = false;
            int next_range = (state.pot_range + 1) % RANGE_MAX;
            state.pot_range = static_cast<PotRange>(next_range);
        }

        // 6. Poll SP3T QUANT Switch
        bool quant_up = !gpio_get(QUANT_UP_PIN);
        bool quant_down = !gpio_get(QUANT_DOWN_PIN);

        if (quant_up)
            state.quantize_mode = 2; // Snap to Scale
        else if (quant_down)
            state.quantize_mode = 0; // Unquantized
        else
            state.quantize_mode = 1; // Chromatic

        // 7. Poll SP3T RANGE Switch
        bool range_up = !gpio_get(RANGE_UP_PIN);
        bool range_down = !gpio_get(RANGE_DOWN_PIN);

        if (range_up)
            state.pot_range = RANGE_5_OCTAVES;
        else if (range_down)
            state.pot_range = RANGE_1_OCTAVE;
        else
            state.pot_range = RANGE_2_OCTAVES;

        // 8. Poll DPDT Pot/CV Switch
        state.pot_mode = !gpio_get(POT_MODE_PIN);

        // 9. Poll External Trigger Enable Switch
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
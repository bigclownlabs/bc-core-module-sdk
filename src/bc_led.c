#include <bc_led.h>

#define BC_LED_DEFAULT_SLOT_INTERVAL 100

static bc_tick_t _bc_led_task(void *param);

static bc_tick_t _bc_led_task_pulse(void *param);

void bc_led_init(bc_led_t *self, bc_gpio_channel_t gpio_channel, bool open_drain_output, bool idle_state)
{
    memset(self, 0, sizeof(*self));

    self->_gpio_channel = gpio_channel;

    self->_open_drain_output = open_drain_output;

    self->_idle_state = idle_state;

    bc_gpio_init(self->_gpio_channel);

    bc_gpio_set_output(self->_gpio_channel, self->_idle_state);

    if (self->_open_drain_output)
    {
        bc_gpio_set_mode(self->_gpio_channel, BC_GPIO_MODE_OUTPUT_OD);
    }
    else
    {
        bc_gpio_set_mode(self->_gpio_channel, BC_GPIO_MODE_OUTPUT);
    }

    self->_slot_interval = BC_LED_DEFAULT_SLOT_INTERVAL;

    bc_scheduler_register(_bc_led_task, self, self->_slot_interval);
}

void bc_led_set_slot_interval(bc_led_t *self, bc_tick_t interval)
{
    self->_slot_interval = interval;
}

void bc_led_set_mode(bc_led_t *self, bc_led_mode_t mode)
{
    uint32_t pattern = 0;

    switch (mode)
    {
        case BC_LED_MODE_OFF:
        {
            break;
        }
        case BC_LED_MODE_ON:
        {
            pattern = 0xffffffff;
            break;
        }
        case BC_LED_MODE_BLINK:
        {
            pattern = 0xf0f0f0f0;
            break;
        }
        case BC_LED_MODE_BLINK_SLOW:
        {
            pattern = 0xffff0000;
            break;
        }
        case BC_LED_MODE_BLINK_FAST:
        {
            pattern = 0xaaaaaaaa;
            break;
        }
        case BC_LED_MODE_FLASH:
        {
            pattern = 0x80000000;
            break;
        }
        default:
        {
            break;
        }
    }

    if (self->_pattern != pattern)
    {
        self->_pattern = pattern;

        self->_selector = 0;
    }
}

void bc_led_set_pattern(bc_led_t *self, uint32_t pattern)
{
    if (self->_pattern != pattern)
    {
        self->_pattern = pattern;

        self->_selector = 0;
    }
}

void bc_led_pulse(bc_led_t *self, bc_tick_t duration)
{
    if (self->_pulse_active)
    {
        bc_scheduler_plan(self->_pulse_task_id, bc_tick_get() + duration);

        return;
    }

    bc_gpio_set_output(self->_gpio_channel, self->_idle_state ? false : true);

    self->_pulse_active = true;

    self->_pulse_task_id = bc_scheduler_register(_bc_led_task_pulse, self, bc_tick_get() + duration);
}

static bc_tick_t _bc_led_task(void *param)
{
    bc_led_t *self = param;

    if (self->_pulse_active)
    {
        return BC_LED_DEFAULT_SLOT_INTERVAL;
    }

    if (self->_selector == 0)
    {
        self->_selector = 0x80000000;
    }

    if ((self->_pattern & self->_selector) != 0)
    {
        bc_gpio_set_output(self->_gpio_channel, self->_idle_state ? false : true);
    }
    else
    {
        bc_gpio_set_output(self->_gpio_channel, self->_idle_state ? true : false);
    }

    self->_selector >>= 1;

    return self->_slot_interval;
}

static bc_tick_t _bc_led_task_pulse(void *param)
{
    bc_led_t *self = param;

    bc_gpio_set_output(self->_gpio_channel, self->_idle_state ? true : false);

    self->_pulse_active = false;

    bc_scheduler_unregister(self->_pulse_task_id);

    return BC_TICK_INFINITY;
}

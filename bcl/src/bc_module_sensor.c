#include <bc_module_sensor.h>
#include <bc_tca9534a.h>
#include <bc_button.h>

#define _BC_MODULE_SENSOR_CH_A_VDD   (1 << 4)
#define _BC_MODULE_SENSOR_CH_A_EN    (1 << 5)
#define _BC_MODULE_SENSOR_CH_B_EN    (1 << 6)
#define _BC_MODULE_SENSOR_CH_B_VDD   (1 << 7)

static struct
{
    bc_tca9534a_t tca9534a;
    uint8_t direction;
    bc_button_t button_port_a;
    bc_button_t button_port_b;

} _bc_module_sensor;


bool bc_module_sensor_init(void)
{
    if (!bc_tca9534a_init(&_bc_module_sensor.tca9534a, BC_I2C_I2C0, 0x3e))
    {
        return false;
    }

    if (!bc_tca9534a_write_port(&_bc_module_sensor.tca9534a, 0x00))
    {
        return false;
    }

    _bc_module_sensor.direction = 0xff;

    if (!bc_tca9534a_set_port_direction(&_bc_module_sensor.tca9534a, _bc_module_sensor.direction))
    {
        return false;
    }

    return true;
}

bool bc_module_sensor_set_button_input_mode(bc_module_channel_t channel, void (*event_handler)(bc_button_t *, bc_button_event_t, void *))
{
    switch(channel)
    {
        case BC_MODULE_SENSOR_CHANNEL_A:
            bc_module_sensor_set_pull(BC_MODULE_SENSOR_CHANNEL_A, BC_MODULE_PULL_4K7);
            bc_button_init(&_bc_module_sensor.button_port_a, BC_GPIO_P4, BC_GPIO_PULL_UP, true);
            bc_button_set_event_handler(&_bc_module_sensor.button_port_a, event_handler, NULL);
            return true;
        case BC_MODULE_SENSOR_CHANNEL_B:
            bc_module_sensor_set_pull(BC_MODULE_SENSOR_CHANNEL_B, BC_MODULE_PULL_4K7);
            bc_button_init(&_bc_module_sensor.button_port_b, BC_GPIO_P5, BC_GPIO_PULL_UP, true);
            bc_button_set_event_handler(&_bc_module_sensor.button_port_b, event_handler, NULL);
            return true;
        default:
            return false;
    }
}

bool bc_module_sensor_set_digital_output_mode(bc_module_channel_t channel)
{
    switch(channel)
    {
        case BC_MODULE_SENSOR_CHANNEL_A:
            bc_gpio_init(BC_GPIO_P4);
            bc_gpio_set_mode(BC_GPIO_P4, BC_GPIO_MODE_OUTPUT);
            return true;
        case BC_MODULE_SENSOR_CHANNEL_B:
            bc_gpio_init(BC_GPIO_P5);
            bc_gpio_set_mode(BC_GPIO_P5, BC_GPIO_MODE_OUTPUT);
            return true;
        default:
            return false;
    }
}

bool bc_module_sensor_set_pull(bc_module_channel_t channel, bc_module_pull_t pull)
{
    if (channel == BC_MODULE_SENSOR_CHANNEL_A)
    {
        _bc_module_sensor.direction |= _BC_MODULE_SENSOR_CH_A_VDD | _BC_MODULE_SENSOR_CH_A_EN;

        if (pull == BC_MODULE_PULL_4K7)
        {
            _bc_module_sensor.direction &= ~_BC_MODULE_SENSOR_CH_A_EN;
        }
        else if (pull == BC_MODULE_PULL_56)
        {
            _bc_module_sensor.direction &= ~_BC_MODULE_SENSOR_CH_A_VDD;
        }
    }
    else
    {
        _bc_module_sensor.direction |= _BC_MODULE_SENSOR_CH_B_EN | _BC_MODULE_SENSOR_CH_B_VDD;

        if (pull == BC_MODULE_PULL_4K7)
        {
            _bc_module_sensor.direction &= ~_BC_MODULE_SENSOR_CH_B_EN;
        }
        else if (pull == BC_MODULE_PULL_56)
        {
            _bc_module_sensor.direction &= ~_BC_MODULE_SENSOR_CH_B_VDD;
        }
    }

    return bc_tca9534a_set_port_direction(&_bc_module_sensor.tca9534a, _bc_module_sensor.direction);
}


void bc_module_sensor_set_digital_output(bc_module_channel_t channel, bool value) {
    bc_gpio_channel_t output_channel;
    switch(channel)
    {
        case BC_MODULE_SENSOR_CHANNEL_A:
            output_channel = BC_GPIO_P4;
        case BC_MODULE_SENSOR_CHANNEL_B:
            output_channel = BC_GPIO_P5;
        default:
            output_channel = BC_GPIO_P4;
    }
    bc_gpio_set_output(output_channel, value ? 1 : 0);
}


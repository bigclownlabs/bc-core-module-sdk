#include <bc_module_power.h>
#include <bc_scheduler.h>
#include <bc_module_relay.h>
#include <bc_scheduler.h>

static bc_tick_t _bc_module_relay_task(void *param);

#define BC_MODULE_RELAY_I2C_ADDRESS_DEFAULT 0x3B
#define BC_MODULE_RELAY_I2C_ADDRESS_ALTERNATE 0x3F

#define BC_MODULE_RELAY_POLARITY_T      ((1 << 6) | (1 << 7))
#define BC_MODULE_RELAY_POLARITY_F      ((1 << 4) | (1 << 5))
#define BC_MODULE_RELAY_POLARITY_NONE   ((1 << 6) | (1 << 4))


static bool _bc_module_relay_hardware_init(bc_module_relay_t *self)
{
    // Init i2C expander driver
    if (!bc_tca9534a_init(&self->_tca9534a, BC_I2C_I2C0, BC_MODULE_RELAY_I2C_ADDRESS_DEFAULT))
    {
        return false;
    }
    // De-energize bistable relay coil - turn off
    bc_tca9534a_write_port(&(self->_tca9534a), BC_MODULE_RELAY_POLARITY_NONE);
    // Enable outputs
    bc_tca9534a_set_port_direction(&self->_tca9534a, 0x00); // inverted: 0 = output
    // Relay is bi-stable, in the begining we don't know the default state
    self->_relay_state = BC_MODULE_RELAY_STATE_UNKNOWN;

    return true;
}

bool bc_module_relay_init(bc_module_relay_t *self)
{
    // Init instance, set state machine initial state
    memset(self, 0, sizeof(*self));
    return _bc_module_relay_hardware_init(self);
}


static void bc_module_relay_scheduler_unregister(bc_module_relay_t *self)
{
    // Check if there is already a task running
    if (self->_task_is_active)
    {
        // Unregister running task
        bc_scheduler_unregister(self->_task_id);
        // Clear task id
        self->_task_id = 0;
        self->_task_is_active = false;
    }
}

static void bc_module_relay_scheduler_register(bc_module_relay_t *self)
{
    // Exit if there's already registered task
    if(self->_task_is_active)
        return;

    // Register relay task
    self->_task_id = bc_scheduler_register(_bc_module_relay_task, self, 0);
    self->_task_is_active = true;
}

static void _bc_module_relay_set_output(bc_module_relay_t *self, bc_module_relay_state_t state)
{
    if (state == BC_MODULE_RELAY_STATE_TRUE)
    {
        if (!bc_tca9534a_write_port(&self->_tca9534a, BC_MODULE_RELAY_POLARITY_T)) // pol A
        {
            self->_error = true;
        }
    }
    else
    {
        if (!bc_tca9534a_write_port(&self->_tca9534a, BC_MODULE_RELAY_POLARITY_F)) // pol B
        {
            self->_error = true;
        }
    }
}

static void _bc_module_relay_set_output_disable(bc_module_relay_t *self)
{
    if (!bc_tca9534a_write_port(&(self->_tca9534a), BC_MODULE_RELAY_POLARITY_NONE))
    {
        self->_error = true;
    }
}

static bc_tick_t bc_module_relay_state_machine(bc_module_relay_t *self)
{
    while(true)
    {
        switch (self->_state)
        {

            case BC_MODULE_RELAY_TASK_STATE_IDLE:
                // Handle Error
                if(self->_error)
                {
                    // Try to initialize relay module again
                    if (_bc_module_relay_hardware_init(self))
                    {
                        self->_error = false;
                    }
                }

                // Handle commands
                if (self->_command == BC_MODULE_RELAY_COMMAND_SET)
                {
                    self->_command = BC_MODULE_RELAY_COMMAND_NONE;
                    self->_state = BC_MODULE_RELAY_TASK_STATE_SET;
                    continue;
                }

                if (self->_command == BC_MODULE_RELAY_COMMAND_PULSE)
                {
                    self->_command = BC_MODULE_RELAY_COMMAND_NONE;
                    self->_state = BC_MODULE_RELAY_TASK_STATE_PULSE;
                    continue;
                }

                // Unregister task if no command is needed
                bc_module_relay_scheduler_unregister(self);
                return 0;
                break;


            //
            // Relay set start state
            //
            case BC_MODULE_RELAY_TASK_STATE_SET:
                // Set relay to the selected polarity
                _bc_module_relay_set_output(self, self->_desired_state);
                self->_relay_state = self->_desired_state;

                self->_state = BC_MODULE_RELAY_TASK_STATE_SET_DEMAGNETIZE;
                return 20;
                break;

            case BC_MODULE_RELAY_TASK_STATE_SET_DEMAGNETIZE:
                // De-energize bistable relay coil - turn off
                _bc_module_relay_set_output_disable(self);

                self->_state = BC_MODULE_RELAY_TASK_STATE_IDLE;
                // Needs 100ms to let the capacitor on relay board to charge
                return 100;
                break;


            //
            // Relay pulse start state
            //
            case BC_MODULE_RELAY_TASK_STATE_PULSE:
                // Create pulse of the set polarity
                _bc_module_relay_set_output(self, self->_desired_state);
                self->_relay_state = self->_desired_state;

                self->_state = BC_MODULE_RELAY_TASK_STATE_PULSE_DEMAGNETIZE;
                return 20;
                break;

            case BC_MODULE_RELAY_TASK_STATE_PULSE_DEMAGNETIZE:
                // De-energize bistable relay coil - turn off
                _bc_module_relay_set_output_disable(self);

                self->_state = BC_MODULE_RELAY_TASK_STATE_PULSE_REVERSE;
                return self->_pulse_duration;
                break;

            case BC_MODULE_RELAY_TASK_STATE_PULSE_REVERSE:
                // Change actual relay state to the oposite polarity
                self->_relay_state = (self->_relay_state == BC_MODULE_RELAY_STATE_TRUE) ? BC_MODULE_RELAY_STATE_FALSE : BC_MODULE_RELAY_STATE_TRUE;
                _bc_module_relay_set_output(self, self->_relay_state);

                self->_state = BC_MODULE_RELAY_TASK_STATE_PULSE_DEMAGNETIZE_2;
                return 20;
                break;

            case BC_MODULE_RELAY_TASK_STATE_PULSE_DEMAGNETIZE_2:
                // De-energize bistable relay coil - turn off
                _bc_module_relay_set_output_disable(self);

                self->_state = BC_MODULE_RELAY_TASK_STATE_IDLE;
                // Needs 100ms to let the capacitor on relay board to charge
                return 100;
                break;

            default:
                break;
        }
    }
}

static bc_tick_t _bc_module_relay_task(void *param)
{
    bc_module_relay_t *self = param;
    return bc_module_relay_state_machine(self);
}

void bc_module_relay_set_state(bc_module_relay_t *self, bool state)
{
    // Save set command
    self->_command = BC_MODULE_RELAY_COMMAND_SET;
    self->_desired_state = (state) ? BC_MODULE_RELAY_STATE_TRUE : BC_MODULE_RELAY_STATE_FALSE;

    bc_module_relay_scheduler_register(self);
}

void bc_module_relay_toggle(bc_module_relay_t *self)
{
    if (self->_relay_state == BC_MODULE_RELAY_STATE_FALSE)
    {
        bc_module_relay_set_state(self, BC_MODULE_RELAY_STATE_TRUE);
    }
    else if (self->_relay_state == BC_MODULE_RELAY_STATE_TRUE)
    {
        bc_module_relay_set_state(self, BC_MODULE_RELAY_STATE_FALSE);
    }
}

void bc_module_relay_pulse(bc_module_relay_t *self, bool state, bc_tick_t duration)
{
    // Save pulse duration
    self->_command = BC_MODULE_RELAY_COMMAND_PULSE;
    self->_pulse_duration = duration;
    self->_desired_state = (state) ? BC_MODULE_RELAY_STATE_TRUE : BC_MODULE_RELAY_STATE_FALSE;

    bc_module_relay_scheduler_register(self);
}

bc_module_relay_state_t bc_module_relay_get_state(bc_module_relay_t *self)
{
    return self->_relay_state;
}

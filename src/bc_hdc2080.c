#include <bc_hdc2080.h>
#include <bc_scheduler.h>

// TODO Clarify timing with TI
#define BC_HDC2080_DELAY_RUN 50
#define BC_HDC2080_DELAY_INITIALIZATION 50
#define BC_HDC2080_DELAY_MEASUREMENT 50

static bc_tick_t _bc_hdc2080_task(void *param, bc_tick_t tick_now);

void bc_hdc2080_init(bc_hdc2080_t *self, bc_i2c_channel_t i2c_channel, uint8_t i2c_address)
{
    memset(self, 0, sizeof(*self));

    self->_i2c_channel = i2c_channel;
    self->_i2c_address = i2c_address;

    bc_scheduler_register(_bc_hdc2080_task, self, BC_HDC2080_DELAY_RUN);
}

void bc_hdc2080_set_event_handler(bc_hdc2080_t *self, void (*event_handler)(bc_hdc2080_t *, bc_hdc2080_event_t))
{
    self->_event_handler = event_handler;
}

void bc_hdc2080_set_update_interval(bc_hdc2080_t *self, bc_tick_t interval)
{
    self->_update_interval = interval;
}

bool bc_hdc2080_get_humidity_raw(bc_hdc2080_t *self, uint16_t *raw)
{
    if (!self->_humidity_valid)
    {
        return false;
    }

    *raw = self->_reg_humidity;

    return true;
}

bool bc_hdc2080_get_humidity_percentage(bc_hdc2080_t *self, float *percentage)
{
    uint16_t raw;

    if (!bc_hdc2080_get_humidity_raw(self, &raw))
    {
        return false;
    }

    *percentage = (float) raw / 65536.f * 100.f;

    if (*percentage >= 100.f)
    {
        *percentage = 100.f;
    }

    return true;
}

static bc_tick_t _bc_hdc2080_task(void *param, bc_tick_t tick_now)
{
    bc_hdc2080_t *self = param;

start:

    switch (self->_state)
    {
        case BC_HDC2080_STATE_ERROR:
        {
            self->_humidity_valid = false;

            if (self->_event_handler != NULL)
            {
                self->_event_handler(self, BC_HDC2080_EVENT_ERROR);
            }

            self->_state = BC_HDC2080_STATE_INITIALIZE;

            return tick_now + self->_update_interval;
        }
        case BC_HDC2080_STATE_INITIALIZE:
        {
            self->_state = BC_HDC2080_STATE_ERROR;

            if (!bc_i2c_write_8b(self->_i2c_channel, self->_i2c_address, 0x0e, 0x80))
            {
                goto start;
            }

            self->_state = BC_HDC2080_STATE_MEASURE;

            return tick_now + BC_HDC2080_DELAY_INITIALIZATION;
        }
        case BC_HDC2080_STATE_MEASURE:
        {
            self->_state = BC_HDC2080_STATE_ERROR;

            if (!bc_i2c_write_8b(self->_i2c_channel, self->_i2c_address, 0x0f, 0x05))
            {
                goto start;
            }

            self->_state = BC_HDC2080_STATE_READ;

            return tick_now + BC_HDC2080_DELAY_MEASUREMENT;
        }
        case BC_HDC2080_STATE_READ:
        {
            self->_state = BC_HDC2080_STATE_ERROR;

            uint8_t reg_interrupt;

            if (!bc_i2c_read_8b(self->_i2c_channel, self->_i2c_address, 0x04, &reg_interrupt))
            {
                goto start;
            }

            if ((reg_interrupt & 0x80) == 0)
            {
                goto start;
            }

            if (!bc_i2c_read_16b(self->_i2c_channel, self->_i2c_address, 0x02, &self->_reg_humidity))
            {
                goto start;
            }

            self->_reg_humidity = self->_reg_humidity << 8 | self->_reg_humidity >> 8;

            self->_humidity_valid = true;

            self->_state = BC_HDC2080_STATE_UPDATE;

            goto start;
        }
        case BC_HDC2080_STATE_UPDATE:
        {
            if (self->_event_handler != NULL)
            {
                self->_event_handler(self, BC_HDC2080_EVENT_UPDATE);
            }

            self->_state = BC_HDC2080_STATE_MEASURE;

            return tick_now + self->_update_interval;
        }
        default:
        {
            self->_state = BC_HDC2080_STATE_ERROR;

            goto start;
        }
    }
}

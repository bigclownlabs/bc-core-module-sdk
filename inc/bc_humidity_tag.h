#ifndef _BC_HUMIDITY_TAG_H
#define _BC_HUMIDITY_TAG_H

#include <stddef.h>
#include <bc_hdc2080.h>
#include <bc_hts221.h>
#include <bc_common.h>
#include <bc_tick.h>

typedef enum
{
    BC_HUMIDITY_TAG_DEVICE_HTS221 = 0,
    BC_HUMIDITY_TAG_DEVICE_HDC2080,
    BC_HUMIDITY_TAG_DEVICE_COUNT

} bc_humidity_tag_device_t;

typedef enum
{
    BC_HUMIDITY_TAG_I2C_ADDRESS_DEFAULT = 0,
    BC_HUMIDITY_TAG_I2C_ADDRESS_ALTERNATE,
    BC_HUMIDITY_TAG_I2C_ADDRESS_COUNT

} bc_humidity_tag_i2c_address_t;

/*
 typedef bc_hdc2080_event_t bc_humidity_tag_event_t;
 typedef bc_hdc2080_t bc_humidity_tag_t;
 */

typedef struct
{
    union
    {
        bc_hts221_t hts221;
        bc_hdc2080_t hdc2080;
    } sensor;
    bc_humidity_tag_device_t device_type;
} bc_humidity_tag_t;

typedef union
{
    bc_hts221_event_t hts221_event;
    bc_hdc2080_event_t hdc2080_event;
} bc_humidity_tag_event_t;

// TODO ... doporu�uju nepou��vat v HAL_I2C_Mem_Read(.....,0xFFFFFFFF),
//              p�i velice �patn�m na�asov�n� vyta�en� tagu se tam program zasekne forever

void bc_humidity_tag_init(bc_humidity_tag_t *self, uint8_t i2c_channel, bc_humidity_tag_i2c_address_t i2c_address);
void bc_humidity_tag_set_event_handler(bc_humidity_tag_t *self, void (*event_handler)(bc_humidity_tag_t *, bc_humidity_tag_event_t));
void bc_humidity_tag_set_update_interval(bc_humidity_tag_t *self, bc_tick_t interval);
bool bc_humidity_tag_get_humidity_raw(bc_humidity_tag_t *self, uint16_t *raw);
bool bc_humidity_tag_get_humidity_percentage(bc_humidity_tag_t *self, float *percentage);

#endif // _BC_HUMIDITY_TAG_H

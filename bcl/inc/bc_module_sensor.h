#ifndef _BC_MODULE_SENSOR_H
#define _BC_MODULE_SENSOR_H

#include <bc_gpio.h>

//! @addtogroup bc_module_sensor bc_module_sensor
//! @brief Driver for Sensor Module
//! @{

//! @brief Sensor module channels

typedef enum
{
    //! @brief Channel A
    BC_MODULE_SENSOR_CHANNEL_A = 0,

    //! @brief Channel B
    BC_MODULE_SENSOR_CHANNEL_B = 1

} bc_module_channel_t;


//! @brief Sensor module pull

typedef enum
{
    BC_MODULE_PULL_NONE = 0,
    BC_MODULE_PULL_4K7 = 1,
    BC_MODULE_PULL_56 = 2,

} bc_module_pull_t;

//! @brief Initialize Sensor Module
//! @return true On success
//! @return false On Error

bool bc_module_sensor_init(void);

//! @brief Set mode of operation for Sensor module channel
//! @param[in] channel Sensor module channel
//! @param[in] mode Sensor module pull
//! @return true On success
//! @return false On Error

bool bc_module_sensor_set_pull(bc_module_channel_t channel, bc_module_pull_t pull);

//! @}

#endif /* _BC_MODULE_SENSOR_H */

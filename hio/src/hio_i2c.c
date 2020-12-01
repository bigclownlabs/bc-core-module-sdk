#include <hio_i2c.h>
#include <hio_tick.h>
#include <stm32l0xx.h>
#include <hio_scheduler.h>
#include <hio_ds28e17.h>
#include <hio_module_sensor.h>
#include <hio_module_x1.h>
#include <hio_onewire.h>
#include <hio_system.h>

#define _HIO_I2C_TX_TIMEOUT_ADJUST_FACTOR 1.5
#define _HIO_I2C_RX_TIMEOUT_ADJUST_FACTOR 1.5

#define _HIO_I2C_MEMORY_ADDRESS_SIZE_8BIT    1
#define _HIO_I2C_MEMORY_ADDRESS_SIZE_16BIT   2
#define _HIO_I2C_RELOAD_MODE                I2C_CR2_RELOAD
#define _HIO_I2C_AUTOEND_MODE               I2C_CR2_AUTOEND
#define _HIO_I2C_SOFTEND_MODE               (0x00000000U)
#define _HIO_I2C_NO_STARTSTOP               (0x00000000U)
#define _HIO_I2C_GENERATE_START_WRITE       I2C_CR2_START
#define _HIO_I2C_BYTE_TRANSFER_TIME_US_100     80
#define _HIO_I2C_BYTE_TRANSFER_TIME_US_400     20

#define __HIO_I2C_RESET_PERIPHERAL(__I2C__) {__I2C__->CR1 &= ~I2C_CR1_PE; __I2C__->CR1 |= I2C_CR1_PE; }

static struct
{
    int initialized_semaphore;
    hio_i2c_speed_t speed;
    I2C_TypeDef *i2c;

} _hio_i2c[] = {
    [HIO_I2C_I2C0] = { .initialized_semaphore = 0, .i2c = I2C2 },
    [HIO_I2C_I2C1] = { .initialized_semaphore = 0, .i2c = I2C1 },
    [HIO_I2C_I2C_1W]= { .initialized_semaphore = 0, .i2c = NULL }
};

static hio_tick_t tick_timeout;
static hio_ds28e17_t ds28e17;

static bool _hio_i2c_mem_write(I2C_TypeDef *i2c, uint8_t device_address, uint16_t memory_address, uint16_t memory_address_length, uint8_t *buffer, uint16_t length);
static bool _hio_i2c_mem_read(I2C_TypeDef *i2c, uint8_t device_address, uint16_t memory_address, uint16_t memory_address_length, uint8_t *buffer, uint16_t length);
static bool _hio_i2c_req_mem_write(I2C_TypeDef *i2c, uint8_t device_address, uint16_t memory_address, uint16_t memory_address_length);
static bool _hio_i2c_req_mem_read(I2C_TypeDef *i2c, uint8_t device_address, uint16_t memory_address, uint16_t memory_address_length);
static void _hio_i2c_config(I2C_TypeDef *i2c, uint8_t device_address, uint8_t length, uint32_t mode, uint32_t Request);
static bool _hio_i2c_watch_flag(I2C_TypeDef *i2c, uint32_t flag, FlagStatus status);
static bool _hio_i2c_is_ack_failure(I2C_TypeDef *i2c);
static bool _hio_i2c_read(I2C_TypeDef *i2c, const void *buffer, size_t length);
static bool _hio_i2c_write(I2C_TypeDef *i2c, const void *buffer, size_t length);
static uint32_t hio_i2c_get_timeout_ms(hio_i2c_channel_t channel, size_t length);
static uint32_t hio_i2c_get_timeout_us(hio_i2c_channel_t channel, size_t length);
static void _hio_i2c_timeout_begin(uint32_t timeout_ms);
static bool _hio_i2c_timeout_is_expired(void);
static void _hio_i2c_restore_bus(I2C_TypeDef *i2c);

void hio_i2c_init(hio_i2c_channel_t channel, hio_i2c_speed_t speed)
{
    if (++_hio_i2c[channel].initialized_semaphore != 1)
    {
        return;
    }

    if (channel == HIO_I2C_I2C0)
    {
        // Initialize I2C2 pins
        RCC->IOPENR |= RCC_IOPENR_GPIOBEN;

        // Errata workaround
        RCC->IOPENR;

        GPIOB->MODER &= ~(GPIO_MODER_MODE10_0 | GPIO_MODER_MODE11_0);
        GPIOB->OTYPER |= GPIO_OTYPER_OT_10 | GPIO_OTYPER_OT_11;
        GPIOB->OSPEEDR |= GPIO_OSPEEDER_OSPEED10 | GPIO_OSPEEDER_OSPEED11;
        GPIOB->PUPDR |= GPIO_PUPDR_PUPD10_0 | GPIO_PUPDR_PUPD11_0;
        GPIOB->AFR[1] |= 6 << GPIO_AFRH_AFRH3_Pos | 6 << GPIO_AFRH_AFRH2_Pos;

        // Enable I2C2 peripheral clock
        RCC->APB1ENR |= RCC_APB1ENR_I2C2EN;

        // Errata workaround
        RCC->APB1ENR;

        // Enable I2C2 peripheral
        I2C2->CR1 |= I2C_CR1_PE;

        hio_i2c_set_speed(channel, speed);
    }
    else if (channel == HIO_I2C_I2C1)
    {
        // Initialize I2C1 pins
        RCC->IOPENR |= RCC_IOPENR_GPIOBEN;

        // Errata workaround
        RCC->IOPENR;

        GPIOB->MODER &= ~(GPIO_MODER_MODE8_0 | GPIO_MODER_MODE9_0);
        GPIOB->OTYPER |= GPIO_OTYPER_OT_8 | GPIO_OTYPER_OT_9;
        GPIOB->OSPEEDR |= GPIO_OSPEEDER_OSPEED8 | GPIO_OSPEEDER_OSPEED9;
        GPIOB->PUPDR |= GPIO_PUPDR_PUPD8_0 | GPIO_PUPDR_PUPD9_0;
        GPIOB->AFR[1] |= 4 << GPIO_AFRH_AFRH1_Pos | 4 << GPIO_AFRH_AFRH0_Pos;

        // Enable I2C1 peripheral clock
        RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;

        // Errata workaround
        RCC->APB1ENR;

        // Enable I2C1 peripheral
        I2C1->CR1 |= I2C_CR1_PE;

        hio_i2c_set_speed(channel, speed);
    }
    else if (channel == HIO_I2C_I2C_1W)
    {
        hio_onewire_t *onewire;

        #if HIO_USE_X1_FOR_I2C_1W != 1
        hio_module_sensor_init();
        hio_module_sensor_onewire_power_up();
        onewire = hio_module_sensor_get_onewire();
        #else
        hio_module_x1_init();
        onewire = hio_module_x1_get_onewire();
        #endif

        hio_tick_t t_timeout = hio_tick_get() + 500;

        while (hio_tick_get() < t_timeout)
        {
            continue;
        }

        hio_ds28e17_init(&ds28e17, onewire, 0x00);

        hio_i2c_set_speed(channel, speed);
    }
}

void hio_i2c_deinit(hio_i2c_channel_t channel)
{
    if (--_hio_i2c[channel].initialized_semaphore != 0)
    {
        return;
    }

    if (channel == HIO_I2C_I2C0)
    {
        // Disable I2C2 peripheral
        I2C2->CR1 |= I2C_CR1_PE;

        // Disable I2C2 peripheral clock
        RCC->APB1ENR &= ~RCC_APB1ENR_I2C2EN;

        // Errata workaround
        RCC->APB1ENR;

        GPIOB->AFR[1] &= ~(GPIO_AFRH_AFRH3_Msk | GPIO_AFRH_AFRH2_Msk);
        GPIOB->OSPEEDR &= ~(GPIO_OSPEEDER_OSPEED10_Msk | GPIO_OSPEEDER_OSPEED11_Msk);
        GPIOB->OTYPER &= ~(GPIO_OTYPER_OT_10 | GPIO_OTYPER_OT_11);
        GPIOB->MODER |= GPIO_MODER_MODE10_Msk | GPIO_MODER_MODE11_Msk;

    }
    else if (channel == HIO_I2C_I2C1)
    {
        // Disable I2C1 peripheral
        I2C1->CR1 &= ~I2C_CR1_PE;

        // Disable I2C1 peripheral clock
        RCC->APB1ENR &= ~RCC_APB1ENR_I2C1EN;

        // Errata workaround
        RCC->APB1ENR;

        GPIOB->AFR[1] &= ~(GPIO_AFRH_AFRH1_Msk | GPIO_AFRH_AFRH0_Msk);
        GPIOB->OSPEEDR &= ~(GPIO_OSPEEDER_OSPEED8_Msk | GPIO_OSPEEDER_OSPEED9_Msk);
        GPIOB->OTYPER &= ~(GPIO_OTYPER_OT_8 | GPIO_OTYPER_OT_9);
        GPIOB->MODER |= GPIO_MODER_MODE8_Msk | GPIO_MODER_MODE9_Msk;
    }
    else if (channel == HIO_I2C_I2C_1W)
    {
        hio_ds28e17_deinit(&ds28e17);

        hio_module_sensor_set_pull(HIO_MODULE_SENSOR_CHANNEL_A, HIO_MODULE_SENSOR_PULL_NONE);
        hio_module_sensor_set_pull(HIO_MODULE_SENSOR_CHANNEL_B, HIO_MODULE_SENSOR_PULL_NONE);
        // TODO: deinit hio_module_sensor and hio_ds28e17
    }
}

hio_i2c_speed_t hio_i2c_get_speed(hio_i2c_channel_t channel)
{
    return _hio_i2c[channel].speed;
}

void hio_i2c_set_speed(hio_i2c_channel_t channel, hio_i2c_speed_t speed)
{
    uint32_t timingr;

    if (_hio_i2c[channel].initialized_semaphore == 0)
    {
        return;
    }

    if (channel == HIO_I2C_I2C_1W)
    {
        hio_ds28e17_set_speed(&ds28e17, speed);

        _hio_i2c[channel].speed = speed;

        return;
    }

    if (speed == HIO_I2C_SPEED_400_KHZ)
    {
        timingr = 0x301d1d;
    }
    else
    {
        timingr = 0x709595;
    }

    if (channel == HIO_I2C_I2C0)
    {
        I2C2->CR1 &= ~I2C_CR1_PE;
        I2C2->TIMINGR = timingr;
        I2C2->CR1 |= I2C_CR1_PE;
    }
    else if (channel == HIO_I2C_I2C1)
    {
        I2C1->CR1 &= ~I2C_CR1_PE;
        I2C1->TIMINGR = timingr;
        I2C1->CR1 |= I2C_CR1_PE;
    }

    _hio_i2c[channel].speed = speed;
}

bool hio_i2c_write(hio_i2c_channel_t channel, const hio_i2c_transfer_t *transfer)
{
    if (_hio_i2c[channel].initialized_semaphore == 0)
    {
        return false;
    }

    if (channel == HIO_I2C_I2C_1W)
    {
        return hio_ds28e17_write(&ds28e17, transfer);
    }

    I2C_TypeDef *i2c = _hio_i2c[channel].i2c;

    hio_system_pll_enable();

    // Get maximum allowed timeout in ms
    uint32_t timeout_ms = _HIO_I2C_TX_TIMEOUT_ADJUST_FACTOR * hio_i2c_get_timeout_ms(channel, transfer->length);

    _hio_i2c_timeout_begin(timeout_ms);

    bool status = false;

    // Wait until bus is not busy
    if (_hio_i2c_watch_flag(i2c, I2C_ISR_BUSY, SET))
    {
        // Configure I2C peripheral and try to get ACK on device address write
        _hio_i2c_config(i2c, transfer->device_address << 1, transfer->length, I2C_CR2_AUTOEND, _HIO_I2C_GENERATE_START_WRITE);

        // Try to transmit buffer and update status
        status = _hio_i2c_write(i2c, transfer->buffer, transfer->length);
    }

    // If error occured ( timeout | NACK | ... ) ...
    if (status == false)
    {
        // Reset I2C peripheral to generate STOP conditions immediately
        __HIO_I2C_RESET_PERIPHERAL(i2c);
    }

    hio_system_pll_disable();

    return status;

}

bool hio_i2c_read(hio_i2c_channel_t channel, const hio_i2c_transfer_t *transfer)
{
    if (_hio_i2c[channel].initialized_semaphore == 0)
    {
        return false;
    }

    if (channel == HIO_I2C_I2C_1W)
    {
        return hio_ds28e17_read(&ds28e17, transfer);
    }

    I2C_TypeDef *i2c = _hio_i2c[channel].i2c;

    hio_system_pll_enable();

    // Get maximum allowed timeout in ms
    uint32_t timeout_ms = _HIO_I2C_RX_TIMEOUT_ADJUST_FACTOR * hio_i2c_get_timeout_ms(channel, transfer->length);

    _hio_i2c_timeout_begin(timeout_ms);

    bool status = false;

    // Wait until bus is not busy
    if (_hio_i2c_watch_flag(i2c, I2C_ISR_BUSY, SET))
    {
        // Configure I2C peripheral and try to get ACK on device address read
        _hio_i2c_config(i2c, transfer->device_address << 1, transfer->length, I2C_CR2_AUTOEND, I2C_CR2_START | I2C_CR2_RD_WRN);

        // Try to receive data to buffer and update status
        status = _hio_i2c_read(i2c, transfer->buffer, transfer->length);
    }

    // If error occured ( timeout | NACK | ... ) ...
    if (status == false)
    {
        _hio_i2c_restore_bus(i2c);
    }

    hio_system_pll_disable();

    return status;
}

bool hio_i2c_memory_write(hio_i2c_channel_t channel, const hio_i2c_memory_transfer_t *transfer)
{
    if (_hio_i2c[channel].initialized_semaphore == 0)
    {
        return false;
    }

    if (channel == HIO_I2C_I2C_1W)
    {
        return hio_ds28e17_memory_write(&ds28e17, transfer);
    }

    I2C_TypeDef *i2c = _hio_i2c[channel].i2c;

    // Enable PLL and disable sleep
    hio_system_pll_enable();

    uint16_t transfer_memory_address_length =
            (transfer->memory_address & HIO_I2C_MEMORY_ADDRESS_16_BIT) != 0 ? _HIO_I2C_MEMORY_ADDRESS_SIZE_16BIT : _HIO_I2C_MEMORY_ADDRESS_SIZE_8BIT;

    // If memory write failed ...
    if (!_hio_i2c_mem_write(i2c, transfer->device_address << 1, transfer->memory_address, transfer_memory_address_length, transfer->buffer, transfer->length))
    {
        // Reset I2C peripheral to generate STOP conditions immediately
        __HIO_I2C_RESET_PERIPHERAL(i2c);

        // Disable PLL and enable sleep
        hio_system_pll_disable();

        return false;
    }

    // Disable PLL and enable sleep
    hio_system_pll_disable();

    return true;
}

bool hio_i2c_memory_read(hio_i2c_channel_t channel, const hio_i2c_memory_transfer_t *transfer)
{
    if (_hio_i2c[channel].initialized_semaphore == 0)
    {
        return false;
    }

    if (channel == HIO_I2C_I2C_1W)
    {
        return hio_ds28e17_memory_read(&ds28e17, transfer);
    }

    I2C_TypeDef *i2c = _hio_i2c[channel].i2c;

    // Enable PLL and disable sleep
    hio_system_pll_enable();

    uint16_t transfer_memory_address_length =
            (transfer->memory_address & HIO_I2C_MEMORY_ADDRESS_16_BIT) != 0 ? _HIO_I2C_MEMORY_ADDRESS_SIZE_16BIT : _HIO_I2C_MEMORY_ADDRESS_SIZE_8BIT;

    // If error occurs during memory read ...
    if (!_hio_i2c_mem_read(i2c, transfer->device_address << 1, transfer->memory_address, transfer_memory_address_length, transfer->buffer, transfer->length))
    {
        _hio_i2c_restore_bus(i2c);

        // Disable PLL and enable sleep
        hio_system_pll_disable();

        return false;
    }

    // Disable PLL and enable sleep
    hio_system_pll_disable();

    return true;
}

bool hio_i2c_memory_write_8b(hio_i2c_channel_t channel, uint8_t device_address, uint32_t memory_address, uint8_t data)
{
    hio_i2c_memory_transfer_t transfer;

    transfer.device_address = device_address;
    transfer.memory_address = memory_address;
    transfer.buffer = &data;
    transfer.length = 1;

    return hio_i2c_memory_write(channel, &transfer);
}

bool hio_i2c_memory_write_16b(hio_i2c_channel_t channel, uint8_t device_address, uint32_t memory_address, uint16_t data)
{
    uint8_t buffer[2];

    buffer[0] = data >> 8;
    buffer[1] = data;

    hio_i2c_memory_transfer_t transfer;

    transfer.device_address = device_address;
    transfer.memory_address = memory_address;
    transfer.buffer = buffer;
    transfer.length = 2;

    return hio_i2c_memory_write(channel, &transfer);
}

bool hio_i2c_memory_read_8b(hio_i2c_channel_t channel, uint8_t device_address, uint32_t memory_address, uint8_t *data)
{
    hio_i2c_memory_transfer_t transfer;

    transfer.device_address = device_address;
    transfer.memory_address = memory_address;
    transfer.buffer = data;
    transfer.length = 1;

    return hio_i2c_memory_read(channel, &transfer);
}

bool hio_i2c_memory_read_16b(hio_i2c_channel_t channel, uint8_t device_address, uint32_t memory_address, uint16_t *data)
{
    uint8_t buffer[2];

    hio_i2c_memory_transfer_t transfer;

    transfer.device_address = device_address;
    transfer.memory_address = memory_address;
    transfer.buffer = buffer;
    transfer.length = 2;

    if (!hio_i2c_memory_read(channel, &transfer))
    {
        return false;
    }

    *data = buffer[0] << 8 | buffer[1];

    return true;
}

static bool _hio_i2c_mem_write(I2C_TypeDef *i2c, uint8_t device_address, uint16_t memory_address, uint16_t memory_address_length, uint8_t *buffer, uint16_t length)
{
    // Get maximum allowed timeout in ms
    uint32_t timeout_ms = _HIO_I2C_TX_TIMEOUT_ADJUST_FACTOR * hio_i2c_get_timeout_ms(i2c == I2C2 ? HIO_I2C_I2C0 : HIO_I2C_I2C1, length);

    _hio_i2c_timeout_begin(timeout_ms);

    // Wait until bus is not busy
    if (!_hio_i2c_watch_flag(i2c, I2C_ISR_BUSY, SET))
    {
        return false;
    }

    // Send slave address and memory address
    if (!_hio_i2c_req_mem_write(i2c, device_address, memory_address, memory_address_length))
    {
        return false;
    }

    // Set size of data to write
    _hio_i2c_config(i2c, device_address, length, _HIO_I2C_AUTOEND_MODE, _HIO_I2C_NO_STARTSTOP);

    // Perform I2C transfer
    return _hio_i2c_write(i2c, buffer, length);
}

static bool _hio_i2c_mem_read(I2C_TypeDef *i2c, uint8_t device_address, uint16_t memory_address, uint16_t memory_address_length, uint8_t *buffer, uint16_t length)
{
    // Get maximum allowed timeout in ms
    uint32_t timeout_ms = _HIO_I2C_RX_TIMEOUT_ADJUST_FACTOR * hio_i2c_get_timeout_ms(i2c == I2C2 ? HIO_I2C_I2C0 : HIO_I2C_I2C1, length);

    _hio_i2c_timeout_begin(timeout_ms);

    // Wait until bus is not busy
    if (!_hio_i2c_watch_flag(i2c, I2C_ISR_BUSY, SET))
    {
        return false;
    }

    // Send slave address and memory address
    if (!_hio_i2c_req_mem_read(i2c, device_address, memory_address, memory_address_length))
    {
        return false;
    }

    // Set size of data to read
    _hio_i2c_config(i2c, device_address, length, I2C_CR2_AUTOEND, I2C_CR2_START | I2C_CR2_RD_WRN);

    // Perform I2C transfer
    return _hio_i2c_read(i2c, buffer, length);
}

static bool _hio_i2c_req_mem_write(I2C_TypeDef *i2c, uint8_t device_address, uint16_t memory_address, uint16_t memory_address_length)
{
    _hio_i2c_config(i2c, device_address, memory_address_length, _HIO_I2C_RELOAD_MODE, _HIO_I2C_GENERATE_START_WRITE);

    // Wait until TXIS flag is set
    if (!_hio_i2c_watch_flag(i2c, I2C_ISR_TXIS, RESET))
    {
        return false;
    }

    // If memory address size is 16Bit
    if (memory_address_length == _HIO_I2C_MEMORY_ADDRESS_SIZE_16BIT)
    {
        // Send MSB of memory address
        i2c->TXDR = (memory_address >> 8) & 0xff;

        // Wait until TXIS flag is set
        if (!_hio_i2c_watch_flag(i2c, I2C_ISR_TXIS, RESET))
        {
            return false;
        }
    }

    // Send LSB of memory address
    i2c->TXDR = memory_address & 0xff;

    // Wait until TCR flag is set
    if (!_hio_i2c_watch_flag(i2c, I2C_ISR_TCR, RESET))
    {
        return false;
    }

    return true;
}

static bool _hio_i2c_req_mem_read(I2C_TypeDef *i2c, uint8_t device_address, uint16_t memory_address, uint16_t memory_address_length)
{
    _hio_i2c_config(i2c, device_address, memory_address_length, _HIO_I2C_SOFTEND_MODE, _HIO_I2C_GENERATE_START_WRITE);

    // Wait until TXIS flag is set
    if (!_hio_i2c_watch_flag(i2c, I2C_ISR_TXIS, RESET))
    {
        return false;
    }

    // If memory address size is 16Bit
    if (memory_address_length == _HIO_I2C_MEMORY_ADDRESS_SIZE_16BIT)
    {
        // Send MSB of memory address
        i2c->TXDR = (memory_address >> 8) & 0xff;

        // Wait until TXIS flag is set
        if (!_hio_i2c_watch_flag(i2c, I2C_ISR_TXIS, RESET))
        {
            return false;
        }
    }

    // Send LSB of memory address
    i2c->TXDR = memory_address & 0xff;

    // Wait until TC flag is set
    if (!_hio_i2c_watch_flag(i2c, I2C_ISR_TC, RESET))
    {
        return false;
    }

    return true;
}

static void _hio_i2c_config(I2C_TypeDef *i2c, uint8_t device_address, uint8_t length, uint32_t mode, uint32_t Request)
{
    uint32_t reg;

    // Get the CR2 register value
    reg = i2c->CR2;

    // clear tmpreg specific bits
    reg &= ~(I2C_CR2_SADD | I2C_CR2_NBYTES | I2C_CR2_RELOAD | I2C_CR2_AUTOEND | I2C_CR2_RD_WRN | I2C_CR2_START | I2C_CR2_STOP);

    // update tmpreg
    reg |= (device_address & I2C_CR2_SADD) | (length << I2C_CR2_NBYTES_Pos) | mode | Request;

    // update CR2 register
    i2c->CR2 = reg;
}

static bool _hio_i2c_watch_flag(I2C_TypeDef *i2c, uint32_t flag, FlagStatus status)
{
    while ((i2c->ISR & flag) == status)
    {
        if ((flag == I2C_ISR_STOPF) || (flag == I2C_ISR_TXIS))
        {
            // Check if a NACK is not detected ...
            if (!_hio_i2c_is_ack_failure(i2c))
            {
                return false;
            }
        }

        if (_hio_i2c_timeout_is_expired())
        {
            return false;
        }
    }
    return true;
}

static bool _hio_i2c_is_ack_failure(I2C_TypeDef *i2c)
{
    if ((i2c->ISR & I2C_ISR_NACKF) != 0)
    {
        // Wait until STOP flag is reset
        // AutoEnd should be initialized after AF
        while ((i2c->ISR & I2C_ISR_STOPF) == 0)
        {
            if (_hio_i2c_timeout_is_expired())
            {
                return false;
            }
        }

        // Clear NACKF flag
        i2c->ICR = I2C_ISR_NACKF;

        // Clear STOP flag
        i2c->ICR = I2C_ISR_STOPF;

        // If a pending TXIS flag is set ...
        if ((i2c->ISR & I2C_ISR_TXIS) != 0)
        {
            // ... write a dummy data in TXDR to clear it
            i2c->TXDR = 0;
        }

        // Flush TX register if not empty
        if ((i2c->ISR & I2C_ISR_TXE) == 0)
        {
            i2c->ISR |= I2C_ISR_TXE;
        }

        // Clear Configuration Register 2
        i2c->CR2 &= ~(I2C_CR2_SADD | I2C_CR2_HEAD10R | I2C_CR2_NBYTES | I2C_CR2_RELOAD | I2C_CR2_RD_WRN);

        return false;
    }

    return true;
}

static bool _hio_i2c_read(I2C_TypeDef *i2c, const void *buffer, size_t length)
{
    uint8_t *p = (uint8_t *) buffer;

    while (length > 0)
    {
        // Wait until RXNE flag is set
        if (!_hio_i2c_watch_flag(i2c, I2C_ISR_RXNE, RESET))
        {
            return false;
        }

        // Read data from RXDR
        *p++ = i2c->RXDR;

        length--;
    }

    // No need to Check TC flag, with AUTOEND mode the stop is automatically generated

    // Wait until STOPF flag is reset
    if (!_hio_i2c_watch_flag(i2c, I2C_ISR_STOPF, RESET))
    {
        return false;
    }

    // Clear STOP flag
    i2c->ICR = I2C_ICR_STOPCF;

    // Clear Configuration Register 2
    i2c->CR2 &= ~(I2C_CR2_SADD | I2C_CR2_HEAD10R | I2C_CR2_NBYTES | I2C_CR2_RELOAD | I2C_CR2_RD_WRN);

    return true;
}

static uint32_t hio_i2c_get_timeout_ms(hio_i2c_channel_t channel, size_t length)
{
    uint32_t timeout_us = hio_i2c_get_timeout_us(channel, length);

    return (timeout_us / 1000) + 10;
}

static uint32_t hio_i2c_get_timeout_us(hio_i2c_channel_t channel, size_t length)
{
    if (hio_i2c_get_speed(channel) == HIO_I2C_SPEED_100_KHZ)
    {
        return _HIO_I2C_BYTE_TRANSFER_TIME_US_100 * (length + 3);
    }
    else
    {
        return _HIO_I2C_BYTE_TRANSFER_TIME_US_400 * (length + 3);
    }
}

static bool _hio_i2c_write(I2C_TypeDef *i2c, const void *buffer, size_t length)
{
    uint8_t *p = (uint8_t *) buffer;

    while (length > 0)
    {
        // Wait until TXIS flag is set
        if (!_hio_i2c_watch_flag(i2c, I2C_ISR_TXIS, RESET))
        {
            return false;
        }

        // Write data to TXDR
        i2c->TXDR = *p++;

        length--;
    }

    // No need to Check TC flag, with AUTOEND mode the stop is automatically generated

    // Wait until STOPF flag is reset
    if (!_hio_i2c_watch_flag(i2c, I2C_ISR_STOPF, RESET))
    {
        return false;
    }

    // Clear STOP flag
    i2c->ICR = I2C_ICR_STOPCF;

    // Clear Configuration Register 2
    i2c->CR2 &= ~(I2C_CR2_SADD | I2C_CR2_HEAD10R | I2C_CR2_NBYTES | I2C_CR2_RELOAD | I2C_CR2_RD_WRN);

    return true;
}

void _hio_i2c_timeout_begin(uint32_t timeout_ms)
{
    tick_timeout = hio_tick_get() + timeout_ms;
}

bool _hio_i2c_timeout_is_expired(void)
{
    bool is_expired = tick_timeout < hio_tick_get() ? true : false;

    return is_expired;
}

static void _hio_i2c_restore_bus(I2C_TypeDef *i2c)
{
    // TODO Take care of maximum rate on clk pin

    if (i2c == I2C2)
    {
        GPIOB->MODER &= ~GPIO_MODER_MODE10_Msk;
        GPIOB->MODER |= GPIO_MODER_MODE10_0;
        GPIOB->BSRR = GPIO_BSRR_BS_10;

        GPIOB->MODER &= ~GPIO_MODER_MODE11_Msk;

        while (!(GPIOB->IDR & GPIO_IDR_ID11))
        {
            GPIOB->ODR ^= GPIO_ODR_OD10;
        }

        GPIOB->BSRR = GPIO_BSRR_BR_11;
        GPIOB->BSRR = GPIO_BSRR_BS_11;

        // Configure I2C peripheral to transmit softend mode
        _hio_i2c_config(i2c, 0xfe, 1, I2C_CR2_STOP, _HIO_I2C_SOFTEND_MODE);

        // Reset I2C peripheral to generate STOP conditions immediately
        __HIO_I2C_RESET_PERIPHERAL(i2c);

        GPIOB->MODER &= ~GPIO_MODER_MODE10_Msk;
        GPIOB->MODER |= GPIO_MODER_MODE10_1;

        GPIOB->MODER &= ~GPIO_MODER_MODE11_Msk;
        GPIOB->MODER |= GPIO_MODER_MODE11_1;

        GPIOB->BSRR = GPIO_BSRR_BR_10;
        GPIOB->BSRR = GPIO_BSRR_BR_11;
    }
    else
    {
        GPIOB->MODER &= ~GPIO_MODER_MODE8_Msk;
        GPIOB->MODER |= GPIO_MODER_MODE8_0;
        GPIOB->BSRR = GPIO_BSRR_BS_8;

        GPIOB->MODER &= ~GPIO_MODER_MODE9_Msk;

        while (!(GPIOB->IDR & GPIO_IDR_ID9))
        {
            GPIOB->ODR ^= GPIO_ODR_OD9;
        }

        GPIOB->BSRR = GPIO_BSRR_BR_9;
        GPIOB->BSRR = GPIO_BSRR_BS_9;

        // Configure I2C peripheral to transmit softend mode
        _hio_i2c_config(i2c, 0xfe, 1, I2C_CR2_STOP, _HIO_I2C_SOFTEND_MODE);

        // Reset I2C peripheral to generate STOP conditions immediately
        __HIO_I2C_RESET_PERIPHERAL(i2c);

        GPIOB->MODER &= ~GPIO_MODER_MODE8_Msk;
        GPIOB->MODER |= GPIO_MODER_MODE8_1;

        GPIOB->MODER &= ~GPIO_MODER_MODE9_Msk;
        GPIOB->MODER |= GPIO_MODER_MODE9_1;

        GPIOB->BSRR = GPIO_BSRR_BR_8;
        GPIOB->BSRR = GPIO_BSRR_BR_11;
    }
}

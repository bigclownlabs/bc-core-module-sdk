#include <bc_usb_cdc.h>
#include <bc_scheduler.h>
#include <bc_fifo.h>

#include <usbd_core.h>
#include <usbd_cdc.h>
#include <usbd_cdc_if.h>
#include <usbd_desc.h>

#include <stm32l0xx.h>

static struct
{
    bc_fifo_t receive_fifo;
    uint8_t receive_buffer[1024];
    uint8_t transmit_buffer[1024];
    size_t transmit_length;

} bc_usb_cdc;

USBD_HandleTypeDef hUsbDeviceFS;

static bc_tick_t _bc_usb_cdc_task(void *param, bc_tick_t tick_now);

void bc_usb_cdc_init(void)
{
    memset(&bc_usb_cdc, 0, sizeof(bc_usb_cdc));

    bc_fifo_init(&bc_usb_cdc.receive_fifo, bc_usb_cdc.receive_buffer, sizeof(bc_usb_cdc.receive_buffer));

    __HAL_RCC_GPIOA_CLK_ENABLE();

    USBD_Init(&hUsbDeviceFS, &FS_Desc, DEVICE_FS);
    USBD_RegisterClass(&hUsbDeviceFS, &USBD_CDC);
    USBD_CDC_RegisterInterface(&hUsbDeviceFS, &USBD_Interface_fops_FS);
    USBD_Start(&hUsbDeviceFS);

    bc_scheduler_register(_bc_usb_cdc_task, NULL, 0);
}

bool bc_usb_cdc_write(const void *buffer, size_t length)
{
    if (length > (sizeof(bc_usb_cdc.transmit_buffer) - bc_usb_cdc.transmit_length))
    {
        return false;
    }

    memcpy(&bc_usb_cdc.transmit_buffer[bc_usb_cdc.transmit_length], buffer, length);

    bc_usb_cdc.transmit_length += length;

    return true;
}

size_t bc_usb_cdc_read(void *buffer, size_t length)
{
    size_t bytes_read = 0;

    while (length != 0)
    {
        uint8_t value;

        if (bc_fifo_read(&bc_usb_cdc.receive_fifo, &value, 1) == 1)
        {
            *(uint8_t *) buffer = value;

            buffer = (uint8_t *) buffer + 1;

            bytes_read++;
        }
        else
        {
            break;
        }

        length--;
    }

    return bytes_read;
}

void bc_usb_cdc_received_data(const void *buffer, size_t length)
{
    bc_fifo_irq_write(&bc_usb_cdc.receive_fifo, (uint8_t *) buffer, length);
}

static bc_tick_t _bc_usb_cdc_task(void *param, bc_tick_t tick_now)
{
    (void) param;

    if (bc_usb_cdc.transmit_length == 0)
    {
        // TODO
        return tick_now;
    }

    HAL_NVIC_DisableIRQ(USB_IRQn);

    if (CDC_Transmit_FS(bc_usb_cdc.transmit_buffer, bc_usb_cdc.transmit_length) == USBD_OK)
    {
        bc_usb_cdc.transmit_length = 0;
    }

    HAL_NVIC_EnableIRQ(USB_IRQn);

    // TODO
    return tick_now;
}

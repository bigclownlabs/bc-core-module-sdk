#include "stm32l0xx.h"
#include <bc_ws2812b.h>
#include <bc_scheduler.h>

#define _BC_WS2812_TIMER_PERIOD 20              // 16000000 / 800000 = 20; 0,125us period (10 times lower the 1,25us period to have fixed math below)
#define _BC_WS2812_TIMER_RESET_PULSE_PERIOD 833 // 60us just to be sure = (16000000 / (320 * 60))
#define _BC_WS2812_COMPARE_PULSE_LOGIC_0      5 //(10 * timer_period) / 36
#define _BC_WS2812_COMPARE_PULSE_LOGIC_1     13 //(10 * timer_period) / 15;

#define _BC_WS2812_BC_WS2812_RESET_PERIOD 100
#define _BC_WS2812_BC_WS2812B_PORT GPIOA
#define _BC_WS2812_BC_WS2812B_PIN GPIO_PIN_1

static struct ws2812b_t
{
	uint32_t *dma_bit_buffer;
	size_t dma_bit_buffer_size;
	bc_ws2812b_type_t type;
	uint16_t count;
    bool transfer;
    bc_scheduler_task_id_t task_id;
    void (*event_handler)(bc_ws2812b_event_t, void *);
    void *event_param;

} _bc_ws2812b;

DMA_HandleTypeDef _bc_ws2812b_dma_update;
TIM_HandleTypeDef _bc_ws2812b_timer2_handle;
TIM_OC_InitTypeDef _bc_ws2812b_timer2_oc1;

static void _bc_ws2812b_dma_transfer_complete_handler(DMA_HandleTypeDef *dma_handle);
static void _bc_ws2812b_dma_transfer_half_handler(DMA_HandleTypeDef *dma_handle);
static void _bc_ws2812b_task(void *param);

const uint32_t _bc_ws2812b_pulse_tab[] = {
		_BC_WS2812_COMPARE_PULSE_LOGIC_0 << 24 | _BC_WS2812_COMPARE_PULSE_LOGIC_0 << 16 | _BC_WS2812_COMPARE_PULSE_LOGIC_0 << 8 | _BC_WS2812_COMPARE_PULSE_LOGIC_0,
		_BC_WS2812_COMPARE_PULSE_LOGIC_1 << 24 | _BC_WS2812_COMPARE_PULSE_LOGIC_0 << 16 | _BC_WS2812_COMPARE_PULSE_LOGIC_0 << 8 | _BC_WS2812_COMPARE_PULSE_LOGIC_0,
		_BC_WS2812_COMPARE_PULSE_LOGIC_0 << 24 | _BC_WS2812_COMPARE_PULSE_LOGIC_1 << 16 | _BC_WS2812_COMPARE_PULSE_LOGIC_0 << 8 | _BC_WS2812_COMPARE_PULSE_LOGIC_0,
		_BC_WS2812_COMPARE_PULSE_LOGIC_1 << 24 | _BC_WS2812_COMPARE_PULSE_LOGIC_1 << 16 | _BC_WS2812_COMPARE_PULSE_LOGIC_0 << 8 | _BC_WS2812_COMPARE_PULSE_LOGIC_0,
		_BC_WS2812_COMPARE_PULSE_LOGIC_0 << 24 | _BC_WS2812_COMPARE_PULSE_LOGIC_0 << 16 | _BC_WS2812_COMPARE_PULSE_LOGIC_1 << 8 | _BC_WS2812_COMPARE_PULSE_LOGIC_0,
		_BC_WS2812_COMPARE_PULSE_LOGIC_1 << 24 | _BC_WS2812_COMPARE_PULSE_LOGIC_0 << 16 | _BC_WS2812_COMPARE_PULSE_LOGIC_1 << 8 | _BC_WS2812_COMPARE_PULSE_LOGIC_0,
		_BC_WS2812_COMPARE_PULSE_LOGIC_0 << 24 | _BC_WS2812_COMPARE_PULSE_LOGIC_1 << 16 | _BC_WS2812_COMPARE_PULSE_LOGIC_1 << 8 | _BC_WS2812_COMPARE_PULSE_LOGIC_0,
		_BC_WS2812_COMPARE_PULSE_LOGIC_1 << 24 | _BC_WS2812_COMPARE_PULSE_LOGIC_1 << 16 | _BC_WS2812_COMPARE_PULSE_LOGIC_1 << 8 | _BC_WS2812_COMPARE_PULSE_LOGIC_0,
		_BC_WS2812_COMPARE_PULSE_LOGIC_0 << 24 | _BC_WS2812_COMPARE_PULSE_LOGIC_0 << 16 | _BC_WS2812_COMPARE_PULSE_LOGIC_0 << 8 | _BC_WS2812_COMPARE_PULSE_LOGIC_1,
		_BC_WS2812_COMPARE_PULSE_LOGIC_1 << 24 | _BC_WS2812_COMPARE_PULSE_LOGIC_0 << 16 | _BC_WS2812_COMPARE_PULSE_LOGIC_0 << 8 | _BC_WS2812_COMPARE_PULSE_LOGIC_1,
		_BC_WS2812_COMPARE_PULSE_LOGIC_0 << 24 | _BC_WS2812_COMPARE_PULSE_LOGIC_1 << 16 | _BC_WS2812_COMPARE_PULSE_LOGIC_0 << 8 | _BC_WS2812_COMPARE_PULSE_LOGIC_1,
		_BC_WS2812_COMPARE_PULSE_LOGIC_1 << 24 | _BC_WS2812_COMPARE_PULSE_LOGIC_1 << 16 | _BC_WS2812_COMPARE_PULSE_LOGIC_0 << 8 | _BC_WS2812_COMPARE_PULSE_LOGIC_1,
		_BC_WS2812_COMPARE_PULSE_LOGIC_0 << 24 | _BC_WS2812_COMPARE_PULSE_LOGIC_0 << 16 | _BC_WS2812_COMPARE_PULSE_LOGIC_1 << 8 | _BC_WS2812_COMPARE_PULSE_LOGIC_1,
		_BC_WS2812_COMPARE_PULSE_LOGIC_1 << 24 | _BC_WS2812_COMPARE_PULSE_LOGIC_0 << 16 | _BC_WS2812_COMPARE_PULSE_LOGIC_1 << 8 | _BC_WS2812_COMPARE_PULSE_LOGIC_1,
		_BC_WS2812_COMPARE_PULSE_LOGIC_0 << 24 | _BC_WS2812_COMPARE_PULSE_LOGIC_1 << 16 | _BC_WS2812_COMPARE_PULSE_LOGIC_1 << 8 | _BC_WS2812_COMPARE_PULSE_LOGIC_1,
		_BC_WS2812_COMPARE_PULSE_LOGIC_1 << 24 | _BC_WS2812_COMPARE_PULSE_LOGIC_1 << 16 | _BC_WS2812_COMPARE_PULSE_LOGIC_1 << 8 | _BC_WS2812_COMPARE_PULSE_LOGIC_1,
};

void DMA1_Channel2_3_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&_bc_ws2812b_dma_update);
}

bool bc_ws2812b_send(void)
{
	if (_bc_ws2812b.transfer)
	{
		return false;
	}
    // transmission complete flag
	_bc_ws2812b.transfer = true;


    HAL_TIM_Base_Stop(&_bc_ws2812b_timer2_handle);
    (&_bc_ws2812b_timer2_handle)->Instance->CR1 &= ~((0x1U << (0U)));

    // clear all DMA flags
    __HAL_DMA_CLEAR_FLAG(&_bc_ws2812b_dma_update, DMA_FLAG_TC2 | DMA_FLAG_HT2 | DMA_FLAG_TE2);

    // configure the number of bytes to be transferred by the DMA controller
    _bc_ws2812b_dma_update.Instance->CNDTR = _bc_ws2812b.dma_bit_buffer_size;

    // clear all TIM2 flags
    __HAL_TIM_CLEAR_FLAG(&_bc_ws2812b_timer2_handle, TIM_FLAG_UPDATE | TIM_FLAG_CC1 | TIM_FLAG_CC2 | TIM_FLAG_CC3 | TIM_FLAG_CC4);

    // enable DMA channels
    __HAL_DMA_ENABLE(&_bc_ws2812b_dma_update);

    // IMPORTANT: enable the TIM2 DMA requests AFTER enabling the DMA channels!
    __HAL_TIM_ENABLE_DMA(&_bc_ws2812b_timer2_handle, TIM_DMA_UPDATE);

    TIM2->CNT = _BC_WS2812_TIMER_PERIOD - 1;

    // Set zero length for first pulse because the first bit loads after first TIM_UP
    TIM2->CCR2 = 0;

    // Enable PWM Compare 1
    //(&timer2_handle)->Instance->CCMR1 |= TIM_CCMR1_OC1M_1;
    // Enable PWM Compare 2
    (&_bc_ws2812b_timer2_handle)->Instance->CCMR1 |= TIM_CCMR1_OC2M_1;

    __HAL_DBGMCU_FREEZE_TIM2();

    // start TIM2
    __HAL_TIM_ENABLE(&_bc_ws2812b_timer2_handle);

    return true;
}

void _bc_ws2812b_dma_transfer_half_handler(DMA_HandleTypeDef *dma_handle)
{
    (void)dma_handle;

}

void _bc_ws2812b_dma_transfer_complete_handler(DMA_HandleTypeDef *dma_handle)
{
    (void)dma_handle;
    //_bc_ws2812b.transfer = false;

	// Stop timer
	TIM2->CR1 &= ~TIM_CR1_CEN;

	// Disable DMA
	__HAL_DMA_DISABLE(&_bc_ws2812b_dma_update);
	// Disable the DMA requests
	__HAL_TIM_DISABLE_DMA(&_bc_ws2812b_timer2_handle, TIM_DMA_UPDATE);

	// Disable PWM output compare 1
	//(&timer2_handle)->Instance->CCMR1 &= ~(TIM_CCMR1_OC1M_Msk);
	//(&timer2_handle)->Instance->CCMR1 |= TIM_CCMR1_OC1M_2;

	// Disable PWM output Compare 2
	(&_bc_ws2812b_timer2_handle)->Instance->CCMR1 &= ~(TIM_CCMR1_OC2M_Msk);
	(&_bc_ws2812b_timer2_handle)->Instance->CCMR1 |= TIM_CCMR1_OC2M_2;

	// Set 50us period for Treset pulse
	//TIM2->PSC = 1000; // For this long period we need prescaler 1000
	TIM2->ARR = _BC_WS2812_TIMER_RESET_PULSE_PERIOD;
	// Reset the timer
	TIM2->CNT = 0;

	// Generate an update event to reload the prescaler value immediately
	TIM2->EGR = TIM_EGR_UG;
	__HAL_TIM_CLEAR_FLAG(&_bc_ws2812b_timer2_handle, TIM_FLAG_UPDATE);

	// Enable TIM2 Update interrupt for Treset signal
	__HAL_TIM_ENABLE_IT(&_bc_ws2812b_timer2_handle, TIM_IT_UPDATE);
	// Enable timer
	TIM2->CR1 |= TIM_CR1_CEN;

}

void TIM2_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&_bc_ws2812b_timer2_handle);
}

// TIM2 Interrupt Handler gets executed on every TIM2 Update if enabled
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    (void)htim;

    TIM2->CR1 = 0; // disable timer

    // disable the TIM2 Update IRQ
    __HAL_TIM_DISABLE_IT(&_bc_ws2812b_timer2_handle, TIM_IT_UPDATE);

    // Set back 1,25us period
    TIM2->ARR = _BC_WS2812_TIMER_PERIOD;

    // Generate an update event to reload the Prescaler value immediatly
    TIM2->EGR = TIM_EGR_UG;
    __HAL_TIM_CLEAR_FLAG(&_bc_ws2812b_timer2_handle, TIM_FLAG_UPDATE);

    // set transfer_complete flag
    _bc_ws2812b.transfer = false;

    bc_scheduler_plan_now(_bc_ws2812b.task_id);
}

void bc_ws2812b_set_pixel(uint16_t position, uint8_t red, uint8_t green, uint8_t blue, uint8_t white)
{

	uint32_t calculated_position = (position * _bc_ws2812b.type * 2);

	_bc_ws2812b.dma_bit_buffer[calculated_position++] = _bc_ws2812b_pulse_tab[(green & 0xf0) >> 4];
	_bc_ws2812b.dma_bit_buffer[calculated_position++] = _bc_ws2812b_pulse_tab[green & 0x0f];

	_bc_ws2812b.dma_bit_buffer[calculated_position++] = _bc_ws2812b_pulse_tab[(red & 0xf0) >> 4];
	_bc_ws2812b.dma_bit_buffer[calculated_position++] = _bc_ws2812b_pulse_tab[red & 0x0f];

	_bc_ws2812b.dma_bit_buffer[calculated_position++] = _bc_ws2812b_pulse_tab[(blue & 0xf0) >> 4];
	_bc_ws2812b.dma_bit_buffer[calculated_position++] = _bc_ws2812b_pulse_tab[blue & 0x0f];

	 if (_bc_ws2812b.type == BC_WS2812B_TYPE_RGBW)
	 {
		 _bc_ws2812b.dma_bit_buffer[calculated_position++] = _bc_ws2812b_pulse_tab[(white & 0xf0) >> 4];
		 _bc_ws2812b.dma_bit_buffer[calculated_position++] = _bc_ws2812b_pulse_tab[white & 0x0f];
	 }

//    uint32_t calculated_column = (position * _bc_ws2812b.type * 8);
//    uint8_t *bit_buffer_offset = &_bc_ws2812b.dma_bit_buffer[calculated_column];

//    *bit_buffer_offset++ = (green & 0x80) ? _BC_WS2812_COMPARE_PULSE_LOGIC_1 : _BC_WS2812_COMPARE_PULSE_LOGIC_0;
//    *bit_buffer_offset++ = (green & 0x40) ? _BC_WS2812_COMPARE_PULSE_LOGIC_1 : _BC_WS2812_COMPARE_PULSE_LOGIC_0;
//    *bit_buffer_offset++ = (green & 0x20) ? _BC_WS2812_COMPARE_PULSE_LOGIC_1 : _BC_WS2812_COMPARE_PULSE_LOGIC_0;
//    *bit_buffer_offset++ = (green & 0x10) ? _BC_WS2812_COMPARE_PULSE_LOGIC_1 : _BC_WS2812_COMPARE_PULSE_LOGIC_0;
//    *bit_buffer_offset++ = (green & 0x08) ? _BC_WS2812_COMPARE_PULSE_LOGIC_1 : _BC_WS2812_COMPARE_PULSE_LOGIC_0;
//    *bit_buffer_offset++ = (green & 0x04) ? _BC_WS2812_COMPARE_PULSE_LOGIC_1 : _BC_WS2812_COMPARE_PULSE_LOGIC_0;
//    *bit_buffer_offset++ = (green & 0x02) ? _BC_WS2812_COMPARE_PULSE_LOGIC_1 : _BC_WS2812_COMPARE_PULSE_LOGIC_0;
//    *bit_buffer_offset++ = (green & 0x01) ? _BC_WS2812_COMPARE_PULSE_LOGIC_1 : _BC_WS2812_COMPARE_PULSE_LOGIC_0;
//
//    *bit_buffer_offset++ = (red & 0x80) ? _BC_WS2812_COMPARE_PULSE_LOGIC_1 : _BC_WS2812_COMPARE_PULSE_LOGIC_0;
//    *bit_buffer_offset++ = (red & 0x40) ? _BC_WS2812_COMPARE_PULSE_LOGIC_1 : _BC_WS2812_COMPARE_PULSE_LOGIC_0;
//    *bit_buffer_offset++ = (red & 0x20) ? _BC_WS2812_COMPARE_PULSE_LOGIC_1 : _BC_WS2812_COMPARE_PULSE_LOGIC_0;
//    *bit_buffer_offset++ = (red & 0x10) ? _BC_WS2812_COMPARE_PULSE_LOGIC_1 : _BC_WS2812_COMPARE_PULSE_LOGIC_0;
//    *bit_buffer_offset++ = (red & 0x08) ? _BC_WS2812_COMPARE_PULSE_LOGIC_1 : _BC_WS2812_COMPARE_PULSE_LOGIC_0;
//    *bit_buffer_offset++ = (red & 0x04) ? _BC_WS2812_COMPARE_PULSE_LOGIC_1 : _BC_WS2812_COMPARE_PULSE_LOGIC_0;
//    *bit_buffer_offset++ = (red & 0x02) ? _BC_WS2812_COMPARE_PULSE_LOGIC_1 : _BC_WS2812_COMPARE_PULSE_LOGIC_0;
//    *bit_buffer_offset++ = (red & 0x01) ? _BC_WS2812_COMPARE_PULSE_LOGIC_1 : _BC_WS2812_COMPARE_PULSE_LOGIC_0;
//
//    *bit_buffer_offset++ = (blue & 0x80) ? _BC_WS2812_COMPARE_PULSE_LOGIC_1 : _BC_WS2812_COMPARE_PULSE_LOGIC_0;
//    *bit_buffer_offset++ = (blue & 0x40) ? _BC_WS2812_COMPARE_PULSE_LOGIC_1 : _BC_WS2812_COMPARE_PULSE_LOGIC_0;
//    *bit_buffer_offset++ = (blue & 0x20) ? _BC_WS2812_COMPARE_PULSE_LOGIC_1 : _BC_WS2812_COMPARE_PULSE_LOGIC_0;
//    *bit_buffer_offset++ = (blue & 0x10) ? _BC_WS2812_COMPARE_PULSE_LOGIC_1 : _BC_WS2812_COMPARE_PULSE_LOGIC_0;
//    *bit_buffer_offset++ = (blue & 0x08) ? _BC_WS2812_COMPARE_PULSE_LOGIC_1 : _BC_WS2812_COMPARE_PULSE_LOGIC_0;
//    *bit_buffer_offset++ = (blue & 0x04) ? _BC_WS2812_COMPARE_PULSE_LOGIC_1 : _BC_WS2812_COMPARE_PULSE_LOGIC_0;
//    *bit_buffer_offset++ = (blue & 0x02) ? _BC_WS2812_COMPARE_PULSE_LOGIC_1 : _BC_WS2812_COMPARE_PULSE_LOGIC_0;
//    *bit_buffer_offset++ = (blue & 0x01) ? _BC_WS2812_COMPARE_PULSE_LOGIC_1 : _BC_WS2812_COMPARE_PULSE_LOGIC_0;
//
//    if (_bc_ws2812b.type == BC_WS2812B_TYPE_RGBW)
//    {
//        *bit_buffer_offset++ = (white & 0x80) ? _BC_WS2812_COMPARE_PULSE_LOGIC_1 : _BC_WS2812_COMPARE_PULSE_LOGIC_0;
//        *bit_buffer_offset++ = (white & 0x40) ? _BC_WS2812_COMPARE_PULSE_LOGIC_1 : _BC_WS2812_COMPARE_PULSE_LOGIC_0;
//        *bit_buffer_offset++ = (white & 0x20) ? _BC_WS2812_COMPARE_PULSE_LOGIC_1 : _BC_WS2812_COMPARE_PULSE_LOGIC_0;
//        *bit_buffer_offset++ = (white & 0x10) ? _BC_WS2812_COMPARE_PULSE_LOGIC_1 : _BC_WS2812_COMPARE_PULSE_LOGIC_0;
//        *bit_buffer_offset++ = (white & 0x08) ? _BC_WS2812_COMPARE_PULSE_LOGIC_1 : _BC_WS2812_COMPARE_PULSE_LOGIC_0;
//        *bit_buffer_offset++ = (white & 0x04) ? _BC_WS2812_COMPARE_PULSE_LOGIC_1 : _BC_WS2812_COMPARE_PULSE_LOGIC_0;
//        *bit_buffer_offset++ = (white & 0x02) ? _BC_WS2812_COMPARE_PULSE_LOGIC_1 : _BC_WS2812_COMPARE_PULSE_LOGIC_0;
//        *bit_buffer_offset++ = (white & 0x01) ? _BC_WS2812_COMPARE_PULSE_LOGIC_1 : _BC_WS2812_COMPARE_PULSE_LOGIC_0;
//    }

}

bool bc_ws2812b_init(void *dma_bit_buffer, bc_ws2812b_type_t type, uint16_t count)
{
	_bc_ws2812b.type = type;
	_bc_ws2812b.count = count;

	_bc_ws2812b.dma_bit_buffer_size = _bc_ws2812b.count * _bc_ws2812b.type * 8;
	_bc_ws2812b.dma_bit_buffer = dma_bit_buffer;

	memset(_bc_ws2812b.dma_bit_buffer, _BC_WS2812_COMPARE_PULSE_LOGIC_0, _bc_ws2812b.dma_bit_buffer_size);

	__HAL_RCC_GPIOA_CLK_ENABLE();

	//Init pin
    GPIO_InitTypeDef GPIO_InitStruct;
    GPIO_InitStruct.Pin = _BC_WS2812_BC_WS2812B_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Alternate = GPIO_AF2_TIM2;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(_BC_WS2812_BC_WS2812B_PORT, &GPIO_InitStruct);


    //Init dma
    __HAL_RCC_DMA1_CLK_ENABLE();

	_bc_ws2812b_dma_update.Init.Direction = DMA_MEMORY_TO_PERIPH;
	_bc_ws2812b_dma_update.Init.PeriphInc = DMA_PINC_DISABLE;
	_bc_ws2812b_dma_update.Init.MemInc = DMA_MINC_ENABLE;
	_bc_ws2812b_dma_update.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
	_bc_ws2812b_dma_update.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
	_bc_ws2812b_dma_update.Init.Mode = DMA_CIRCULAR; //TODO DMA_NORMAL
	_bc_ws2812b_dma_update.Init.Priority = DMA_PRIORITY_VERY_HIGH;
	_bc_ws2812b_dma_update.Instance = DMA1_Channel2;
	_bc_ws2812b_dma_update.Init.Request = DMA_REQUEST_8;

	_bc_ws2812b_dma_update.XferCpltCallback = _bc_ws2812b_dma_transfer_complete_handler;
	//dmaUpdate.XferHalfCpltCallback = dma_transfer_half_handler;

	__HAL_LINKDMA(&_bc_ws2812b_timer2_handle, hdma[TIM_DMA_ID_UPDATE], _bc_ws2812b_dma_update);


	HAL_DMA_Init(&_bc_ws2812b_dma_update);

	HAL_NVIC_SetPriority(DMA1_Channel2_3_IRQn, 0, 0);
	HAL_NVIC_EnableIRQ(DMA1_Channel2_3_IRQn);


	//HAL_DMA_Start_IT(&dmaUpdate, (uint32_t) dma_bit_buffer, (uint32_t) &(TIM2->CCR2), BUFFER_SIZE);
	HAL_DMA_Start_IT(&_bc_ws2812b_dma_update, (uint32_t) _bc_ws2812b.dma_bit_buffer, (uint32_t) &(TIM2->CCR2), _bc_ws2812b.dma_bit_buffer_size);

	 // TIM2 Periph clock enable
	__HAL_RCC_TIM2_CLK_ENABLE();

	_bc_ws2812b_timer2_handle.Instance = TIM2;
	_bc_ws2812b_timer2_handle.Init.Period = _BC_WS2812_TIMER_PERIOD;
	_bc_ws2812b_timer2_handle.Init.Prescaler = 0x00;
	_bc_ws2812b_timer2_handle.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
	_bc_ws2812b_timer2_handle.Init.CounterMode = TIM_COUNTERMODE_UP;
	HAL_TIM_PWM_Init(&_bc_ws2812b_timer2_handle);

	HAL_NVIC_SetPriority(TIM2_IRQn, 0, 0);
	HAL_NVIC_EnableIRQ(TIM2_IRQn);

	_bc_ws2812b_timer2_oc1.OCMode = TIM_OCMODE_PWM1;
	_bc_ws2812b_timer2_oc1.OCPolarity = TIM_OCPOLARITY_HIGH;
	_bc_ws2812b_timer2_oc1.Pulse = _BC_WS2812_COMPARE_PULSE_LOGIC_0;
	_bc_ws2812b_timer2_oc1.OCFastMode = TIM_OCFAST_DISABLE;
	HAL_TIM_PWM_ConfigChannel(&_bc_ws2812b_timer2_handle, &_bc_ws2812b_timer2_oc1, TIM_CHANNEL_2);

	HAL_TIM_PWM_Start(&_bc_ws2812b_timer2_handle, TIM_CHANNEL_2);

	TIM2->DCR = TIM_DMABASE_CCR2 | TIM_DMABURSTLENGTH_1TRANSFER;


	_bc_ws2812b.task_id = bc_scheduler_register(_bc_ws2812b_task, NULL, BC_TICK_INFINITY);

	_bc_ws2812b.transfer = 0;

	return true;
}

void bc_ws2812b_set_event_handler(void (*event_handler)(bc_ws2812b_event_t, void *), void *event_param)
{
	_bc_ws2812b.event_handler = event_handler;
	_bc_ws2812b.event_param = event_param;
}

static void _bc_ws2812b_task(void *param)
{
	if (_bc_ws2812b.event_handler != NULL)
	{
		_bc_ws2812b.event_handler(BC_WS2812B_SEND_DONE, _bc_ws2812b.event_param);
	}
}

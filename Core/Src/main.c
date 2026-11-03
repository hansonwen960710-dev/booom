#include "main.h"

#define LED_ON  GPIO_PIN_SET
#define LED_OFF GPIO_PIN_RESET

typedef enum {
    GAME_PLAYING,
    GAME_WIN,
    GAME_EXPLODED
} GameState_t;

/* ---------------- 全域 Handles & 宣告 ---------------- */
TIM_HandleTypeDef htim3;

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM3_Init(void);

/* ---------------- 狀態與變數 ---------------- */
volatile GameState_t game_state = GAME_PLAYING;
volatile uint8_t progress = 0;
volatile uint8_t current_sweep_idx = 0;
volatile uint8_t button_pending = 0;
volatile uint32_t debounce_start_tick = 0;

const uint16_t SWEEP_PINS[5] = {GPIO_PIN_0, GPIO_PIN_1, GPIO_PIN_4, GPIO_PIN_5, GPIO_PIN_6};
GPIO_TypeDef* SWEEP_PORTS[5] = {GPIOA, GPIOA, GPIOA, GPIOA, GPIOA};

const uint16_t PROGRESS_PINS[4] = {GPIO_PIN_0, GPIO_PIN_1, GPIO_PIN_2, GPIO_PIN_10};
GPIO_TypeDef* PROGRESS_PORTS[4] = {GPIOB, GPIOB, GPIOB, GPIOB};

/* ---------------- EXTI 按鍵中斷 ---------------- */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == GPIO_PIN_13 && !button_pending)
    {
        HAL_NVIC_DisableIRQ(EXTI15_10_IRQn);
        button_pending = 1;
        debounce_start_tick = HAL_GetTick();
    }
}

/* ---------------- GPIO LED 輸出控制 ---------------- */
void Update_Progress_LEDs(uint8_t count)
{
    for (int i = 0; i < 4; i++) {
        HAL_GPIO_WritePin(PROGRESS_PORTS[i], PROGRESS_PINS[i],
                          (i < count) ? LED_ON : LED_OFF);
    }
}

void Update_Sweep_LEDs(uint8_t active_idx)
{
    for (int i = 0; i < 5; i++) {
        HAL_GPIO_WritePin(SWEEP_PORTS[i], SWEEP_PINS[i],
                          (i == active_idx) ? LED_ON : LED_OFF);
    }
}

/* ---------------- 主程式 ---------------- */
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_TIM3_Init();

    // 啟動 PB4 (TIM3_CH1) PWM 輸出
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);

    int8_t sweep_dir = 1;
    uint32_t sweep_speed = 120;
    const uint32_t MAX_GAME_TIME = 25000; // 25 秒限時

    Update_Sweep_LEDs(current_sweep_idx);
    Update_Progress_LEDs(progress);

    uint32_t game_start_time = HAL_GetTick();
    uint32_t last_sweep_time = HAL_GetTick();

    while (1)
    {
        uint32_t current_time = HAL_GetTick();

        // 1. 按鍵防彈跳處理
        if (button_pending && (current_time - debounce_start_tick >= 20))
        {
            button_pending = 0;

            if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_RESET)
            {
                if (game_state == GAME_PLAYING)
                {
                    if (current_sweep_idx == 2) // PA4 中間標靶
                    {
                        progress++;
                        if (progress >= 4) game_state = GAME_WIN;
                    }
                    else
                    {
                        game_state = GAME_EXPLODED;
                    }
                }
            }

            __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_13);
            HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
        }

        // 2. 遊戲進行邏輯
        if (game_state == GAME_PLAYING)
        {
            if (current_time - game_start_time > MAX_GAME_TIME)
            {
                game_state = GAME_EXPLODED;
            }

            // --- PWM 炸彈呼吸燈漸變控制 ---
            uint32_t elapsed = current_time - game_start_time;

            // 呼吸燈週期隨時間與進度縮短 (從 1200ms 加速至 180ms)
            int32_t breath_period = 1200 - (elapsed / 25) - (progress * 150);
            if (breath_period < 180) breath_period = 180;

            // 計算當前 Phase 並輸出三角波 PWM Duty (0 ~ 999)
            uint32_t phase = current_time % breath_period;
            uint32_t pwm_duty = 0;
            if (phase < (uint32_t)breath_period / 2) {
                pwm_duty = (phase * 999) / (breath_period / 2);
            } else {
                pwm_duty = ((breath_period - phase) * 999) / (breath_period / 2);
            }

            // 更新 TIM3 CH1 暫存器 (CCR1) 改變炸彈 LED 亮度
            __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, pwm_duty);

            // 掃蕩燈來回移動
            if (current_time - last_sweep_time >= sweep_speed)
            {
                last_sweep_time = current_time;
                current_sweep_idx += sweep_dir;

                if (current_sweep_idx >= 4) {
                    current_sweep_idx = 4;
                    sweep_dir = -1;
                }
                else if (current_sweep_idx <= 0) {
                    current_sweep_idx = 0;
                    sweep_dir = 1;
                }

                Update_Sweep_LEDs(current_sweep_idx);
            }

            Update_Progress_LEDs(progress);
        }
        else if (game_state == GAME_EXPLODED)
        {
            // 爆炸：PWM 調整為 100% Duty Cycle (999) 恆亮
            __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 999);
            Update_Sweep_LEDs(5); // 關閉掃蕩燈
        }
        else if (game_state == GAME_WIN)
        {
            // 拆彈成功：PWM 調整為 0% Duty Cycle (0) 熄滅
            __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 0);
            Update_Progress_LEDs(4);

            if (current_time - last_sweep_time >= 200) {
                last_sweep_time = current_time;
                HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6);
            }
        }
    }
}

/* ---------------- TIM3 PWM 初始化 ---------------- */
static void MX_TIM3_Init(void)
{
    TIM_ClockConfigTypeDef sClockSourceConfig = {0};
    TIM_MasterConfigTypeDef sMasterConfig = {0};
    TIM_OC_InitTypeDef sConfigOC = {0};

    htim3.Instance = TIM3;
    htim3.Init.Prescaler = 159;                      // 160MHz / 160 = 1MHz
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim3.Init.Period = 999;                          // 1MHz / 1000 = 1kHz PWM 頻率
    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_Base_Init(&htim3);

    sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
    HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig);
    HAL_TIM_PWM_Init(&htim3);

    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig);

    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 0;                              // 預設 Duty Cycle 0
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1);

    __HAL_RCC_TIM3_CLK_ENABLE();
}

/* ---------------- GPIO 初始化 ---------------- */
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6, LED_OFF);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_10, LED_OFF);

    // PA0, PA1, PA4, PA5, PA6 -> 掃蕩 LED
    GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    // PB0, PB1, PB2, PB10 -> 進度 LED
    GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    // PB4 -> TIM3_CH1 PWM 輸出 ( Alternate Function 2 )
    GPIO_InitStruct.Pin = GPIO_PIN_4;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF2_TIM3;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    // PC13 -> EXTI 按鈕
    GPIO_InitStruct.Pin = GPIO_PIN_13;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    HAL_NVIC_SetPriority(EXTI15_10_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}

/* ---------------- 系統時脈設定 ---------------- */
__attribute__((weak)) void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV1;
    RCC_OscInitStruct.PLL.PLLN = 20;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
    RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4);
}

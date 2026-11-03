# booom
期中
## 系統架構

### 有限狀態機 (FSM)

```
GAME_PLAYING ──(EXTI13 按鈕中斷)──► 判定 index == PA4 ?
   ▲                                   ├─ 是 (HIT)  → 進度條 +1，掃描燈與呼吸燈加速 ──┐
   └───────────────────────────────────┘                                              │
                                       └─ 否 (MISS) / 倒數超時 → GAME_FAIL (炸彈爆炸)
```

- `GAME_PLAYING`：依目前階段的間隔來回切換掃描燈。
- 按鈕事件判定：按下按鈕觸發 EXTI 中斷，檢查目前掃描位置是否為中間標靶。
- `GAME_FAIL`：炸彈燈恆亮／急閃。

### 使用的周邊與技術

| 項目 | 說明 |
|---|---|
| GPIO 輸出 | 掃描燈、進度條，以 BSRR 設／清 (`MODER = 01`) |
| GPIO 輸入 + EXTI | PC13 (B1) 下降緣觸發中斷 (`MODER = 00`、SYSCFG EXTICR4、`EXTI15_10_IRQn`) |
| 複用功能 AF | PB4 輸出 TIM3_CH1 (`MODER = 10`、AF2) |
| TIM3 PWM | 調整 CCR1 占空比與週期，產生呼吸燈並隨階段加速 |
| 時脈 | HSI 16 MHz，SysTick 1 ms Tick 作為計時與跑馬燈頻率基準 |
| 軟體去彈跳 | 以時間戳判斷：`now - last_btn >= 20 ms` 才視為有效按壓 |
| 未使用 | UART、Input Capture |

## 建置與執行

1. 使用 STM32CubeIDE 或 CubeMX 開啟／建立 NUCLEO-G474RE 專案。
2. 依上表設定 GPIO、TIM3 (CH1 PWM, PB4) 與 PC13 的 EXTI（下降緣）。
3. 編譯並透過板載 ST-LINK 燒錄。
4. 上電後掃描燈開始流動，在亮燈位於 `PA4` 時按下 B1 即為命中。

## 專案文件

- 簡報：`STM32G474RE_拆彈定時反應遊戲簡報_修改版.pptx`

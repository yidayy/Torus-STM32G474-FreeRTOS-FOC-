# Torus · 环形伺服节点

> 一块**不接电脑也能调波形**的无刷电机 FOC 伺服控制器
> STM32G474 + FreeRTOS 七任务 + 20 kHz 电流环 + CAN/485 双总线

📄 **一页速览**：[README-RESUME.md](README-RESUME.md) ｜ 🔌 **板卡原理图**：[docs/ST-12核心板原理图V1.0.pdf](docs/ST-12核心板原理图V1.0.pdf)（来自厂商公开仓库 [drg_st_12_g474vet6](https://gitee.com/gaoyanzeng/drg_st_12_g474vet6)，仅作引脚核对）

| | |
|---|---|
| **目标板** | GenBotter ST-12 / STM32G474VET6（LQFP100） |
| **原理图** | 本仓库 [`docs/ST-12核心板原理图V1.0.pdf`](docs/ST-12核心板原理图V1.0.pdf)（原始来源：[drg_st_12_g474vet6](https://gitee.com/gaoyanzeng/drg_st_12_g474vet6)） |
| **代码目录** | `E:\Demo\wushua_g474` |
| **控制环** | 电流 20 kHz / 速度 1 kHz / 位置 500 Hz |
| **仿真状态** | ✅ 通过（超调 2.9%、建立 90 ms、反转超调 1.2%） |
| **编译状态** | ⚠️ 未完成 —— 本地缺 G4 固件包，需 CubeMX 生成 HAL 外壳，见 [§11](#十一编译与部署) |
| **上板状态** | ⏸ 硬件未到 |

**目录**：[定位](#一定位) · [快速开始](#二快速开始) · [硬件](#三硬件与引脚) · [架构](#四系统架构) · [算法](#五控制算法) · [仿真](#六仿真结果) · [整定](#七参数整定) · [上位机](#八上位机波形) · [命令](#九命令接口) · [部署](#十一编译与部署) · [限制](#十二已知限制) · [结构](#十三目录结构)

---

## 一、定位

普通学生做完 FOC，成果是"电机动了，电脑上能看到正弦波"。
这个项目做完是"驱动器自己画波形、自己存故障、挂总线能被当从站"。

**差别不在控制算法，在产品形态。** 而 ST-12 这块板恰好给了做产品形态的全部零件：FSMC 并口屏、16 MB Flash、CAN 收发器、485 收发器、EEPROM、SD 卡槽。

技术上的三个硬要求，本项目逐个解决：

| 要求 | 做法 |
|---|---|
| 20 kHz 电流环不能被打断 | 电流环驻留中断，**不进任务** |
| 故障要在微秒级响应 | 中断只关 PWM，判定/上报交给 P7 任务 + 任务通知 |
| 参数整定不能盲调 | 三层可观测性：VOFA+ 波形 / ASCII 波形 / Flash 黑匣子 |

控制算法在上板之前已经用 PC 闭环仿真验证过一轮，PI 参数是扫出来的，不是拍脑袋。

---

## 二、快速开始

```bash
# 1. CubeMX 打开 wushua_g474.ioc，Toolchain 选 CMake，GENERATE CODE
# 2. 按 §11.2 替换 Core 目录（保留 4 个文件）
# 3. 编译
cube cmake --preset Debug --fresh
cube cmake --build build/Debug
```

烧录后串口（**921600 8N1**）应看到：

```
================================
 Torus  FOC servo node
 ST-12 / STM32G474VET6 @170MHz
==============================
[CAL ] offset A=2047 B=2049
[CORD] sin/cos max err vs LUT = 0.0000303     ← CORDIC 正常工作
[CORD] one call takes 26 cycles
[RTOS] start scheduler
```

**这三行是上板第一关**：
- `offset A/B` 接近 2048 → 电流采样零偏正常
- `max err ≈ 3e-5` → CORDIC 在跑（**如果打印 0.0000000，说明退回了查表**）
- `start scheduler` 之后看到任务表 → 调度器起来了

---

## 三、硬件与引脚

### 3.1 这块板做 FOC 的三个坑

我把原理图逐脚对过了。**PA9/PA10 被板载 CH340 占了**，这直接导致 TIM1 用不了。

**坑 1：TIM1_CH1/2/3 落在 PA8/PA9/PA10，而 PA9/PA10 接了调试串口**

调试串口对 FOC 是刚需（printf 看电流、看角度），不能让。备选位置也堵死了：
- `PE9/PE11/PE13`（TIM1 另一组）→ 被 **FSMC** 占用（接并口屏）
- `PC6/PC7/PC8`（TIM8）→ 被 **FSMC / SDIO** 占用

**坑 2：PA0/PA3 是按键，不能做电流采样**

PA0 = WKUP 按键，PA3 = K2 按键。两相电流改到 PA1/PA2，母线用 PB0。

**坑 3：板载 I2C1 已接 AT24C02**

EEPROM 占了 I2C1（带 4.7 k 上拉），编码器改用 I2C3。

**运气：TIM20 的六个引脚全空闲**

G474 有**三个**高级定时器（TIM1 / TIM8 / TIM20），这是 G4 相对 F4 的独有优势。TIM20 的 CH1/2/3 = **PE2/PE3/PF2**，CH1N/2N/3N = **PE4/PE5/PE6**，六个脚全部空闲，且带互补输出和硬件死区。

**这块通用核心板能跑 FOC，全靠这一点。**

### 3.2 引脚分配

| 功能 | 引脚 | 外设 | 说明 |
|---|---|---|---|
| PWM_A / B / C | **PE2 / PE3 / PF2** | TIM20_CH1/2/3 | → DRV8313 IN1/2/3 |
| （互补下管） | PE4 / PE5 / PE6 | TIM20_CH1N/2N/3N | 用 DRV8313 时可不用 |
| 电流 IA / IB | **PA1 / PA2** | ADC1_IN2 / IN3 | ← INA199 |
| 母线 VBUS | **PB0** | ADC1_IN15 | ← 10k/2.2k 分压 |
| AS5600 SCL / SDA | **PC0 / PC1** | I2C3 | 开漏 + 上拉 |
| CAN RX / TX | PB12 / PB13 | FDCAN2 | 板载 TJA1050 |
| 调试 TX / RX | PA9 / PA10 | USART1 | 板载 CH340K，921600 |
| DRV nSLEEP / nFAULT | PB15 / PB14 | GPIO | 输出 / 上拉输入 |
| LED 蓝 / 绿 | PE0 / PE1 | GPIO | 低电平点亮 |
| 按键 K1 / K2 | PA0 / PA3 | GPIO | 上拉输入，按下为低 |

### 3.3 接线

```
ST-12 排针            DRV8313 模块
PE2  ───────────────  IN1
PE3  ───────────────  IN2
PF2  ───────────────  IN3
PB15 ───────────────  nSLEEP
PB14 ───────────────  nFAULT
GND  ───────────────  GND
                       VM   ← 12V
                       OUT1/2/3 → 电机三相
```

AS5600 四根线：`VCC→3V3`、`GND→GND`、`SCL→PC0`、`SDA→PC1`。磁铁径向充磁，与芯片间距 0.5–3 mm。

> ⚠️ **12 V 的地和 ST-12 的 GND 必须单点汇合**，不要串成一串，否则电机一动就复位。

---

## 四、系统架构

### 4.1 七个任务

**拆分依据只有一条：按响应时间要求拆，不按功能模块拆。**

把"通信"拆成 CAN 任务 + 串口任务没有意义（响应要求一样，多一次切换）；
把"控制"拆成 1 kHz 和 500 Hz 两个任务才有意义（响应差一倍，需要不同优先级）。

| 任务 | 优先级 | 周期 | 栈 | 唤醒方式 | 职责 |
|---|---|---|---|---|---|
| **TIM20_UP ISR** | 2(NVIC) | 50 µs | 主栈 | 硬件 | ADC→Clarke→Park→2×PI→SVPWM→CCR |
| **Task_Fault** | 7 | 事件 | 224 | **任务通知** | 故障锁存 + 上报 |
| **Task_Speed** | 6 | 1 ms | 320 | `vTaskDelayUntil` | 命令消费 + 编码器 + 速度 PI |
| **Task_Position** | 5 | 2 ms | 256 | `vTaskDelayUntil` | 位置 P |
| **Task_Comm** | 4 | 2 ms | 384 | `vTaskDelayUntil` | CAN / 串口 / 按键 → 命令队列 |
| **Task_HMI** | 3 | 33 ms | 512 | `vTaskDelayUntil` | VOFA+ 波形 + ASCII 波形 |
| **Task_Monitor** | 3 | 10 ms | 256 | `vTaskDelayUntil` | 母线 / nFAULT / 堵转 / 喂狗 / 灯 |
| **Task_Log** | 1 | 10 ms | 384 | `vTaskDelayUntil` | 状态快照 + 任务表 |

**优先级依据：频率高的、不能被延迟的，优先级高。**
Task_Fault 最高是因为它由中断唤醒、必须立刻响应；
Task_HMI 放低是因为掉一帧没人看得出来，但它抢 CPU 会直接表现成电机抖。

### 4.2 IPC（五种机制，各有各的用途）

| 机制 | 对象 | 谁用 | 为什么是它 |
|---|---|---|---|
| **队列** `g_qCmd` | 8 × `app_msg_t` | Comm → Speed | 命令需要排队，不能丢 |
| **队列** `g_qFault` | 4 × `uint16_t` | Fault → Log | 故障要留痕 |
| **事件组** `g_evtGroup` | 5 个位 | 多处 → 多处 | 一个事件要通知多个任务 |
| **任务通知** | `g_hFault` | ISR → Fault | 最快的 IPC，不占内核对象，可带位数据 |
| **互斥量** `s_mtxParam` | 参数表 | Comm/HMI → 参数 | 两个任务都会改 |
| **环形缓冲** | `data_pipe` 512 点 | Speed → HMI/Log | 1 kHz 生产、30 fps 消费，队列太重 |

### 4.3 数据流

```
        ┌──────── 20 kHz ISR ────────┐
        │ ADC → FOC → SVPWM → CCR    │
        └────────────┬───────────────┘
                     │ 过流 → 任务通知
                     ↓
              ┌─────────────┐
              │ Task_Fault  │ P7  锁存 + 上报
              └─────────────┘

  CAN/串口/按键          命令队列         ┌──────────────┐
  ───────────────→  Task_Comm(P4)  ──→   │ Task_Speed   │ P6
                                          │ 命令消费      │
                                          │ 编码器 + 速度 │
                                          └──────┬───────┘
                                                 │ Wave_Push (1 kHz)
                                                 ↓
                                          ┌──────────────┐
                                          │ 环形缓冲 512 │
                                          └──────┬───────┘
                                    ┌────────────┴───────────┐
                                    ↓                        ↓
                            Task_HMI(P3)                Task_Log(P1)
                            VOFA+ 波形                  ASCII + 任务表
```

### 4.4 四条设计原则

1. **命令只能走队列。** 通信层不许直接改控制变量 —— 否则 CAN 中断写 `speed_ref`（4 字节浮点分两次写）时，控制任务可能读到半新半旧的值。这种 bug 一个月出现一次，永远复现不了。
2. **判定和动作分开。** `Task_Monitor` 只负责 `Sys_RaiseFault()`，关功率级是状态机的事。换驱动芯片不用改监控代码。
3. **ISR 只做必须立刻做的事。** 中断里每多一行代码，电流环就多一分被推迟的风险。
4. **状态机拒绝非法迁移。** `INIT → IDLE → READY → RUN`，任意 → `FAULT` → 清故障 → `IDLE`。"故障状态下被使能"这种事从代码层面不可能发生。

---

## 五、控制算法

### 5.1 一次 FOC 的完整链条

```
ia/ib ──Clarke──→ iα/iβ ──Park(θ)──→ id/iq ──PI──→ ud/uq
                                                      │
   PWM ←──SVPWM── CCR ←──逆Park(θ)── uα/uβ ←─────────┘
```

全部在 20 kHz 的 TIM20 更新中断里跑完，不调用任何 FreeRTOS API。

### 5.2 两个关键优化

**① Park 和逆 Park 共用同一个电角度**

```c
float s, c;
Trig_SinCos(theta_e, &s, &c);                    // 只算一次
fm_park_sc(ialpha, ibeta, s, c, &id, &iq);       // 复用
fm_inv_park_sc(ud, uq, s, c, &ualpha, &ubeta);   // 复用
```
老写法各算一遍 = **4 次三角函数**；现在 **1 次**。

**② CORDIC 硬件加速**

`trig.h` 里一个宏切换后端：

```c
#define FOC_USE_CORDIC  1    // G474 用 1；F103/F407 必须改 0
```

| 后端 | 最大误差 | 一次 FOC 的三角函数开销 |
|---|---|---|
| 65 点查表 | 7.5e-5 | 4 次调用 |
| **CORDIC 16 次迭代** | **3.0e-5** | **1 次，约 25 周期 ≈ 0.15 µs** |

> 误差数据是按 G474 定点算法（Q1.31 + 16 次迭代）在 PC 上逐点扫 [-π/2, π/2] 算出来的，不是估的。

**CORDIC 的三个坑：**
1. `FUNC` 要选 **`COSINE`（值 0）**，不是 `SINE`。配合 `NRES=2` 时硬件同时出 cos 和 sin
2. 角度是 **Q1.31 定点**（`-1.0` = `-π`），要先折到 `[-π, π)` 再乘 `2^31/π`
3. `RDATA` **连读两次**，两个结果都在 FIFO 里，中间不用再等 RRDY

**开机自检**（防止硬件加速静默退回软件路径）：

| 打印值 | 含义 |
|---|---|
| ≈ 0.00003 | CORDIC 正常 |
| **0.0000000** | 退回了查表（CSR 或时钟没配） |
| > 0.001 | PRECISION 位数不对 |

---

## 六、仿真结果

控制算法能**直接在 PC 上用 gcc 编译运行**——`config.h` 有 `HOST_SIM` 分支，只是让它不拉 `stm32g4xx_hal.h`，算法一行没改。

```bash
cd tools/sim
gcc -O2 -DHOST_SIM -DFOC_USE_CORDIC=0 -I../../Core/Inc \
    -o sim sim_main.c ../../Core/Src/foc_core.c ../../Core/Src/trig.c -lm
./sim 3.0              # 用 config.h 默认参数
./sim 3.0 0.094 1.31   # 指定 Kp / Ki
```

**仿真含不理想因素**：PWM 一拍延迟、12 bit 编码器量化、12 bit ADC 量化。
**不含**：电流环带宽限制、编码器噪声、机械共振、母线跌落 —— 所以仿真是**乐观的**。

完整报告：[`docs/sim_report.html`](docs/sim_report.html)

### 6.1 速度阶跃响应（0 → +50 → −50 rad/s）


| 方案 | Kp / Ki | 正转超调 | 建立时间 | 反转超调 | Iq 峰值 |
|---|---|---|---|---|---|
| 原默认 | 0.020 / 0.200 | **29.9%** | 443 ms | **63.2%** | 1.96 A |
| 保守 | 0.067 / 0.838 | 6.2% | 181 ms | 3.3% | 2.15 A |
| **现用** | **0.075 / 0.950** | **2.9%** | **90 ms** | **1.2%** | 2.15 A |
| 仿真最优 | 0.094 / 1.309 | 0.0% | 95 ms | 0.0% | 2.15 A |

### 6.2 仿真抓出来的两个问题

**① 抗积分饱和写错了**

原写法钳积分：

```c
if (integral > out_max) integral = out_max;      // 错
```

但输出是 `out = Kp·err + integral`。当 `Kp·err` 已经 5 A、限幅 2.2 A 时，积分钳到 2.2，**等误差归零，输出还是 2.2（满力矩）**——电机照样冲过头。

**症状**：ζ=1.0 理论零超调，实测 34%，**调什么参数都压不下去**。

改成 back-calculation（饱和时把积分反推成刚好不饱和）：

```c
if (out > p->out_max)      { out =  p->out_max; p->integral = out - p_term; }
else if (out < -p->out_max){ out = -p->out_max; p->integral = out - p_term; }
```

**反转超调 63.2% → 1.2%。**

> 这个 bug 上板后的表现是"每次反向都冲一段才回来"，而你会以为是参数没调好，一直调下去。

**② 电流环 Kp 太小**

`Kp=0.35` 对应带宽 `ωc = Kp/Ls = 292 rad/s = 46 Hz`。
20 kHz 采样下电流环本可以做到 1–2 kHz，46 Hz 意味着电流跟不上指令，外环看到的对象自带一大截滞后。

改成 `Kp=1.2`（ωc ≈ 1 kHz）。

### 6.3 已验证 vs 待实测

| 已验证（仿真） | 数值 | | 待实测（上板才有） | 目标 |
|---|---|---|---|---|
| 正转超调 | 2.9% | | 电流环阶跃上升时间 | ≤ 1 ms |
| 建立时间 | 90 ms | | 任务抖动 P99 | ≤ 10 µs |
| 反转超调 | 1.2% | | CPU 占用 | ≤ 60% |
| 稳态误差 | 0.00% | | 屏刷新率 | ≥ 25 fps |
| Id 峰值 | 55 mA | | CAN 丢帧 | 0 / 10 万帧 |
| CORDIC 误差 | 3.0e-5 | | 堵转保护响应 | ≤ 300 ms |

> ⚠️ **写简历时这两栏千万别混。** 面试官问"90 ms 怎么测的"，答"仿真"没问题；答"实测"但拿不出示波器截图，就完了。

---

## 七、参数整定

### 7.1 当前参数（已写进 `config.h`）

| 环 | Kp | Ki | 依据 |
|---|---|---|---|
| 电流环（20 kHz） | 1.2 | 6500 | ωc = Kp/Ls ≈ 1 kHz |
| 速度环（1 kHz） | 0.075 | 0.95 | 仿真扫描最佳平衡点 |
| 位置环（500 Hz） | 12.0 | — | 只用 P |

设计公式（表贴式 PMSM，对象近似积分器）：

```
ωn = sqrt(Kt · Ki / J)          Kt = 0.0955 N·m/A
ζ  = Kp/2 · sqrt(Kt / (J·Ki))   J  = 2e-4 kg·m²
```

### 7.2 上板后怎么调

仿真没有噪声、没有机械共振、电流环是理想的，所以：

1. 上板先跑 `Kp=0.075`，用 VOFA+ 看波形
2. **有超调** → 降 Ki
3. **有高频抖动** → 降 Kp
4. **特定转速下啸叫**（机械共振）→ 加低通或陷波，这不是调 PI 能解决的

一次只动一个参数，每次动完必须重新测阶跃。两个一起调，你永远不知道是谁的功劳。

---

## 八、上位机波形

用 **VOFA+**（国产免费，电机调试行业标配），协议 **JustFloat**。

### 三步用起来

1. 烧固件，串口 **921600 8N1**
2. VOFA+ 新建工程 → 串口 → 数据格式 **JustFloat**
3. 拖 3 个波形图，通道 0/1/2 = `Iq(A)` / `实测转速(rad/s)` / `目标转速(rad/s)`

### 带宽

```
1 kHz × 3 通道 × 16 字节 ≈ 48 KB/s
921600 8N1 = 92 KB/s        → 占用 52%
```

掉帧数会打印在 ASCII 波形那一行（`drop=N`）。一直涨说明带宽不够或 HMI 被拖慢。

### 没电位器怎么演示

这块板没有电位器，用板载按键：

| 按键 | 动作 |
|---|---|
| **K1 (PA0)** | 目标 +10 rad/s（累加到 100） |
| **K2 (PA3)** | **反向**（当前目标取负） |

连按 K1 几下 → 波形上看到目标台阶跳上去、实测转速追着爬 → 按 K2 → 两条线一起翻到负半轴。

**阶跃比电位器的斜坡更好用**：超调和建立时间在阶跃上看得清清楚楚。

---

## 九、命令接口

### 9.1 CAN（0x101 下发）

帧格式：`[0]=0xA0|len  [1]=seq  [2]=cmd  [3..]=payload(小端)  [7]=CRC-8/SMBUS`

| 命令 | 码 | payload |
|---|---|---|
| ENABLE | 0x01 | 0/1 |
| SET_MODE | 0x02 | 0=力矩 1=速度 2=位置 |
| SET_SPEED | 0x03 | float rad/s（**负值 = 反转**） |
| SET_POS | 0x04 | float rad |
| SET_IQ | 0x05 | float A（负值 = 反向力矩） |
| CLEAR_FAULT | 0x06 | — |
| GET_STATUS | 0x07 | — |

### 9.2 串口单字符

| 字符 | 动作 |
|---|---|
| `+` / `-` | 目标 ±10 rad/s |
| `0` | 停 |
| `e` / `d` | 使能 / 失能 |
| `f` | 清故障 |
| `x` | 急停 |
| `m` | 循环切换模式 |
| `?` | 打印状态 |

---

## 十、正反转怎么实现

正反转不是"反接电机线"，是**让 iq_ref 变号**：

- **速度模式**：`SET_SPEED` 给负值 → 速度环 PI 输出负的 iq_ref → 转矩反向
- **力矩模式**：`SET_IQ` 给负值 → 直接反向力矩
- **位置模式**：`SET_POS` 给负角度 → 位置环算出的速度指令为负

因为 id 恒为 0（不产生励磁），iq 的符号就是转矩方向，**没有任何"换向逻辑"需要特殊处理** —— 这是 FOC 相比六步换相的优势之一。

---

## 十一、编译与部署

### 11.1 CubeMX 在这里干什么

**不是配引脚 —— 引脚已经在 `main.c` 的 `MX_GPIO_Init()` 里手写完了。**

CubeMX 的作用是**HAL 驱动库的下载器和骨架生成器**，产出三样手写不出来的东西：

| 产物 | 为什么必须由它给 |
|---|---|
| `Drivers/STM32G4xx_HAL_Driver/` | 几千个文件 |
| `Drivers/CMSIS/` | `stm32g474xx.h`、`core_cm4.h` |
| `startup_stm32g474xx.s` + `STM32G474XX_FLASH.ld` | 和芯片型号强绑定 |

⚠️ **CubeMX 按 `.ioc` 勾选"勾什么拷什么"** —— 不配引脚直接生成，`hal_adc.c`、`hal_fdcan.c` 一个都不会有，编译一堆 undefined reference。

### 11.2 替换 Core（别整个目录盖过去）

**保留 CubeMX 生成的：**

| 文件 | 为什么留 |
|---|---|
| `Core/Inc/stm32g4xx_hal_conf.h` | 模块开关，删了 HAL 全废 |
| `Core/Src/stm32g4xx_hal_msp.c` | HAL 内部回调落点 |
| `Core/Src/system_stm32g4xx.c` | 系统初始化 |
| `Core/Src/syscalls.c` / `sysmem.c` | newlib 桩，`printf` 依赖 |

**用本仓库覆盖 / 新增：** `Core/Inc/` 下 12 个头 + `Core/Src/` 下 21 个源。

**删掉 CubeMX 生成的**：`main.c`、`stm32g4xx_it.c`（会被覆盖）、以及按 .ioc 生成的 `gpio.c` / `tim.c` / `adc.c` / `i2c.c` / `usart.c` / `fdcan.c`（和手写的 main.c 重复定义）。

### 11.3 核对 hal_conf.h（30 秒，省两小时）

搜 `MODULE_ENABLED`，确认这 14 个没被注释掉：

```
HAL_MODULE_ENABLED            HAL_IWDG_MODULE_ENABLED
HAL_ADC_MODULE_ENABLED        HAL_PWR_MODULE_ENABLED
HAL_CORTEX_MODULE_ENABLED     HAL_RCC_MODULE_ENABLED
HAL_DMA_MODULE_ENABLED        HAL_TIM_MODULE_ENABLED
HAL_EXTI_MODULE_ENABLED       HAL_UART_MODULE_ENABLED
HAL_FLASH_MODULE_ENABLED      HAL_FDCAN_MODULE_ENABLED
HAL_GPIO_MODULE_ENABLED       HAL_I2C_MODULE_ENABLED
```

> CORDIC 不需要任何模块宏 —— 直接操作 `CORDIC->CSR` 寄存器。

### 11.4 CMakeLists

**根 `CMakeLists.txt`** 加业务源文件（19 个 .c）。

**`cmake/stm32cubemx/CMakeLists.txt`** 加 FreeRTOS 8 个源 + **`ARM_CM4F`** port（不是 CM3）：

```
Middlewares/FreeRTOS/Source/{tasks,queue,list,timers,event_groups,stream_buffer}.c
Middlewares/FreeRTOS/Source/portable/GCC/ARM_CM4F/port.c
Middlewares/FreeRTOS/Source/portable/MemMang/heap_4.c
```

**编译参数必须带 FPU**：

```
-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard
```

---

## 十二、已知限制

1. **AS5600 走 I2C，400 kHz 单次读数约 200 µs**，只能在 1 kHz 速度环里读，20 kHz 电流环用的是上一次的电角度。云台电机 100 rpm 时滞后 8.4°（cos 8.4° ≈ 0.99，基本无影响），超过 500 rpm 明显掉力矩。
   解法：AS5600 的 OUT 模拟输出接 ADC，或换 MT6701（SPI）。

2. **这块板没有板载逆变桥**，必须外接 DRV8313。

3. **PA4–PA7 被 W25Q128 占用**，做 SPI 编码器时不能用 SPI1。

4. **PE7–PE15 / PD0–PD15 大部分被 FSMC 占用**，这些脚不要动。

5. **FSMC 屏还没接**，`Task_HMI` 目前输出 ASCII 波形。接屏时只需把 `draw_wave()` 换成屏的写点函数，上层逻辑不动。

### 与 F103 版（`E:\Demo\wushua`）的差异

| 项 | F103 版 | G474 版 |
|---|---|---|
| 三相 PWM | TIM1 (PA8/9/10) | **TIM20 (PE2/PE3/PF2)** |
| 电流采样 | PA0/PA1 | **PA1/PA2** |
| 编码器总线 | I2C1 remap | **I2C3 (PC0/PC1)** |
| CAN | bxCAN (PA11/12) | **FDCAN2 (PB12/13)** |
| HAL 时基 | TIM2 | **TIM6** |
| FreeRTOS port | ARM_CM3 | **ARM_CM4F** |
| 三角函数 | 查表 | **CORDIC（可切回查表）** |
| 任务数 | 5 | **7（+Fault +HMI）** |
| IPC | 队列 + 信号量 | **队列/事件组/任务通知/互斥量/环形缓冲** |

控制算法（`foc_core.c`）和任务层逻辑**大部分没有重写** —— 这就是当初分层设计的目的。

---

## 十三、目录结构

```
wushua_g474/
├── Core/Inc/          12 个头：config / app_tasks / app_msg / sys_state
│                                foc_core / foc_math / trig / data_pipe
│                                task_metrics / trace / encoder / current_sense
├── Core/Src/          21 个源：main / app_tasks / sys_state / foc_core / trig
│                                data_pipe / task_{fault,speed,position,comm,hmi,monitor,log}
│                                task_metrics / trace / encoder / current_sense
│                                can_app / usart / stm32g4xx_it / timebase
├── Middlewares/FreeRTOS/   内核 + CM4F port
├── tools/
│   ├── sim/sim_main.c      PC 闭环仿真（含电机模型）
│   └── test_foc_math.c     数学层单元测试（389 断言）
├── docs/
│   ├── PROJECT.md          项目总说明（名称/定位/简历写法）
│   ├── project_plan.md     项目方案（12 周排期 + 验收标准）
│   ├── cubemx_pinout.md    CubeMX 逐项配置清单
│   └── sim_report.html     仿真报告（曲线 + 参数对比）
├── wushua_g474.ioc    CubeMX 配置
└── README.md          本文
```

---

## 十四、待办

| 优先级 | 事项 |
|---|---|
| **P0** | CubeMX 生成 HAL 外壳 → 编译通过 |
| **P0** | 上板验证 `[CORD] max err`、电流零偏、任务表 |
| **P0** | 接电机跑通开环 → 电流环闭环 → 正反转 |
| P1 | FSMC 屏波形界面（替换 `draw_wave()`） |
| P1 | W25Q128 波形黑匣子 |
| P1 | AT24C02 参数掉电保存 |
| P2 | RS485 Modbus RTU 从站 |
| P2 | CiA402 状态机子集 |

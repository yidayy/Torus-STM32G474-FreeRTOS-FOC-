# Torus · 环形伺服节点（简历版）

> **一块不接电脑也能调波形的无刷电机 FOC 伺服控制器**
> STM32G474 + FreeRTOS 七任务 + 20 kHz 电流环 + CAN/485 双总线
> 个人项目 ｜ C ｜ 作者：张培正

| 阶段 | 状态 |
|---|---|
| **控制算法（PC 闭环仿真）** | ✅ 速度阶跃超调 **2.9%**，建立 **90 ms**，反转超调 **1.2%** |
| **固件编译** | ⚠️ 待 CubeMX 生成 HAL 外壳（本地无 G4 固件包） |
| **上板实测** | ⏸ 硬件未到，**故不写任何实测数字** |

---

## 1. 一句话定位

普通学生做完 FOC，成果是"电机动了、电脑上能看到正弦波"。
本项目做的是"**驱动器自己画波形、自己存故障、挂总线能被当从站**"——差别不在算法，在产品形态。

## 2. 关键数据（严格分区，不混写）

**已验证：PC 闭环仿真**（电机模型 Rs=6.5Ω / Ls=1.2mH / Kt=0.0955 / J=2e-4，含 PWM 一拍延迟 + 12bit ADC/编码器量化）

| 指标 | 原默认参数 | **本项目参数** |
|---|---|---|
| 正转超调 | 29.9% | **2.9%** |
| 建立时间 | 443 ms | **90 ms** |
| 反转超调 | 63.2% | **1.2%** |
| 稳态误差 | — | **0.00%** |
| Iq 峰值 | 1.96 A | 2.15 A |

**其他已验证项**

| 项 | 数值 |
|---|---|
| CORDIC 三角函数误差（16 次迭代，Q1.31） | **3.0e-5**（65 点查表 7.5e-5，提升 2.5×） |
| 单次 FOC 三角函数调用 | **4 次 → 1 次**（Park/逆 Park 共用 sin-cos） |
| 数学层单元测试 | **389 条断言**通过 |
| 电流环带宽 | 46 Hz → **≈1 kHz**（Kp 0.35→1.2，ωc=Kp/Ls） |

**待实测（上板才有的，简历不写）**：电流环上升 ≤1 ms ｜ 任务抖动 P99 ≤10 µs ｜ CPU ≤60% ｜ CAN 0 丢帧/10 万帧 ｜ 堵转保护 ≤300 ms

## 3. 我解决了什么问题

| 问题 | 做法 | 结果 |
|---|---|---|
| **20 kHz 电流环不能被打断** | 电流环驻留 TIM20 更新中断，**不进任务**、不调任何 RTOS API | 控制周期 50 µs 可预测 |
| **故障要微秒级响应** | 中断只关 PWM，判定与上报交给 P7 任务 + **任务通知** | ISR 里不跑业务代码 |
| **超调压不下去（ζ=1 仍 34%）** | 发现抗饱和写错：只钳积分，`out=Kp·err+integral` 时仍满力矩；改 **back-calculation** `integral = out - p_term` | 反转超调 **63.2%→1.2%** |
| **通用核心板没有 TIM1** | 原理图逐脚核对：PA9/PA10 被板载 CH340 占、TIM1 其余引脚被 FSMC/SDIO 占 → 改用 G4 独有的第三个高级定时器 **TIM20（PE2/PE3/PF2 + 互补死区）** | 这块板能跑 FOC，全靠这一点 |
| **四条三角函数太贵** | Park 与逆 Park 共用同一电角度，sin-cos 提出外算一次；再上 **CORDIC** 硬件加速 | 4 次调用 → 1 次（约 25 周期 ≈0.15 µs） |
| **参数读到半新半旧**（4 字节浮点分两次写） | **命令只能走队列**，通信层不许直接改控制变量 | 这类"一个月出现一次、永远复现不了"的 bug 从架构上消除 |
| **硬件加速静默失效** | 开机自检：CORDIC 与查表逐点比对，打印 `max err` | 打印 0.0000000 = 已退回查表，立刻能发现 |

## 4. 系统架构：按"响应时间"拆任务，不按"功能模块"拆

| 任务 | 优先级 | 周期 | 职责 |
|---|---|---|---|
| TIM20_UP ISR | NVIC 2 | 50 µs | ADC→Clarke→Park→2×PI→SVPWM→CCR |
| Task_Fault | 7 | 事件 | 故障锁存 + 上报（任务通知唤醒） |
| Task_Speed | 6 | 1 ms | 命令消费 + 编码器 + 速度 PI |
| Task_Position | 5 | 2 ms | 位置 P |
| Task_Comm | 4 | 2 ms | CAN / 串口 / 按键 → 命令队列 |
| Task_HMI | 3 | 33 ms | VOFA+ / ASCII 波形 |
| Task_Monitor | 3 | 10 ms | 母线 / nFAULT / 堵转 / 喂狗 |
| Task_Log | 1 | 10 ms | 状态快照 + 任务表 |

**五种 IPC 各用其长**：队列（命令排队）· 事件组（一事件通知多任务）· 任务通知（ISR→任务最快，零内核对象）· 互斥量（参数表被两个任务改）· 环形缓冲（1 kHz 生产 / 30 fps 消费，队列太重）

## 5. 控制链

```
ia/ib → Clarke → Park(θ) → 2×PI(id,iq) → 逆Park(θ) → SVPWM → CCR      20 kHz
                        ↑ CORDIC sin/cos（Q1.31，一次调用出两个值）
```

当前参数：电流环 `1.2 / 6500`（ωc≈1 kHz）｜ 速度环 `0.075 / 0.95`（仿真扫描平衡点）｜ 位置环 `P 12`

## 6. 可观测性（三层，参数不是盲调）

1. **VOFA+（JustFloat）** 921600 8N1，1 kHz×3 通道 ≈48 KB/s，占带宽 52%，掉帧数直接打印
2. **ASCII 波形**：串口单字符命令 `+ - 0 e d f x m ?` 现场操作
3. **Flash 黑匣子**（待做）：W25Q128 存故障前 10 秒波形

## 7. 通信接口

```
CAN 0x101：[0]=0xA0|len [1]=seq [2]=cmd [3..6]=payload(小端) [7]=CRC-8/SMBUS
命令：ENABLE / SET_MODE(力矩·速度·位置) / SET_SPEED / SET_POS / SET_IQ / CLEAR_FAULT / GET_STATUS
正反转 = iq_ref 变号，无任何"换向逻辑"——这是 FOC 相比六步换相的优势
```

## 8. 技术栈

`STM32G474VET6 @170MHz` · `FreeRTOS 7 任务（CM4F port + FPU）` · `FOC / SVPWM / 三环 PI` · `CORDIC` · `FDCAN2` · `USART 921600` · `I2C3(AS5600)` · `ADC+DMA` · `DRV8313` · `PC 端 gcc 仿真`

## 9. 复现

```bash
# 算法层：PC 上直接编译运行（config.h 有 HOST_SIM 分支，算法零改动）
cd tools/sim
gcc -O2 -DHOST_SIM -DFOC_USE_CORDIC=0 -I../../Core/Inc -o sim \
    sim_main.c ../../Core/Src/foc_core.c ../../Core/Src/trig.c -lm
./sim 3.0 0.075 0.95        # 仿真 3 秒，指定 Kp/Ki

# 固件层
# 1) CubeMX 打开 wushua_g474.ioc → CMake 工具链 → GENERATE CODE
# 2) 覆盖 Core（保留 hal_conf.h / hal_msp.c / system_stm32g4xx.c / syscalls.c）
# 3) cube cmake --preset Debug --fresh && cube cmake --build build/Debug
```

## 10. 已知限制（诚实清单）

- **编译未完成**：本地无 G4 固件包，需 CubeMX 生成 HAL 外壳；本文所有数字均来自 PC 仿真，**无一条实测数据**
- **AS5600 走 I2C 单次约 200 µs**，只能进 1 kHz 速度环；云台电机 100 rpm 时电角度滞后 8.4°（cos≈0.99 基本无感），>500 rpm 明显掉力矩。解法：模拟输出接 ADC，或换 MT6701（SPI）
- **FSMC 屏未接**：`Task_HMI` 暂输出 ASCII 波形，接屏只需替换 `draw_wave()`，上层不动
- **Modbus RTU 从站 / CiA402 状态机**：规划中，未实现
- 板上无逆变桥，需外接 DRV8313

---

<sub>详细技术过程见 `README.md`（14 章，含三个板级坑、CORDIC 三个坑、参数整定方法）；仿真报告见 `docs/sim_report.html`。</sub>

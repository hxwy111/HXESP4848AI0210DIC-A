# 蜂鸣器测试项目 (Buzzer Test)

## 项目说明

本项目用于测试 HXESP4848AI0210DIC-A 开发板上的蜂鸣器功能。

## 硬件连接

- **蜂鸣器类型**: 有源蜂鸣器（高电平即响，无需 PWM 方波）
- **控制方式**: 通过 I2C IO 扩展芯片 **TCA9554** 的 **P7** 引脚（原理图网络名 **Extend_IO8**）
- **IO 扩展芯片**: TCA9554，器件地址 **0x20**
- **I2C 总线**: I2C0，SCL = **GPIO40**，SDA = **GPIO41**（与 QMI8658 六轴 / 触摸屏共用总线）

> ⚠️ 蜂鸣器**不接在 GPIO0** 上。GPIO0 既是 ESP32-S3 的启动 strap 引脚，又接在自动下载电路上，不能当作普通输出使用。

## 功能特性

- ✅ 蜂鸣器间隔 1 秒响一次
- ✅ 每次响声持续 1 秒
- ✅ 循环工作，带计数显示
- ✅ 串口日志输出状态信息

## 工作逻辑

```
TCA9554 P7 输出高电平（响）→ 持续1秒 → 输出低电平（停）→ 停止1秒 → 循环
```

## 编译和烧录

### 1. 配置 ESP-IDF 环境
```bash
# 在 ESP-IDF 终端中执行
cd F:\softwork_2.0\HXESP4848AI0210DIC-A\ESP-IDF\08_Buzer_Test
```

### 2. 编译项目
```bash
idf.py build
```

### 3. 烧录到开发板
```bash
idf.py -p COMx flash monitor
```
(将 COMx 替换为实际的串口号，如 COM3)

## 串口输出示例

```
I (xxx) Buzzer_Test: Buzzer Test Started
I (xxx) Buzzer_Test: TCA9554 在线，配置寄存器 = 0xFF
I (xxx) Buzzer_Test: Buzzer ready (TCA9554 P7 / Extend_IO8)
I (xxx) Buzzer_Test: Buzzer ON (#1)
I (xxx) Buzzer_Test: Buzzer OFF
I (xxx) Buzzer_Test: Buzzer ON (#2)
...
```

## 代码说明

### 主要宏定义
- `TCA9554_ADDR`: 0x20 - IO 扩展芯片 I2C 地址
- `I2C_SCL_IO` / `I2C_SDA_IO`: GPIO40 / GPIO41 - I2C0 总线引脚
- `BUZZER_PIN`: 7 - TCA9554 的 P7 引脚
- `BEEP_ON_MS`: 1000ms - 蜂鸣器响的持续时间
- `BEEP_OFF_MS`: 1000ms - 蜂鸣器停的间隔时间

### 核心代码逻辑
1. **初始化**: 初始化 I2C0，探测 TCA9554 是否在线
2. **配置引脚**: 将 TCA9554 的 P7 设为输出（其余引脚保持不变）
3. **主循环**:
   - 写 TCA9554 输出寄存器使 P7 为高电平（蜂鸣器响）
   - 延时 1 秒
   - 写 P7 为低电平（蜂鸣器停）
   - 延时 1 秒
   - 重复上述过程

## 修改参数

如需修改蜂鸣器响的频率和持续时间，可在 `main/main.c` 中修改以下宏定义：

```c
#define BEEP_ON_MS      1000  // 修改此值改变响的持续时间
#define BEEP_OFF_MS     1000  // 修改此值改变停的间隔时间
```

例如：
- 快速短响：`BEEP_ON_MS = 100`, `BEEP_OFF_MS = 100`
- 长响短停：`BEEP_ON_MS = 2000`, `BEEP_OFF_MS = 500`

## 项目结构

```
08_Buzer_Test/
├── CMakeLists.txt           # 项目根 CMake 配置
├── sdkconfig.defaults       # ESP-IDF 默认配置
├── README.md                # 本说明文档
└── main/
    ├── CMakeLists.txt       # main 组件 CMake 配置
    └── main.c               # 主程序源码
```

## 故障排除

1. **蜂鸣器不响**：
   - 确认串口日志是否打印 `TCA9554 在线`；若打印 `无响应`，检查 I2C 接线、供电与器件地址（0x20）
   - 用万用表测量 TCA9554 P7 电平是否随日志 ON/OFF 变化
   - 确认蜂鸣器为有源蜂鸣器（有源蜂鸣器需静态高电平，无源蜂鸣器才需要方波）

2. **串口无输出**：
   - 确认串口波特率设置为 115200
   - 检查 USB 串口驱动是否正常安装

3. **编译错误**：
   - 确保 ESP-IDF 环境已正确配置
   - 检查 ESP-IDF 版本是否兼容（建议 v5.x）

# ESP32 Legacy 固件归档说明

本目录为历史 Arduino/PlatformIO 实现归档，当前不再作为开发与发布来源。

## 使用规则

- 仅用于历史对照和问题追溯。
- 不再新增功能。
- 不再进行日常修复。
- 不再作为任何版本发布来源。

## 当前主线

请使用 [../esp32_idf](../esp32_idf) 作为唯一有效开发目录。

## 目录保留内容

- `fish_tank_controller.ino`
- `platformio.ini`
- `src/`

这些内容仅作为迁移历史参考，默认不参与构建流程。

## 最近同步记录（2026-04-09）

为保证双线联调一致性，legacy 分支已同步关键运行策略：

- WiFi 重连改为非阻塞推进，减少网页与按键卡顿
- DS18B20 改为非阻塞采样流程（12-bit, 760ms 转换窗口）
- 主循环采样改为 50ms 轮询 updateSensors，降低转换完成后的等待抖动
- 串口状态日志新增 SAMPLE / MAX / CONV_AGE 三项采样观测指标

说明：以上同步仅用于联调一致性，不改变“IDF 为唯一主线”的版本策略。

新增同步（2026-04-09）：

- SET 短按切换蜂鸣器，SET+UP/SET+DOWN 长按维护动作
- WiFi 保存后延时重连，网页进入重连监控并自动刷新
- WiFi 密码输入框不回显历史密码，留空提交不覆盖已保存密码
- 无有效 STA 凭据时上电直接进入 AP 配网模式
- PlatformIO 构建参数同步为 16MB Flash + PSRAM（见 platformio.ini：flash_size=16MB、default_16MB.csv、BOARD_HAS_PSRAM）

### 1. 准备环境

需要先安装：

- Arduino IDE 2.x
- ESP32 Arduino Core
- 依赖库：DallasTemperature、OneWire、Adafruit GFX Library、Adafruit SSD1306

### 2. 安装 ESP32 开发板支持包

打开 Arduino IDE：

1. 进入 文件 -> 首选项
2. 在“附加开发板管理器网址”里加入：

<https://espressif.github.io/arduino-esp32/package_esp32_index.json>

1. 打开 工具 -> 开发板 -> 开发板管理器
2. 搜索 esp32
3. 安装 Espressif Systems 提供的 ESP32 开发板包

### 3. 安装依赖库

打开 工具 -> 管理库，安装以下库：

- DallasTemperature
- OneWire
- Adafruit GFX Library
- Adafruit SSD1306

### 4. 打开工程文件

在 Arduino IDE 中打开：

- fish_tank_controller.ino

文件位置：

- [项目100L鱼缸温度控制器/firmware/esp32/fish_tank_controller.ino](项目100L鱼缸温度控制器/firmware/esp32/fish_tank_controller.ino)

### 5. 修改参数

编译前不再强制要求手动修改 WiFi 账号密码。

推荐做法：

- 首次烧录后若无有效 STA 凭据，设备会直接进入 AP 配网
- 连接 AP 后进入网页填写并保存你的 WiFi 参数

如你的硬件和默认假设不一致，还需要检查：

- RELAY_ACTIVE_LEVEL
- DisplayConfig::I2C_ADDRESS

当前会保存到掉电存储的参数：

- 目标温度 setpoint
- 温度回差 hysteresis
- 探头差值告警阈值 sensorDiffAlarm
- 蜂鸣器开关 buzzerEnabled
- WiFi SSID
- WiFi Password

### 6. 选择开发板和串口

在 Arduino IDE 顶部或工具菜单中选择：

- 开发板：ESP32S3 Dev Module 或 ESP32-S3-DevKitC-1 对应型号
- 端口：选择开发板对应的 COM 口

如果你不确定串口号，可以先拔掉开发板看一下端口列表，再插上开发板对比新增的 COM 口。

### 7. 编译

点击 Arduino IDE 左上角的“验证”按钮即可编译。

如果编译通过，说明代码和库依赖已经正常。

### 8. 烧录

点击“上传”按钮即可开始烧录。

正常情况下，ESP32 开发板会自动进入下载模式。

### 9. 如果无法自动进入下载模式

部分开发板需要手动操作：

1. 按住 BOOT 键不放
2. 点击上传
3. 当 Arduino IDE 出现正在连接或开始下载时，松开 BOOT 键

如果还是不行，再尝试：

1. 按住 BOOT
2. 短按一下 EN 或 RST
3. 松开 EN 或 RST
4. 保持 BOOT 直到开始写入后再松开

### 10. 查看运行结果

烧录完成后，可以用以下方式确认程序是否正常运行：

- 打开串口监视器，波特率设置为 115200
- 查看 OLED 单页是否显示 TC/SET、MODE、T1/T2、故障码、IP
- 查看继电器是否按温度条件切换
- 查看串口输出或访问设备 IP 对应网页

### 11. 网页访问

如果 WiFi 连接成功：

1. 打开串口监视器或看 OLED 底行 IP 信息
2. 在手机或电脑浏览器里输入该 IP
3. 访问首页查看状态

也可以访问：

- /status：查看 JSON 状态接口
- /set：通过 POST 修改目标温度
- /settings/control：通过网页表单修改目标温度、回差、探头差值告警阈值、蜂鸣器
- /settings/wifi：通过网页表单修改 WiFi 参数，保存后触发自动重连
- /settings：兼容入口，等效于 /settings/control

如果设备无法连接到已配置的路由器，会自动切换到 AP 配网模式：

- AP 名称格式：FishTankCtrl-Setup-xxxx
- AP 密码：12345678

此时连接该热点后，访问：

- 192.168.4.1

即可进入设置页重新填写 WiFi 参数。

如果 AP 模式下长时间没有客户端接入，系统会自动再次尝试切回已保存的 STA 网络。

### 12. 常见问题

#### 12.1 找不到串口

可能原因：

- USB 线只有供电没有数据
- CH340 或 CP210x 驱动未安装
- 开发板未正常上电

#### 12.2 编译报缺少库

说明依赖库没装全，回到“管理库”把缺少的库安装完整。

#### 12.3 烧录时报连接失败

优先检查：

- 端口是否选对
- 是否需要手动按 BOOT
- USB 驱动是否正常

#### 12.4 OLED 不亮

优先检查：

- VCC 是不是接对了 3.3V 或 5V
- SDA/SCL 是否接反
- I2C 地址是否是 0x3C 或 0x3D

#### 12.5 温度读不到

优先检查：

- DS18B20 是否接了 4.7k 上拉
- 探头供电是否正常
- GPIO4 和 GPIO16 是否接对

## VS Code 中用 PlatformIO 的步骤

如果你要在 VS Code 里完成编译、烧录、串口监视，按下面做。

当前这个工程位于一个更大的 STM32 资料工作区内部，不是工作区根目录。PlatformIO 扩展在这种嵌套工程场景下，可能出现类似“Cannot read properties of undefined (reading 'id')”的任务加载异常。

项目本身和 PlatformIO Core 配置是正常的。遇到这个报错时，优先用下面两种方式之一：

- 方式 1：直接把 [项目100L鱼缸温度控制器/firmware/esp32](项目100L鱼缸温度控制器/firmware/esp32) 单独作为 VS Code 工作区打开
- 方式 2：在当前大工作区中，直接运行已经配置好的 VS Code 任务，绕过 PlatformIO 扩展的项目任务发现

如果你选择方式 1，不要只是在当前大工作区里点“重新打开”。要明确打开 [fish-tank-esp32.code-workspace](fish-tank-esp32.code-workspace) 或直接只打开 [项目100L鱼缸温度控制器/firmware/esp32](项目100L鱼缸温度控制器/firmware/esp32) 这个目录。

### 1. 打开工程目录

在 VS Code 中打开目录：

- [项目100L鱼缸温度控制器/firmware/esp32](项目100L鱼缸温度控制器/firmware/esp32)

不要只打开单个 main.cpp 或 .ino 文件，最好直接打开 esp32 这个固件目录。

### 2. 让 PlatformIO 识别项目

因为这个目录下已经有：

- [platformio.ini](platformio.ini)

PlatformIO 会自动把它识别成工程。

### 3. 先检查参数

当前默认 WiFi 参数和控制常量位于：

- [src/app_context.h](src/app_context.h)

首次编译不是必须修改 WiFi，设备在 STA 连接失败后会自动切入 AP 配网模式。

如你的硬件和默认假设不一致，重点检查：

- DEFAULT_WIFI_SSID
- DEFAULT_WIFI_PASSWORD
- RELAY_ACTIVE_LEVEL
- DisplayConfig::I2C_ADDRESS

### 4. 编译

在 VS Code 下有几种方式：

1. 点击底部状态栏的 PlatformIO 对勾图标
2. 打开 PlatformIO 侧边栏，选择 Build
3. 在命令面板运行 PlatformIO: Build

如果出现“Cannot read properties of undefined (reading 'id')”，请不要继续点 PlatformIO 侧边栏按钮，改用 VS Code 任务：

1. 运行 Tasks: Run Task
2. 选择 FishTank: PlatformIO Build

### 5. 烧录

插上 ESP32 后：

1. 点击底部状态栏的 PlatformIO 右箭头上传图标
2. 或在命令面板运行 PlatformIO: Upload

如果 PlatformIO 扩展按钮报错，改用任务：

1. 运行 Tasks: Run Task
2. 选择 FishTank: PlatformIO Upload

如果自动下载失败，处理方式和 Arduino IDE 一样：

1. 按住 BOOT
2. 执行上传
3. 看到开始连接后再松开

### 6. 串口监视

上传成功后可以：

1. 点击底部状态栏的串口监视图标
2. 或运行 PlatformIO: Serial Monitor

如果扩展侧边栏不可用，改用任务：

1. 运行 Tasks: Run Task
2. 选择 FishTank: PlatformIO Monitor

当前串口波特率已经在 [platformio.ini](platformio.ini) 中设为 115200。

### 7. 为什么我建议用 PlatformIO

对你这个项目，PlatformIO 比单纯 Arduino 扩展更适合，原因是：

- 自动管理库依赖
- 更适合后续把程序拆成多个源文件
- 编译、烧录、监视串口都在一个工作流里
- 后面如果要加更多模块，维护成本更低

## Web 接口

- /：网页状态页
- /status：JSON 状态接口
- /set：POST 设置目标温度，参数名 value
- /settings/control：POST 保存控制参数，不触发 WiFi 重连
- /settings/wifi：POST 保存 WiFi 参数，并触发 WiFi 重连
- /settings：兼容控制参数接口（等效 /settings/control）

串口运行日志新增诊断字段：

- SAMPLE：最近一次有效样本周期（ms）
- MAX：启动以来观测到的最大样本周期（ms）
- CONV_AGE：当前 DS18B20 转换已等待时长（ms）

当前 /status 额外包含以下诊断字段：

- faultCode
- lastFaultText
- faultLatched
- faultDescription
- lowTempCutoff
- highTempCutoff
- heatRelayWearWarning
- coolRelayWearWarning
- totalHeatOnMs
- totalCoolOnMs
- alarmCount
- degradedCount
- faultStopCount
- heatRelaySwitchCount
- coolRelaySwitchCount

新增控制接口：

- /faults/reset：POST，清除锁定故障
- /stats/reset：POST，清零运行统计

当前网页表单在参数越界、缺少参数或保存成功时，会直接在页面顶部显示结果提示，不再只返回纯文本错误。

本地按键维护快捷键：

- 短按 SET：切换蜂鸣器开关
- 长按 SET + UP：清除锁定故障
- 长按 SET + DOWN：清零运行统计

OLED 当前为单页核心显示：

- TC/SET
- MODE 与 H/C 输出状态
- T1/T2
- 最近故障短码
- 网络 IP
- 按键提示（正常/锁定保护）

当前绝对温度硬保护阈值位于 [src/app_context.h](src/app_context.h)：

- ABS_LOW_TEMP_CUTOFF = 18.0C
- ABS_HIGH_TEMP_CUTOFF = 34.0C

当控制温度越过以上阈值时，系统会立即关闭加热和制冷输出，进入锁定停机状态。即使温度回到正常区间，也需要人工确认后通过网页执行一次故障复位。

当前还会累计保存继电器动作次数，达到 [src/app_context.h](src/app_context.h) 中的 RELAY_WARN_SWITCH_COUNT 阈值后，网页状态页和 JSON 接口会给出寿命预警。

网页状态页和 /status JSON 还会同时给出短故障码和中文故障说明，便于远程值守时快速判断故障含义。

## 首次联调文档

- [首次上电调试步骤.md](%E9%A6%96%E6%AC%A1%E4%B8%8A%E7%94%B5%E8%B0%83%E8%AF%95%E6%AD%A5%E9%AA%A4.md)

## 下一步建议

- 增加网页端参数校验提示和操作结果提示
- 将状态统计拆分为独立模块，进一步降低 main/runtime 耦合
- 增加实际 PlatformIO 全量编译与联调记录

其中网页端参数校验提示和操作结果提示已完成。

如果你下一步要我继续实现代码，我会直接在这个目录下创建可编译的 ESP32 工程文件。

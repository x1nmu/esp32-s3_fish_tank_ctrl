# ESP32 IDF C 版本说明

本目录是鱼缸温控器固件的唯一主线版本。
当前项目统一以 ESP-IDF 作为开发、编译、烧录和维护标准。

## 当前目录结构

- CMakeLists.txt：ESP-IDF 顶层工程文件
- sdkconfig.defaults：默认配置
- main/CMakeLists.txt：主组件构建文件
- main/app_config.h：引脚与应用参数配置
- main/app_state.h：运行状态结构定义
- main/app_core.c：运行时核心、状态机、按键、继电器控制、参数保存
- main/app_network.c：WiFi、AP 回退、HTTP 页面与接口
- main/app_oled.c：SH1106/SSD1306 兼容 I2C OLED 驱动与显示渲染
- main/main.c：主循环入口

## 当前已经迁移的内容

- GPIO 初始化
- 双 DS18B20 采样驱动
- I2C OLED 显示驱动
- 加热 / 制冷继电器控制与互锁
- 启动抑制、制冷最小关断保护、最小导通时间控制
- 双探头滤波、失效判定、降级运行与差值告警
- 高温 / 低温安全切断与故障锁存
- 本地三按键极简交互：UP/DOWN 调目标温度，SET 短按切换蜂鸣器，SET+UP/SET+DOWN 长按做维护操作
- NVS 参数掉电保存
- WiFi STA 联网与 AP 回退配网
- HTTP 状态页、JSON 状态接口、控制参数接口、WiFi 参数接口
- 运行事件日志
- 继电器动作统计、运行时长统计和维护预警
- 网页 AJAX 刷新与异步提交
- 设置拆分保存：控制参数保存不重连，WiFi 保存触发重连
- WiFi 非阻塞重连：连接发起与超时/AP 回退由维护任务周期推进
- DS18B20 非阻塞轮询采样：转换进行中返回进行态，不阻塞采样任务
- 采样任务节拍对齐：50ms 固定轮询（vTaskDelayUntil）降低采样抖动
- 串口采样观测指标：SAMPLE / MAX / CONV_AGE

## 当前仍需继续完善的部分

当前已经完成主要逻辑迁移，但仍有几点需要继续完善：

- OLED 字库目前是精简版，只覆盖当前状态页需要的字符
- DS18B20 驱动当前为位操作实现，后续可以继续做更完整的异常与时序优化
- Web 页面已支持 AJAX 刷新与异步提交，后续可继续优化视觉样式
- 已完成真实 ESP-IDF 构建验证，后续重点转为硬件联调与稳定性优化

如果你临时没有接好温度探头，也可以在：

- [main/app_config.h](main/app_config.h)

里把：

- `APP_USE_FAKE_SENSOR`

改成 `1`，用模拟温度先验证网页、状态机和显示。

## 如何编译

前提：

- 已安装 ESP-IDF
- 已在 ESP-IDF PowerShell 或 ESP-IDF 命令环境中打开本目录

当前目标板按 ESP32-S3-DevKitC-1 设计，引脚也已同步到 S3 版本。

在本目录执行：

1. `idf.py set-target esp32s3`
2. `idf.py build`

## 如何烧录

连接 ESP32 后执行：

1. `idf.py -p COMx flash`

如果想边烧录边看串口：

1. `idf.py -p COMx flash monitor`

其中 `COMx` 改成你的实际串口号。

## 关键配置

WiFi 和应用配置在：

- [main/app_config.h](main/app_config.h)

主要配置包括：

- `APP_WIFI_SSID`（默认空，首启建议走 AP 配网）
- `APP_WIFI_PASSWORD`（默认空）
- S3 引脚定义：OLED 8/9，蜂鸣器 15，按键 12/13/14
- 控温阈值、保护参数、AP 回退参数
- `APP_WIFI_SAVE_RECONNECT_DELAY_MS`（WiFi 保存后重连延时）

## 下一步建议

如果你继续沿着 C 版本做，合理顺序是：

1. 在真实硬件上验证双 DS18B20 读温、继电器动作和 OLED 刷新是否稳定
2. 跑一次完整的 `idf.py build` 和 `flash monitor`，确认没有 IDF 级链接或 menuconfig 依赖问题
3. 继续优化网页交互视觉与移动端排版
4. 根据实测结果优化 DS18B20 时序、OLED 字库和网络异常恢复策略

## Web 接口行为（当前版本）

- /：网页状态页
- /status：JSON 状态接口
- /set：POST 单独设置目标温度
- /settings/control：POST 保存控制参数（不触发 WiFi 重连）
- /settings/wifi：POST 保存 WiFi 参数（触发 WiFi 重连）
- /settings：兼容控制参数接口（等效 /settings/control）
- /faults/reset：POST 清除锁定故障
- /stats/reset：POST 清零运行统计

## 最近交互更新（当前版本）

- 网页 WiFi 密码不回显；留空表示不修改密码。
- WiFi 保存后前端进入重连监控并自动刷新，不需要手动复位。
- 无有效 STA 凭据时，系统直接启动 AP 配网，不再等待 STA 超时。
- 网页状态区已包含蜂鸣器开关状态展示。

## 串口运行指标（当前版本）

周期状态日志中新增以下字段，用于联调观察采样稳定性：

- SAMPLE：最近一次有效样本周期（ms）
- MAX：启动以来观测到的最大样本周期（ms）
- CONV_AGE：当前 DS18B20 转换已等待时长（ms）

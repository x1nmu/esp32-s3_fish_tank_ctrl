# esp32-s3_fish_tank_ctrl
基于esp32-s3的鱼缸温度控制器

## 项目目标

本项目用于实现一套鱼缸温度控制器，具备以下能力：

- 检测鱼缸当前水温
- 自动加热
- 自动制冷
- 本地显示运行状态
- 远程查看当前状态

## 项目推荐平台

推荐使用 ESP32 作为主控。

原因：

- 自带 WiFi，远程查看状态实现更直接
- GPIO 资源足够满足测温、显示、继电器控制等需求
- 相比 STM32 + WiFi 模块，硬件更简洁

如果后续必须切换为 STM32，也可以保留本项目结构，只需将主控更换为 STM32F103，并增加 WiFi 模块。

当前显示模块已经定版为 I2C 接口 OLED，后续原理图和程序默认都按该接口实现。

## 目录结构

- docs：方案设计和物料清单
- firmware/esp32_idf：唯一主线固件目录（ESP-IDF）
- firmware/esp32_legacy_arduino：历史 Arduino/PlatformIO 目录（只读归档，不再新增功能）
- hardware：原理图、接线图、硬件说明

## 固件开发规范

- 当前固件只在 [firmware/esp32_idf](firmware/esp32_idf) 持续开发、调试和发布。
- [firmware/esp32_legacy_arduino](firmware/esp32_legacy_arduino) 仅保留为历史参考，禁止新增功能和修复提交。
- 统一构建命令使用 ESP-IDF：idf.py build / idf.py -p COMx flash monitor。

## 当前输出

- 文档总入口（已去重）：[docs/README.md](docs/README.md)
- 软件架构文档（主文档）：[docs/软件架构设计.md](docs/%E8%BD%AF%E4%BB%B6%E6%9E%B6%E6%9E%84%E8%AE%BE%E8%AE%A1.md)
- 软件架构文档（HTML 版）：[docs/软件架构设计.html](docs/%E8%BD%AF%E4%BB%B6%E6%9E%B6%E6%9E%84%E8%AE%BE%E8%AE%A1.html)

## 快速开始（ESP-IDF 主线）

1. 进入目录：`firmware/esp32_idf`
2. 编译：`idf.py build`
3. 烧录监视：`idf.py -p COMx flash monitor`

说明：若在中文长路径下遇到构建问题，优先使用 `tools/build_ascii.ps1`。

## 建议下一步

1. 按交流负载版或低压直流版确定执行器硬件路线
2. 在原理图中补独立硬件过温保护、保险丝和浪涌防护
3. 按测试清单完成首轮联调和验收记录

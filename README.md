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

- 文档导航：[docs/README.md](docs/README.md)
- 总体方案：[docs/方案设计.md](docs/%E6%96%B9%E6%A1%88%E8%AE%BE%E8%AE%A1.md)
- 物料清单：[docs/物料清单.md](docs/%E7%89%A9%E6%96%99%E6%B8%85%E5%8D%95.md)
- 详细设计：[docs/模块接线与控制细化.md](docs/%E6%A8%A1%E5%9D%97%E6%8E%A5%E7%BA%BF%E4%B8%8E%E6%8E%A7%E5%88%B6%E7%BB%86%E5%8C%96.md)
- 执行器分版建议：[hardware/执行器驱动分版建议.md](hardware/%E6%89%A7%E8%A1%8C%E5%99%A8%E9%A9%B1%E5%8A%A8%E5%88%86%E7%89%88%E5%BB%BA%E8%AE%AE.md)
- 联调与验收清单：[docs/联调与验收测试清单.md](docs/%E8%81%94%E8%B0%83%E4%B8%8E%E9%AA%8C%E6%94%B6%E6%B5%8B%E8%AF%95%E6%B8%85%E5%8D%95.md)
- V1 发布清单：[docs/V1发布清单.md](docs/V1%E5%8F%91%E5%B8%83%E6%B8%85%E5%8D%95.md)

## 快速开始（ESP-IDF 主线）

1. 进入目录：`firmware/esp32_idf`
2. 编译：`idf.py build`
3. 烧录监视：`idf.py -p COMx flash monitor`

说明：若在中文长路径下遇到构建问题，优先使用 `tools/build_ascii.ps1`。

## 建议下一步

1. 按交流负载版或低压直流版确定执行器硬件路线
2. 在原理图中补独立硬件过温保护、保险丝和浪涌防护
3. 按测试清单完成首轮联调和验收记录

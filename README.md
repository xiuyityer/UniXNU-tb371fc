# UniXNU-tb371fc

联想小新 Pad Pro 12.7（2023，**TB371FC / Snapdragon 870 / Adreno 650**）的 UniXNU 设备适配源码。

当前整理基线为 **V129，iPadOS 17.7.11（21H461）**。实机已进入桌面，触摸、GPU 渲染和电源键已有用户验证。本项目仍在开发，尚不能作为日常使用系统。

这是从实机移植工作区提取的设备驱动与集成源码快照。**独立、完整的一键构建流程尚未整理完成，仓库目前不提供可直接刷入的系统镜像。** 现有完整构建还依赖本地 XNU 构建产物、目标系统 ABI 生成步骤及用户自行准备的输入，详见 [构建说明](docs/BUILDING.md)。

## 设备与支持状态

| 项目 | 当前结果 |
| --- | --- |
| 设备 | Lenovo TB371FC；bootloader 产品名 `spinel`；测试配置 8 GB + 128 GB |
| 系统 | iPadOS 17.7.11 / 21H461 已进入桌面 |
| CPU | 八核启动；日志确认各核计时器和 IPI 活动 |
| UFS / Data | 原生存储访问；持久 Data 写入和回读已验证 |
| 显示 | 可显示桌面与启动进度 |
| 触摸 | 桌面滑动可用；V128 日志确认 HID 上报无错误 |
| GPU | Adreno 650 已工作；V129 用户反馈滑动较 V128 提高约 5–10 FPS |
| 电源键 | 用户已验证息屏、亮屏 |
| 音量键 | 尚未正常工作，需要继续修复 |
| 应用与壁纸 | 此前有 Waiting、设置/图库黑屏、壁纸缺失；GPU 接入后尚未逐项复核 |
| 电池与充电 | 容量、充电状态尚未正常识别 |
| 其他功能 | 未列出的功能不代表已支持 |

帧率来自用户左上角显示工具在滑动桌面时的观察；不是统一条件下的帧时间基准。V129 改善后，GPU 性能优化按用户决定暂告一段落。

## 源码目录

- `package/Tb371fcTouch`、`Tb371fcButtons`、`Tb371fcInput`：触摸、PMIC 按键与 HID 生命周期。
- `package/Tb371fcGpu`、`Tb371fcDisplay`：目标 GPU 启动、帧缓冲与显示生命周期。
- `drivers/Adreno650`、`ArmSmmu4K`、`A650Gmu`、`AdrenoPM4`、`AdrenoFirmware`：GPU 和 SMMU 相关源码。
- `drivers/AdrenoMetal`：当前使用的 Metal 前端、桥接源码与 21 项 Pipa 转译补丁。
- `package/PlatformPower`、`Tb371fcEmbeddedFat`、`Tb371fcFastbootReturn`：平台与启动恢复集成源码。
- `package/RootLog`、`UfsProducerGate`：通用日志及 UFS 生产者门禁源码；不包含设备专属绑定记录。
- `patches/`：V129 连续内存分配策略改动。
- `upstream/`：上游固定版本、UFS 来源记录及逐文件 SHA-256。

不含 iPadOS 系统文件、Apple 原始二进制、设备固件、个人设备备份、原始设备日志或账号凭据。

## 从哪里开始

1. 阅读 [实机状态与里程碑](docs/STATUS.md)。
2. 查看 [上游来源与许可证](docs/UPSTREAM.md)。
3. 在自己的工作目录克隆本仓库：

   ```sh
   git clone https://github.com/xiuyityer/UniXNU-tb371fc.git
   cd UniXNU-tb371fc
   python3 scripts/check_repository.py
   ```

   该命令核验源码清单和仓库内容，不会连接设备或刷写。

4. 参与移植前阅读 [构建边界](docs/BUILDING.md) 和 [贡献说明](CONTRIBUTING.md)。

## 上游致谢

本项目基于 UniXNU 社区的工作，并参考、复用 xVanTuring 的 Pipa / Adreno 650 实现、sheng 的主线实现、FFT-infra 的原生 UFS 实现，以及 metal2vulkan、Mesa 等项目。设备适配不等于这些通用驱动的原创实现；具体来源见 [UPSTREAM.md](docs/UPSTREAM.md)。

不同文件保留各自许可证与版权声明。根目录默认许可证为 BSD-2-Clause-Patent；MIT、BSD-2-Clause 等组件按原许可证使用，见 [NOTICE.md](NOTICE.md)。

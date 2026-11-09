# UniXNU-tb371fc

联想小新 Pad Pro 12.7（2023，**TB371FC / Snapdragon 870 / Adreno 650**）的 UniXNU 设备适配源码。

当前实机基线为 **V135，iPadOS 17.7.11（21H461）**。用户确认 V135 应用、设置、触摸和 GPU 正常；壁纸仍为黑屏，已停止该项实机调试。本项目仍在开发，不能作为日常使用系统。

仓库现已包含 V135 内核定点派生、原生 macOS 固件构建和 Android boot 镜像打包入口。完整 XNU 源码重编译仍未接通；V135 保留已验证的 21H461 内核父镜像，并只对一个显示时序状态查询作等长 ARM64 指令替换。干净固件重编译已完成；用留存的 V135 固件快速打包可逐字节复现实机镜像。详见[构建说明](docs/BUILDING.md)及[V135 恢复记录](docs/V135-RESUME.md)。

## 设备与支持状态

| 项目 | 当前结果 |
| --- | --- |
| 设备 | Lenovo TB371FC；bootloader 产品名 `spinel`；测试配置 8 GB + 128 GB |
| 系统 | iPadOS 17.7.11 / 21H461 已进入桌面 |
| CPU | 八核启动；日志确认各核计时器和 IPI 活动 |
| UFS / Data | 原生存储访问；持久 Data 写入和回读已验证 |
| 显示 | 可显示桌面与启动进度 |
| 触摸 | 桌面滑动可用；V128 日志确认 HID 上报无错误 |
| GPU | Adreno 650 已工作；V129 用户反馈滑动较 V128 提高约 5–10 FPS，V135 用户确认 GPU 正常 |
| 电源键 | 用户已验证息屏、亮屏 |
| 音量键 | 尚未正常工作，需要继续修复 |
| 应用与设置 | V135 用户确认应用 Waiting 修复及设置 About 页面正常 |
| 壁纸 | V135–V138 尝试未解决，用户决定暂停壁纸调试 |
| 电池与充电 | 容量、充电状态尚未正常识别 |
| 其他功能 | 未列出的功能不代表已支持 |

帧率来自用户左上角显示工具在滑动桌面时的观察；不是统一条件下的帧时间基准。GPU 性能优化按用户决定暂告一段落。

## 源码目录

- `package/Tb371fcTouch`、`Tb371fcButtons`、`Tb371fcInput`：触摸、PMIC 按键与 HID 生命周期。
- `package/Tb371fcGpu`、`Tb371fcDisplay`：目标 GPU 启动、帧缓冲与显示生命周期。
- `drivers/Adreno650`、`ArmSmmu4K`、`A650Gmu`、`AdrenoPM4`、`AdrenoFirmware`：GPU 和 SMMU 相关源码。
- `drivers/AdrenoMetal`：当前使用的 Metal 前端、桥接源码与 21 项 Pipa 转译补丁。
- `package/PlatformPower`、`Tb371fcEmbeddedFat`、`Tb371fcFastbootReturn`：平台与启动恢复集成源码。
- `package/RootLog`、`UfsProducerGate`：通用日志及 UFS 生产者门禁源码；不包含设备专属绑定记录。
- `patches/`：V129 连续内存分配策略改动。
- `patches/v135-display-timing-query.patch`：V135 GOP timing-enable 查询派生。
- `upstream/`：上游固定版本、UFS 来源记录及逐文件 SHA-256。

不含 iPadOS 系统文件、Apple 原始二进制、设备固件、个人设备备份、原始设备日志或账号凭据。

## 官网设备目录

根目录 `board.toml` 提供官网识别所需的设备身份，`porting.toml` 记录 13 个子系统的实现、限制和下一步。配置保持 `example=true`，完整独立构建尚未整理完成。官网收录由 UniXNU 上游同步与部署决定，添加文件不等于页面已经上线。

详见 [官网收录说明](docs/CATALOG.md) 和 [公开验证摘要](docs/VALIDATION.md)。

## 从哪里开始

1. 阅读 [实机状态与里程碑](docs/STATUS.md)。
2. 查看 [上游来源与许可证](docs/UPSTREAM.md)。
3. 克隆并检查公开源码（只读源码与格式检查）：

   ```sh
   git clone https://github.com/xiuyityer/UniXNU-tb371fc.git
   cd UniXNU-tb371fc
   python3 scripts/check_repository.py
   ```

   该命令核验源码清单和仓库内容，不会连接设备或刷写。

4. 要生成 V135 boot 镜像，按[构建说明](docs/BUILDING.md)准备维护者保留的本地输入包。Apple 系统、设备数据、固件、原始恢复备份和生成 ABI 不在公开仓库中。

## 上游致谢

本项目基于 UniXNU 社区的工作，并参考、复用 xVanTuring 的 Pipa / Adreno 650 实现、sheng 的主线实现、FFT-infra 的原生 UFS 实现，以及 metal2vulkan、Mesa 等项目。设备适配不等于这些通用驱动的原创实现；具体来源见 [UPSTREAM.md](docs/UPSTREAM.md)。

不同文件保留各自许可证与版权声明。根目录默认许可证为 BSD-2-Clause-Patent；MIT、BSD-2-Clause 等组件按原许可证使用，见 [NOTICE.md](NOTICE.md)。

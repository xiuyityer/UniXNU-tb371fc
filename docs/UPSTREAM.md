# 上游来源

固定的完整提交见 `upstream/sources.lock.json`。复制源码的逐文件 SHA-256 见 `upstream/source-manifest.json`，包含对应组件及基线版本。

| 来源 | 用途 |
| --- | --- |
| [UniXNU/UniXNU](https://github.com/UniXNU/UniXNU) | XNU 移植、启动及通用组件 |
| [xVanTuring/UniXNU-pipa](https://github.com/xVanTuring/UniXNU-pipa) | 同系列 SoC 的 GPU 电源、GMU、显示与输入参考 |
| [xVanTuring/Adreno650](https://github.com/xVanTuring/Adreno650) | A650 内核侧 GPU 实现 |
| [xVanTuring/ArmSmmu4K](https://github.com/xVanTuring/ArmSmmu4K) | SMMU 页表与上下文 |
| [xVanTuring/AdrenoMetal](https://github.com/xVanTuring/AdrenoMetal) | 当前匹配的 Metal 前端、桥接与转译补丁 |
| [UniXNU/Adreno740](https://github.com/UniXNU/Adreno740) | V129 仅显式请求连续物理内存的策略参考，不复用 A740 板级常量 |
| [UniXNU/AdrenoMetal](https://github.com/UniXNU/AdrenoMetal) | sheng 的更新实现与性能优化参考；未整体替换当前前端 |
| [FFT-infra/NativeUfs-alioth](https://github.com/FFT-infra/NativeUfs-alioth) | 当前原生 UFS 来源，完整文件锁定见 `ufs-source-lock.json` |
| [UniXNU/ShengUfs](https://github.com/UniXNU/ShengUfs) | 原生 UFS 主线实现 |
| [steelbrain/metal2vulkan](https://github.com/steelbrain/metal2vulkan) | Metal 到 Vulkan 转译 |
| [Mesa](https://gitlab.freedesktop.org/mesa/mesa) | Turnip Vulkan 用户态依赖；本仓库不包含完整 Mesa |
| Linux Qualcomm 驱动、目标 Android FDT 与设备 Linux 适配 | 目标地址、中断、触摸、PMIC 与时钟行为的参考 |

Pipa 的硬件事实不自动等于 TB371FC 的硬件事实。目标常量需有目标设备资料支持。
当前现代 sheng 转译接口与本项目固定的旧版本并不完全兼容，不能只替换库文件。

## 本地改动

- `drivers/Adreno650/NativeAdrenoUserClient.cpp`：V129 显式连续分配策略；对应补丁位于 `patches/`。
- `package/Tb371fcDisplay/DisplayLifecycle.cpp`：保留 V128 的输入启动调用。
- `package/Tb371fcTouch`：目标板接入、坐标与 HID 上报。
- `package/NovatekProtocol`：目标复位顺序回调适配。
- `package/Tb371fcButtons`、`Tb371fcInput`：目标 PMIC 读取与组合输入启动。

请保留每个文件的原始 SPDX 和版权行；源码来源与本地改动不应混为原创声明。

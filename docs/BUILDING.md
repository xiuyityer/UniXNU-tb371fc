# V135 构建

公开仓库提供了确定的本机入口；目标系统和设备数据属于本地私有输入，不会从 GitHub 下载。构建目标是 V135，不是从干净的 XNU 源码重新编译系统内核。

## 生成 boot 镜像

需要原生 macOS、Python 3.11 或更新版本、Xcode 命令行构建工具，以及本地 V135 输入包。完整重编固件还需要输入包内保留的 EDK2 与 SimpleInit 源码和 LLVM 工具链。

```sh
git clone https://github.com/xiuyityer/UniXNU-tb371fc.git
cd UniXNU-tb371fc
python3 scripts/check_repository.py
python3 scripts/build_v135.py \
  --inputs /path/to/private-v135-inputs \
  --output /path/to/new-v135-build \
  --tools /path/to/private-v135-inputs/native-tools/bin
```

每个 --output 必须是新目录。命令不访问 USB、fastboot 或任何设备。正常模式会基于已留存的 V131 父内核执行 V135 定点派生，重新编译固件和 BootShim，并按 Android boot v1 格式打包。构建记录会明确标注没有完整重编 XNU、没有设备测试。

若目标只是快速生成与已验证 V135 字节一致的 boot 镜像，可用 --quick-pack。它使用已留存的 V135 固件，只重编 BootShim 并重新打包；含完整私有工具哈希校验的本机实测约 13.5 秒。正常固件构建从私有 capsule 工具本机实测约 150 秒。耗时受 Mac 和工具版本影响。

正常重编固件的候选镜像可能与原 V135 不同；该路径只证明源码可独立编译并生成镜像。需要准确恢复已验证镜像时用 --quick-pack，其 SHA-256 应为 1a5265f9fcbb19314510310c0e341c98c88f96b58ab9a99aeecf39c9e0fb3174。

## 应用安装元数据

首次创建 Data 或重建应用安装状态时，完整应用包必须已经由匹配的 21H461 系统输入放入 Data。把离线 Data 卷挂载到本机后，先生成只读计划：

```sh
python3 scripts/prepare_apps_v135.py --data-root /Volumes/Data
```

元数据来源默认为 Data 中可验证的原生 metadata；缺失或不匹配时回退到 V132 修复时保存的原生 metadata。也可用 --metadata-root 指向从本机原始输入恢复的对应 container 目录。检查计划后才加 --apply。应用会保留完整应用可执行文件，备份被替换的 metadata 与 xattr，并移动旧 LaunchServices 缓存到同一 Data 卷的时间戳备份目录。该命令只操作显式指定的离线本机目录，不接受块设备路径，也不复用旧的物理页增量。

## 私有输入

公开源码不含 Apple 固件、OS/SystemOS、完整目标 Data、生成 ABI、GPU addon/trust 输入或原 bootloader 恢复备份。维护者的本机归档位于工作区忽略的 research/v135-resume-private-2026-11-09/，不上传到此仓库。重建时需要这一输入包，或者用匹配的本机 21H461 原始输入重新生成它。inputs.json 校验打包的构建源和父内核；稀疏 OS/Data 分卷另有逐块摘要。仓库没有发布可直接刷写或可公开分发的完整系统。

旧物理测试 runner、ledger、分区位置和设备恢复记录属于单次操作证据，不能用于新设备或重放。设备恢复/写入需按当时设备状态单独核对；本构建器不会执行这些动作。

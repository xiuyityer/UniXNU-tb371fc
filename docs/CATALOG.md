# UniXNU 官网收录

本仓库为公开设备仓库，名称、描述包含 UniXNU，且已有 `unixnu` topic。官网同步器通过 GitHub 搜索发现设备仓库，再读取默认分支根目录的 `board.toml` 和 `porting.toml`。

## 已补齐的入口

- `board.toml`：格式 2，设备名 `tb371fc`、SoC `SM8250`、系统 `ipados-17.7.11-21H461`。
- `porting.toml`：13 个子系统的输入、配置、步骤、发现、完成条件、限制、下一步及证据路径。
- `docs/VALIDATION.md`：公开验证摘要，区分历史实机结果、当前公开源码及未验证功能。
- `scripts/check_catalog.py`：调用固定版本的官方记录校验器；CI 同时检查源码清单和适配记录。

`board.example=true` 和空组件构建列表用于明确本仓库仍是源码快照。官网目录收录不需要假装完整通用构建已经接通；后续完整构建配置应来自实际流程，不能填入占位硬件参数。

## 自动收录和时间

核查的上游提交为 `5dc00d201496a1e7a72d1f5db23f041eb94ace1c`。该版本同步工作流每日 UTC 00:00、12:00（北京时间 08:00、20:00）扫描，并在目录变化后触发文档部署。GitHub 定时任务与搜索索引可能延迟；这不是上线时限保证。

发布后须区分：本仓库内容已推送、自己的 CI 已通过、上游已收录、官网已部署。前两项完成不能代替后两项。

正常情况下，不必为新增设备手动修改上游 `devices.toml`。若经过后续成功同步仍缺失，再查看上游工作流或通过 issue 提供仓库链接及校验结果，请维护者排查；本次未向上游发送 issue 或消息。

官网路由由同步器分配；若 `tb371fc` 路由未被占用，中文页面预计为 `/zh/devices/tb371fc`。这只是规则推导，尚不代表页面已上线。

## 本次发现的上游阻碍

截至 2026-11-08 本次检查，最近一次上游同步（北京时间 2026-11-08 01:39；run `42397382890`）失败，报错：

```text
device catalog: Split this search into smaller queries: unixnu in:name,description fork:true is:public
```

当前同步器在单个搜索超过 1000 条结果时主动退出。该错误发生在搜索阶段，不能据此判断本仓库的记录无效。需要上游修复搜索拆分或其他发现策略并成功同步，才可完成官网自动收录。仅等待定时任务并不能保证解决同一故障。

[失败的上游同步运行](https://github.com/UniXNU/UniXNU/actions/runs/42397382890)。本次只读取公开日志，未以用户身份联系上游。

## 官方依据

- [设备仓库指南](https://unixnu.com/zh/porting/device-repository)
- [适配记录指南](https://unixnu.com/zh/porting/records)
- [设备目录](https://unixnu.com/zh/devices/)
- [固定版本的设备同步器](https://github.com/UniXNU/UniXNU/blob/5dc00d201496a1e7a72d1f5db23f041eb94ace1c/scripts/sync-devices.py)
- [固定版本的同步工作流](https://github.com/UniXNU/UniXNU/blob/5dc00d201496a1e7a72d1f5db23f041eb94ace1c/.github/workflows/sync-devices.yml)

运行 `python3 scripts/check_catalog.py` 需要 Python 3.11+ 和网络，只验证格式与公开证据路径，不构建或刷写系统。

# 实时动画调试与记录

## 当前交付状态

新增独立模块 `BetterEndfield.AnimationDebugger.dll`，版本 `0.1.0`，默认关闭。
提供实时悬浮窗口、目标选择、Animator 与 PlayableGraph 观测、会话记录、历史样本查看和 JSON/CSV 导出。
本模块只读取动画，不修改播放、速度、状态、图连接或角色模型。

已完成原生构建、数据检查、模拟 Host 读取检查和隐藏窗口交互检查。当前开发环境没有运行中的游戏，尚未完成真实客户端字段覆盖、画面对应、30 分钟稳定性与性能验收；不能将模拟检查结果视为游戏内验证。

## 启用

完整原生构建会在发布目录 `modules` 下生成：

- `BetterEndfield.AnimationDebugger.dll`
- `betterendfield.animation_debugger.module.ini`

已有安装可将这两个文件放入软件自身的 `modules` 目录。游戏退出后，在现有
`%LocalAppData%\BetterEndfield\BetterEndfield.ini` 中增加或合并以下节，保留其他配置和原有 UTF-16LE 编码：

```ini
[betterendfield.animation_debugger]
enabled=true
sample_hz=30
refresh_hz=10
```

通过现有 Better Endfield 加载方式启动游戏。模块成功加载后显示独立的置顶调试窗口。
首次启用建议重新启动游戏，以确保 Host 发现模块。设置 `enabled=false` 可停止采样；下次启动时不加载模块。
当前尚未在 WinUI 主控制器中增加设置页，配置入口为上述 INI。

窗口使用 Windows 桌面置顶窗口；建议使用窗口化或无边框模式，独占全屏覆盖尚未验证。
窗口运行于独立线程，但属于游戏进程，不需要另外启动 Overlay EXE。

## 使用

1. 默认“跟随当前角色”，读取游戏 `GameUtil.playerTrans` 下的 Animator。
   只有一个候选时自动选中；有多个候选时提示手动选择，不猜测哪一个是身体 Animator。
2. 下拉框可选择场景中的具体 Animator，名称后带实例 ID。登录展示等没有当前玩家的场景使用此方式。
   固定对象失效后显示 `target_lost`，不会自动换成其他对象。切回跟随后可以重新选择。
3. 查看状态层、Clip、播放时间、速度、Loop 和权重；勾选“展开图”查看输出路径及节点信息。
   Clip 筛选为区分大小写的名称子串匹配。
4. 点击“开始记录”。默认采样 30 Hz，显示刷新 10 Hz，最长 1800 秒，估算记录容量 64 MiB。
   采样可选 1–120 Hz，刷新可选 1–30 Hz，容量可选 1–256 MiB；记录开始后采样频率固定到本次会话。
5. “冻结显示”只冻结当前面板。关闭按钮和 `Ctrl+Alt+F8` 会最小化/恢复窗口，记录继续，任务栏标题显示 `REC`。
   快捷键被其他程序占用时，可通过任务栏恢复。
6. 停止后拖动历史滑块查看样本，左右方向键逐条移动。可输入角色/Animator/ID、起止秒和 Clip 名称，再点击“筛选历史”。
   点击事件可定位其对应的最近后续样本。“返回实时”恢复当前观测。
7. 点击“导出 JSON/CSV”，窗口显示输出目录。开始下一会话前必须先导出当前会话，防止误覆盖内存记录。

事件窗口最多显示最近 500 条，完整事件保存在导出文件中；历史滑块可查看全部匹配样本。
历史筛选只影响查看，导出始终包含整个会话。每次导出创建独立目录，重复导出也不会覆盖旧文件。

## 数据解释

| 信息 | 含义与边界 |
| --- | --- |
| Character / Animator | 跟随模式使用当前玩家 Transform 名称；固定模式使用 Animator 对象名作为目标标签，不保证是角色本地化姓名 |
| State hash | 运行时状态完整路径哈希；目前不反解为状态名称，不推断起步、收尾、攻击等语义 |
| Layer normalized / state length | 状态层的归一化时间和长度，不能当作混合中每个 Clip 的时间 |
| `animator_clip_info` | Animator 报告的当前/下一状态 Clip；过滤已知零权重项，保留所属层和当前/下一状态路径 |
| `playable_local_input` | 从绑定所选 Animator 的动画输出反向遍历所得到的 ClipPlayable；时间和速度直接读取该节点 |
| Clip 时间 | ClipPlayable 的累计本地时间，单位秒；循环进度单独显示。仅有 Animator ClipInfo 时不伪造 Clip 时间和速度 |
| Loop | 从 `Motion.isLooping` 读取的资源属性，不等价于观察到了完整的一次循环 |
| Weight | 当前节点在父节点上的局部输入权重，或 Animator ClipInfo 权重；不同来源不能相加，也不代表最终骨骼贡献 |
| Activity | `reported_by_animator` 表示 Animator 报告；`connected_graph_path_contribution_unverified` 表示存在连接路径但最终贡献未验证；`graph_stopped` 表示图已停止 |
| Graph | 图有效性、播放状态和绑定该 Animator 的动画输出路径；图节点保留零权重分支，但已知零权重祖先下的 Clip 不进入 Clip 列表 |
| Unknown / null | 数据缺失、不支持或无法可靠读取。未知布尔值不写成 False，未知时间不写成 0 |

同一动画可能同时出现在 Animator 和图观测中，也可能从多个图路径参与播放，记录会保留这些条目。
Animator 数组条目的路径含当前样本中的数组序号；数组重排可能表现为采样间的 Clip 退出/进入，不保证该序号是持久播放实例 ID。
图路径使用输出索引和输入索引，节点标识由运行期句柄及版本组成，只用于本次进程诊断。

## 记录格式 v1

输出根目录：`%LocalAppData%\BetterEndfield\animation-sessions`。
每个导出目录包含：

| 文件 | 内容 |
| --- | --- |
| `session.json` | 会话元信息、全部样本、逐样本图结构和事件 |
| `samples.csv` | 按样本 × Clip 路径展开；没有 Clip 的样本仍保留一行 |
| `events.csv` | 与 JSON 相同的事件时间、种类和详情 |
| `COMPLETE.txt` | 所有文件完成写入后才生成的完成标记 |

CSV 的会话元信息、状态层、节点关系与数据质量详情从同目录 `session.json` 获取。
文件采用 UTF-8，CSV 按标准规则转义逗号、双引号和换行。CSV 数值/布尔空字段对应 JSON `null`。
样本时间来自单调时钟，以开始记录为零点；会话起止墙钟时间采用 UTC。

### 会话与样本字段

| 字段 | 类型与含义 |
| --- | --- |
| `schema_version` / `tool_version` | 格式版本 / 模块版本 |
| `game_version` | 当前版本为 `null`，尚未接入可靠的客户端版本读取 |
| `session_id` / `started_utc` / `stopped_utc` | 会话标识与起止 UTC 时间 |
| `sample_hz` / `max_seconds` / `max_estimated_bytes` | 采样目标及本次限制 |
| `recording` / `stop_reason` | 当前记录状态与停止原因 |
| `sequence` / `time` | 从 0 开始的样本序号 / 会话相对秒 |
| `target_id` / `character` / `animator` / `mode` | 目标标识与名称，以及 `follow` / `fixed` |
| `status` | `valid`、`no_target`、`target_lost`、`no_main_thread_samples` 等有效性状态；`valid` 不保证每个可选字段可读 |
| `animator_speed` | Animator 层级速度，未知为 `null` |
| `graph_id` / `graph_status` | 图标识与状态，未知或无效时明确区分 |
| `layers` | 层索引、名称、当前/下一状态哈希、归一化时间、状态长度、权重和过渡状态 |
| `clips` | Clip ID、名称、路径、来源、累计时间、长度、速度、Loop、局部权重和观测活动状态 |
| `nodes` | 图节点标识、路径、类型、本地时间、速度、局部权重和原始 PlayState 枚举值 |
| `issues` | 字段读取失败、范围截断、图循环、队列丢弃等质量信息 |

### 事件

包括 `session_started`、`session_stopped`、`target_changed`、`state_change_observed`、
`clip_enter_observed`、`clip_exit_observed`、`graph_changed`、`quality_changed`、`sampling_gap`、`data_issue`、`queue_drop_total`。

变化事件根据相邻样本比较生成，时间表示首次观察到变化的样本时刻，而非游戏内部精确触发时刻。
目标或数据有效性变化时不跨缺失区间推断 Clip 进入/退出。短于采样间隔的动作可能未被捕获。

## 限制与异常行为

- 通过 `Canvas.SendWillRenderCanvases` 回调结束后采样；帧率、暂停、最小化、无 Canvas 回调等因素会限制实际频率。
  目标采样频率不是保证频率，可从样本时间差检查实际间隔。
- 超过 0.5 秒未收到主线程样本，面板标为过期；记录加入缺失样本，不延用旧 Clip 假装连续。
  主线程到窗口的队列最多 120 个样本，争锁或队列已满时丢弃新样本，并记录计数及后续质量提示。
- 每 2 秒刷新候选 Animator，最多 256 个；每次采样最多 32 层、每层 128 个 Clip 条目、
  Animator 来源总计 512 条。图最多 32 输出、256 个节点路径、24 层深度、每节点 128 输入；触及限制显示提示。
- 记录容量使用包含容器与事件余量的保守估算，达到时长或容量限制自动停止，保留已记录部分。
  此限制不是整个游戏进程的 RSS 上限；采样队列、窗口内容和导出序列化还会临时占用内存。
- 会话首先保存在内存中。导出失败时保留内存数据，显示错误并允许重试；不完整目录没有完成标记。
  正常模块关闭时尝试自动导出未导出的会话，强制结束游戏或进程崩溃不能保证保留尚未导出的数据。
- DLL 在进程内保持加载以保护尚在返回途中的 Hook 回调；关闭时释放读取资源和 Hook，不提供进程内热卸载/重载。
- 不支持从磁盘重新载入历史会话到窗口；既有导出可以用 JSON/CSV 工具离线检查。

## 开发验证

```powershell
cmake -S native -B artifacts/animation-debugger-build -G "Visual Studio 17 2022" -A x64
cmake --build artifacts/animation-debugger-build --config Release --target BetterEndfield.Layout --parallel 4
cmake --build artifacts/animation-debugger-build --config Release --target BetterEndfield.AnimationDebuggerChecks BetterEndfield.AnimationDebuggerReaderChecks BetterEndfield.AnimationDebuggerWindowChecks --parallel 4

& artifacts/animation-debugger-build/Release/BetterEndfield.AnimationDebuggerChecks.exe artifacts/animation-debugger-checks
& artifacts/animation-debugger-build/Release/BetterEndfield.AnimationDebuggerReaderChecks.exe
& artifacts/animation-debugger-build/Release/BetterEndfield.AnimationDebuggerWindowChecks.exe artifacts/animation-debugger-window-checks

# 或在构建上述检查目标后统一运行：
ctest --test-dir artifacts/animation-debugger-build -C Release --output-on-failure
```

隐藏窗口测试只使用合成样本，不显示测试窗口，不连接或启动游戏。
检查覆盖转义、空值、多路径、事件、采样缺口、容量/时长停止、固定目标失效、实例 ID 复用、图循环、托管根释放、冻结继续记录和导出。

### 游戏内待验收

| 项目 | 当前状态 / 操作 |
| --- | --- |
| 运行时契约 | 待实际启动；检查 Host 日志中各读取项 `available/unavailable` |
| 待机、移动、攻击、技能 | 待将画面与实时 Clip、状态和记录逐一对照 |
| 登录展示、多个 Animator | 待验证固定选择与多候选提示 |
| 多 Clip、多层、复杂图 | 待确认实际可观测范围，检查局部权重和输出目标匹配 |
| 切换角色、替换模型、切换场景 | 待验证目标边界、失效与缺失区间 |
| 独占全屏、不同 DPI | 待验证窗口布局和可见性 |
| 连续 30 分钟 | 待实测稳定性、内存曲线和采样间隔 |
| 性能目标 | 待在固定机器和场景比较关闭 / 实时 / 实时+记录的平均及 P95 帧时间；尚未证明平均增幅 ≤5% |

上述游戏内验收完成前，V2 计划的运行时覆盖与性能验收仍为未完成状态。

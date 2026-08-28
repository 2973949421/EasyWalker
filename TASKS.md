# ADV Walkman Backlog

> 版本：V0.2  
> 原则：Backlog 只管理实际执行任务。  
> V1 产品侧设计已基本冻结；UI 字号、歌词列宽、ASCII 网格和音效参数只允许真机小范围校准，不阻塞 P0。

状态：

```text
TODO
DOING
BLOCKED
DEVICE TEST
DONE
DEFERRED
```

---

# P0 — Audio Backend Benchmark

## P0-01 Benchmark Harness

Status: DONE

目标：

建立最小基准固件，使三种音频 Backend 可以在尽量相同条件下测试。

AC：

### Automatic Validation

- [x] PlatformIO 工程可以构建
- [x] 可选择 / 切换 A、B、C 实验环境或 Backend
- [x] 使用固定测试 MP3
- [x] 串口可输出 Backend、sample rate、heap、minimum heap、underrun 等必要信息
- [x] 不包含正式 UI

自动构建与 2026-08-24 真机验收均已通过；固定测试 MP3 的设备端 SHA-256 与 PC 记录一致。

### Device Validation

- [x] Cardputer ADV 正常启动
- [x] 原生 3.5mm 能播放测试音频
- [x] 无明显启动异常

---

## P0-02 Candidate A — Cardio-style M5.Speaker

Status: DONE

AC：

- [x] MP3 320 kbps / 44.1 kHz 可播放
- [x] 正确 L+R downmix
- [x] sample rate 处理正确
- [x] 使用合理 triple buffer / M5.Speaker 参数
- [x] Pause / Resume 可用
- [x] 使用同一 Fixture 完成 Restart / Flush / Reopen 生命周期验证
- [x] 固定 320 kbps Fixture 的定点 Seek 可用
- [x] 记录 heap / underrun
- [x] 真机旧耳机听感记录

---

## P0-03 Candidate B — Direct I2S

Status: DONE — NOT SELECTED

AC：

- [x] MP3 320 kbps / 44.1 kHz 可播放
- [x] ES8311 init 有官方 / M5Unified 依据
- [x] Direct I2S 启动与播放生命周期正常
- [x] 正确 L+R downmix
- [ ] Pause / Resume 可用
- [ ] 使用同一 Fixture 完成 Restart / Flush / Reopen 生命周期验证
- [ ] 固定 320 kbps Fixture 的定点 Seek 可用
- [ ] 记录 heap / underrun
- [x] 真机旧耳机听感记录

Candidate B 已完成构建、真机播放与等响度听感比较。按“只对听感胜者执行短压力测试”的既定简化协议，未勾选的生命周期 / 指标项不再追加人工测试；B 保留为可工作的备选实现。

---

## P0-04 Candidate C — BackgroundAudio

Status: DEFERRED

AC：

- [ ] 独立环境构建成功
- [ ] 不破坏当前稳定 PlatformIO 主环境
- [ ] ADV + ES8311 可正常输出
- [ ] MP3 320 kbps / 44.1 kHz 可播放
- [ ] 使用同一 Fixture 完成 Restart / Flush / Reopen 与定点 Seek
- [ ] 记录 heap / underrun
- [ ] 真机旧耳机听感记录

Candidate C 已完成独立环境构建和真机出声，但用户未听到相对 A/B 的明确收益，同时它占用最大 App 分区并引入 IDF5 工具链维护成本。P0 收敛为 A/B 二选一；C 代码与固件保留为备用，不继续投入人工测试。

---

## P0-05 Stress Test

Status: DONE

AC：

- [x] Candidate A 固件提供 `T` 键一键自动压力测试；无需依赖当前无响应的应用串口
- [x] Candidate A 真机完成约 3 min 自动压力测试并显示 `PASS`
- [x] 用户人工观察到的暂停、跳转与重播均对应脚本步骤，未报告额外异常
- [x] 记录明显爆音 / 卡顿 / 崩溃结果
- [x] 记录 Heap 是否持续下降

自动流程固定为：Baseline 30 s → UI Stress 60 s → UI + SD Stress 60 s → Pause 3 s → Resume 10 s → `seek 60` 后 10 s → Restart 后 10 s，总计约 3 分钟。压力阶段只读取既有 Benchmark MP3，不写 Flash；流程开始前单次写入 `RUNNING` 标记，结束并关闭 SD Stress 后单次覆盖最终摘要 `/ADVWalkman/logs/p0-a-stress-last.txt`，测试负载期间不写日志。

结果页显示 `PASS/FAIL`、state / sample rate、heap delta / sampled minimum heap、backpressure、service max、UI frames、SD KiB 与日志保存状态。`Listen: manual` 明确表示自动结果不能判断主观听感；用户仍需人工报告声音是否正常。

取消 A/B 各 30 min 和胜者 2 h 的人工硬门槛。Candidate A 已完成约 183 s 真机短测并通过；长期稳定性在 P1 实际开发与使用中继续验证。

首次自动短测暴露 Candidate A 的 Decoder service 不归还主循环：M5.Speaker 队列等待后持续返回成功，使 ESP8266Audio 单次 `loop()` 长期运行。`0.2.0-p0.a-stresslog2` 改为每提交一个 768-sample Buffer 后合作式让出主循环，保留未消费样本并且不伪增 Backpressure；修复版真机压力测试已通过。

---

## P0-06 Select V1 Audio Backend

Status: DONE

AC：

- [x] `docs/AUDIO_BENCHMARK.md` 填写结果
- [x] 写明最终选择和原因
- [x] 更新 `docs/TECH_DESIGN.md`
- [x] 不因为“更底层”而自动选择复杂方案
- [x] 形成可进入 P1 的冻结 Audio Backend

V1 冻结选择 Candidate A：`ESP8266Audio 1.9.7 → triple-buffer M5.Speaker → ES8311`。等响度后 A 同样具备用户认可的空间感，短压力测试通过，并且比 Direct I2S 更成熟、维护成本更低；B 保留为备选，C 保持 Deferred。

### 固定比较方式

- 只使用 `/Music/ADVWalkmanBenchmark/benchmark.mp3`，不再派生 M4A、48 kHz 或 VBR 测试矩阵。
- A/B 分别生成独立 M5Launcher App；耳机线控保持最大，完整原曲各听一次，听完立即记录印象。
- 等响度基准采用用户已听过的原 B 响度：A 使用 M5.Speaker `128/255`，B 使用线性增益 `0.25`。此前向 A=`64/255` 下调的方案导致两者均不舒适，已放弃。
- C 保留为 Deferred 备用，不参与本轮听感和压力测试。
- “Restart”只验证 Decoder / File / Output 生命周期，不能冒充正式 Player 的 Next / Previous 功能。

---

# P1 — Player Core

P0-06 已完成，正式 Player 固定沿用 Candidate A；P1 不重新选择 Backend，也不提前实现 Library、正式 UI、歌词、ASCII Cover 或 DSP。

## P1-01 MP3 Playback Core

Status: DONE

AC：

- [x] MP3 CBR
- [x] MP3 VBR
- [x] 最高目标 320 kbps
- [x] 44.1 kHz
- [x] 48 kHz
- [x] Track end 自动结束
- [x] 播放结束可通知 Player 进入下一首
- [x] 自然 EOF 先排空尾部 Buffer，且每首只发一次 TrackEnded
- [x] 缺文件、损坏文件和 Decoder 错误稳定进入 Error，不自动重启
- [x] 生成的 CBR 44.1 kHz、VBR 44.1 kHz、CBR 48 kHz Fixture 真机通过
- [x] Gate A 在设备端计算合法 / 截断 Fixture SHA-256，并与 Manifest 一致

2026-08-26，`0.3.0-p1.gate-a2` 真机 Gate A 通过：三份合法 Fixture 产生三次且仅三次 `TrackEnded`，截断文件与故意缺失文件各产生一次预期 Error，四份 Fixture SHA-256 全部匹配。`p1-01-last.txt` 的最终 `state=ERROR / file_open_failed` 是缺失文件负向测试的通过现场，不是 Gate 残留故障。

---

## P1-02 Transport Controls

Status: DONE

AC：

- [x] Play
- [x] Pause
- [x] Resume
- [x] Stop
- [x] Next
- [x] Previous
- [x] Seek
- [x] 不因连续操作崩溃
- [x] 无频繁明显爆音
- [x] Pause 中 Seek / Next / Previous 后仍保持 Pause
- [x] Previous 在播放超过 5 秒时回到本曲开头，否则进入上一首
- [x] CBR 与带 Xing/VBRI 的 VBR Seek 目标误差约不超过 1 秒
- [x] VBR Seek 后实际恢复 Decoder 并持续播放，不能只核对估算位置

同一次 Gate A 完成 Transport 真机验收：最终 `STOPPED / NONE`，最大 Seek 误差 60 ms，Backpressure 0；用户确认测试音播放正常、无异常爆音或重启。测试音量刻意保守，偏小不代表正式音频链路异常。

---

## P1-03 Queue & Playback Modes

Status: DONE

AC：

- [x] Queue
- [x] Sequential
- [x] Shuffle
- [x] Repeat One
- [x] Repeat All
- [x] 模式切换状态正确
- [x] 重启后可恢复
- [x] Repeat 与 Shuffle 独立；Repeat One 不阻止手动 Next / Previous
- [x] Shuffle 单轮不重复，Previous 按真实历史返回
- [x] 空队列、单曲、队首和队尾安全

2026-08-26，`0.3.0-p1.gate-b` 真机 Gate B 通过：Sequential、单轮 Shuffle、Repeat One、Repeat All、手动导航、模式切换保持当前歌曲及 Previous history 全部满足断言；最终 `STOPPED / NONE`，三次自然 EOF、零 Audio Error、零 Backpressure。

---

## P1-04 Playback State Persistence

Status: DONE

AC：

- [x] 保存歌曲
- [x] 保存位置
- [x] 保存队列
- [x] 保存播放模式
- [x] 重启恢复
- [x] 恢复后保持 Pause
- [x] 不自动出声
- [x] Queue / Session 使用 CRC32 与 A/B 双槽，新槽回读通过后才生效
- [x] 恢复时选择最新完整 Queue / Session 配对，并保留 order / cursor / Previous history
- [x] Session 播放中约每 10 秒 checkpoint，Queue 只在队列变化时重写
- [x] 未知版本、CRC 错误、截断文件和无 SD 时不重启循环

同一次 Gate B 完成一次真实 SD 保存与受控软件重启。重启后恢复第 2/3 首、`4003 ms`、source offset `61248`、Repeat All、Shuffle On、完整 order/cursor/history，并保持 `PAUSED` 且至少 3 秒无 Speaker 输出。首代有效状态写入 `queue-a.bin / session-a.bin` 属于正常 A/B 轮换起点；主机只读解析确认 schema、generation 与 CRC32 有效。

---

# P2 — Music Library

## P2-01 Recursive Folder Browser

Status: DONE

AC：

- [x] `/Music/`
- [x] 多层目录
- [x] 中文路径
- [x] 隐藏文件过滤
- [x] P2 已实现阶段非 MP3 文件不进入可播放列表；FLAC / WAV 暴露由 P6-04 在 Decoder 可用后统一扩展
- [x] 播放时浏览目录不主动 Stop Audio

实现与 PC Fixture 验收已通过。2026-08-26 的 `0.4.2-p2.gate` 真机日志独立确认：根目录边界、多层中文路径、过滤、自然排序、当前文件夹 Queue、Queue pin 与播放中浏览全部通过。最终 `0.4.4-p2.final-gate` 再次 PASS；P2-01 的一次 112.933 ms PCM 间隔发生在刻意的短曲 EOF / Track 切换生命周期，`repeat_restart_max_us=8832`，记录为非连续长曲阶段 WARN，不作为隐藏的连续播放放宽。

---

## P2-02 Lazy Scan / Cache

Status: DONE

AC：

- [x] 不整库常驻 RAM
- [x] 当前目录按页显示
- [x] 大目录有界内存
- [x] 缓存可淘汰
- [x] 约 1000 首库规模可完成扫描、排序、分页和 Queue pin
- [x] 播放中扫描不造成明显断音

4 个 SD session cache slot、3×32 项 RAM LRU page 与 Queue pin 已实现。`0.4.2-p2.gate` 已把首个真实失败收敛到千文件扫描：单次 Library 调用 92.6 ms，使 Player service 间隔擦线达到 101 ms，但当时仍为 Playing / 44.1 kHz、无 Audio Error、无 Backpressure，PCM 最大间隔 98.397 ms。

`0.4.3-p2.gate` 真机在约 22 秒内完成 1,000 项扫描、排序、分页、LRU 与 Queue pin，P2-02 逻辑通过；但测得 PCM 最大提交间隔 53.942 ms，超过原 `3 × 768` 约 52 ms 的实际缓冲余量，用户听到卡顿，证明旧 100 ms Gate 门槛过宽。最终 `0.4.4-p2.final-gate` 保持 Candidate A 与三缓冲模型，只把正式 Player 单 Buffer 增至 1536 samples，并在 70 ms 缓冲感知门槛下通过：1,000 项与 32 个代表分页样本全部验证，PCM 最大间隔 60.317 ms，Audio Error / Backpressure / TrackEnded 均为 0，最低 Heap 90,148 bytes。

---

## P2-03 Metadata

Status: DONE

AC：

- [x] Title
- [x] Artist
- [x] Album
- [x] Track Number
- [x] 中文 Metadata 可规范解析为 UTF-8（CJK 字形显示留给 P3）
- [x] Metadata 解析在长曲播放期间完成且未增加 Audio Error / Backpressure
- [x] 无 Metadata 时合理回退到文件名

ID3v2.3 / v2.4、四种文本编码、unsynchronization、extended header 与损坏标签 fallback 已通过 PC Fixture；最终 `0.4.4-p2.final-gate` 在长曲播放期间完成全部 10 个案例，PCM 最大间隔 61.796 ms，Audio Error / Backpressure 均为 0，P2-03 真机收口。

---

## P2-04 Recent Tracks

Status: DONE

AC：

- [x] 播放累计 5 秒后保存最近播放，Pause 时间不计
- [x] 路径不存在时安全忽略
- [x] 仅使用 `recent-a.bin / recent-b.bin` A/B 双槽，不为每首歌制造状态文件
- [x] 32 首上限、按完整路径去重且最新在前

`0.4.3-p2.gate` 已通过真实播放 5 秒记录、Pause 排除和 pending-path 切歌保护；唯一失败来自把启动时 31 条 Recent 冷加载错误放在播放期，产生 0.89 秒同步 SD 阻塞。最终 `0.4.4-p2.final-gate` 在播放中通过真实 5 秒 publish，再在停播后的正确启动生命周期验证 A→B→A 去重、32 项淘汰、缺失路径过滤和 A/B CRC cold reload；Host Validator 确认 generation 37/36、CRC 有效并整体 PASS。

---

# P3 — UI

## P3 Delivery Gates

执行明细见 `docs/P3_DELIVERY.md`，冻结顺序为：

| Gate | Status | Tasks | Result |
|---|---|---|---|
| P3A UI Foundation | DEVICE TEST | P3-01、P3-08、P3-09；P3-07 功能骨架 | `0.5.1` 功能 Gate PASS；`0.5.2` 文本修复及本地构建完成，待 Gate A-fix+B+C 回归 |
| P3B Now Playing Chrome | DEVICE TEST | P3-02 | `0.6.0` 代码、自动检查及三环境构建完成；待联合 Gate 验收 |
| P3C Media Resources | DEVICE TEST | P3-03～06、P3-12 | `0.7.6` 导航、分区绘制、自检及14px时间修复；仅本地，待与P3D合并真机确认 |
| P3D Product UI Completion | DEVICE TEST | P3-07、P3-10、P3-11 | `0.8.0` 本地实现、62项检查与三环境构建通过；与P3C导航修复合并真机验收 |

P3A 编译成功后进入 `DEVICE TEST`，不能提前把 P3-01 / 08 / 09 标为 `DONE`。
P3-07 在 P3A 只完成可用骨架，最终视觉仍由 P3D 验收。
基础文字不越界属于 P3A 完成条件，不得延后到 P3D；P3B 复用到 Now Playing，P3C
补齐正式中日文字体 / CJK 度量，P3D 仅做最终视觉校准。
为减少用户实操，P3A 文本修复不单独安装：工程按 `P3A fix → P3B → P3C` 分开构建
和提交，真机仍只安装同一个 P3ABC BIN。0.7.3 改为自由试用后台分项记录，不强制用户
按 A/B/C 顺序操作；P3A 文本回归未通过时仍不得标记 P3-01 / 08 / 09 为 `DONE`。

### P3 完整刷新收尾候选 — 0.8.4（当前，DEVICE TEST）

实施与验收记录：`docs/P3_RENDER_FIX.md`。基线21bb6fc，0.8.3 boot8约14分36秒日志：输入74ms、选择880ms、暖返回2655ms、暖View1643ms、歌词呈现135.300ms仍超限；PCM45.370ms、正常歌词到期143ms、Audio Error/Backpressure=0。11张封面CRC完整不能证明显示正确；本轮针对共享缓冲被chrome覆盖的确定缺陷，不重新生成封面。

- [x] 实际调度入口编译期正反例，重新注入旧fallthrough/虚假完成逻辑必须失败
- [x] 固定135×18、Pending独占、有效高度提交、帧归属/连续行验证、统一取消与一次受控重建
- [x] 完整View/局部修复/资源占位分别统计，偏好和唤醒只由新完整画面确认
- [x] 暖窗口保留正式Title/metadata标志、最终省略文本缓存、按face准备、已知字模地址跳过重复索引
- [x] 固定174/22/44区域、长名滚动、非空固定三唱片及三处各自蓝色弧形短名
- [x] 四套曲库字体离线轻加粗，44张实际字模预览，其他字体/封面/音乐/歌词不改
- [x] 89项本地检查通过（发现机制含重复导入，不等于89项独立设备场景）；实际M5GFX像素自检已编入，待设备执行
- [x] Dev/P3ABC/P3A构建，联合BIN789680bytes；媒体/新增状态48892/49152，完整静态RAM143800；SD13文件SHA核验，19项存档/历史日志未变
- [ ] 新版图面无残留/无缺条、暖返回/View/选择/输入严格性能真机确认
- [ ] Lyrics/Cover/Playlist各两次睡醒、约20分钟耳机使用后返回及T保存/重启复验
- [ ] A/B/C/D分别收口；旧唤醒停音/输入失效仍UNVERIFIED，设置与Launcher未覆盖不得自动通过
- 扬声器破音DEFERRED；不进入P4。构建、内存及精确SD文件记录见附件。

### P3 收尾修复 — 0.8.3（历史，显示与部分性能未通过）

实施/验收记录：`docs/P3_REFINEMENT.md`。0.8.2日志已记录PCM45.201ms、到期138ms、选择40ms，但暖返回3071ms、暖View1331ms、呈现106.622ms、输入69ms仍超限。用户报告唤醒后停音且全部按键失效，重启后恢复；根因尚未确认，不能推定为自然播放结束。

- [x] 显示Cancel/Drain/Apply生命周期、逐句柄关闭、短按唤醒/整组吞键/代次隔离；睡眠跨曲目恢复真实位置
- [x] 复位原因、16byte RTC阶段、唤醒里程碑、音频进展和阶段峰值日志；缺证据不算通过
- [x] 六行12项元数据缓存查询，选中优先、其余可见行补齐；暖窗口保留，播放状态不重建列表
- [x] 四首仅标签修正、11首官方Title核验；文件名/排序/Queue/Session不改
- [x] 华文行楷18/12和Kunstler22/14，16byte字模记录；完整合图、居中大名称、弧形重叠轮盘及PC预览
- [x] 84项PC检查（含重复导入用例）和生产C++编译期检查通过；运行时性能不是PC验证结论
- [x] Dev/P3ABC/P3A/P2构建通过；媒体+新增状态48700/49152bytes，同名联合BIN782848bytes；SD17项核验、19项存档/历史日志Hash未变
- [ ] 新版严格性能与15秒跨页面唤醒两轮、约20分钟耳机试用、保存/重启复验
- [ ] 唤醒停音/失去输入异常解释与真机复验；目前UNVERIFIED
- [ ] A/B/C/D分别按新日志和人工确认收口；设置/Launcher未覆盖仍未覆盖
- 扬声器持续噪声/破音：DEFERRED，不在本轮修复。

### P3 性能修复 — 0.8.2（历史，部分性能未通过）

实施和验收附件：`docs/P3_PERFORMANCE_FIX.md`。基线61583dd的日志仍是失败证据：暖返回8305ms、选择2827ms、歌词准备5680ms/迟到3558ms、呈现111.887ms、PCM306.048ms。不能以本次构建覆盖这些事实。

- [x] 固定16项输入队列；公开键盘接口有界捕获短按、释放，页面代次和唤醒吞键保留
- [x] View以最后完整画面决定目标，待完成重复请求合并，无等待提示；完成后才保存偏好
- [x] FIDX v2直接码点索引、旧索引兼容、字体配对检查、Cover期间歌词准备
- [x] 六行列表逐区准备/绘制，移除失败字模查询的20ms节流；复用暖窗口
- [x] 共享135×18条带填充与提交互斥；曲库CRC暖返回保留；音频/存档服务分开
- [x] Queue每步一个路径、写/Flush/关/回读分开，日志分步关闭，新增分阶段测量
- [x] 曲库135×173完整合图与居中名称、紧凑唱片；10首译文复核与原文时间记录核对
- [x] Crucifix X本地无重编码迁移，11首绑定，暗黑天国保持无歌词
- [x] 71项PC检查、C++编译期回归及六环境最终构建通过；BIN≤0x140000，媒体+输入48436 / 49152 bytes
- [x] 同名联合BIN和22项资源已同步SD并核对；精确退役5个benchmark文件，19个存档/日志Hash未变；体积、SHA-256及证据边界见附件
- [ ] 真机输入≤50ms、选择≤100ms、暖返回/暖View≤300ms、冷View≤1500ms
- [ ] 真机PCM≤70ms、歌词呈现≤100ms、正常预取迟到≤200ms；Audio Error/Backpressure为0
- [ ] 设置、息屏、Launcher及恢复仍按独立证据收口，不能自动标DONE

### P3 修复与优化 — 0.8.1（历史，未通过性能验收）

详细证据见`docs/P3_OPTIMIZATION_FIX.md`，不进入P4、不更换音频Backend。

- [x] Tab页面导航与Enter重播分离；同窗口高亮局部更新、最终排版字模准备
- [x] 当前/下一歌词槽、双帧独立固定、512byte索引页缓存；保留原内存上限
- [x] 修复封面校验文件生命周期；同曲暖返回不重读完整LRC/CRC；灰色首组预览
- [x] 11张全宽ASCII由既有源图重制；Cover Header居中、时间右侧两标识
- [x] 67项PC/生产C++编译期检查通过；包括298组双帧+UI字模、旧封面关句柄反例
- [x] Dev/P3ABC/P3A及共享计时涉及的P1 A/B/P2六环境最终构建通过；BIN和媒体内存合规
- [x] SD仅覆盖同名联合BIN和11张歌曲ASCII封面，Hash核对；音乐/歌词/存档/旧日志未改
- [ ] 播放/暂停分别Tab往返保持进度，Enter重播；息屏首次Tab仅唤醒
- [ ] 同窗口移动反馈≤100ms、暖返回可交互首屏≤300ms；冷加载无歌曲行残留
- [ ] Sophie/Black Birthday/Symbol 1/Crucifix X封面及歌词实际表现确认
- [ ] PCM≤70ms、歌词呈现≤100ms、正常预取更新≤200ms；不排除页面/加载/日志负载
- [ ] 设置、息屏、返回Launcher及恢复按真实日志独立收口，不自动判通过

### P3C 导航修复 — 0.7.6（历史）

证据与实现：`docs/P3C_NAVIGATION_FIX.md`。不以存在10首MP3或接受Esc代替选歌成功。
实施顺序：**P3C 修复与自动验证 → 单独规划并实施 P3D → 一次安装、合并真机验收**。
`0.7.6` 没有单独写SD或安装；现推进 `0.8.0` P3D，本轮仍不添加实体上一首/下一首。

### P3D 合并交付 — 0.8.0

详见 `docs/P3D_IMPLEMENTATION.md`。

- [x] 独立LCOV曲库封面、黑胶短动画、两行名称/弧形短名和中文菜单本地完成
- [x] DSPL A/B显示存档、两类超时、完整物理位图吞键、正常Launcher返回路径本地完成
- [x] 62项PC/生产C++编译期检查；Dev/P3ABC/P3A构建及0x140000/48KiB检查通过
- [x] SD仅覆盖同名联合BIN并新增AveMujica独立封面，核对复制结果；字体未变
- [ ] Lyrics/Cover退列表无残留，能选择至少两首（含暗黑天国），实际目录队列正确
- [ ] 黑胶/中文设置/亮度/15秒临时时限清晰可用，首组View/音量/组合键仅唤醒
- [ ] ≥60秒真实跨页播放，PCM≤70ms、歌词呈现≤100ms/正常预取更新≤200ms，无异常声音
- [ ] 保存并返回Launcher，再启动恢复暂停、歌曲/视图/亮度/时限正确；Host日志与人工确认通过

Automatic Validation：

- [x] 目标页面立即切换，旧媒体取消，目录/分页异步加载，错误可重试/返回
- [x] 导航代次、Ready/Pending/Error、5秒无进展、取消及过期结果的生产 C++ 编译期测试
- [x] 六行窗口分步准备/绘制，按需字模；屏幕135×240和18px缓冲的正反自检
- [x] 14px Times时间实际字模检查；播放队列仍通过当前目录 selectTrack 建立
- [x] 日志区分自检/导航/字体/性能故障，记录实际列表成帧、不同歌曲选择和队列数量
- [x] Dev/P3ABC/P3A/P2构建、0x140000体积及48KiB媒体编译断言；不重建P1/Benchmark

Device Validation（并入后续P3D自由试用，不要求独立安装）：

- [ ] 从Lyrics及Cover退出均显示干净列表，加载中Esc和失败后Enter/Esc可用
- [ ] 可进入AveMujica，选至少两首不同歌曲（含暗黑天国），实际队列为10首
- [ ] 浏览不改变benchmark原单曲队列；选歌后Metadata/歌词/封面归属正确
- [ ] 跨页播放持续，光标/目录返回合理，14px时间可读且无越界
- [ ] ≥60秒自由播放，PCM≤70ms、歌词呈现≤100ms、正常预取更新≤200ms
- [ ] 日志及人工确认全部满足才收口A/B/C；旧154.195ms/109.328ms/687ms仍是失败证据

### 历史 P3ABC 收尾 — 0.7.5

本轮实施记录见 `docs/P3ABC_CLOSURE.md`，不进入P3D/P4，不重选Backend。

Automatic Validation：

- [x] 双布局、7种字体/4-bit缓存、音量映射和跨启动日志代码已完成
- [x] 10首歌曲与9份译文绑定、298组实际歌词/Metadata字模与分页检查通过
- [x] 48项PC检查、六环境构建及0x140000尺寸/48KiB媒体编译断言通过
- [x] 本版SD交付核对：同名BIN、63个变化资源、受管清单；不清理其他文件

Device Validation（下列不得用构建成功代替）：

- [ ] Cover 28/188/24，Lyrics 216/24无Header；切换无残留、不改变播放
- [ ] 18px楷体微加粗、14px Times歌词；UI统一实际字体度量，英文整词换列
- [ ] raw102上限、初始逻辑80/raw32；不提高开机响度
- [ ] 10首新MP3/9份原文译文/40×32封面绑定；暗黑天国Cover-only
- [ ] 70ms PCM、100ms呈现、200ms更新不放宽，真实加载/绘制/日志计入
- [ ] 自由操作≥60s；T保存后人工重启，核对Paused、歌曲、偏好和静音
- [ ] 新版日志与人工可读性/封面/音量/听感确认后才能DONE

### 历史校准 — 0.7.4（保留证据，不是当前参数）

- 0.7.3 SD：45份有效CRC检查点，42次自然换句；呈现91.890ms / 延迟141ms，
  Audio Error0 / Backpressure0；PCM最大93.100ms仍超70ms，A导航未覆盖，不能DONE。
- [x] 去前后句和前奏预览；CJK16、六列；英文单词优先整体换列，超长词才拆字符
- [x] 逻辑音量0～255映射Speaker0～63，启动raw32；不恢复旧高值，不更换音频Backend
- [x] ASCII默认34×26、收紧字格空白；文件格式 / 设备内存不扩容
- [x] 重复Esc不重启同一异步目录扫描；未宣称已解决93ms峰值，保留原阈值复验
- [ ] 真机确认当前句清晰完整、英文无不必要断词、音量区间舒适、封面更可辨识
- [ ] 70ms PCM及现有未覆盖导航 / Seek / 重启偏好项仍按事实补验；不要求重新全套Gate
- 构建与交付见 `docs/P3C_VALIDATION.md`，继续自由操作 + 15秒 / T后台日志。

### P3ABC 自由试用修复 — 0.7.3（历史）

- 0.7.2：连续窗口 60010 ms，Audio44100 / Error0 / Backpressure0 / PCM42404µs，
  1723 buffers；歌词 Loading、Cover Ready、10 帧封面、0 个自然歌词 deadline。
  资源 381454 bytes 恰为 Cover校验34588 + 10×34560 + 日文1266，中文尚未读取。
  根因是封面优先调度使冷歌词无法完成；旧 fail() 主动暂停，不是已证实设备重启。
- [x] 公平轮转资源 worker；Loading 不再等同 Missing；实际 C++ 调度回归拒绝旧实现
- [x] 完整双语首句 / 七列续行，28/188/24、CJK14、标点；实际 29 组字模参考检查
- [x] 透明细音量条、当前区域恢复、移除 NORM Original；真实固定播放 / 音量键前置
- [x] 只观察的 FreeSession，15 秒 / T 分块 CRC 日志，第一错误保留，未覆盖写 INCOMPLETE
- [ ] 新版实际显示、真实按键、歌词更新 / 音频连续性与人工听感通过
- [ ] Seek、跨歌曲无歌词回退、重启视图偏好恢复补验；自由试用不自动执行或假报通过
- 构建 / SD / Hash 记录统一在 `docs/P3C_VALIDATION.md`；仅构建通过不改变 DEVICE TEST。
- 以下 0.7.0～0.7.2 顺序 Gate 记录保留为历史，不是新版操作要求。

### P3ABC Fix — 2026-08-27

- `0.7.1` 新日志：105 ms 即报 `preflight / phase_timeout`；输入自检和 12 文件名额 /
  关闭恢复已通过，A/B SKIPPED。原因是 Gate 用旧 now 减切换后的新 phaseAt，uint32
  下溢；不能归因于用户操作或资源读取失败。
- `0.7.2-p3c.timer` 只修 Gate 计时，增加同一 C++ 函数的 13 项编译期回归与旧缺陷反例；
  阶段时限 45 秒、总自动时限 5 分钟及音频 / 显示阈值不变。真机状态仍 DEVICE TEST。
- 原 `0.7.0`：A 导航和长名两行 PASS，B SKIPPED，C 为 `real_track_media_missing_or_bad`。
  SD 资源与交付包一致；默认 5 文件名额是风险，但旧日志不能证明其为唯一根因。
- `0.7.1-p3c.fix`：完整键盘位图、25 ms 去抖、单键导航；歌词整组准备 / pin / 呈现，
  去横移、去“前奏”标签；提示卡和真实画面分离。
- 自动检查包括资源故障分类、29 组歌词实际字模容量、输入 / 帧契约与日志 NA / SKIPPED；
  这些是 PC 参考或源码检查，不冒充 ADV 运行结果。构建记录见 `docs/P3C_VALIDATION.md`。
- 设备前置检查包含真实 LRC、Cover CRC、代表字模、只读文件名额及关闭恢复；连续窗口
  仍为 60 秒，44100 Hz / Error=0 / Backpressure=0 / PCM gap≤70 ms。
- [ ] 单键无漏报、无重复；简化 A 导航和自动页面路由通过
- [ ] 正常预取后歌词 ≤200 ms 更新、整帧 ≤100 ms 呈现；呈现期媒体 SD 读取为零
- [ ] 新旧句不持续混杂；无提示压字、无前奏标签；正常 View 与浮层恢复
- [ ] 资源错误有组件 / 路径 / 操作 / errno / 长度；失败现场与测量数据分开
- [ ] 六环境自动构建和新版联合日志 / 人工确认全部通过后才收口；当前仍 DEVICE TEST

### P3A Text Fix — 2026-08-27

Automatic Validation：

- [x] `UiTextLayout` 使用当前字体像素度量、128-byte 行缓冲、UTF-8 安全换行 / 省略及区域裁剪，无动态分配
- [x] 四页动态文本不再使用固定字符数截断；Playlist 标记先预留实测宽度
- [x] `player-dev` 与 `player-p3a-gate` 均构建为 `0.5.2-p3a.textfix`，分别为 708,176 / 714,608 bytes，低于 `0x140000`
- [x] 静态 Font0 尺寸核对预期为 `ADVWalkman` 90 px + `Benchmark` 81 px，两行均不超过 97 px；这不是设备运行结果
- [x] Gate 已编入 `library_text_lines / width_px / available_px / truncated / invalid_utf8 / layout_error` 六项完整 `library_text_` 前缀日志与独立失败原因
- [x] 本轮仅生成本地 artifact；没有复制 SD、安装或重建历史 P1/P2 Gate

Device Validation — 等待 Gate A-fix+B+C：

- [ ] 真实 `ADVWalkmanBenchmark` 恰好两行、完整可读、不省略、不越界；UTF-8 / layout error 均为 false
- [ ] 原有方向、按键、页面导航与音频连续性无回归
- [ ] 联合 Gate 先报告 P3A 文本回归，再进入 B/C 验收；结果通过后才更新 P3A 完成状态

上述为 P3A 文本修复的历史构建记录；P3B 的当前进度见下文 P3-02。
内置字体的中日文字形能力不在文本修复范围，正式字体仍由 P3C 完成。

---

## P3-01 Portrait UI Shell

Status: DEVICE TEST

AC：

- [ ] 逻辑画布以 135×240 竖屏为主
- [ ] 耳机孔朝上为主要播放器姿态
- [ ] 不启用 IMU 自动旋转
- [ ] Cover三段与Lyrics无Header的两段结构均正确，Footer始终保留
- [ ] 所有文本按区域可用像素宽度和实际字体度量布局，不以固定字符数猜测宽度
- [ ] UTF-8 文本只在合法字符边界换行 / 截断，绘制区域带裁剪保护
- [ ] UI 更新不造成可感知音频卡顿

---

## P3-02 Now Playing Header / Footer

Status: DEVICE TEST

Automatic Validation — 2026-08-27：

- [x] 按路径 Metadata、显示模型、局部行缓冲、标题滚动、三秒音量显示事件接口已实现
- [x] 10 项 PC 几何 / 时间参考 / 源码契约检查通过；编译期实际 timing / volume 公式断言通过
- [x] 完整可注入时钟检查、M5GFX 裁剪和浮层背景恢复检查已编译；尚未在设备执行
- [x] Dev / P3A Gate / P2 Gate 均构建成功，715,552 / 721,744 / 724,256 bytes，均低于 `0x140000`
- [x] 生成物大小及 SHA-256 见 README；没有复制 SD、烧录或执行 P1/P2 历史设备测试
- [x] `P3BValidation` 保留独立显示 / 音频归因及 70 ms PCM 条件；未执行不写成 PASS

Device AC — 等待 Gate A-fix+B+C，不用构建结果代替：

- [ ] Cover 28 / 188 / 24 px、Lyrics 216 / 24 px；6 px 边距；Title14 / Artist12 / 时间14 px
- [ ] 按歌曲路径核对异步 Title / Artist；缺失时分别回退文件名 / 留空，不串歌
- [ ] 复用 P3A 文本布局；Header / Footer 在长文本和不同字号下均不越界
- [ ] 长 Title 静止约 5 秒后滚动一遍
- [ ] 滚动完成后再次静止约 5 秒
- [ ] 24 px/s、最多 20 fps；Pause 继续滚动，换歌 / 更新标题 / 重入页面重置
- [ ] Footer真实时间/进度/状态；右侧模式箭头或1/A/S与Original小标识互不重叠
- [ ] 恢复后未知总时长显示 --:-- / 未知进度，不额外 Probe；异常模式显示 ? 并保留原值
- [ ] 左侧音量浮层仅收到事件才显示，0 / 128 / 255 对应 0 / 50 / 100%，3 秒隐藏并局部恢复
- [ ] 实体 Vol+/− 每次 ±8 后反映实际音量；细条 / 数字无不透明背景，3秒局部恢复
- [ ] 标题 / 时间 / 状态局部刷新，非 Player 不因秒数整页重绘
- [ ] Header 动画不持续抢占歌词视觉
- [ ] Gate A-fix+B+C 真机显示通过，连续音频窗口 Error / Backpressure=0、PCM gap≤70 ms

---

## P3-03 Lyrics Renderer

Status: DEVICE TEST

P3C Automatic Validation — 2026-08-27：

- [x] 字体 / LRC / ASCII / View / Session 与联合 Gate 已实现；15 项媒体检查和 10 项 P3B 检查通过
- [x] 29 组真实歌词全部使用生成的 SD 字模做像素边界检查；密集 Latin 与极长句分页参考检查通过
- [x] Session 自测通过，旧零值 / 非法 View 值回退 Lyrics；没有改变原歌曲和 Queue 语义
- [x] Dev、P3ABC、P3A、P1 A/B、P2 六环境构建通过，均在 `0x140000` 内
- [x] 资源包与预览仅在 Git 忽略的本地目录；未写 SD、未烧录；详细记录见 `docs/P3C_VALIDATION.md`
- [x] 后续用户确认 SD 在 PC，已同步联合固件与 14 项资源并核对 Hash；旧 Dev / P2 / P3A BIN 备存 PC 后从 SD 移除，未烧录，仍待真机验收

以下行为与音频条件仍需联合 Gate / 人工显示确认，不以以上自动检查勾选真机 AC。

AC：

- [ ] 从 `/Lyrics/` 按 `/Music/` 相对路径和 basename 机械匹配逐行 LRC
- [ ] 支持基础 `<basename>.lrc` 以及 `zh-Hans / zh-Hant / en / ja / ko` 语言后缀
- [ ] 时间戳初始容差约 300 ms
- [ ] 中文 / CJK 竖排
- [ ] 英文字形逐 glyph 旋转 90°后纵向排列
- [ ] 正常播放仅当前组；中文右/原文左，同语言向左续列
- [ ] 仅当前双语组高亮；无前后句预览；当前长句续列不变暗
- [ ] 整组就绪再切换，不做横移动画，不出现长期新旧句混杂
- [ ] 不做逐字 Karaoke
- [ ] 歌词楷体18px微加粗、Times New Roman14px；英文整词换列；全部正常UI统一字体
- [ ] 字体大小允许真机约 ±2 px 微调
- [ ] UI 不自动旋转
- [ ] 中文右 / 原文左、同语言右起续列；长句先完整多列，仅极长句自动阅读分页
- [ ] 首句前灰色首组第一页，到期原位置点亮；Pause冻结分页、Seek直接定位；更新≤200ms / 呈现≤100ms
- [ ] LRC BOM / offset / 多时间戳、128 KiB / 512 cues / 1024-byte line 边界明确处理

---

## P3-04 Font Loading

Status: DEVICE TEST

AC：

- [ ] 主要 CJK / Latin 字体可从 SD 加载
- [ ] Flash 保留最小 fallback
- [ ] 缺少 SD 字体时系统不崩溃
- [ ] 字体加载失败不影响 Audio Core
- [ ] 字体原文件不默认提交到公开 repo

---

## P3-05 Color ASCII Cover Tool

Status: DEVICE TEST

目标：建立 PC 端机械批处理，不使用 Agent 一张一张生成。

AC：

- [ ] 递归扫描 `/Music` 并按相同相对路径 / basename 查找 `/CoverSource` 中的 JPG / PNG
- [ ] 生成彩色 ASCII
- [ ] 同时输出电脑 Preview
- [ ] 为每首歌曲输出独立 `/ADVWalkman/covers/<relative>/<basename>.cover.adv`
- [ ] `.cover.adv` 包含可验证的 Magic / Width / Height / Pixel Format 与 RGB565 Pixels
- [ ] 不建立 Album / Folder 公共封面、模糊匹配、Hash 数据库或 JSON Manifest
- [ ] 同目录重复 basename 明确报冲突，不猜测应该绑定哪一个音频版本
- [ ] 初始支持 26×20 / 30×24 / 34×26 三档
- [ ] 默认40×32，并提供34×26/48×40预览；保留真实字符mask，新旧ACOV兼容
- [ ] 方图135×135，其他比例适配135×188、居中不裁切；旧120×144资源兼容
- [ ] 批处理可重复运行，不需要 Agent 逐张介入

---

## P3-06 ASCII Cover Renderer

Status: DEVICE TEST

AC：

- [ ] 有歌词时可作为 View Selector 的 Cover 视图
- [ ] 无歌词时 Content Stage 只显示 ASCII Cover
- [ ] ADV 不实时做图片→ASCII 转换
- [ ] 可直接读取 RGB565
- [ ] 读图不造成明显音频卡顿
- [ ] 缺少 Cover 时有简单 fallback

---

## P3-07 Library UI

Status: DEVICE TEST — P3D VISUAL BUILT, COMBINED DEVICE TEST PENDING

AC：

- [ ] 上半区显示当前曲库的独立大封面
- [ ] 下半区显示横向叠放的黑胶唱片选择带
- [ ] Left / Right 切换曲库，Enter 进入对应播放列表
- [ ] 曲库名固定y174高22px单行，短名居中，长名按Player规则滚动；轮盘固定y196，不被标题挤压
- [ ] 当前唱片以上浮为主要高亮，并可辅以更亮 / 露出更多标签
- [ ] 非空固定三张，各有自己的蓝色弧形短名；1/2/多库正确映射、首尾循环且真实遮挡
- [ ] `S` 进入设置，Esc 在曲库页不继续退出
- [ ] 曲库封面与歌曲封面相互独立，不隐式继承第一首歌曲封面
- [ ] 浏览不主动停止当前音乐
- [ ] 切换动画短、轻，不造成可感知音频卡顿

---

## P3-08 Playlist UI

Status: DEVICE TEST

AC：

- [ ] 标准列表显示当前曲库名称、歌曲序号、Title 和选择高亮
- [ ] 正在播放歌曲可有独立标识；Artist 仅在不牺牲可读性时显示
- [ ] Up / Down 选择，Enter 播放并进入播放器页面
- [ ] Esc 返回曲库并尽量保持原曲库位置
- [ ] 底层复用 P1 Queue，但不建设独立 Queue 页面或复杂重排 UI

---

## P3-09 Page Navigation & Cross-page Playback

Status: DEVICE TEST

AC：

- [ ] 实际页面固定为播放器 / 播放列表 / 曲库 / 设置，无额外主页
- [ ] 播放器 Esc → 播放列表；播放列表在子目录先返回父目录，到曲库根后再 Esc → 曲库
- [ ] 曲库 `S` → 设置；设置 Esc → 曲库；曲库 Esc no-op
- [ ] 页面返回尽量保持当前歌曲、曲库和列表选择位置
- [ ] 音频跨页面持续，不因退出播放器或列表而 Stop
- [ ] 完整播放控制只在播放器页面，不复制到其他页面
- [ ] 有有效恢复歌曲时启动进入播放器并保持 Pause；无有效状态时进入曲库

---

## P3-10 Settings UI

Status: DEVICE TEST

AC：

- [ ] 亮度默认70%，10%～100%每次10%，即时预览/后台保存，失败显示未保存
- [ ] 两类息屏时间分别保存；默认Player3分钟、其他30秒，可选15/30/60/180/300/600秒/永不
- [ ] 关于显示项目名、版本和设备型号；菜单楷体/Times，简体中文
- [ ] 返回Launcher默认取消；确认后Pause保存位置/视图/显示设置再返回，失败不重启
- [ ] Display A/B+CRC恢复，不改变Player Session或增加音量持久化
- [ ] 不为了填充页面增加无实际用途的设置

---

## P3-11 Screen-off Soft Lock

Status: DEVICE TEST

AC：

- [ ] Player播放/暂停默认3分钟，其他页面30秒；播放/刷新不重置计时
- [ ] Screen Off 时所有按键原功能失效
- [ ] 第一次任意键只唤醒，包含无绑定键/修饰键/组合键，吞掉直到全部松开
- [ ] 全部松开后第二次按键才执行正常功能，不泄漏View/音量动作
- [ ] 唤醒后重用当前页面正常时限；永不/页面切换/时钟回绕正确
- [ ] 息屏 / 唤醒不打断音频
- [ ] V1 不单独实现复杂 Lock 系统

---

## P3-12 Now Playing View Selector

Status: DEVICE TEST

AC：

- [ ] 有可用歌词时可通过播放器 3×4 区的 `View` 在 Lyrics / Color ASCII Cover 间切换
- [ ] 无可用歌词时始终显示 Cover，按 `View` 不进入空白 Lyrics 页面
- [ ] `preferredNowPlayingView` 默认 Lyrics，只在用户成功切换时更新
- [ ] View 偏好跨歌曲保留
- [ ] 无歌词导致的临时 Cover 不覆盖用户偏好；下一首有歌词时按偏好恢复 Lyrics
- [ ] View 偏好通过现有 cooperative Session A/B 生命周期持久化，重启后恢复
- [ ] Footer不变；Cover有Header，Lyrics无Header并停止标题动画；切换无旧标题残留
- [ ] 切换不影响当前歌曲、播放状态、进度、Queue、Sound Preset 或 Volume
- [ ] 切换和 Cover / Lyrics 资源读取不造成可感知音频卡顿
- [ ] 其他页面不派发 `View` Action，也不暗中改变偏好
- [ ] 无歌词提示若实现，必须轻量且非阻塞；提示本身不是 V1 必做项
- [ ] 联合 Gate PASS 后 Enter 回普通曲库、保持暂停；同版本下一次启动不自动重跑

---

# P4 — Keymap

## P4-01 Input Context Router

Status: TODO

AC：

- [ ] 3×4 专用映射只在播放器页面生效
- [ ] 离开播放器页面立即恢复普通键盘映射
- [ ] 播放列表、曲库、设置中 Arrow / Enter / Esc 保持 UI 语义
- [ ] 曲库页面单独识别 `S → Settings`，其他页面的 `S` 不进入设置
- [ ] 旧数字列和 `H/L/Q/R/S/V` 全局快捷键不再生效

---

## P4-02 Now Playing 3×4 Blind Zone

Status: TODO

目标：

实现耳机孔朝上竖持时、仅属于播放器页面的顶部三排四列盲操区。以下数字是物理位置编号，不代表键帽字符。

Keymap：

```text
1  Volume +       2  Play/Pause  3  Play/Pause  4  Previous
5  Volume -       6  View        7  Play Mode   8  Next
9  Original      10  Tape       11  Radio      12  Vocal Clear
```

0.7.3授权例外：1/5音量与2/3播放暂停已接通代码，6 View沿用P3C；真机仍待验。
这不是P4整体完成，4/8上一首下一首、7播放模式与9–12音效仍TODO。

AC：

- [ ] 1 / 5 为 Volume + / -，4 / 8 为 Previous / Next
- [ ] 2 / 3 均为 Play / Pause，形成较大的盲操命中区域
- [ ] 6 为 View，7 为 Play Mode
- [ ] 9–12 直接选择 Original / Tape / Radio / Vocal Clear
- [ ] Esc 不属于 3×4 区，仍返回播放列表
- [ ] 离开播放器页后这 12 个物理位置恢复普通键盘语义
- [ ] 所有 Action 不直接耦合 Audio Backend

---

## P4-03 Play Mode Action

Status: TODO

循环顺序：

```text
Normal → Repeat One → Repeat All → Shuffle → Normal
```

AC：

- [ ] UI 四态原子映射到既有 `RepeatMode + Shuffle` 两维模型
- [ ] Normal = Off + Shuffle Off
- [ ] Repeat One = One + Shuffle Off
- [ ] Repeat All = All + Shuffle Off
- [ ] Shuffle = Off + Shuffle On，一轮结束后停止
- [ ] 不暴露 Repeat + Shuffle 的复杂组合
- [ ] 切换不重启当前歌曲，Footer 明确反馈当前模式

---

## P4-04 Normal-page Input

Status: TODO

AC：

- [ ] 播放列表 Up / Down 选择、Enter 播放、Esc 返回曲库
- [ ] 曲库 Left / Right 选择、Enter 进入列表、S 进入设置、Esc no-op
- [ ] 设置 Arrow / Enter 操作、Esc 返回曲库
- [ ] 未来若出现文本输入，数字 / 字母恢复普通输入且不派发播放器 Action

---

## P4-05 Screen-off Input Behavior

Status: TODO

AC：

- [ ] Screen Off 时全部快捷键原功能失效
- [ ] 任意键第一次只唤醒屏幕
- [ ] 唤醒按键事件不继续传递
- [ ] Screen Off 时第一次按 `View` 只唤醒，不切换 View
- [ ] 亮屏后第二次按键才执行
- [ ] V1 不增加独立 Lock / Unlock 按键
- [ ] V1 不要求长按或组合键

---

# P5 — DSP / Sound

## P5-01 Sound Preset Engine

Status: TODO

目标：

实现最小可行音效系统。一次只能选择一个固定 Preset，不做叠加，不向用户开放复杂参数。

V1 Preset：

- Original
- Tape
- Radio
- Vocal Clear

AC：

- [ ] 四种 Preset 可切换
- [ ] 一次只允许一个 Preset 生效
- [ ] Original 不增加主动声音染色
- [ ] Tape 实现轻度暖化、高频衰减和轻软饱和
- [ ] Radio 实现约 200–5000 Hz 带通、轻压缩和轻软饱和
- [ ] Vocal Clear 轻度突出中频和 2–4 kHz 人声存在感
- [ ] 切换 Preset 不崩溃
- [ ] 切换时无频繁明显爆音
- [ ] Preset 不造成可感知播放卡顿
- [ ] 用户不需要手动调 DSP 参数

---

## P5-02 Gain / Limiter Safety

Status: TODO

AC：

- [ ] 预设处理后不存在频繁明显 clipping
- [ ] Original 路径不被过度压缩
- [ ] 保护逻辑低延迟、低复杂度
- [ ] 参数通过真机 A/B 小范围微调
- [ ] 不实现复杂 Mastering Limiter

---

## P5-03 Deferred Effects

Status: DEFERRED

V1 不做：

- Surround / Spatial Audio
- Stereo Widening
- Reverb
- Wow & Flutter
- Bitcrusher
- Vinyl Noise
- 复杂 Tape Simulation

这些能力不属于当前 MVP。外置 Stereo Backend 或后续版本出现明确需求时再评估。

---

# P6 — System & Polish

## P6-01 Screen Off Playback

Status: TODO

AC：

- [ ] 屏幕关闭音乐继续
- [ ] 唤醒不打断播放
- [ ] 熄屏按键行为按最终 Keymap 执行

---

## P6-02 Launcher Integration

Status: TODO

AC：

- [ ] Launcher 可安装
- [ ] 正常启动
- [ ] 正常返回 Launcher
- [ ] 不使用猜测 offset
- [ ] 不破坏 Launcher 分区

---

## P6-03 Long-run Stability

Status: TODO

AC：

- [ ] 2h 连续播放
- [ ] Heap 无持续明显下降
- [ ] 长时间熄屏播放
- [ ] 多次切歌 / seek
- [ ] 无周期性崩溃

---

## P6-04 V1 Audio Format Compatibility

Status: TODO

目标：在不重新选择 Candidate A Audio Backend 的前提下，把当前已验证的 MP3 主路径扩展为 V1 的 MP3 / FLAC / WAV 格式集合。

AC：

- [ ] Format Detector 与 format-specific Decoder 不写入 PlayerController 状态机
- [ ] MP3 既有 320 kbps / CBR / VBR / Seek / Persistence 行为不回归
- [ ] FLAC 在无 PSRAM 内存预算内稳定解码并正确 Stereo → Mono 下混
- [ ] WAV 支持最终确认的常见 PCM 组合，不承诺 Hi-Res
- [ ] Library 只在对应 Decoder 可用后把 `.flac` / `.wav` 暴露为可播放文件
- [ ] FLAC / WAV Metadata 使用各自适配器，不把 ID3 假装成通用格式
- [ ] 三种格式共享 DSP、Limiter、Volume 和 Candidate A Backend
- [ ] 格式切换、Pause / Resume、Next / Previous 不造成崩溃或持续 Heap 下降

---

# Later / Deferred

当前不做：

- AAC / M4A
- OGG / Opus
- 传统图片 Album Art 直接显示（V1 仍保留 PC 预生成 Color ASCII Cover）
- External Stereo DAC
- External Codec
- Bluetooth Audio
- Web Radio
- Phone App
- Wi-Fi Library Sync
- Advanced Reverb
- Spatial Audio
- Hi-Res

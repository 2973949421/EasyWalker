# Changelog

## V0.4 — P2 Music Library Development

Date: 2026-08-26

### V1 UI Design Increment — Now Playing Dual View

- 将 Now Playing Content Stage 从“有歌词自动显示 Lyrics、无歌词显示 Cover”扩展为：有可用歌词时可通过 `V` 在 Lyrics / Color ASCII Cover 间切换，无歌词时保持 Cover-only。
- Header / Footer、当前歌曲、播放状态、进度、Queue、Sound Preset 与 Volume 不受 View 切换影响。
- 冻结 `preferredNowPlayingView = Lyrics | Cover`：默认 Lyrics，跨歌曲和重启保留；无歌词时临时退化为 Cover 不覆盖用户偏好。
- `V` 仅在 Now Playing 且有可用歌词时改变偏好；其他页面 no-op，Screen Off 时第一次按键仍只唤醒并吞掉事件。
- 技术方案保留现有两个 Renderer，仅增加薄型 View Selector；后续实现可复用 Session v1 预留字段与既有 cooperative A/B 保存，不新增状态文件。
- 本次只同步 V1 设计、Keymap 与 AC，不提前实现 P3 UI，也不改变当前 P2 `DEVICE TEST` 状态或既有产品路线。

### Library Engine

- 新增以 `/Music` 为边界的多层目录浏览、隐藏项与非 MP3 过滤、文件夹优先自然排序，以及当前文件夹非递归 Queue。
- 新增 cooperative `Open → Scan → Sort → Finalize` 扫描、4 个 SD session cache slot、3×32 项 RAM LRU page 与 Queue 生命周期 pin；不把整库路径常驻 RAM。
- 新增 ID3v2.3 / v2.4 Metadata reader，支持 ISO-8859-1、UTF-16 BOM、UTF-16BE、UTF-8、unsynchronization、extended header 与安全 fallback；APIC 只跳过。
- 新增 32 项 Recent Tracks 与 CRC32 A/B 双槽；累计 Playing 5 秒后记录，Pause 不计时，缺失路径读取时忽略。
- 正式 Player 主循环采用四路轮转，一轮只执行一个有界 Library / Queue selection / Metadata / Recent 工作；当前曲目路径改为 RAM cache，避免播放中每轮读取 SD。

### Validation Harness

- 新增 marker-owned P2 Fixture 和主机 validator，覆盖多层中文/日文路径、1,000 项大目录、自然排序、过滤、ID3 编码与 Recent/Cache binary CRC；测试数据与 MP3 不提交 Git。
- 新增 `player-p2-gate`：一次按 `T` 自动覆盖 P2-01～P2-04，屏幕只在阶段变化时刷新，停止音频后再写四份完整日志。
- 根据首轮真机日志修正 Gate 诊断口径：相邻 Player service 入口间隔与各主循环阶段耗时独立记录，不再把混合采样间隔误报为 Speaker starvation；最大间隔冻结上一轮阶段与耗时，Pause / Resume 后首轮空 channel 不计为异常，并补充 FAIL 日志主机端诊断输出。
- `player-dev`、P2 Gate 和两个历史 P1 Gate 均保留独立构建入口；P2 不改变 Candidate A Audio Backend、P1 Queue / Session 语义或 Launcher 分区。
- PC Fixture、静态校验与自动构建已通过；P2-01～P2-04 当前统一处于 `DEVICE TEST`，必须以四份真机日志为准，尚未标记 `DONE`。

## V0.3 — P1 Player Core Development

Date: 2026-08-25～2026-08-26

### Player / Audio

- 新增隔离的 `player-dev` PlatformIO 环境，正式 Player 不编译 P0 Candidate B/C 或 IDF5 实验源码。
- 从 Candidate A 提取 `Mp3PlaybackEngine` 与 3×768-sample `M5SpeakerPcmOutput`，保持 32-bit Stereo → Mono downmix 和 `128/255` 音量。
- 新增 CBR、Xing/Info、VBRI 与无 TOC VBR 的有界 Probe / Seek；保存 source offset 作为恢复重同步提示。
- 自然 EOF 先排空 M5.Speaker 尾部 Buffer，再发送单次 `TrackEnded`；截断文件和 Decoder / Read Error 不再冒充自然结束。
- 新增 Play / Pause / Resume / Stop / Next / Previous / Seek，以及冻结的 5 秒 Previous 行为。

### Queue / Persistence

- 新增最多 1,024 首的有界 Queue、Fisher–Yates Shuffle、Repeat Off / All / One 和 32 项 Previous history。
- 新增按需 `TrackSource`，不把 1,024 条完整路径常驻 RAM。
- 新增 Queue / Session schema v1 与 SD A/B 双槽、CRC32、完整 pair 回退、回读校验和每步不超过 1 KiB 的 cooperative 保存。
- 恢复统一为 Paused；启动不打开 Decoder、不自动出声。缺失当前歌曲时寻找下一首有效 MP3。

### Validation Harness

- 生成并归档到本地 / SD 的无版权 CBR 44.1 kHz、VBR 44.1 kHz、CBR 48 kHz 与真实截断 MP3 Fixture；Fixture 不提交 Git。
- 新增 P1 Gate A / B 开发测试状态机、设备端 Fixture SHA-256 核验、VBR Seek 后实际解码验证、串口 Transport 命令和 `/ADVWalkman/logs/p1-01-last.txt`～`p1-04-last.txt`。
- 新增只读 MP3 / 状态槽检查工具和统一 `ADV-Walkman-Dev.bin` 构建脚本。
- Gate A / Gate B 构建环境均已完成自动构建与 Launcher 体积检查；编译成功不代替 Device Validation。
- 首次 Gate A 真机运行暴露两个 Harness 问题：libmad 在正常 EOF 留下的 `MAD_ERROR_BUFLEN` 被误判为 Decoder Error；无版权 Fixture 约 `-40 dBFS`，叠加 `128/255` 平方音量曲线后几乎不可听。
- `0.3.0-p1.gate-a2` 允许正常 EOF 的 terminal `BUFLEN`，通过 Xing/Info/VBRI 声明字节数提前拒绝本轮截断 Fixture，并将 Fixture 提升到保守但可听的约 `-20.4 dBFS` Mono Peak；正式 Player 音量仍保持 `128/255`。
- Gate 测试画面改为仅在阶段切换时整屏刷新，消除原先每 250 ms 清屏造成的频闪；FAIL 日志在 Stop 前保存快照并增加 `player_error / audio_error`。
- 2026-08-26 Gate A 真机通过：三份合法 MP3、截断/缺失错误处理、Pause/Resume、Stop/Replay、Next/Previous、CBR/VBR Seek 全部通过；Fixture Hash 全匹配，最大 Seek 误差 60 ms、Backpressure 0，用户确认声音正常。P1-01、P1-02 完成，进入 Gate B 的 P1-03、P1-04 真机验收。
- 2026-08-26 Gate B 真机通过：Sequential、Shuffle、Repeat One/All/Off、Previous history 与模式切换断言全部通过；状态写入 SD 后受控重启，正确恢复第 2 首约 4 秒位置、Repeat All、Shuffle order/cursor/history，并保持 Paused 与至少 3 秒静音。
- 主机只读解析 `queue-a.bin / session-a.bin` 确认 schema v1、generation 1、CRC32、三首完整 UTF-8 路径和 Session 字段有效。P1-03、P1-04 完成，P1 Player Core 正式收口；下一阶段按既定顺序进入 P2 Music Library。

## V0.2 — V1 Design Baseline

Date: 2026-08-24

### Product / UI

- 冻结耳机孔朝上竖持的主要播放器姿态，V1 不启用 IMU 自动旋转。
- Now Playing 采用 Header / Content Stage / Footer 三段结构。
- 长标题先静止约 5 秒，再滚动一遍，再静止约 5 秒。
- 有歌词时优先显示歌词；无歌词时显示彩色 ASCII Cover。
- 歌词采用逐行 LRC，不做逐字 Karaoke。
- 上一组在左、当前组居中、下一组在右；换句整体向左移动。
- 外文歌曲支持 `.lrc + .zh.lrc` 的“原文 + 中文译文”双语模式。
- 中文 / CJK 默认楷体约 16 px。
- 英文默认 Times New Roman 约 12 px，每个字形单独旋转 90°后纵向排列。
- 大字体优先从 SD 加载，Flash 只保留最小 fallback。

### ASCII Cover

- 采用 PC 端机械批处理，不使用 Agent 逐张生成。
- 扫描封面文件或 MP3 Embedded Cover。
- 转换为彩色 ASCII / ANSI 风格艺术。
- 同时生成电脑 Preview 和 ADV 预渲染 RGB565。
- ADV 直接读取 RGB565，不实时执行图片→ASCII。
- 初始测试 26×20 / 30×24 / 34×26，默认先测 30×24。
- Pixel Cover 推迟到 Later。

### Other Screens

- Library：简单目录 / 歌曲列表，浏览期间当前音乐继续播放。
- Queue：简单队列列表，可直选歌曲。
- Sound：Original / Tape / Radio / Vocal Clear 四项。
- Settings：Brightness / Screen Timeout / About / Return to Launcher 等最小集合。

### Screen-off

- 息屏状态等同 V1 Soft Lock。
- Screen Off 时全部按键原功能失效。
- 第一次任意按键只唤醒并吞掉事件，第二次按键才执行功能。
- 正常播放默认约 15 秒无 UI 操作息屏；唤醒后约 5 秒无输入重新息屏。
- V1 不增加独立 Lock 系统。

### Documentation

- 修正 `REFERENCES.md` 的硬件事实优先级。
- 清理 PRD / Technical Design 中残留的 V0.1“待确认”表述，以已经冻结的 V0.2 Keymap、UI 和 Screen-off 规则为准。
- 明确 Lyrics 与 PC 预生成 Color ASCII Cover 属于 V1；仅传统图片 Album Art 直接显示推迟到 Later。
- V1 产品侧设计达到可正式交付 Codex 的基线。
- 下一正式开发阶段仍为 P0 Audio Backend Benchmark。

### P0-01 Benchmark Harness

- 建立 `bench-a`、`bench-b`、`bench-c` 三个 PlatformIO 环境。
- Candidate A 采用 ESP8266Audio 1.9.7、三缓冲 M5.Speaker 输出与 32-bit Stereo → Mono 下混。
- B / C 当前为明确的可编译占位，不伪装成已实现 Backend。
- 增加串口指标、最小控制命令、SD 测试文件 SHA-256 与错误状态输出。
- 增加只读 MP3 检查工具和 Launcher app 尺寸硬检查。
- 三环境自动构建通过。
- P0-01 已通过 M5Launcher、固定 320 kbps / 44.1 kHz / Stereo MP3、扬声器、3.5mm 耳机和串口指标真机验收。
- 设备端测试文件 SHA-256 与 PC 记录一致；Candidate A 运行于 44.1 kHz 且无启动错误。

### P0 Backend Route Refinement

- 保持既有 A/B/C Benchmark 和 V1 产品路线不变，将三个候选明确落实为隔离的 M5Launcher App。
- 删除低收益的 M4A 衍生、48 kHz / VBR 测试矩阵；P0 只使用已确认的 320 kbps / 44.1 kHz Fixture。
- 将 Restart、Seek、UI Stress、SD Stress 和统一统计协议纳入 P0-02～P0-06。
- Candidate C 固定为独立 pioarduino / IDF5 环境，不迁移或污染 A/B 稳定环境。
- Candidate A 补齐 Restart、Seek、Loop 和真实 `playRaw()` backpressure 统计。
- Candidate B 落实为 ESP8266Audio Direct I2S Port 1 + M5Unified 0.2.20 Cardputer ADV ES8311 官方初始化序列。
- Candidate C 落实为 BackgroundAudio 1.4.4 + Arduino-ESP32 3.3.8 / ESP-IDF 5.5.4，并提供统一 Mono downmix 包装。
- 增加统一串口控制、30 Hz UI Stress、第二只读文件句柄 SD Stress 和扩展统计字段。
- Candidate C packages 独立放在 `B:\PlatformIO\isolated\adv-walkman-c\packages`，避免替换 A/B 的稳定 framework。
- 三候选已通过自动构建和 Launcher 尺寸检查，状态进入 `DEVICE TEST`；最终 Backend 尚未选择。
- 根据首轮真机听感将 P0 收敛为 A/B 二选一；C 无明确听感收益且工具链、固件与维护成本最高，转为 `DEFERRED` 备用。
- 首次等响度固件将 B 向 A=`64/255` 下调，真机反馈两者均不舒适；改为以用户已听过的原 B 响度为基准，A=`128/255`、B=`0.25`，仍使用完整原曲各听一次。
- 将人工验收缩短为听感胜者约 3–4 分钟的 UI/SD 联合压力测试；长期稳定性转入 P1 实际使用验证。
- 因当前已安装应用串口没有返回可用响应，Candidate A 增加 `T` 键一键自动压力测试：Baseline 30 s → UI 60 s → UI+SD 60 s → Pause 3 s → Resume / Seek / Restart 各观察 10 s，总计约 3 分钟。
- 自动测试的压力阶段只读既有 Benchmark MP3、不写 Flash；测试前先写入并关闭 `RUNNING` 标记，结束并关闭 SD Stress 后再一次性覆盖 `/ADVWalkman/logs/p0-a-stress-last.txt`。结果页显示 `PASS/FAIL`、state / SR、heap delta / sampled minimum heap、backpressure、service max、UI frames、SD KiB 和日志状态，并以 `Listen: manual` 明确保留人工听感验收。
- 首次 `0.2.0-p0.a-stresslog` 真机运行停在 `BASELINE / PLAYING / SR=0`；持久化 `RUNNING` 日志确认首次 Decoder service 未返回主循环。
- 根因是 M5.Speaker 队列等待后通常返回成功，导致 ESP8266Audio 在 `ConsumeSample()` 持续为 `true` 时长期留在单次 `loop()`；A 改为每成功提交一个 768-sample Buffer 后合作式让出主循环，不丢样、不重复，也不伪增 Backpressure。
- 修复版 `0.2.0-p0.a-stresslog2` 构建为 648,224 bytes，在现有 640 KiB App 槽中余 7,136 bytes；SD 副本 SHA-256 为 `db2e2e76644ecaf40b04afb4a829fce77949a4edac70f7bc0f9bbf565c4d87eb`。
- 修复版完成 183,044 ms 真机自动压力测试：44.1 kHz、Heap delta 0、Backpressure delta 0、UI 3,870 frames、SD 额外读取 19,988,356 bytes，Pause / Resume / Seek / Restart 全部通过。
- 根据等响度听感、真机压力结果和维护成本，P0-06 冻结 Candidate A 为 V1 Audio Backend；B 保留为可工作备选，C 继续 Deferred，PRD 产品范围不变。
- Launcher 尺寸检查改为按环境使用真实分区上限：A/B 为 `0xA0000`，Deferred C 为历史 `0x3F0000`，避免 A/B 超过 640 KiB 时仍被构建误放行。

## V0.1.2 — Keymap Freeze

Date: 2026-08-24

### Changed

- 冻结 V1 Keymap 主体。
- 将“耳机孔朝上、设备竖持”确定为播放器的重要使用姿态。
- 方向键、Enter、Esc 等核心功能键保留 UI / 导航语义。
- 数字列用于高频播放和音效直选：
  - `0` Volume +
  - `9` Previous
  - `8` Play / Pause
  - `7` Next
  - `6` Volume -
  - `5` Reserved
  - `4` Vocal Clear
  - `3` Radio
  - `2` Tape
  - `1` Original
- 字母快捷键：
  - `H` Now Playing / Home
  - `L` Library
  - `Q` Queue
  - `R` Repeat Mode
  - `S` Shuffle
- 文本输入状态下，字母和数字恢复普通输入，不触发播放器快捷键。
- Lock、熄屏快捷键和长按 / 组合键细节留到 UI / System 阶段确认。
- UI 视觉设计仍待确认。

## V0.1.1 — Sound MVP Freeze

Date: 2026-08-24

### Changed

- 冻结 V1 最小可行音效方案。
- V1 固定四种互斥 Preset：Original / Tape / Radio / Vocal Clear。
- 用户不手动调 DSP 参数，参数由固件内置。
- Tape：轻度 EQ 染色 + 高频衰减 + 轻软饱和。
- Radio：约 200–5000 Hz 带通 + 轻压缩 + 轻软饱和。
- Vocal Clear：轻度突出中频和 2–4 kHz 人声存在感。
- V1 不做 Surround / Spatial Audio、Stereo Widening、复杂 Reverb、Wow & Flutter、Bitcrusher、Vinyl Noise。
- 音效复杂度不得牺牲连续播放稳定性。
- UI 和 Keymap 仍待确认。

## V0.1 — Development Baseline

Date: 2026-08-24

### Added

- 建立 ADV Walkman 项目文档基线。
- 明确项目定位：复古随身播放器 + 嵌入式音频学习。
- 冻结第一阶段硬件：Cardputer ADV 原生 ES8311 + 3.5mm + 现有旧耳机。
- 明确 V1 以 MP3 为主，目标最高 320 kbps。
- 明确音乐根目录 `/Music/`，允许自由多层目录。
- 明确开机恢复歌曲、位置、队列和播放模式，恢复后保持 Pause。
- 明确 UI、Keymap、具体 DSP 音效暂不冻结。
- 建立 P0 Audio Backend Benchmark：
  - Cardio-style M5.Speaker
  - Direct I2S
  - BackgroundAudio
- 建立开源方案参考：
  - Cardio
  - BrokenSignal / BrokenSignal Next
  - SomaFM for CardPuter
  - BackgroundAudio
  - ESP8266Audio
  - M5Stack / M5Unified / M5Cardputer
- 建立轻量 Backlog + AC。
- 建立 Codex / Agent 工作规约。

### Engineering Rules

新增项目级开发原则：

- 效率优先；
- 不过度设计；
- 不过度防御；
- 不把普通开发做成电脑攻防大战；
- 允许 Codex 自主新增依赖、合理重构、Commit 和正常真机 Flash；
- Build Success 与 Device Validation 分离；
- 小问题 Agent 自行解决，重大产品 / 技术取舍与用户沟通。

### Firmware

- 尚未开始正式 V1 firmware 实现。
- 下一主任务：P0 Audio Backend Benchmark。

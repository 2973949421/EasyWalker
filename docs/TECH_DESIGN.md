# ADV Walkman Technical Design

> 版本：V0.3
> 状态：基线架构；V1 Audio Backend 已由 P0 Benchmark 冻结为 Candidate A

## 1. 设计目标

技术设计优先级：

1. 音频连续性
2. 稳定性
3. 低内存占用
4. 快速实体交互
5. 简单可维护
6. 后续可替换音频输出底层
7. UI 个性化

不以抽象层数量、设计模式数量或“未来可扩展性”作为主要评价标准。

---

## 2. 硬件约束

目标平台：M5Stack Cardputer ADV

当前关键约束：

- ESP32-S3FN8
- 双核
- 8MB Flash
- 无外置 PSRAM
- microSD
- 240×135 TFT
- 56 键键盘
- ES8311
- 原生 3.5mm
- 原生音频链路为单声道输出

因此：

- 不能按有大 PSRAM 的 ESP32 音频板思路设计；
- 音乐库不应整库常驻 RAM；
- UI 刷新不能长时间占用音频喂数路径；
- Stereo 音源需要正确下混到 Mono；
- 后续如需真立体声，必须走外置 Audio Backend。

---

## 3. 总体架构

```text
microSD
   │
   ▼
┌─────────────────────┐
│ Library             │
│ 音乐库               │
│ lazy scan / cache   │
└─────────┬───────────┘
          │
          ▼
┌─────────────────────┐
│ Player              │
│ 队列 / 状态 / 播放模式 │
└─────────┬───────────┘
          │
          ▼
┌─────────────────────┐
│ Format / Decoder    │
│ MP3 / FLAC / WAV    │
└─────────┬───────────┘
          │ Stereo PCM
          ▼
┌─────────────────────┐
│ DSP                 │
│ EQ / effects        │
└─────────┬───────────┘
          │
          ▼
┌─────────────────────┐
│ Downmix / Safety    │
│ Stereo→Mono / Limit │
└─────────┬───────────┘
          │ Mono PCM
          ▼
┌─────────────────────┐
│ Audio Backend       │
│ 音频输出底层         │
└─────────┬───────────┘
          │
          ▼
       ES8311
          │
          ▼
        3.5mm
```

UI 与 Player / Library 交互，但不能直接控制底层 I2S 生命周期。

---

## 4. 模块设计

## 4.1 Library

职责：

- 浏览 `/Music/`
- 按需扫描目录
- 过滤文件
- 构建当前目录视图
- 缓存最近访问目录
- 读取基础 Metadata
- Recent
- 为 Player 提供可播放 Track

### 内存策略

参考 BrokenSignal Next：

- Lazy Scan（按需扫描）
- Pagination / Window（分页 / 窗口）
- LRU 风格缓存淘汰
- 不一次将完整音乐库装入 RAM
- 对大目录只维护当前展示或播放所需数据

V1 不建立复杂数据库。

### P2 行为与边界

- Library 根目录固定为 `/Music`，导航不得逃出该根目录；完整 UTF-8 路径最多 511 bytes，超长报错而不截断。
- Recursive Folder Browser 表示用户可逐层进入任意深度，不表示启动时递归扫描整库。
- P2 已验证实现只显示非 dot-hidden 目录和大小写不敏感的 `.mp3`。V1 最终在格式扩展任务完成后将同一过滤入口扩展到 `.flac` / `.wav`；不得在 Decoder 尚未可用时先把不可播放文件暴露给用户。
- 顺序固定为“文件夹优先 + ASCII 大小写不敏感自然排序”；数字段按数值比较，非 ASCII UTF-8 按原始字节保持稳定顺序。
- 在 Library 选中 Track 后，Queue 为“当前文件夹内排序后的受支持音频”，不递归包含子目录，并从所选 Track 开始。P2 Gate 阶段的受支持集合仍只有 MP3。
- Browser 当前页不直接充当 `TrackSource`。正在播放的 `FolderQueueSource` 必须绑定不可被 LRU 淘汰的稳定目录索引，直到下次 Queue 安全发布。
- 单目录最多索引 2,048 个“目录 + MP3”条目；单目录超过 1,024 首 MP3 时仍可浏览，但创建 Queue 明确返回 `QueueTooLarge`。

### P2 扫描与缓存

- 当前目录使用 `Open → Scan → Sort → Finalize → Ready / Error` cooperative 状态机。目录扫描每轮只处理一个目录项；Arduino-ESP32 2.0.16 使用官方 `getNextFileName(bool*)` 读取名称和类型，禁止为枚举目的通过 `openNextFile()` 对每个文件额外执行 `stat + fopen`。RAM 排序和 Finalize 使用 750 µs 软时间片。该预算约束可拆分的 CPU 工作，不把目录读取、SD read/write/close 等不可拆分调用伪装成确定的硬实时上限，超时调用必须按阶段记录。
- SD session cache 位于 `/ADVWalkman/cache/library/`，使用 4 个目录索引槽；重启后重建，不建设持久化音乐数据库。
- RAM 使用 3 个 32-entry page 做 LRU，最多驻留 96 个条目。扫描和排序仅保留数字 offset，不将整批路径常驻 RAM。
- 扫描期临时分配 2,048 项 `SortKey` 和两组 16-bit order，总计 65,536 bytes；每项缓存 20-byte basename prefix，只有比较结果无法在 prefix 内确定时才回读完整名称。相较旧 24-byte prefix 回收 8 KiB Heap 给正式音频缓冲，排序语义不变，只可能增加少量精确 fallback。Scan 阶段复用尚未参与 merge 的 `indicesB` 作为 4 KiB `.dat` 写缓冲，将逐记录小写合并为少量批量写；进入 Sort 前必须完整落盘，`.dat/.idx` 格式和 record offset 语义不变，且不得增加额外内存峰值。排序完成、取消或失败后立即释放 Scratch；`scratch_bytes` 记录本轮峰值而非当前占用。
- 当前 Queue 的索引槽保持 pinned；新 Queue 在 Player persistence 空闲后再切换，旧索引才可解除 pin。
- 应用循环始终先 service Player，再做一次有界 Library / Metadata / Recent 工作；无需新增 RTOS task。
- `LibraryRuntime` 采用 work-aware 调度：活跃 Directory 工作至少获得四轮中的三轮；Metadata / Recent 仅在确有工作时占用后台轮次。Recent 的 Playing 时间每轮在 RAM 中累计，SD 发布仍 cooperative。正式开发串口的目录 / Recent 输出同样使用逐项游标，不在一次命令中同步遍历整个目录。
- `PlayerController` 在 Queue / 当前曲目变化时缓存规范化完整路径；`PlayerRuntime::currentPath()` 只复制 RAM 缓存，Recent 与状态画面不得在每轮播放循环重新打开 Library index。
- `LibraryRuntime` 与其 pinned `FolderQueueSource` 是单次绑定生命周期；禁止对仍被 Player 引用的实例执行重新 begin。Queue source 只有在没有异步 State Store 工作时才能解除 pin。
- P2 Gate 使用独立 Recent 测试槽并暂停正式 Queue / Session persistence；四份日志在停止测试音频后一次写完并回读完整性标记，避免日志本身污染播放中断音指标。
- P2 Gate 先用短 Fixture 验证 P2-01 Queue / Recent 语义，再通过正式 `MusicLibrary → FolderQueueSource` 切换到 `/Music/ADVWalkmanBenchmark/benchmark.mp3`。长曲播放期间执行 P2-02、P2-03 及 P2-04 的真实 5 秒 Recent 发布；设备端确认千文件 count 后抽查 32 个首尾、分页边界和远端 LRU 代表点，PC Fixture 继续全量核对文件集合与参考排序。Recent 的 32 项 MRU、缺失路径过滤和 A/B CRC 冷加载属于启动生命周期，在捕获 live-audio 快照并停止音频后验证，不得把冷启动同步读取伪装成播放期负载。
- 音频硬指标保持 Player state、44.1 kHz、Audio Error、Backpressure、TrackEnded、PCM Buffer 提交进度与 PCM 提交间隔。正式 Player 的三组 1536-sample Buffer 总余量约 104 ms，Gate 使用 70 ms 缓冲感知上限，避免原 100 ms 门槛把用户可听见的耗尽误判为 PASS。单个 Player service 间隔超过 100 ms 只记录 timing warning；连续三次超过 100 ms 或单次超过 500 ms 仍为硬失败。`M5.Speaker.isPlaying(0)` 只表示 M5Unified 请求槽占用，保留为诊断采样，不得命名或判定为硬件 underrun / starvation。测量期间不刷新屏幕，停止音频后再显示和写日志。
- 最终 `0.4.4-p2.final-gate` 在长曲压力阶段测得最大 PCM 间隔 61.796 ms，低于 70 ms 上限；1,000 项扫描、Metadata 与 Recent live publish 期间无 Audio Error / Backpressure / 意外 EOF，最低 Heap 90,148 bytes。P2-01 的 112.933 ms WARN 属于短曲 EOF / 切歌生命周期，Repeat One 快速重启 8.832 ms，不纳入连续长曲门槛，也不得从历史报告中删除。

---

## 4.2 Player

职责：

- 当前 Track
- Queue
- 当前播放位置
- 播放状态
- 播放模式
- 自动下一首
- Shuffle
- Repeat One
- Repeat All
- 状态恢复

建议 Player 不负责：

- I2S 细节
- ES8311 寄存器
- UI 绘制
- 目录扫描实现

### P1 正式模块

```text
Mp3PlaybackEngine
→ 单首 MP3、Decoder、PCM Output、位置、EOF、错误

PlayerController
→ Transport、Queue、Repeat、Shuffle、自动下一首

PlaybackQueue
→ 有界顺序、Shuffle order、Previous history

PlayerStateStore
→ SD Queue / Session A/B 双槽保存

PlayerRuntime / PlayerMain
→ cooperative service、设备初始化、诊断与开发测试入口
```

`player-dev` 与 P0 Benchmark 源码通过 PlatformIO source filter 隔离。P0 A/B/C 保留为历史实验，不编入正式 Player。

### 状态与行为

Audio Engine 状态：

```text
Empty / Loading / Playing / Paused / Draining / Stopped / Error
```

Player 状态：

```text
Empty / Stopped / Playing / Paused / Error
```

规则：

- `Repeat Off / All / One` 与 `Shuffle Off / On` 独立；
- Repeat One 只接管自然 EOF，手动 Next / Previous 仍可离开当前歌曲；
- Previous 在当前位置超过 5 秒时回到本曲开头，否则按真实历史进入上一首；
- Pause 中 Seek / Next / Previous 后保持 Pause；
- Stop 关闭音频、位置归零，但保留 Queue 和当前歌曲；
- 文件或 Decoder Error 不自动重试，也不自动跳过。

Queue 最多 1,024 首，单条 UTF-8 路径最多 511 bytes。RAM 只保存 source index、播放顺序、32 项 Previous history 和少量当前路径；完整路径由 `TrackSource` 按需读取。

---

## 4.3 Format / Decoder

V1 产品目标为 MP3 / FLAC / WAV。当前已完成并真机验证的是 MP3；增加 FLAC / WAV 不重新开启 Audio Backend 选型，也不改变 Decoder → PCM → DSP → Downmix / Safety → Candidate A Backend 的边界。

```text
Audio File
→ Format Detector
→ format-specific Decoder
→ PCM
→ shared DSP / Downmix / Backend
```

MP3 目标：最高 320 kbps、44.1 / 48 kHz、CBR / VBR、16-bit PCM。FLAC / WAV 的具体位深、声道和采样率兼容矩阵在 V1 格式任务中以实际库能力和无 PSRAM 内存预算冻结；不承诺 Hi-Res。

P0 已冻结 `ESP8266Audio 1.9.7`。P1 的 `Mp3Probe` 负责跳过 ID3v2、识别 MPEG Layer III、CBR / VBR、Xing / Info / VBRI，并为 Seek 提供有界 Frame resync。无 TOC 的 VBR 使用比例估算后在最多 64 KiB 范围内寻找连续合法 Frame，不做无限扫描。

当前 `Mp3PlaybackEngine` 只管理单首 MP3，不包含 Queue / Repeat。后续格式扩展通过薄型 format-specific engine / decoder adapter 接入，PlayerController 不按扩展名硬编码播放状态机：

- 播放位置按实际提交到 PCM Output 的 Frame 计算；
- 自然 EOF 先进入 `Draining`，提交尾部 Buffer 并等待 M5.Speaker Channel 0 排空；
- 每首自然结束只发送一次 `TrackEnded`；
- 截断文件、SD Read Error 和 Decoder Error 发送 `Error`，不得伪装成 EOF；
- CBR / VBR Seek 与恢复都从合法 MPEG Frame 开始；Session 同时保存最近 source offset 作为 VBR 重同步提示。

---

## 4.4 DSP

V1 采用最小可行方案：四种固定、互斥的 Sound Preset（音效预设）。

规则：

- 一次只能选择一个 Preset；
- 不允许多个音效叠加；
- 不向用户暴露复杂参数；
- 参数写在固件中；
- 只采用低 CPU / RAM 成本的 EQ、滤波、轻压缩和轻软饱和；
- 音频连续性优先于效果复杂度。

### Original

```text
Stereo PCM
→ Stereo→Mono
→ 必要的 Gain / Limiter Safety
→ Backend
```

默认不额外开启 EQ、带通、压缩或饱和。

### Tape

建议初始参数：

```text
Low / Bass        +1 dB
Low-Mid           +1 dB
Mid                0 dB
Upper-Mid         -1 dB
Treble            -3 dB
Headroom          约 -2 dB
Soft Saturation   Light
```

具体中心频率随最终 EQ 实现确定；V1 不做 Wow & Flutter、Tape Hiss 或复杂重采样。

### Radio

建议初始参数：

```text
Band-pass         约 200–5000 Hz
Compression       Light
Soft Saturation   Light
```

参数以真机听感为准，不追求精确模拟某一台收音机。

### Vocal Clear

建议初始参数：

```text
Bass / Low        -1 dB
Low-Mid            0 dB
Mid               +1 dB
Presence 2–4 kHz  +2 dB
Treble            +1 dB
```

它是轻度人声 EQ，不宣称产生真实“高解析”。

### V1 不做

- Surround / Spatial Audio
- Stereo Widening
- Reverb
- Wow & Flutter
- Bitcrusher
- Vinyl Noise

原生 ES8311 输出为 Mono，环绕与立体声扩宽缺乏有效的左右声道输出条件。后续增加外置 Stereo Backend 后再评估。

### Gain / Limiter Safety

只实现简单、低延迟的保护，避免 EQ / Effect 造成明显 Clipping（削波）；不做复杂 Mastering Limiter。

---

## 4.5 Stereo → Mono Downmix

ADV 原生输出为 Mono，因此 Stereo 音源需要下混。

基础策略：

```text
Mono = (Left + Right) / 2
```

计算时使用足够宽的中间类型，避免 `int16` 直接相加溢出。

后续可根据实测加入：

- Headroom（余量）
- Gain Compensation（增益补偿）
- Limiter

但不应直接丢弃某一声道。

---

## 4.6 Audio Backend

播放器上层不直接依赖某一个输出实现。

接口目标可以保持很薄，例如概念上支持：

```text
begin()
stop()
pause()
resume()
setSampleRate()
setVolume()
write/consume PCM
getStats()
```

实际接口以最终代码最简单可用为准，不为了“完美抽象”增加复杂度。

P0 比较采用三个隔离的 M5Launcher App，而不是在同一固件中同时装载三套驱动：

```text
ADV-Walkman-Bench-A.bin
ADV-Walkman-Bench-B.bin
ADV-Walkman-Bench-C.bin
```

三者共用固定 MP3、串口协议、压力负载和统计字段。Candidate C 使用独立 IDF5 / pioarduino environment；其 packages 固定隔离在 `B:\PlatformIO\isolated\adv-walkman-c\packages`，通过 `tools/build_p0_backends.ps1` 与 A/B 分步构建，避免 PlatformIO 因同名 framework package 来回替换版本。这只是 P0 技术实验边界，不代表 V1 主线已经迁移工具链。

### V1 候选 Backend

P0 最终选择 Candidate A：

```text
ESP8266Audio 1.9.7
→ 32-bit L+R downmix
→ 3 × 1536-sample M5.Speaker buffer
→ M5Unified Cardputer ADV / ES8311 board support
```

P0 选定的 Backend 与三缓冲所有权模型保持不变。P2 真机播放中扫描测得约 54 ms PCM 提交间隔，而原 768-sample 配置总余量约 52 ms，并被用户实际听到为卡顿；正式 Player 因此将单 Buffer 增至 1536 samples，以约 4.5 KiB 额外静态内存换取约 104 ms 总余量。

基准音量为 M5.Speaker `128/255`。真机等响度比较中 A 与 B 都表现出可接受的空间感，B 没有形成足以抵消额外 I2S / Codec 维护成本的优势；A 随后通过 UI、SD、Pause / Resume、Seek 和 Restart 联合短压力测试。因此 V1 Player 以 A 为正式底层，B 保留为故障时的可工作备选，C 保持 Deferred。长期稳定性继续在 P1 实际开发中验证，不重新打开产品路线。

#### A. M5.Speaker Backend

参考 Cardio：

```text
ESP8266Audio
→ Triple Buffer
→ tuned M5.Speaker
→ ES8311
```

优势：

- 与 M5Unified 集成较好；
- 已有成熟社区实现；
- 维护成本较低。

P0 已验证：

- 固定 320 kbps / 44.1 kHz MP3 正常播放；
- 约 120 s UI Stress 与 60 s 并行 SD Stress 通过；
- Pause / Resume、Seek、Restart 通过；
- Heap 起止一致，Backpressure delta 为 0；
- 等响度后与 Direct I2S 没有决定性听感差距。

#### B. Direct I2S Backend

参考 SomaFM：

```text
ESP8266Audio
→ Direct I2S
→ ES8311
```

优势：

- 控制更直接；
- 可以自己管理 DMA；
- 有机会减少中间层和卡顿。

代价：

- 自己承担 I2S / Codec 生命周期；
- 维护成本更高。

#### C. BackgroundAudio Backend

```text
BackgroundAudio
→ I2S
→ ES8311
```

优势：

- 后台 / Buffer 驱动架构；
- 理论上更不容易被 UI 主循环阻塞。

代价：

- 可能要求较新 IDF5 / pioarduino 环境；
- 当前主项目工具链与其要求不同；
- V1 是否需要迁移需实测决定。

最终选择以 `docs/AUDIO_BENCHMARK.md` 真机结果为准。

---

## 5. 任务与并发原则

核心目标：

> UI / Library 的短时工作不能饿死音频输出。

候选结构：

```text
Audio Path
- decoder
- PCM buffer
- backend / DMA

Application Path
- UI
- keyboard
- library
- metadata
- settings
```

具体 Core 0 / Core 1 分配不在文档阶段先拍死。

P0 Benchmark 应验证：

- M5.Speaker 自带 task 是否已经足够；
- Direct I2S 是否需要独立 Audio Task；
- BackgroundAudio 是否自然解决此问题。

只有实测后才冻结 Core / Task 分配。

---

## 6. Buffer / DMA 原则

Buffer（缓冲区）用于吸收短时 UI、SD 或任务调度抖动。

DMA（Direct Memory Access，直接内存访问）负责让 I2S 外设连续取数。

目标：

- 避免过小导致 underrun；
- 避免因为“保险”申请过大内存；
- 用 Benchmark 找到稳定且合理的参数。

禁止：

- 凭感觉把 buffer 堆到很大；
- 为了掩盖架构问题无限加 buffer。

---

## 7. Storage（存储）

### 7.1 音乐目录

V1：

```text
/Music/
```

用户可自由使用多层目录。

不强制：

```text
Artist/Album/Track
```

### 7.2 配置与状态

项目自己的状态文件与音乐目录分离：

```text
/ADVWalkman/state/
  queue-a.bin
  queue-b.bin
  session-a.bin
  session-b.bin
```

媒体资源使用镜像相对路径，不与 Audio 文件混放：

```text
/Music/<relative>/<basename>.<mp3|flac|wav>

/Lyrics/<relative>/<basename>.lrc
/Lyrics/<relative>/<basename>.zh-Hans.lrc
/Lyrics/<relative>/<basename>.zh-Hant.lrc
/Lyrics/<relative>/<basename>.en.lrc
/Lyrics/<relative>/<basename>.ja.lrc
/Lyrics/<relative>/<basename>.ko.lrc

/CoverSource/<relative>/<basename>.<jpg|png>       # 可选，主要供 PC 工具
/ADVWalkman/covers/<relative>/<basename>.cover.adv

/ADVWalkman/
  fonts/
    cjk-12.vlw / cjk-12.idx
    cjk-14.vlw / cjk-14.idx
    cjk-16.vlw / cjk-16.idx
    latin-12.vlw / latin-12.idx
  config.*
  cache/
```

资源键固定为 `/Music` 下的规范化 `relative path + basename`。同一目录内两个不同音频不得使用完全相同 basename；版本差异必须体现在文件名中。V1 不使用模糊标题匹配、AI、UUID、Hash 数据库或 JSON Manifest。每首歌曲保存独立 `.cover.adv`，即使内容相同也不做 Album / Folder 共享与 fallback；曲库封面是另一类资源，不从第一首歌曲封面继承。曲库封面的最终目录和命名在 P3 实现前冻结，但必须保持每曲库独立且可机械查找。

P1 schema version 1 使用小端二进制。每个文件含 20-byte Header：magic、schema version、header size、generation、payload length、CRC32。

- Queue payload：length-prefixed UTF-8 path，最多 1,024 首，总 payload 最多 256 KiB；
- Session payload：Queue generation、当前 source index、position、source offset、Repeat、Shuffle、order / cursor 与 Previous history；
- P3 / P4 实现时复用 Session v1 payload 的 24-byte 固定区中预留 byte 21 保存 `preferredNowPlayingView`：`0=Lyrics`、`1=Cover`，旧状态的零值自然保持原有 Lyrics 默认行为，非法值回退为 Lyrics；无需新增状态文件或改变 schema version；
- A/B 双槽交替写入；新槽 `write → flush → close → reopen → CRC` 通过后才成为当前版本；
- 单次 cooperative write / verify read 不超过 1 KiB；
- Queue 只在队列变化时保存，Session 播放中约每 10 秒 checkpoint，并在控制或模式变化后保存。

恢复时不把 Queue 与 Session 各自独立取最新，而是选择最新且 generation 匹配的完整 pair；后续 A/B 写入以该 pair 为锚，优先覆盖孤儿槽，避免连续两次发布中断破坏最后一份可恢复状态。

### 7.3 恢复内容

保存：

- last track path
- playback position
- queue
- playback mode
- sound settings
- `preferredNowPlayingView`（用户选择的 Lyrics / Cover 偏好，不是当前歌曲的临时有效视图）

启动时：

- 恢复状态；
- 保持 Pause；
- 不自动出声。

只持久化 `preferredNowPlayingView`。歌曲无可用歌词时由 View Selector 临时选择 Cover，不得把该 effective view 回写为用户偏好。播放器 3×4 区的 `View` Action 更新偏好后只标记 Session dirty，继续沿用 cooperative A/B 保存，不在按键处理路径同步写 SD。

启动恢复只读取 Queue / Session，不打开 Decoder、不向 Speaker 提交 PCM。当前歌曲缺失时沿当前 order 寻找下一首当前已支持的音频并以 `Paused @ 0` 恢复；全部缺失则安全进入 Empty。CRC 错误、截断记录或未知 schema 不触发重启循环。

是否按断电间隔决定恢复精确 position 尚未冻结。除非能从官方硬件能力或真机验证得到可靠时间来源，否则 V1 继续采用“恢复最后 checkpoint 并保持 Pause”，不猜测断电时长。

---

## 8. Metadata

V1 目标字段：

- Title
- Artist
- Album
- Track Number

可选：

- Year
- Genre

Metadata 读取不能造成长时间播放卡顿。

如果解析成本较高：

- 按需解析；
- 缓存；
- 不在播放关键路径同步做大量工作。

V1 不要求运行时解码传统 Album Art；Color ASCII Cover 使用 PC 预生成资源。有歌词时它是可手动选择的第二视图，无歌词时是唯一有效视图。

P2 的 Metadata Reader 按需、cooperative 解析 MP3 ID3v2.3 / v2.4 的 `TIT2 / TPE1 / TALB / TRCK`，支持常见 ISO-8859-1、UTF-16 和 UTF-8 文本。单步最多读取 512 bytes，APIC 只跳过而不加载。Title 缺失时回退为去掉音频扩展名的文件名。P2 验证 CJK UTF-8 字节正确，字形显示留给 P3 Font / UI Gate；FLAC / WAV Metadata adapter 随对应 Decoder 一起实现，不能假装复用 ID3 解析即可覆盖所有格式。

### 8.1 Recent Tracks

- Recent 最多 32 首，最新在前，按规范化完整路径去重。歌曲达到 5 秒阈值时先复制一份待发布路径；即使紧接着切歌，后台 SD 发布也不得被新曲观察状态覆盖。
- 同一 Track 累计处于 `Playing` 5 秒后记录；Pause 不计时，Seek / Repeat / Resume 不重复置顶。
- 使用 `/ADVWalkman/state/recent-a.bin` 和 `recent-b.bin` 的 schema v1 / generation / CRC32 A/B 双槽，不为每首歌建独立文件。
- 缺失路径在读取时安全忽略，下次保存时紧缩。Recent 不替代 P1 Queue / Session。

---

## 9. UI 架构

P3 的工程交付顺序和 Gate 见 `P3_DELIVERY.md`。以下章节定义正式 UI 技术边界，
不得用阶段性占位实现反向修改产品行为。

### 9.0 P3A Foundation Types

```cpp
enum class UiPage : uint8_t {
    Player,
    Playlist,
    Library,
    Settings,
};

enum class UiAction : uint8_t {
    None,
    Up,
    Down,
    Left,
    Right,
    Confirm,
    Back,
    OpenSettings,
};
```

`UiCoordinator` 只持有页面、光标、目录路径和 Dirty 状态；Transport / Queue 继续
通过 `PlayerRuntime`，目录 / Metadata 继续通过 `LibraryRuntime`。Input Router 以
Cardputer ADV 官方键盘的完整物理位图生成 edge-triggered Action，不把方向键猜成
标准 PC Arrow 字符，也不依赖只比较按键数量的 `Keyboard.isChange()`。每键 25 ms
稳定去抖，按住不重复；相同数量的不同键切换仍可识别。导航不需要 Fn，组合按下
不派发动作；非 Player 页面使用键帽箭头位置，Player 仅保留 Esc 与已有实体 View。

P3A 主循环顺序固定为：

```text
PlayerRuntime::service
→ LibraryRuntime::service
→ M5Cardputer.update
→ InputRouter
→ one bounded UI step
```

Cardputer ADV 无 PSRAM，P3A 不分配完整 `135×240×RGB565` Framebuffer；页面切换
和选择变化使用 Dirty Region 与小型行缓冲。P3A 只接通普通页面导航，Player
顶部 3×4 的完整产品 Action 仍按后续对应任务推进。

### 9.1 Display Orientation

```text
135 × 240
Portrait
Headphone Jack Up
M5GFX Rotation 2
```

首次 P3A 真机检查确认 `rotation 0` 在耳机孔朝上时上下颠倒，因此 Cardputer ADV
Portrait 基线冻结为 `rotation 2`。V1 不使用 IMU 自动旋转 UI。

### 9.2 Screen Regions

按有效视图选择布局，完整呈现期间固定坐标与资源代次：

```text
Header          28 px  (y=0)
Content Stage  188 px  (y=28)
Footer          24 px  (y=216)

Lyrics view:
Content Stage  216 px  (y=0; no Header)
Footer          24 px  (y=216)
```

135×240、rotation 2、水平边距 6 px。Title 约 14 px，Artist 12 px，Footer 时间 14 px。
前一布局为Cover；Lyrics不显示/滚动不可见标题。P3C Content显示真实媒体；
自由试用没有覆盖歌词的Gate指引。全部正常UI使用实际楷体/Times字模；故障时才备用字体。

### 9.2.1 Text Layout Contract

P3A 使用统一的 `UiTextLayout`。任何 Renderer 都不得
再用固定 ASCII 字符数（例如 `%.17s`）推断是否能放进屏幕。布局输入至少包含：

```text
drawable rect + active font + text size + UTF-8 text + max lines + overflow policy
```

基础规则：

- 使用当前字体的实际像素度量计算每行可用宽度，并扣除页面 margin / padding；
- 只在合法 UTF-8 字符边界断行或截断，不拆开中文、日文等多字节字符；
- 不能假设文本含空格；无空格长名称也要在能容纳的字符边界断行；
- P3A历史Library骨架最多两行；当前0.8.4正式Library固定单行滚动，布局见下方LibraryVisual章节。Player使用单行Title滚动；
- Playlist 顶部曲库名及各歌曲 / 目录行使用单行省略，播放 / 目录标记先测量并预留宽度；
- 状态、Footer 每条显式提示、版本和固定数值区默认单行，超限时省略；Now Playing 的长 Title
  在 P3B 使用规定的 Marquee；
- 根据字体 line height 和区域高度共同限制行数，绘制时设置 clip rect 作为最后保护；
- 完整调试路径不进入正式产品 UI；需要提示时显示 basename 或明确缩略形式。

`UiTextLayout::draw(display, text, UiTextBox, byteLength)` 使用当前字体、字号和颜色；
`measure()` 复用同一逻辑但不绘制。`byteLength` 仅用于 Footer 显式行的有界文本视图，
不是显示宽度。每行使用固定 128-byte Buffer，无 Heap 分配；M5GFX `textLength()`
采用 `width + 1` 以接纳恰好等宽的文本，再通过 `textWidth()` 核对。换行优先使用
显式换行、空格 / 分隔符 / CamelCase 边界；语义断点不足行宽 75% 时退回最远完整
UTF-8 字符边界。末行省略使用 ASCII `...`；无效 UTF-8 字节仅在显示副本中替换为
`?` 并报告 `invalidUtf8`，源文本不变。

历史Library名称框`(19,76,97,38)`、12px最多两行，现仅保留为布局回归Fixture，不是正式轮盘的标题区域。PC实际Times字模检查和
无声启动字体检查要求`ADVWalkmanBenchmark`完整两行、每行≤97px；旧Font0宽度不再适用，
最终仍须真机确认可读性和裁剪。
Renderer 返回 `UiTextLayoutResult`（行数、最大行宽、可用宽度、截断 / UTF-8 / 布局
错误），`UiStats` 保留实际名称的最近结果及是否为 Benchmark 名称。Gate 等待返回
Library 后的新一次绘制，才执行 `P3AGate::libraryTextPasses()`，避免读取旧页面证据。

UI 渲染上下文拥有曲库名和六个可见列表行的有界完整文本，不持有局部
`LibraryDescriptor` 的指针，也不提前按 64 bytes 截断列表名称。该固定上下文驻留
`UiCoordinator`，不扩大 Arduino loop 的临时栈；它不是音乐库全量路径缓存。

阶段职责：P3A 先保证内置字体下所有基础页面不越界；P3B 将同一布局用于 Header /
Footer 并增加长标题滚动；P3C 以正式 SD 中日文字体 glyph metrics 完成 CJK 适配，
Lyrics Renderer 保持独立竖排规则；P3D 只做字号、行距、圆弧、留白和截断阈值的
真机视觉校准，不得把基础防越界推迟到最终阶段。

### 9.2.2 Navigation and Browser Presentation

0.7.6列表返回/选歌实现见 `P3C_NAVIGATION_FIX.md`：`NavigationLoad`独立记录目标路径
对应的请求代次及Loading/Ready/Error；页面切换不等待目录或字模Ready。
退出Player先取消媒体代次、释放歌词/封面与固定字模，再清屏，随后打开目录。
加载中Esc能取消返回；错误Enter重试。连续5秒无扫描/排序/写索引进展报告停滞，
六行分页准备另记录进展；无依据不归因于OOM或SD，也不扩缓存/重写排序。

`MusicLibrary::cancelOpen()`只取消浏览器临时工作，不解除正在播放Queue的pin；
`LibraryRuntime::cancelSelection()`阻止过时的待选歌请求。只有列表选歌成功才进入
Player，通过原`selectTrack()`建立当前文件夹非递归Queue。浏览和返回不调用Stop。
列表上下文按行准备，绘制不读SD；字模只加载固定标签/当前可见文本，不预热全ASCII。
标题/六行/Footer分区绘制，列表选中项改变不再整屏清空；首次页面切换仍可靠清屏。

Footer时间用现有Latin14，文本框`(33,2,96,16)`位于18px行缓冲，最终贴到y216；
24px Footer、图标与进度条不变，音量百分比仍Latin10。
布局自检在135×240显示对象上度量`(19,76,97,38)`；18px缓冲只在局部合法坐标测试墨迹。
同一错误屏幕坐标放到18px缓冲必须被负例拒绝，不删除自检来制造通过。

### 9.3 Header Marquee

P3的Tab导航、双帧预取及0.8.1修复明细见[`P3_OPTIMIZATION_FIX.md`](P3_OPTIMIZATION_FIX.md)。
Tab只改变页面：Player→当前歌曲目录、Playlist/Library→Player；Enter仍走选歌重播。
同曲离页暂停媒体I/O/绘制并关闲置句柄，不释放歌词索引/已验证封面；换歌才替换资源。
列表六行窗口区分数据失效与高亮变化；最终排版文本（包括省略号/标记）准备后再绘制。
资源prepare/current及UI使用独立pin；LyricsTimeline仅current+next文本，Renderer双布局，
当前帧完成立即预取，显示阶段无SD读取。512byte索引页缓存、200项metrics、15KiB位图，
媒体与LibraryVisual、文件系统预留仍受48KiB断言限制；0.8.2另将新输入事件队列加入联合断言。具体ELF尺寸记录在`P3_PERFORMANCE_FIX.md`，不沿用旧47896bytes估计。P3B原有4860byte行缓冲、其余UI模型和日志静态内存另列，不冒充免费空间。盖住浮层后恢复已显示帧。

0.8.2采用`ViewTransition`区分displayed/requested/generation；在endFrame发布实际视图和Header状态后才更新偏好。同目标合并，不改变开始时间。Cover可见时也准备当前/下一歌词，独立Current/Next/Ui固定标记保证双帧不互相淘汰。

`.idx2`为FIDX v2：512byte头、16byte码点直索引记录（BMP65536槽，Latin256槽），不存在记录全零；单次冷查询只需一个512byte索引页。头保留VLW长度和前24字节CRC用于字体配对检查；PC逐记录比较旧索引，运行时检查头、边界和配对。缺.idx2才退旧.idx，坏文件不静默fallback；按字体缓存已验证头，位图仍为4bit，15KiB/200metrics不扩。

主循环音频优先，公开键盘接口每片最多8次更新/2ms，16项固定事件队列保留短按、组按键抑制和页面代次。后台6ms软预算不是可抢占SD承诺。PlayerRuntime分serviceAudio/servicePersistence；存档每步最多一个文件API操作或路径取得，r+原位重写非当前同长度槽，长度变更才重建；Flush、关闭、回读、发布分开。A/B配对恢复与Queue先发布、Session后关联不变。日志写、Flush、关也分开。不可分割SDK调用超时据实记录，不修改70ms标准。

Cover Title / Artist能完整显示时居中、保持静态。超长 Title：

```text
hold 5s → scroll left once → hold 5s → repeat
```

初始速度 `24 px/s`，最多 20 次/s；暂停不停止动画，换歌、标题更新或重新进入 Player
重置开头计时。Artist 单行省略，不同步滚动。Title 使用完整有界文本度量和裁剪，
不得因为 UiTextLayout 的 128-byte 行缓冲丢掉后半段。

### 9.3.1 P3B Display Model and Partial Rendering

`NowPlayingModel` / `NowPlayingPresenter` 只持有歌曲显示副本、真实 Snapshot、动画
时钟、音量浮层与区域 Dirty 标记，不拥有 Decoder、Queue 或 Session。
`LibraryRuntime::requestMetadataPath()` 独立于浏览目录，复用现有单 Reader / 12 项缓存；
只接受已经规范化的 `/Music/...mp3` 路径，拒绝 `..`、`.` 和空路径段，不暗中转换 key。
结果通过 `metadataForPath()` 核对。Player 与 Playlist 各自保留显示副本，读取失败
或标签警告时保留文件名回退，不影响音频。Reader 的 192-byte 文件名回退不替换
Player 自己的完整 512-byte 有界名称，以免长文件名在滚动前就被截断。

时间 / 进度只来自 Snapshot；恢复 Pause 且 duration=0 时总时长为 `--:--`、进度未知，
不额外Probe。14px时间区域x17/width84；右侧x103～112显示顺序箭头或1/A/S，
x118～128圆内直线表示Original。异常组合绘制?并记录原值，不伪装DSP。

标题、Artist、时间 / 进度、状态行与 Content 独立更新。非 Player 页面不因秒数变化
重绘；页面切换允许一次完整初始化。固定 `135×18×2=4860 bytes` RGB565 行缓冲复用，
不分配全屏 Sprite；UiTextLayout 面向 `lgfx::LovyanGFX`，绘制不读 SD。
主循环保持 Player → Library → 输入 → UI burst → 日志。UI burst 最多 64 个小工作或
6 ms 软预算，然后回到音频；避免每条 2 / 18 px 显示带之间都等待约 35 ms 的音频
buffer。不可拆分 SD 操作可超软预算，真实 burst / PCM 耗时仍记录，不排除慢操作。
0.7.6在Library单次工作达到6ms时先补一次音频service，再进入输入/UI；输入耗时
包含导航动作，额外音频service同样记峰值。日志追加目录/分页准备、实际页面完成、
不同歌曲选择、队列数量和资源代次，音频/显示限值与首项故障分别保留。

`notifyVolumeAdjusted(volume, nowMs)` 只接收已调节音量的显示事件，不改变音频音量。
左侧浮层只有 3 px 条和透明背景数字，覆盖 Content，不改变布局；3 秒后局部恢复。
未收到事件不显示，离页隐藏。局部恢复只更新 x6 / width25 / y68 / height82 区域；
如果歌词代次或页面已变化则显示当前整帧，不能贴回过时截图。
本次授权提前接通固定 PlayerKeys：Vol+ (13,0)、Vol− (12,0)、Play/Pause (13,1)/(13,2)、
View (12,1)，来自官方键盘坐标。UiCoordinator 先调用真实音量 setter / play / pause，
再派发显示事件；逻辑音量步长 8，默认 80，未增加音量存档或改 Queue / Session。
0.7.5 的 VolumePolicy 在 PlayerRuntime 边界统一映射 raw=(level*102+127)/255；
启动先设置 raw32 再 begin，最大 raw102。UI 与日志 volume 仍是逻辑刻度，另记
speaker_volume_raw / speaker_volume_cap，避免百分比与硬件值混淆。P0 历史基准不改。

P3B 本地验证与设备验证区分：可注入时间检查、布局检查编入测试支持；真机才验证
实际显示。连续播放 Audio Error / Backpressure=0、PCM gap≤70 ms。0.7.3 使用
`/ADVWalkman/logs/p3-free-last.txt`，按 A/B/C 主路径覆盖与实际错误分别记录。

测试交接：`P3BChecks` 包含编译期 geometry / timing / volume 断言，及待设备执行的
`checkP3BModel()`、`checkP3BDrawing(135×18 scratch)`、
`checkP3BOverlayRestoration(presenter)`。后者复用同一行缓冲，逐条带核对浮层隐藏后的
背景像素与原背景一致，不分配另一个屏幕缓冲。
0.7.3 在 UI begin、媒体绑定前使用原行缓冲执行这些检查（包含浮层背景未被面板覆盖
的像素断言），只操作 RAM，不启动 / 停止音乐；结果进入后台日志。旧 P3BValidation
脚本保留用于历史回归，自由试用不调用其主动停止或清零流程。不由 A 的 PASS 推导 B。
`tools/check_p3b.py --artifacts` 是 PC 几何 / 参考 / 源码契约与生成物检查，不能代替
上述设备函数或用户可见显示验收。本阶段不增加独立 P3B Gate 安装。

### 9.4 Now Playing View Selector

保留独立的 Lyrics Renderer 与 Color ASCII Cover Renderer，在 UI / Application 层增加薄型 View Selector；不得把该状态放进 Audio Engine 或 `PlayerController`。

```cpp
enum class PreferredNowPlayingView : uint8_t {
    Lyrics = 0,
    Cover = 1,
};

effectiveView =
    lyricsKnownUnavailable ? Cover : preferredView;
```

`hasUsableLyrics` 表示本地 LRC 存在且可成功解析；缺失、空文件或损坏歌词均按 unavailable 处理，禁止进入空白 Lyrics 页面。
Loading / Idle 是尚未知，不是 Missing；准备时保留完整旧画面或等待首帧，不能因为
封面先 Ready 就无限刷封面而延误歌词。字体、歌词、封面 worker 轮转，跳过无工作者。

行为规则：

- `View` Action 仅由播放器页面的 3×4 区派发；其他页面不暗中改变偏好；
- 有可用歌词时，`View` 执行 Lyrics ↔ Cover，并更新 `preferredNowPlayingView`；
- 无可用歌词时，`View` no-op；可以显示短暂、非阻塞的 `No lyrics` 提示，但不是必做项；
- 偏好为 Lyrics、当前歌曲无歌词时只临时显示 Cover，不修改偏好；下一首有歌词时自动回到 Lyrics；
- 偏好为 Cover 时，无论是否有歌词都显示 Cover；
- 切换按有效视图重排上方216px：Cover为Header28/Content188，Lyrics为Content216，无Header；Footer不变。完整帧固定坐标与资源代次，资源加载继续cooperative；
- View Action 不调用 Play / Pause / Seek / Queue / Track / Sound / Volume，不改变播放进度或 Decoder 生命周期。

### 9.5 Lyrics Renderer

V1 解析标准逐行 LRC。

文件约定：

```text
/Lyrics/<relative>/<basename>.lrc
/Lyrics/<relative>/<basename>.zh-Hans.lrc
/Lyrics/<relative>/<basename>.zh-Hant.lrc
/Lyrics/<relative>/<basename>.en.lrc
/Lyrics/<relative>/<basename>.ja.lrc
/Lyrics/<relative>/<basename>.ko.lrc
```

概念数据模型：

```text
LyricLine { timestamp, original, chinese }
```

配对：时间戳优先，`≤300 ms` 作为初始容差；缺失译文则单语显示；不在 MCU 上做语义匹配。

渲染：

```text
original block | Chinese block
      current cue only
```

换句取消横移动画；目标组布局与全部字模就绪后，固定帧数据，再分条带快速呈现。
准备时不擦旧画面；当前帧完成立即预取下一分页/下一组，不再等剩两秒。中文块右、原文块左，每列向下、
同语言向左续列；长句先多列，极长句才阅读分页，短语言不重复消失。分页按相邻
时间戳区间分配；Pause 以 Player position 冻结，Seek 直接定位。仅当前组显示，
首句前灰色显示首组第一页，到期原位置点亮；正常播放不显示前后句。内框123×202 px，
CJK 18 px微加粗、列距1 px、双语间距6 px、最多六列。MediaLayout定义这些公共常量。
VerticalWords 按真实 Latin advance 预量整个英文单词；本列放不下则整体换列，
超过整列的超长词才拆字符。计列与放置共用 advanceColumn，保证分页不会漏字。
词内 ASCII 撇号 / 连字符保留；lookahead 超过一列即停止，无额外动态内存。
自然换句到期后显示延迟 ≤200 ms、单次完整呈现 ≤100 ms；Seek / View 取消旧代次。
呈现期固定 glyph，不读媒体 SD 文件、不允许 Header 淘汰它们，在有界 UI burst 内
连续推进已准备条带，然后返回音频；100 / 200 ms 限值不变。
Gate 提示卡和真实媒体互斥，不把 STEP / 确认文字压在歌词上。

CJK：楷体 `18 px` 微加粗，正常竖排；小标点右上定位，括号 / 书名号 / 引号旋转。

Latin：Times New Roman `14 px`，每个glyph顺时针旋转90°后沿纵轴布局；不是整句整体旋转。
整词换列按实际advance与旋转后bitmap宽度共同计算。V1 UI不跟随设备物理旋转。

### 9.6 Font Storage

字体资源优先从 SD 加载：

```text
/ADVWalkman/fonts/
  cjk-12.vlw / cjk-12.idx
  cjk-14.vlw / cjk-14.idx
  cjk-16.vlw / cjk-16.idx
  latin-12.vlw / latin-12.idx
```

Flash 只保留最小 fallback。字体加载失败不得影响 Audio Core。

P3C 格式、缓存预算、分步加载、帧调度与联合 Gate 的实现约定见
[`P3C_IMPLEMENTATION.md`](P3C_IMPLEMENTATION.md)。不使用 M5GFX 直接从 SD 在 draw
路径中加载整套 VLW；字模有只读索引，渲染只访问已经准备好的 RAM。

### 9.7 Color ASCII Cover

ASCII 生成由 PC 工具完成，不占用 ESP32 实时图像处理预算。

该 Renderer 既服务于有歌词歌曲的 Cover 偏好，也服务于无歌词歌曲的强制 fallback。

PC Tool 负责：

- 按 `/Music` 的相对路径和 basename 查找 `/CoverSource` 下对应 JPG / PNG；
- 转彩色 ASCII；
- 输出电脑 Preview；
- 为每首歌曲输出独立 `/ADVWalkman/covers/<relative>/<basename>.cover.adv`；
- 不建立 Album / Folder 共享封面或运行时 fallback 数据库。

网格保留26×20 / 30×24 / 34×26，增加40×32（默认）和48×40。可打印ASCII按轮廓、密度和边缘匹配，
不单独放大标点；字符形状/密度拟合与原图取色不变。ACOV v1头仍28bytes，方图135×135，
其他比例适配135×188；旧120×144仍可读，不增加全屏缓冲。

`.cover.adv`使用Header加RGB565 Pixels，按实际width计算stride和payload，不硬编码480bytes/两行。
每次读取≤512bytes：135px一行270bytes；绘制居中，保持比例和完整图面。

设备侧优先：

```text
read RGB565 → push image
```

而不是运行时逐字符转换。

### 9.8 Page Model and Other Screens

V1 只有四个实际页面：播放器、播放列表、曲库、设置；不增加额外 Home、独立 Queue 页面或独立 Sound 页面。

启动路由：存在有效恢复歌曲时进入播放器并保持 Paused；无有效状态或全部歌曲失效时进入曲库。恢复流程仍不得打开 Decoder 或提交 PCM，第一次 Play 才实际出声。

```text
播放器 --Esc--> 播放列表 --Esc--> 曲库
曲库 --S--> 设置 --Esc--> 曲库
```

曲库是最外层内容页，Esc no-op。播放状态跨页面持续，但完整 3×4 控制只属于播放器页面。

`/Music` 的可见一级目录按排序顺序映射为曲库；根目录 MP3 以合成项“未分类”
暴露。Playlist 可以进入一级曲库下的子目录；在子目录按 Esc 返回父目录，在
一级曲库根再次按 Esc 才返回 Library。选歌仍建立当前文件夹非递归 Queue。

恢复状态进入 Player 后，如果用户按 Esc，Application 从当前 Track 路径安全派生
父目录并请求 Library 打开。该入口只接受 `/Music` 自身或 `/Music/` 后代路径，
拒绝 `..`、超长路径和其他根目录；不得通过恢复路径逃出 Music Root。

曲库页面采用上方独立曲库封面、下方横向黑胶唱片堆叠选择带。当前项以上浮为主要高亮，可辅以提高亮度和露出更多标签；短名允许沿圆弧排列。Left / Right 切换、Enter 进入播放列表，动画必须 Dirty / Throttled、短且不阻塞音频。尺寸、圆弧角度、重叠比例和时长由 P3 真机校准。

播放列表采用低复杂度标准列表：Up / Down 选择、Enter 播放并进入播放器、Esc 返回曲库；至少显示序号、Title、选择高亮和可选的正在播放标识。底层可以复用 P1 Queue，但 UI 不改变 Queue / Session 语义。

Library绘制交给`LibraryVisual`：固定顶部174px独立封面、y174高22px单行名称、y196至240轮盘。名称按实际advance与斜体墨迹边界居中/裁剪，超长复用Player的5秒/24px每秒/5秒循环和50ms刷新间隔，离页暂停，进入/唤醒重置。只重画名称区。唱片半径26px，圆心沿半径60px共同圆弧、相邻40度布置，按远近绘制、中央最高。约160ms/最多4帧，重定向最新选择，不排队；静止完成状态不得因重入又从动画第0帧开始。

`wheelIndex`将非空目录映射为三个独立绘制位置；count=1三同名，count=2两侧同为另一项，>=3首尾循环。`LibraryCatalog::at`不改变选择；三份44byte有界UTF-8短名绑定本次catalog刷新，重复邻项只查一次、不复制路径。三个弧形短名分别按实际advance、46px弧长及完整旋转字形边界计算，随唱片真实遮挡。四字体离线4倍栅格MaxFilter(3)加粗后缩小，重算墨迹边界，VLW/IDX/IDX2同源，不增设备叠画。复用135×18行缓冲；同库CRC有效结果保留，新选择取消未提交条带。

0.8.4共享条带：`RenderStep`区分Idle/Waiting/Submitted/Failed，实际Presenter调用`dispatchContent`，必须在content调用后再次检查占用，阻止新开始的Pending条带落入chrome路径。Canvas始终135×18，局部有效高度由clip和push目标clip共同限定；借用接口不再setBuffer。`ImageBand`验证canvas/clip/y/height与owner，提交后才释放。`FrameCommit`固定代次/id/起止行，只有同代次连续已提交行可推进；全帧、浮层局部修复、资源占位分开统计。Cover Header在新帧前准备并提交；完整View成功后才保存偏好。取消与一次受控重建不触碰transport，再失败停止错误内容重试但保留导航/错误底栏。实际显示API返回不等于物理LCD读回证明，真机观感仍需人工确认。

暖路径保留六行正式Title和metadataReady标志，缓存最终省略文本的字节边界/标记宽度，仅绘制旧新高亮。字体请求按face分组；已知位图地址及配对验证可复用时，仅重开VLW，不再次打开/查询索引。标题度量不因同曲返回重做。当前图片条带先推进读取，准备完直接提交，避免资源与无效绘制调用机械轮转；保持6ms软预算和音频优先，不豁免SD/日志负载。

0.8.4渲染日志：render_contract、render_pixel_selfcheck、frame预期/提交行数、full/patch/fallback、取消/拒绝/受控重建、请求/准备/首条带/完成及等待/CPU绘制/提交峰值；分Lyrics/Cover/Playlist/Library/Settings睡醒计数，仅新完整画面记唤醒完成。字段沿原16KiB缓冲落盘；相同当前曲目路径使用media_track_ref=track避免重复大字段。无声启动使用实际M5GFX行缓冲与ImageBand做逐像素图案/末条带/坏canvas检查，编译通过不能代替设备执行。

0.8.3显示生命周期：`ScreenPowerController`仅处理物理活动、时限和整组唤醒吞键；`DisplayLifecycle`为Cancel→Drain→Apply，Drain中新的睡醒请求只更新最终目标，不跳过清理。Coordinator的Cancel释放显示固定状态和未提交条带，Drain每次至多关闭一个字体/歌词/图片句柄，Apply恢复亮度并重画。Player在非活动状态先更新真实路径，再激活媒体，避免睡眠跨自然换歌后恢复旧资源。输入代次在页面、睡眠及唤醒变化时更新。暂停状态不因显示生命周期改变。

六行标题使用`LibraryRuntime::cachedMetadataForPath`直接查询已有12项缓存，命中不取消读者。选中行优先，其余可见行逐步请求；不为每行复制完整路径。目录代次变化才重建模型；播放状态变化不使整个窗口失效，当前曲目变化只更新相关播放标记。暖返回和唤醒首帧分别计时。

新增四个Library字体编号：CJK12/18、Latin14/22，资源名`library-cjk-*`及`library-latin-*`，不覆盖原字体。`CachedGlyph`仍16bytes，独立字体字节、8bit age；回绕时全体年龄减半并从128继续。仅原Latin10..14可访问三档advance表。11种字体保留15KiB位图/200项度量及.idx/.idx2兼容。相同覆盖度像素按行段绘制，图像≤512byte转换为本地RGB565后写入已有条带，提交阶段不读SD。

诊断：`RuntimeDiagnostics`记录复位原因及Audio/Keyboard/Directory/Settings/Persistence/Ui/Log峰值。16byte RTC阶段记录只有复位原因与校验均有效才采用；断电不承诺保留。`runtime_peaks_us`按上述七阶段顺序排列；唤醒日志记录捕获、背光、媒体恢复、首帧、解除吞键、恢复时位置/PCM，当前值复用`position_ms`/`pcm_buffers`。未完成明确标PENDING/NA；首个metadata回退原因1=无Title、2+Mp3MetadataError=解析错误、255=路径过长。完整日志仍受16KiB上限检查，静态常量明细从ELF构建清单读取。

独立`LibraryCoverReader`按下列入口取普通彩色图，绝不从歌曲封面推断：

```text
/ADVWalkman/library-covers/folders/<一级目录名>/cover.adv
/ADVWalkman/library-covers/root.cover.adv   # 未分类
```

LCOV v1头24bytes，小端：magic[4]、version/headerLength/width/height/format/reserved各u16、payloadLength/CRC32各u32；按实际宽高读取RGB565，宽≤135/高≤174，兼容旧120×120及新135×173。歌曲ACOV头不变。`ImageBand`每次≤512bytes，填满18px条带再一次提交；共享缓冲占有期间不得绘制Header/Footer，条带边界公平轮转。顺序读取不重复Seek，呈现不访问SD；同资源重绘不重算CRC。缺失/损坏占位并记录具体原因。

设置仅包含屏幕亮度、息屏时间、关于、返回Launcher。SettingsPanel内部子面板不是第五个页面。独立`DisplaySettingsStore`保存显示偏好，不改Player Session：

```text
/ADVWalkman/state/display-a.bin
/ADVWalkman/state/display-b.bin
```

DSPL v1固定24bytes：magic[4]、version(u16)、recordLength(u16)、generation(u32)、payloadLength(u32=4)、brightness/playerTimeoutIndex/otherTimeoutIndex/reserved各u8、前20bytes CRC32(u32)。亮度10..100步长10默认70；时限索引0..6对应15/30/60/180/300/600秒及永不，默认3/1。启动选择有效最新槽，坏槽/打开失败单列诊断；无有效槽采用默认值。调整停止1秒或离开设置后安排保存，按open/write/flush/close/reopen/verify分轮推进，与Player persistence及日志错开。回读校验后才切换generation；写入期间再次调整通过revision继续安排下一次保存。失败保持RAM值，显示未保存，不同步阻塞按键。

返回Launcher借鉴Cardio的APP_TEST路线：运行时找非当前TEST分区，检查app描述；用户确认后Pause并请求Player checkpoint及显示保存，再保存诊断。只有存档成功才调用`esp_ota_set_boot_partition`（由IDF验证image）和`esp_restart`；任何一步失败留在设置。没有地址常量、Factory fallback、分区表写入或Stop归零。保存10秒、日志15秒超时返回具体错误；不更改Launcher的自动启动设置。

### 9.9 Screen-off

Screen Off 等同 V1 Soft Lock：`ScreenPowerController`在InputRouter动作派发之前检查56键完整物理位图，25ms稳定后处理。首次任意键只唤醒并吞掉整组，直到全部松开；无绑定键、修饰键、组合键也如此。InputRouter继续更新去抖状态，但不派发被吞掉的动作，避免按住后漏出新事件。

该规则同样覆盖 `View`：息屏状态第一次按对应物理键只唤醒，不能同时切换 View。

冻结 Timer：

```text
Player (Playing or Paused): 180s default
Other pages:                30s default
Options per context:        15/30/60/180/300/600s/never
After waking:               normal timeout for the current page
```

使用uint32差值支持millis回绕；只有物理操作和页面切换重置时限。息屏仅backlight=0，释放不可见媒体准备、不再绘制；Player和必要存档照常服务。唤醒恢复用户亮度、根据当前播放位置重新准备/清屏，无旧歌词补画。记录睡眠/唤醒次数、唤醒位图和实际被抑制动作数；不是硬件睡眠，不引入长按锁屏。


---

## 10. Keymap

V1 Keymap 已冻结。

### 10.1 主使用姿态

主要播放器使用姿态：

```text
Portrait（竖持）
Headphone Jack Up（耳机孔朝上）
```

UI 与按键布局都应以这一姿态作为重要设计输入。

### 10.2 Player-page 3×4 Blind Zone

专用映射只在播放器页面生效。以耳机孔为顶部，使用最靠近耳机孔的三排、每排四颗；以下 `1–12` 是物理位置编号，不是键帽字符：

```text
1  Volume +       2  Play/Pause  3  Play/Pause  4  Previous
5  Volume -       6  View        7  Play Mode   8  Next
9  Original      10  Tape       11  Radio      12  Vocal Clear
```

规则：

- 2 / 3 故意重复 Play/Pause，扩大盲操命中区域；
- Sound Preset 使用四颗物理键直接选择，不经过循环页；
- `View` 只派发 Now Playing View Action；无歌词时保持 Cover；
- 这些 Action 调用 Player / UI / Sound 层，不直接操作 Audio Backend；
- 离开播放器页面立即停用该映射；旧数字列及 `H/L/Q/R/S/V` 全局快捷键不得重新出现。

### 10.3 Play Mode Projection

UI 只暴露四个互斥状态：

```text
Normal → Repeat One → Repeat All → Shuffle → Normal
```

P1 内部 `RepeatMode` 与 `Shuffle` 两维状态模型保持不变，由 UI 原子映射：

```text
Normal      = Repeat Off + Shuffle Off
Repeat One  = Repeat One + Shuffle Off
Repeat All  = Repeat All + Shuffle Off
Shuffle     = Repeat Off + Shuffle On
```

因此不需要迁移既有 Queue / Session schema，也不允许暴露复杂 Repeat + Shuffle 组合。Shuffle 完成一轮后按 Repeat Off 语义停止。

### 10.4 Normal-page Input

播放列表、曲库、设置使用普通 UI 映射：

- Arrow Keys：方向导航；
- Enter / OK：确认；
- Esc：按页面层级返回；
- 曲库页面额外识别 `S → Settings`；
- 曲库 Esc no-op；
- 播放器 Esc 返回播放列表；播放列表 Esc 返回曲库；设置 Esc 返回曲库。

如果以后出现真正的 Text Input，字母和数字恢复普通输入，方向键、Enter、Esc 继续维持 UI 语义。

### 10.5 Screen-off Input Gate

Screen Off 状态在所有页面先于 Context Keymap 处理：第一次任意键只唤醒并吞掉事件，第二次按键才派发。因此第一次按 Volume、Play、Next、Sound、Play Mode 或 View 都不能同时执行功能。

### 10.6 尚待 UI / System 阶段确认

- Lock（锁键）；
- 长按 / 组合键是否有必要；
- 3×4 物理位置到 M5Cardputer 键盘事件的最终映射与去抖参数；
- 特殊页面是否需要额外上下文行为。

---

## 11. Launcher

V1 要求：

- 可通过 M5Launcher 安装；
- 固件启动正常；
- 可返回 Launcher；
- 不猜 Flash offset；
- 不把 full-flash recovery image 当普通 app 安装。

---

## 12. 性能与稳定性指标

最终至少关注：

- 320 kbps MP3 连续播放
- Free Heap
- Minimum Heap
- Underrun
- 长时间 Heap 是否持续下降
- UI 压力下音频稳定性
- 目录浏览时音频稳定性
- Pause / Resume 爆音
- Seek
- 切歌
- 最大可用音量
- 主观底噪与失真

---

## 13. 仍需通过 P1 / 真机原型确定的技术细节

以下是实现 / 校准项，不是产品侧重新设计问题：

- Core / Task 分配
- 最终音量曲线
- 各 Sound Preset 的小幅参数微调
- 中文 / 英文字号约 ±2 px
- 歌词列宽与间距
- ASCII Cover 最终网格密度
- Header 滚动速度

以最小可行实现和真机结果为准，不应扩大成重新设计 V1。

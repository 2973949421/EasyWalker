# ADV Walkman Technical Design

> 版本：V0.2  
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
│ Decoder             │
│ MP3 解码             │
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
- 当前目录只显示非 dot-hidden 目录和大小写不敏感的 `.mp3`；其他文件不进入 V1 可播放列表。
- 顺序固定为“文件夹优先 + ASCII 大小写不敏感自然排序”；数字段按数值比较，非 ASCII UTF-8 按原始字节保持稳定顺序。
- 在 Library 选中 Track 后，Queue 为“当前文件夹内排序后的 MP3”，不递归包含子目录，并从所选 Track 开始。
- Browser 当前页不直接充当 `TrackSource`。正在播放的 `FolderQueueSource` 必须绑定不可被 LRU 淘汰的稳定目录索引，直到下次 Queue 安全发布。
- 单目录最多索引 2,048 个“目录 + MP3”条目；单目录超过 1,024 首 MP3 时仍可浏览，但创建 Queue 明确返回 `QueueTooLarge`。

### P2 扫描与缓存

- 当前目录使用 `Open → Scan → Sort → Finalize → Ready / Error` cooperative 状态机；每轮只处理一个目录项或一个有界读写 / 排序步骤。
- SD session cache 位于 `/ADVWalkman/cache/library/`，使用 4 个目录索引槽；重启后重建，不建设持久化音乐数据库。
- RAM 使用 3 个 32-entry page 做 LRU，最多驻留 96 个条目。扫描和排序仅保留数字 offset，不将整批路径常驻 RAM。
- 当前 Queue 的索引槽保持 pinned；新 Queue 在 Player persistence 空闲后再切换，旧索引才可解除 pin。
- 应用循环始终先 service Player，再做一次有界 Library / Metadata / Recent 工作；无需新增 RTOS task。
- `LibraryRuntime` 采用四路轮转，每个主循环只推进一次 Directory、Queue selection、Metadata 或 Recent 工作；正式开发串口的目录 / Recent 输出同样使用逐项游标，不在一次命令中同步遍历整个目录。
- `PlayerController` 在 Queue / 当前曲目变化时缓存规范化完整路径；`PlayerRuntime::currentPath()` 只复制 RAM 缓存，Recent 与状态画面不得在每轮播放循环重新打开 Library index。
- `LibraryRuntime` 与其 pinned `FolderQueueSource` 是单次绑定生命周期；禁止对仍被 Player 引用的实例执行重新 begin。Queue source 只有在没有异步 State Store 工作时才能解除 pin。
- P2 Gate 使用独立 Recent 测试槽并暂停正式 Queue / Session persistence；四份日志在停止测试音频后一次写完并回读完整性标记，避免日志本身污染播放中断音指标。
- P2 Gate 在完整 P2-01～P2-04 生命周期持续检查 Player state、44.1 kHz、Audio Error、Backpressure 与相邻 service 间隔；播放中单次超过 100 ms 的间隔或非预期状态持续超过 100 ms 均作为 starvation 失败，不能由下一轮补音掩盖。

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

## 4.3 Decoder

V1 只要求 MP3。

目标：

- 最高 320 kbps
- 44.1 / 48 kHz 常见音源
- CBR / VBR 兼容
- 输出 16-bit PCM

P0 已冻结 `ESP8266Audio 1.9.7`。P1 的 `Mp3Probe` 负责跳过 ID3v2、识别 MPEG Layer III、CBR / VBR、Xing / Info / VBRI，并为 Seek 提供有界 Frame resync。无 TOC 的 VBR 使用比例估算后在最多 64 KiB 范围内寻找连续合法 Frame，不做无限扫描。

`Mp3PlaybackEngine` 只管理单首歌曲，不包含 Queue / Repeat：

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
→ 3 × 768-sample M5.Speaker buffer
→ M5Unified Cardputer ADV / ES8311 board support
```

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

其他资源可采用：

```text
/Music/
  <album>/
    song.mp3
    song.lrc
    song.zh.lrc
    cover_ascii.rgb565

/ADVWalkman/
  fonts/
    kaiti_16.vlw
    times_12.vlw
  config.*
  cache/
```

P1 schema version 1 使用小端二进制。每个文件含 20-byte Header：magic、schema version、header size、generation、payload length、CRC32。

- Queue payload：length-prefixed UTF-8 path，最多 1,024 首，总 payload 最多 256 KiB；
- Session payload：Queue generation、当前 source index、position、source offset、Repeat、Shuffle、order / cursor 与 Previous history；
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
- UI / keymap 相关设置（待设计后补充）

启动时：

- 恢复状态；
- 保持 Pause；
- 不自动出声。

启动恢复只读取 Queue / Session，不打开 Decoder、不向 Speaker 提交 PCM。当前歌曲缺失时沿当前 order 寻找下一首有效 MP3 并以 `Paused @ 0` 恢复；全部缺失则安全进入 Empty。CRC 错误、截断记录或未知 schema 不触发重启循环。

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

V1 不要求运行时解码传统 Album Art；无歌词时优先使用 PC 预生成的彩色 ASCII Cover。

P2 的 Metadata Reader 按需、cooperative 解析 ID3v2.3 / v2.4 的 `TIT2 / TPE1 / TALB / TRCK`，支持常见 ISO-8859-1、UTF-16 和 UTF-8 文本。单步最多读取 512 bytes，APIC 只跳过而不加载。Title 缺失时回退为去掉 `.mp3` 扩展名的文件名。P2 验证 CJK UTF-8 字节正确，字形显示留给 P3 Font / UI Gate。

### 8.1 Recent Tracks

- Recent 最多 32 首，最新在前，按规范化完整路径去重。
- 同一 Track 累计处于 `Playing` 5 秒后记录；Pause 不计时，Seek / Repeat / Resume 不重复置顶。
- 使用 `/ADVWalkman/state/recent-a.bin` 和 `recent-b.bin` 的 schema v1 / generation / CRC32 A/B 双槽，不为每首歌建独立文件。
- 缺失路径在读取时安全忽略，下次保存时紧缩。Recent 不替代 P1 Queue / Session。

---

## 9. UI 架构

### 9.1 Display Orientation

```text
135 × 240
Portrait
Headphone Jack Up
```

V1 不使用 IMU 自动旋转 UI。

### 9.2 Screen Regions

初始布局：

```text
Header         ~26 px
Content Stage ~184 px
Footer         ~30 px
```

允许真机原型小范围调整。

### 9.3 Header Marquee

Title / Artist 能完整显示时保持静态。超长 Title：

```text
hold 5s → scroll left once → hold 5s → repeat
```

滚动速度初始目标约 `20–25 px/s`，允许真机调整。

### 9.4 Lyrics Renderer

V1 解析标准逐行 LRC。

文件约定：

```text
song.lrc
song.zh.lrc
```

概念数据模型：

```text
LyricLine { timestamp, original, chinese }
```

配对：时间戳优先，`≤300 ms` 作为初始容差；缺失译文则单语显示；不在 MCU 上做语义匹配。

渲染：

```text
previous group | current group | next group
dim              highlight       dim
```

换句时整组向左移动。

CJK：楷体约 `16 px`，正常竖排。

Latin：Times New Roman 约 `12 px`，每个 glyph 单独旋转 90°后沿纵轴布局；不是整句整体旋转。V1 UI 不跟随设备物理旋转。

### 9.5 Font Storage

字体资源优先从 SD 加载：

```text
/ADVWalkman/fonts/
  kaiti_16.vlw
  times_12.vlw
```

Flash 只保留最小 fallback。字体加载失败不得影响 Audio Core。

### 9.6 Color ASCII Cover

ASCII 生成由 PC 工具完成，不占用 ESP32 实时图像处理预算。

PC Tool 负责：

- 递归处理音乐库；
- 找 `cover.jpg` / `folder.jpg`；
- 必要时读取 MP3 ID3 / Embedded Cover；
- 转彩色 ASCII；
- 输出电脑 Preview；
- 输出设备端预渲染 RGB565。

初始网格候选：26×20 / 30×24 / 34×26，默认先测 30×24。

设备侧优先：

```text
read RGB565 → push image
```

而不是运行时逐字符转换。

### 9.7 Other Screens

Library / Queue / Sound / Settings 使用低复杂度列表 UI。共同原则：方向键导航、Enter 确认、Esc 返回、当前音乐继续播放、不做持续动画、Dirty / Throttled Redraw、UI 不阻塞音频。

### 9.8 Screen-off

Screen Off 等同 V1 Soft Lock：所有按键原功能暂时失效；首次任意键只唤醒且事件不向 Player / UI Action 派发；亮屏后第二次按键才正常派发。

建议 Timer：

```text
normal idle timeout: ~15s
wake-only timeout:   ~5s
```


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

### 10.2 功能分层

按键分成三类：

```text
数字键
→ 高频播放控制 + 音效直选

字母键
→ 页面跳转 + 播放模式

方向键 / Enter / Esc
→ UI 导航与确认
```

UI 功能键不被重新占用为媒体键。

### 10.3 数字键

```text
0  Volume +
9  Previous
8  Play / Pause
7  Next
6  Volume -

5  Reserved

4  Vocal Clear
3  Radio
2  Tape
1  Original
```

规则：

- `0–6` 的播放控制按耳机孔朝上竖持时的物理方向排列；
- `5` 保留，不为填满键盘而强行分配；
- `1–4` 直接切换四个互斥 Sound Preset；
- 快捷键调用 Player / Sound 层，不直接操作 Audio Backend。

### 10.4 字母快捷键

```text
H  Now Playing / Home
L  Library
Q  Queue
R  Repeat Mode
S  Shuffle
```

`R`：

```text
Off → Repeat All → Repeat One → Off
```

`S`：

```text
Shuffle Off ↔ Shuffle On
```

### 10.5 UI 导航键

- Arrow Keys：方向导航；
- Enter / OK：确认；
- Esc：返回；
- Tab / Modifier Keys 等默认保留原本语义，后续 UI 需要时再定义。

### 10.6 Context（上下文）

普通播放器状态：

- 数字 / 字母快捷键生效。

Text Input（文本输入）状态：

- 字母和数字恢复普通输入；
- 播放器快捷键暂时关闭；
- 方向键、Enter、Esc 继续维持 UI 语义。

### 10.7 尚待 UI / System 阶段确认

- Lock（锁键）；
- 长按 / 组合键是否有必要；
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

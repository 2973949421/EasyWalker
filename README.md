# ADV Walkman

> 工作名：ADV Walkman  
> 当前阶段：P3C 本地实现与构建完成；P3A/B/C 均为 `DEVICE TEST`，等待一次联合验收
> 平台：M5Stack Cardputer ADV

## 1. 项目是什么

ADV Walkman 是一个基于 M5Stack Cardputer ADV 的个人化复古随身音乐播放器固件。

项目目标不是替代手机 + 无线耳机，也不追求商业产品级功能堆叠。核心价值是：

- 复古随身播放器的情绪价值与实体交互体验；
- 通过真实硬件项目学习嵌入式音频、I2S、Codec、microSD、UI、按键、缓存与 DSP；
- 在不增加外置音频硬件的前提下，先尽量发挥 Cardputer ADV 原生 3.5mm 音频链路的实际能力；
- 后续如果确认原生单声道输出成为主要瓶颈，再增加外置立体声音频模块，但不推翻播放器主体架构。

## 2. 当前硬件基线

- M5Stack Cardputer ADV
- ESP32-S3FN8
- 8MB Flash
- 无外置 PSRAM
- microSD
- 240×135 TFT
- 56 键键盘
- ES8311 音频 Codec
- 原生 3.5mm 耳机口
- 当前先使用手头已有的旧耳机测试

第一阶段不购买新耳机、不增加外置 DAC。

## 3. V1 核心范围

V1 聚焦“本地随身播放器”：

- microSD 本地音乐
- V1 优先支持 MP3 / FLAC / WAV；当前已验证主路径仍为 MP3
- MP3 目标覆盖最高 320 kbps
- 44.1 kHz / 48 kHz 常见采样率
- 原生 ES8311 + 3.5mm 输出
- 正确 Stereo（立体声）→ Mono（单声道）下混
- 多层音乐目录
- 按需扫描与缓存，避免整库常驻 RAM
- Metadata（元数据）：歌名、歌手、专辑、曲目号等基础信息
- 播放 / 暂停 / 上一首 / 下一首 / Seek（跳转）
- Queue（播放队列）
- 顺序 / 随机 / 单曲循环 / 列表循环
- 熄屏继续播放
- 关机/重启恢复：歌曲 + 播放位置 + 队列 + 播放模式 + Now Playing 视图偏好；恢复后保持 Pause（暂停），不自动出声
- 基础 DSP（数字信号处理）框架
- V1 固定四种互斥音效预设：Original / Tape / Radio / Vocal Clear
- 用户不手动调参，参数由固件内置
- Limiter（限幅器）等必要的保护性音频处理
- M5Launcher 兼容

## 4. 当前明确不做

V1 不以以下内容为目标：

- 外置 DAC / 外置立体声 Codec
- 蓝牙耳机
- 网络电台
- 手机 App
- AI / 云服务
- 24bit / 96kHz 等 Hi-Res 追求
- 空间音频
- 大型媒体数据库
- 为功能数量而功能数量
- 为未来假想需求提前搭建大型框架

## 5. 当前最重要的技术任务

### P3C 当前交付入口

版本 `0.7.0-p3c.media`：字体、中文右 / 原文左的竖排双语歌词、长句多列 / 极长句
分页、彩色 ASCII 封面、实体 View 和 Session 偏好保存。仍不是 P4 完整盲操播放器。
唯一下一次安装是 `ADV-Walkman-P3ABC-Gate.bin`，同时回归 A 文本与验收 B/C。
P3D 黑胶曲库 / Settings / 息屏不在本轮。

本地媒体检查 15 项、既有 P3B 检查 10 项、Session 自测及 29 组真实歌词像素边界
检查通过；Dev / 联合 Gate / P3A / P1 A/B / P2 共六环境构建通过。大小与完整 SHA-256
集中记录在 `docs/P3C_VALIDATION.md`，不将这些结果当成真机 PASS。

本地构建 / 检查入口（不烧录）：

```powershell
.\tools\build_player.ps1 -Target Dev
.\tools\build_player.ps1 -Target P3ABCGate
& '.\.venv-media\Scripts\python.exe' tools/check_p3c.py
& 'B:\PlatformIO\penv\Scripts\python.exe' tools/check_p3b.py
```

- 资源包：`test-data/local/p3-media/package/`，字体 / 歌词 / 原图均不提交 Git。
- 三档封面及歌词像素预览：`test-data/local/p3-media/previews/`。
- 重建资源：独立 `.venv-media` 使用 `tools/media-requirements.txt`；执行
  `tools/prepare_p3_media.py --fonts`。只有首次取官方单曲图才需 `--download`。
- 批处理：`prepare_p3_media.py --audio-root <Music> --image-root <CoverSource> --output <covers>`。
- 用户确认 SD 在 PC 后，才执行 `tools/sync_p3_media.py --sd-root D:\`。它只复制本次
  package 和联合固件，核对原曲及拷贝结果，不重写 MP3，不清理旧固件或状态。
- 2026-08-27 用户确认后已同步到 SD，资源及固件 Hash 全部核对通过。另按用户要求
  将 SD 上旧 Dev / P2 Gate / P3A Gate 三个 BIN 备存 PC 后移除；当前 Walkman 安装项
  只保留 `/firmware/ADV-Walkman-P3ABC-Gate.bin`。Bruce、UIFlow2、原曲和状态未改。

真机屏幕逐步引导：A 原导航 → 前奏 / Header 确认 → 双语长句确认 → View（顶部
第二排第二颗）→ Cover 确认 → 自动连续播放 / 浮层 → 听感确认 → Pause/Seek、无歌词
回退、保存并重启一次。Enter 确认，确认页 Fn+Esc 表示不通过。脚本动作前有提示，
自动部分目标 3–5 分钟，等待人确认不计时。`PASS` 后 Enter 可直接回曲库继续试用，
保持暂停；同版本下次启动不自动重跑。

测试后只需 SD 回 PC，读取：

```powershell
& 'B:\PlatformIO\penv\Scripts\python.exe' tools/validate_p3abc_gate.py --sd-root D:\
```

技术细节见 [`P3C_IMPLEMENTATION.md`](docs/P3C_IMPLEMENTATION.md)，构建与验收记录见
[`P3C_VALIDATION.md`](docs/P3C_VALIDATION.md)。以下保留 A/B 历史记录，不代表当前 artifact。

### A/B 历史实施记录

P3 已按 `P3A → P3B → P3C → P3D` 分步冻结，完整路线见
[`docs/P3_DELIVERY.md`](docs/P3_DELIVERY.md)。当前 P3A 只建立真实可操作的竖屏
UI、曲库 / 播放列表和跨页播放。P3B 在其上实现正式 Now Playing 歌曲信息、滚动、
进度和状态，仍不提前制作歌词、ASCII Cover、黑胶动画或完整 Settings。

`0.5.1-p3a.gate` 已通过按键、页面路由、跨页播放和音频连续性的真机 Gate；当前仍
保持 `DEVICE TEST`，因为真实曲库名 `ADVWalkmanBenchmark` 暴露了卡片文字越界。
`0.5.2-p3a.textfix` 已完成按像素宽度、字体度量和 UTF-8 边界工作的统一换行 /
省略机制，覆盖当前四页动态文本。Library 名称框为 97×38 px、最多两行，保留原有
左对齐与字号；P3B 复用到 Now Playing，P3C 完成正式中日文字体适配，P3D 只做最终
视觉校准。代码和静态尺寸检查通过不代表真机显示已通过。

当前实施与真机节奏为：代码和提交继续按 `P3A text fix → P3B → P3C` 分开推进；
由于原 Gate A 的功能与音频路径已经通过，不再要求单独安装只修换行的 P3A 固件。
用户下一次实机操作将安装一份 Gate A-fix+B+C 固件，一次确认曲库名换行并验收
Now Playing、字体、歌词、ASCII Cover 与 View Selector。其后只剩 P3D Gate。

以下是 P3B 当时的本地构建入口，不是当前联合交付的安装清单：

```powershell
.\tools\build_player.ps1 -Target Dev
.\tools\build_player.ps1 -Target P3AGate
.\tools\build_player.ps1 -Target P2Gate
& 'B:\PlatformIO\penv\Scripts\python.exe' tools/check_p3b.py --artifacts
```

P3A 历史文本修复 `0.5.2-p3a.textfix` 两个构建为 708,176 / 714,608 bytes；
P3B 当时的 Dev / P3A 回归版本为 `0.6.0-p3b.chrome`，历史生成物记录如下，
Launcher 上限仍为 `0x140000`（1,310,720 bytes）：

| 本地生成物 | 大小 / bytes | SHA-256 |
|---|---:|---|
| `artifacts/ADV-Walkman-Dev.bin` | 715,552 | `f7daf3c01477ffdf65e2e56252cc6d05d5dc3719572e56c824447a864cc7d913` |
| `artifacts/ADV-Walkman-P3A-Gate.bin` | 721,744 | `f320147dc5e427a28ed7794479464e08590d36051aadff11df1997b4ed90ef54` |
| `artifacts/ADV-Walkman-P2-Gate.bin` | 724,256 | `b839e90ae78b2f82a649961401f809f2db47288a85ac15ffe16b015732b72fe3` |

2026-08-27 三个环境均构建通过。P2 保留历史版本 `0.4.4-p2.final-gate`，这次只做
Metadata 接口兼容构建，不表示重新执行 P2 真机 Gate。

Dev / P3A 静态 RAM 为 109,176 / 110,040 bytes，比文本修复各增加 6,640 bytes，
其中固定 RGB565 行缓冲为 4,860 bytes；没有全屏 Sprite。

Gate 新增实际曲库名的行数、像素宽度、截断、UTF-8 和布局错误日志；
`ADVWalkmanBenchmark` 必须完整显示为两行，失败独立归因为 `library_text_layout`。
原有方向、按键、页面和音频条件保持不变。下次 A-fix+B+C 复用这组断言；联合固件
现已由 P3C 实现，但仍未真机验收。P3B 当时没有改写 SD，最后已验收的历史版本为 `0.5.1-p3a.gate`。

P3B 自动检查包含 10 项 PC 几何 / 时间参考 / 源码契约检查，以及构建时执行的
实际动画 / 音量公式 constexpr 断言。可注入时钟的完整模型检查、M5GFX 像素裁剪、
浮层背景恢复和 P3B 日志支持已编译，但没有在 ADV 上执行；不能据此声称屏幕或
音频通过。P3C 接入联合 Gate 时复用这些支持，P3B 连续播放窗口仍要求 Error /
Backpressure=0、PCM gap≤70 ms。P3B 不增加安装步骤。

P0 Audio Backend Benchmark 已完成，V1 正式冻结 Candidate A：

```text
ESP8266Audio 1.9.7
→ 32-bit Stereo to Mono downmix
→ 3 × 1536-sample M5.Speaker buffer
→ M5Unified Cardputer ADV / ES8311
```

P2 真机验证确认原 768-sample 配置的约 52 ms 缓冲余量会被正常 SD 目录工作耗尽；正式 Player 保持 Candidate A、三缓冲、downmix 与音量不变，只将单 Buffer 增至 1536 samples，提供约 104 ms 总余量。

P2 Music Library 已于 2026-08-26 通过 `0.4.4-p2.final-gate` 真机与 Host Validator 验收：多层目录、1,000 项 Lazy Scan / Cache、Metadata、Recent Tracks 和播放中连续性全部收口。P2 交付 Library Engine 与开发验收入口；实体按键可直接操作的正式 Library UI 仍属于 P3。选型证据见 [`docs/AUDIO_BENCHMARK.md`](docs/AUDIO_BENCHMARK.md)。

### P2 历史开发构建与 Gate 复现

以下保留复现入口，不是本轮操作要求；P2 已完成，不需要重新制作夹具或安装。

默认 PlatformIO 环境为 `player-dev`：

```powershell
.\tools\build_player.ps1
```

脚本每次都先重新构建，再生成并校验：

```text
artifacts/ADV-Walkman-Dev.bin
```

microSD 已挂载为例如 `D:\` 时，可一并复制并复核 SHA-256：

```powershell
.\tools\build_player.ps1 -SdRoot D:\
```

目标位置为 `/firmware/ADV-Walkman-Dev.bin`，由 M5Launcher 正常安装；不使用普通 PlatformIO upload 覆盖 Launcher。当前 `player-dev` 已是 P3B UI 开发入口，P2 自动验收仍使用独立的 `P2Gate`。

P2 本地 Fixture 由已有无版权 P1 音频派生，包含多层 UTF-8 路径、1,000 个 scan-only 文件、长 common-prefix 排序样本和 Metadata 边界样本。测试数据位于 Git 忽略目录，不提交仓库。千文件目录使用只读目录项枚举与 4 KiB 批量缓存写入；设备端抽查 32 个分页 / LRU / 首尾代表点，PC 端仍全量核对 Fixture。Gate 还会只读使用已经验收的 `/Music/ADVWalkmanBenchmark/benchmark.mp3` 作为持续播放音频；准备脚本只核对其大小和 SHA-256，不复制、删除或改写该歌曲。microSD 例如挂载为 `D:\` 时：

```powershell
python .\tools\prepare_p2_library.py --sd-root D:\
.\tools\build_player.ps1 -Target P2Gate -SdRoot D:\
```

脚本只会重建带专用 marker 的 `/Music/ADVWalkmanP2Test/`；同名目录没有 marker 时会停止，不接触用户其他音乐。Gate 固件为：

```text
/firmware/ADV-Walkman-P2-Gate.bin
```

通过 M5Launcher 安装后启动，按一次物理 `T`，随后不要操作。Gate 目标约 60～120 秒完成，固件在 240 秒时自动判定超时；正常情况下用户等待不会超过 5 分钟。长曲播放期间验证真实的千文件扫描、分页、Metadata 和一次 Recent 发布；P2-04 的 32 项/缺失路径/CRC 冷加载属于启动生命周期，在停止音频后验证，不再制造不可能出现在正常播放中的压力。单次 Player 调度超过 100 ms 会写入 `WARN`；PCM submit 超过 70 ms 的缓冲感知上限、连续调度阻塞、Audio Error、Backpressure 或 PCM 停止推进仍会失败。测量期间不刷新屏幕，只在开始和停止音频后的最终结果刷新，P2-01～P2-04 结果写入：

```text
/ADVWalkman/logs/p2-01-last.txt
/ADVWalkman/logs/p2-02-last.txt
/ADVWalkman/logs/p2-03-last.txt
/ADVWalkman/logs/p2-04-last.txt
```

PASS 后把 SD 插回 PC，执行只读复核：

```powershell
python .\tools\validate_p2_gate.py --sd-root D:\
```

历史 P1 Gate 仍保留独立构建入口 `-Target P1GateA` 与 `-Target P1GateB`，用于回归源码隔离；`T` 与串口命令都只是开发入口，不改变冻结的 V1 Keymap。

### 历史 P0 三候选构建

使用仓库提供的统一脚本：

```powershell
.\tools\build_p0_backends.ps1
```

脚本先在稳定的 Arduino-ESP32 2.0.16 环境构建 A/B，再把 C 的 Arduino-ESP32 3.3.8 / IDF5 packages 隔离到：

```text
B:\PlatformIO\isolated\adv-walkman-c\packages
```

共享 PlatformIO Core 仍位于 `B:\PlatformIO`。三个 app binary 生成后分别以 `ADV-Walkman-Bench-A.bin`、`ADV-Walkman-Bench-B.bin`、`ADV-Walkman-Bench-C.bin` 放入 microSD `/firmware/`，由 M5Launcher 安装；不使用普通 PlatformIO upload 覆盖 Launcher。

## 6. 文档入口

| 文档 | 作用 |
|---|---|
| [`AGENTS.md`](AGENTS.md) | Codex / Agent 开发规约 |
| [`docs/PRD.md`](docs/PRD.md) | 产品目标、范围、体验与待确认事项 |
| [`docs/TECH_DESIGN.md`](docs/TECH_DESIGN.md) | 技术架构、模块、数据流与技术原则 |
| [`docs/P3_DELIVERY.md`](docs/P3_DELIVERY.md) | P3A/B/C/D 固定执行顺序、边界与 Gate |
| [`docs/REFERENCES.md`](docs/REFERENCES.md) | 开源项目与官方资料参考总结 |
| [`docs/AUDIO_BENCHMARK.md`](docs/AUDIO_BENCHMARK.md) | 音频底层 A/B/C 基准测试 |
| [`TASKS.md`](TASKS.md) | Backlog（任务池）、优先级与 AC（验收标准） |
| [`CHANGELOG.md`](CHANGELOG.md) | 已完成版本变化 |

## 7. 当前设计状态

V1 产品侧主要设计已完成，已足够交付 Codex 正式开工。

已冻结：

- 项目定位与 V1 范围
- 原生 ES8311 + 3.5mm 第一阶段路线
- 四种互斥 Sound Preset
- 仅在播放器页面生效的顶部 3×4 盲操区
- 耳机孔朝上竖持的主要使用姿态
- 播放器 / 播放列表 / 曲库 / 设置四页面层级与逐层返回
- Now Playing 信息结构
- Now Playing Lyrics / Color ASCII Cover 双视图与偏好规则
- 逐行竖排歌词逻辑
- 中文 / 原文双语歌词规则
- 彩色 ASCII Cover 方向
- 息屏 Soft Lock 逻辑
- 上方曲库封面 + 下方黑胶唱片堆叠的曲库方向
- 整洁标准列表形式的播放列表
- Music / Lyrics / Cover 分离、相对路径镜像与每首歌独立 Cover

允许真机原型小范围校准：

- 中文 / 英文字号约 ±2 px
- 歌词区域列宽与间距
- ASCII Cover 网格密度
- 标题滚动速度
- Sound Preset 的细微参数

这些属于真机校准，不改变 V1 产品定义。

## 8. 开发原则摘要

- 音频稳定优先于 UI 花哨。
- UI、文件浏览和元数据处理不能破坏连续播放。
- 先测原生硬件上限，再决定是否增加外置硬件。
- 优先复用成熟开源方案中的有效设计，不从零重复踩坑。
- 不过度设计，不过度防御，以实际收益和开发效率为优先。

## 8. V1 Keymap 摘要

V1 以“耳机孔朝上、设备竖持”为主要播放器使用姿态。播放器页面独占最靠近耳机孔的顶部 3×4 物理区域；这里的 `1–12` 是位置编号，不代表键帽字符：

```text
┌────────┬────────────┬────────────┬──────────┐
│ Vol +  │ Play/Pause │ Play/Pause │ Previous │
├────────┼────────────┼────────────┼──────────┤
│ Vol -  │    View    │ Play Mode  │   Next   │
├────────┼────────────┼────────────┼──────────┤
│Original│    Tape    │   Radio    │VocalClear│
└────────┴────────────┴────────────┴──────────┘
```

`View` 在有可用歌词时切换 Lyrics / Cover；无歌词时保持 Cover。`Play Mode` 按 `Normal → Repeat One → Repeat All → Shuffle → Normal` 循环。旧数字列以及 `H/L/Q/R/S/V` 全局快捷键不再是 V1 基线。

离开播放器页面后立即恢复普通 UI 输入：方向键导航、Enter 确认、Esc 返回。曲库页面额外使用 `S` 进入设置；播放器和播放列表中的 `S` 不承担该功能。


## 9. V1 UI 摘要

主要播放器姿态：

```text
135 × 240 logical portrait
耳机孔朝上
UI 不自动旋转
```

四页面层级：

```text
播放器 --Esc--> 播放列表 --Esc--> 曲库
曲库 --S--> 设置 --Esc--> 曲库
```

曲库是最外层内容页，`Esc` 不再退出到额外主页。音频可跨页面继续播放，但完整的 3×4 播放控制只属于播放器页面。

有有效恢复歌曲时启动进入播放器页面并保持 Pause；第一次使用或没有可恢复歌曲时进入曲库页面。

Now Playing：

```text
Header
歌曲名 / Artist
长标题静止约 5 秒后滚动一遍，再停约 5 秒

Content Stage
有可用歌词 + 偏好 Lyrics → 竖排同步歌词
有可用歌词 + 偏好 Cover  → 彩色 ASCII Cover
无可用歌词                → 彩色 ASCII Cover
View                       → Lyrics ↔ Cover

Footer
时间 / 进度 / 播放状态 / Play Mode / Sound Preset
```

Header / Footer 不随 View 切换变化。切换不影响歌曲、播放状态、进度、Queue、Sound Preset 或 Volume。`preferred_now_playing_view` 默认 `LYRICS` 并跨歌曲、跨重启保留；无歌词歌曲临时退化为 Cover 时不覆盖该偏好，下一首重新有歌词后会恢复 Lyrics。

P3B 确认三区高度为 34 / 168 / 38 px；Title 约 14 px、Artist / Footer 约 12 px。
Title 单行，长标题首尾各停 5 秒、24 px/s 滚动，暂停不冻结；Artist 单行省略。
音量不常驻 Footer，改为 Content 左侧 3 秒临时浮层；本阶段仅提供显示事件接口，
P4 才接入真实按键。P3B 音效仍为实际 Original，不表示其他 DSP 已实现。

歌词：

- 逐行 LRC，不做逐字 Karaoke；
- 上一组在左、当前组居中、下一组在右；
- 换句时整体向左移动；
- 中文 / CJK 使用楷体约 16 px；
- 英文使用 Times New Roman 约 12 px，每个字形单独旋转 90°后沿纵向排列；
- 外文歌曲优先支持“中文译文 + 原文”双语；
- 字体资源优先放 SD。

Color ASCII Cover：

- PC 端按每首歌曲批量生成彩色 ASCII Cover；
- ADV 不实时转换图片；
- PC 端预渲染 RGB565，ADV 直接读取；
- 初始目标网格约 30×24，真机允许一次性校准。

曲库页面采用上方独立曲库封面、下方黑胶唱片堆叠选择带；播放列表先采用清晰的标准歌曲列表。媒体资源按相同相对路径和 basename 机械匹配：

```text
/Music/<relative>/<song>.<audio>
/Lyrics/<relative>/<song>[.<language>].lrc
/ADVWalkman/covers/<relative>/<song>.cover.adv
```

每首歌保存独立设备封面，不使用专辑公共封面、模糊匹配、Hash 数据库或 JSON Manifest。曲库封面与歌曲封面相互独立；曲库封面的最终文件命名和具体动画参数留到 P3 真机原型冻结。

息屏：

- 息屏后所有按键原功能失效；
- 第一次任意键只唤醒并吞掉该按键；
- 第二次按键才执行；
- 因此息屏时第一次按 3×4 区的 `View` 只唤醒，不切换 View；
- 唤醒后约 5 秒无操作重新息屏；
- 正常播放时默认约 15 秒无 UI 操作自动息屏。

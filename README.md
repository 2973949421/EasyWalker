# ADV Walkman

> 工作名：ADV Walkman  
> 当前阶段：P3 修复 `0.8.1-p3d.fix`，Tab导航、媒体预取和稳定刷新；A/B/C/D仍待真机收口
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

### 当前交付入口 — 0.8.1 Tab导航与媒体修复

本轮实现与交付证据见 [`P3_OPTIMIZATION_FIX.md`](docs/P3_OPTIMIZATION_FIX.md)。
67项本地检查与六环境构建通过；SD已更新同名联合BIN及11张全宽歌曲ASCII封面。
性能和手感尚待新版真机日志，A/B/C/D不提前标记DONE。
Player单键Tab打开当前歌曲目录并定位；Playlist/Library单键Tab回Player，保持歌曲、
进度和暂停状态。Enter仍选歌从头播放，Esc仍逐层返回；Settings不接管Tab。
同窗口移动只更新两行；同曲页面往返保留歌词索引和封面校验，真正换歌才替换资源。
歌词当前＋下一组提前准备、独立字模固定；封面结束绘制不再误关校验文件。

前序已按 **P3C修复 → 独立P3D实施** 完成本地交付；当前修复实测问题，仍用同一BIN合并自由验收。
P3D已加入普通彩色曲库封面、黑胶选择带、四项中文设置、独立显示存档及息屏吞键。
实施、构建和SD交付见 [`P3D_IMPLEMENTATION.md`](docs/P3D_IMPLEMENTATION.md)，
P3C前置修复见 [`P3C_NAVIGATION_FIX.md`](docs/P3C_NAVIGATION_FIX.md)。

Esc 先退出歌词/封面并清屏，再异步打开播放列表；加载时仍可返回，错误显示原因，
Enter 重试、Esc 返回。可从曲库进入 AveMujica，选中歌曲后才切换为该文件夹队列。
浏览本身不换歌；不会把 benchmark 的单曲队列强行变成全 SD 队列。
本轮没有提前接通实体上一首/下一首。时间保留14 px Times，音量、歌词不改；歌曲ASCII封面按原图重新生成全宽版本。

合并固件自由试用，不再单独安装0.7.6：

仍安装同一个 `/firmware/ADV-Walkman-P3ABC-Gate.bin`，名称保留以免增加 SD 安装项。
启动直接进入普通界面；恢复歌曲保持 Paused。没有必须按顺序完成的提示卡、自动
Seek / 切 View / 暂停 / 重启，也不再盯着封面等 60 秒后被测试程序打断。

单键操作（不按 Fn；仅 Player 启用音量、播放和 View）：

| 物理键帽 | 当前作用 |
|---|---|
| Backspace / del | 音量 +8（显示刻度 0～255，实际输出限幅） |
| `= +` | 音量 −8 |
| `\` 或 Enter | 播放 / 暂停，两个冻结盲操位置均可用 |
| `] }` | Lyrics / Cover |
| Esc | 返回播放列表；列表内继续返回曲库 |
| Tab | Player→当前歌曲目录；Playlist/Library→Player，不重播；Settings及无当前歌曲时无动作 |
| 方向位置、Enter | 曲库 / 播放列表导航、选歌 |
| S（曲库页） | 进入设置；上下选择、左右调整、Enter打开内部子面板、Esc返回 |
| T | 请求状态及诊断保存；等现有状态保存完成后显示“已保存” |

可以按自己的顺序听歌、看歌词、切封面、调音量和浏览。日志每 15 秒自动分块保存；
日志按启动编号追加，不覆盖上次启动。结束前在播放器按 T，等“已保存”后再关机取 SD。
日志保留同版本每次启动的第一项错误，不会为了测试主动停播。
自然连续播放满 60 秒只是后台覆盖项；没测到的操作写 INCOMPLETE，不是假 PASS。
本次最后在设置里确认“返回Launcher”，由固件保存并返回；重新启动后等至少3秒不要按键，确认恢复暂停、歌曲、视图和显示设置正确；
不自动重启，也不要求每首完整听完。播放中的 Seek 仍单列未验，不以无声启动自检冒充。

Cover 保留28 / 188 / 24 px；Lyrics 隐藏歌名/歌手，将上方216 px全部留给歌词。
歌词采用18 px微加粗楷体 / 14 px Times New Roman，只显示当前双语组；首句前灰色预览第一页，到期点亮。
长句完整续列 / 必要时分页；英文单词放不下优先整体换列，不拆 never。
正常UI同样统一楷体与Times，歌名14、歌手12、底栏时间14 px；透明音量条不加黑底。
ASCII 默认40×32，从原图重制全宽135px真字符mask，方图135×135，其他比例适配135×188不裁切。
Cover歌名、歌手分别居中；14px时间右侧增加播放模式和Original原声小标识，不占歌词空间。
新版100%对应最初未限幅版40%（Speaker raw102）；启动约31%仍是raw32，不提高开机响度。
这是当前耳机的用户校准，不是绝对安全声压保证；先从低音量试听，不必试到最大。
默认模式不常驻 NORM Original。`/Music/AveMujica/`新增10首转码歌曲、9组中日歌词；
暗黑天国故意无歌词，用于Cover-only验证。原文件、benchmark及日文原稿不改。
只提前接通已冻结的播放暂停和音量，其他 P4 按键 / DSP 仍未实施。

本轮自由验收要点（不锁顺序）：

- 从歌词及封面退回列表，去AveMujica选择至少两首不同歌曲，包含无歌词的暗黑天国。
- 分别在播放和暂停时Tab往返，确认不重播；Enter仍重播。息屏首次Tab仅唤醒，松开再按才导航。
- 自选Sophie、Black Birthday、Symbol 1、Crucifix X查看封面和歌词；不要求逐首完整听完。
- 看曲库五人合图、黑胶切换和两行长名称；浏览时音乐持续。
- 曲库按S进入设置，试调亮度；播放器默认3分钟、其他页面默认30秒，可先改成15秒测试。
- 息屏后第一次按View或音量只亮屏，全部松开再按才执行；组合键也仅唤醒。
- 自然操作累计至少60秒连续播放；最后恢复希望保留的设置，返回Launcher再启动。
- 返回后先静置至少3秒，再等待一次15秒日志保存（或按T保存），才关机把SD交回PC读取。

设置保存停止调整约1秒后自动进行；显示未保存时不要靠重启解决，保留日志。
Launcher若出现其原有自动启动倒计时，按它的Enter提示进入菜单；本固件不改其配置。

检查与构建：

```powershell
.\tools\build_player.ps1 -Target Dev
.\tools\build_player.ps1 -Target P3ABCGate
& '.\.venv-media\Scripts\python.exe' tools/check_p3_free.py
& '.\.venv-media\Scripts\python.exe' tools/check_p3_navigation.py
& '.\.venv-media\Scripts\python.exe' tools/check_p3_closure.py
& '.\.venv-media\Scripts\python.exe' tools/check_p3d.py
& '.\.venv-media\Scripts\python.exe' tools/check_p3_optimization.py
& '.\.venv-media\Scripts\python.exe' tools/preview_p3_lyrics.py
& 'B:\PlatformIO\penv\Scripts\python.exe' tools/validate_p3_free.py D:\ADVWalkman\logs\p3-free-last.txt
```

媒体 package / 预览仍在 `test-data/local/p3-media/`，Git 忽略；只用现有独立媒体环境。
P3D独立LCOV不变；本轮用`tools/refresh_p3_covers.py`重制11张已绑定歌曲ASCII及预览，
仅同步歌曲ASCII和同名BIN。已有字体覆盖新文字；音乐、歌词、曲库封面、存档与旧日志不动。
资源格式见 `docs/TECH_DESIGN.md`；本次交付结果见 `docs/P3_OPTIMIZATION_FIX.md`。
音频 70 ms PCM / 零 Error / 零 Backpressure、歌词 100 ms 呈现 / 200 ms 到期延迟
阈值不变；本地检查不代表实际听感、刷新或按键已通过。

历史0.7.5六环境构建、48项PC检查及D盘同步已完成；0.7.6仅本地，本轮由0.8.0统一交付。
前版收尾内容见 `docs/P3ABC_CLOSURE.md`。历史0.7.4约10分钟日志
Audio Error / Backpressure为0，但PCM峰值70.494 ms仍超70 ms；不能据此宣称收口。
新版仍须实际显示、音频和手动重启日志通过，才将对应任务标记DONE。

### P3C 0.7.0～0.7.2 历史交付与旧 Gate（以下不再是操作步骤）

联合 Gate 版本 `0.7.2-p3c.timer`：保留 `0.7.1` 的字体、中文右 / 原文左的竖排双语歌词、长句多列 / 极长句
分页、彩色 ASCII 封面、实体 View 和 Session 偏好保存。仍不是 P4 完整盲操播放器。
唯一下一次安装是 `ADV-Walkman-P3ABC-Gate.bin`，同时回归 A 文本与验收 B/C。
P3D 黑胶曲库 / Settings / 息屏不在本轮。

初版真机 A 导航和两行通过，B 未执行，C 资源失败；资源包并未漏放。修复采用单键
导航、整组歌词准备与呈现、资源前置检查、具体失败日志；取消横移与“前奏”标签。
`0.7.1` 随后的启动测试在累计 105 ms 时误报 `preflight / phase_timeout`，A/B 尚未开始。
`0.7.2` 只修正 Gate 的阶段计时：阶段切换后不使用旧时间判定新阶段；真正 45 秒超时
不变。13 项同一 C++ 判定函数的编译期测试通过，并确认旧实现会被测试拒绝。
普通 Dev / P3A 固件仍为 `0.7.1`；本次只重建和交付联合 Gate，不改产品或音频。
本地媒体检查 15 项、既有 P3B 检查 10 项及新增修复检查 8 项；六环境构建结果、大小与完整 SHA-256
集中记录在 `docs/P3C_VALIDATION.md`，不将这些结果当成真机 PASS。

本地构建 / 检查入口（不烧录）：

```powershell
.\tools\build_player.ps1 -Target Dev
.\tools\build_player.ps1 -Target P3ABCGate
& '.\.venv-media\Scripts\python.exe' tools/check_p3c.py
& '.\.venv-media\Scripts\python.exe' tools/check_p3abc_fix.py
& 'B:\PlatformIO\penv\Scripts\python.exe' tools/check_p3b.py
& 'B:\PlatformIO\penv\Scripts\python.exe' tools/check_p3abc_timer.py
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

`0.7.2` 计时修复已覆盖同名联合 BIN（761360 bytes），PC/SD Hash 一致；详见
`docs/P3C_VALIDATION.md`。资源未变，
不再次复制媒体，也不清理其他文件。启动先自动检查资源，失败会
标出组件 / 阶段 / 具体原因；通过才出现单键说明卡。
真机顺序：Library 左 / 右 / Enter → Playlist 上 / 下 / Enter → 播放后单键 Esc；
剩余 A 页面路由自动回归。随后提示卡说明 B/C 看什么，再清除提示，分别显示真实
Header / 暗色首句预览、双语长句、View（顶部第二排第二颗）与 Cover。
真实展示按 Enter 确认，单键 Esc 拒绝，不再按 Fn；提示不会压在歌词上。
之后自动 60 秒连续播放 / 冷资源 / 浮层 → 听感确认 → Pause/Seek、缺歌词回退及重启。
脚本动作前有提示；人工提示卡使用短行，保留完整的下一键说明。
自动部分目标 3–5 分钟，等待人确认不计时。`PASS` 后 Enter 可直接回曲库继续试用，
保持暂停；同版本下次启动不自动重跑。

70 ms PCM、100 ms 完整歌词呈现及 200 ms 正常换句延迟均未放宽。SD 文件名额为 12；
SDK 全局文件表比原默认值增加 28,959 bytes，独立于仍受 48 KiB 限制的媒体工作集。
总 Heap 余量需看真机记录，不能只看编译静态 RAM。

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

以下为历史 A-fix+B+C 安排；当前按上方“P3C 修复 → 独立 P3D → 合并真机”执行。
此前代码和提交按 `P3A text fix → P3B → P3C` 分开推进；
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

Footer不变，Header仅Cover可见。切换不影响歌曲、播放状态、进度、Queue、Sound Preset 或 Volume。`preferred_now_playing_view` 默认 `LYRICS` 并跨歌曲、跨重启保留；无歌词歌曲临时退化为 Cover 时不覆盖该偏好，下一首重新有歌词后会恢复 Lyrics。

当前Cover三区为28 / 188 / 24 px，Lyrics为216 / 24 px；Title14、Artist12、时间14 px。
Title 单行，长标题首尾各停 5 秒、24 px/s 滚动，暂停不冻结；Artist 单行省略。
音量不常驻 Footer，Content左侧3秒透明细条；已按授权接通实体Vol+/−。
音效仍为实际 Original，不表示其他 DSP 已实现。

歌词：

- 逐行 LRC，不做逐字 Karaoke；
- 当前双语组居中，中文右、原文左；正常播放不显示前后组；
- 整组准备完成后切换，不做横移动画；前奏灰色首组第一页；
- 中文 / CJK 使用楷体18 px微加粗；
- 英文使用 Times New Roman 14 px，每个字形单独旋转90°后纵向排列，整词换列；
- 外文歌曲优先支持“中文译文 + 原文”双语；
- 字体资源优先放 SD。

Color ASCII Cover：

- PC 端按每首歌曲批量生成彩色 ASCII Cover；
- ADV 不实时转换图片；
- PC 端预渲染 RGB565，ADV 直接读取；
- 当前默认网格40×32，保持彩色字符画，真机确认精细度。

曲库页面采用上方独立曲库封面、下方黑胶唱片堆叠选择带；播放列表先采用清晰的标准歌曲列表。媒体资源按相同相对路径和 basename 机械匹配：

```text
/Music/<relative>/<song>.<audio>
/Lyrics/<relative>/<song>[.<language>].lrc
/ADVWalkman/covers/<relative>/<song>.cover.adv
```

每首歌保存独立设备封面，不使用专辑公共封面、模糊匹配、Hash 数据库或 JSON Manifest。
曲库普通彩色封面独立存放在`/ADVWalkman/library-covers/folders/<一级目录名>/cover.adv`，
未分类为`/ADVWalkman/library-covers/root.cover.adv`；黑胶切换约160ms/最多4帧，平时静止。

息屏：

- 息屏后所有按键原功能失效；
- 第一次任意键只唤醒并吞掉整组按键，直到全部松开；
- 全部松开后再按才执行；
- 因此息屏时第一次按 3×4 区的 `View` 只唤醒，不切换 View；
- 唤醒后重用所在页面的正常时限；
- Player播放/暂停默认3分钟，其他页面30秒；设置中分别可选15/30/60/180/300/600秒/永不。

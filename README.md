# ADV Walkman

> 工作名：ADV Walkman  
> 当前阶段：P2 Music Library（音乐库引擎）
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
- MP3 为主格式
- 目标覆盖最高 320 kbps MP3
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

P0 Audio Backend Benchmark 已完成，V1 正式冻结 Candidate A：

```text
ESP8266Audio 1.9.7
→ 32-bit Stereo to Mono downmix
→ 3 × 768-sample M5.Speaker buffer
→ M5Unified Cardputer ADV / ES8311
```

当前进入 P2 Music Library，在已通过真机验收的 P1 Player Core 上实现多层目录、按需扫描、有界缓存、Metadata 和 Recent Tracks。P2 交付 Library Engine 与开发验收入口；实体按键可直接操作的正式 Library UI 仍属于 P3。选型证据见 [`docs/AUDIO_BENCHMARK.md`](docs/AUDIO_BENCHMARK.md)。

### P2 开发构建与一次性 Gate

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

目标位置为 `/firmware/ADV-Walkman-Dev.bin`，由 M5Launcher 正常安装；不使用普通 PlatformIO upload 覆盖 Launcher。`player-dev` 提供 P2 Library 串口开发入口，但不提前实现 P3 正式按键 UI。

P2 本地 Fixture 由已有无版权 P1 音频派生，包含多层 UTF-8 路径、1,000 个 scan-only 文件和 Metadata 边界样本。测试数据位于 Git 忽略目录，不提交仓库。microSD 例如挂载为 `D:\` 时：

```powershell
python .\tools\prepare_p2_library.py --sd-root D:\
.\tools\build_player.ps1 -Target P2Gate -SdRoot D:\
```

脚本只会重建带专用 marker 的 `/Music/ADVWalkmanP2Test/`；同名目录没有 marker 时会停止，不接触用户其他音乐。Gate 固件为：

```text
/firmware/ADV-Walkman-P2-Gate.bin
```

通过 M5Launcher 安装后启动，按一次物理 `T`，随后约 30～60 秒不要操作。Gate 会自动验证 P2-01～P2-04，屏幕只在阶段变化时刷新，最终结果写入：

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
- Keymap 主体
- 耳机孔朝上竖持的主要使用姿态
- Now Playing 信息结构
- Now Playing Lyrics / Color ASCII Cover 双视图与偏好规则
- 逐行竖排歌词逻辑
- 中文 / 原文双语歌词规则
- 彩色 ASCII Cover 方向
- 息屏 Soft Lock 逻辑
- Library / Queue / Sound / Settings 的 V1 基础交互

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

V1 以“耳机孔朝上、设备竖持”为主要播放器使用姿态。

### 数字列：高频播放与音效

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

### 字母快捷键：页面、播放模式与视图

```text
H  Now Playing / Home
L  Library
Q  Queue
R  Repeat Mode
S  Shuffle
V  View（仅 Now Playing：Lyrics ↔ Cover）
```

`V` 只切换 Now Playing 的 Content Stage；在其他页面不暗中改变偏好。无可用歌词时保持 Cover，不进入空白 Lyrics 页面。

### UI 导航键

方向键、Enter、Esc 等功能键保留原本的 UI / 导航语义，不被播放快捷键占用。

进入真正的文本输入状态时，字母和数字恢复普通输入，不触发播放器快捷键。


## 9. V1 UI 摘要

主要播放器姿态：

```text
135 × 240 logical portrait
耳机孔朝上
UI 不自动旋转
```

Now Playing：

```text
Header
歌曲名 / Artist
长标题静止约 5 秒后滚动一遍，再停约 5 秒

Content Stage
有可用歌词 + 偏好 Lyrics → 竖排同步歌词
有可用歌词 + 偏好 Cover  → 彩色 ASCII Cover
无可用歌词                → 彩色 ASCII Cover
V                          → Lyrics ↔ Cover

Footer
进度 / Sound Preset / Volume
```

Header / Footer 不随 View 切换变化。切换不影响歌曲、播放状态、进度、Queue、Sound Preset 或 Volume。`preferred_now_playing_view` 默认 `LYRICS` 并跨歌曲、跨重启保留；无歌词歌曲临时退化为 Cover 时不覆盖该偏好，下一首重新有歌词后会恢复 Lyrics。

歌词：

- 逐行 LRC，不做逐字 Karaoke；
- 上一组在左、当前组居中、下一组在右；
- 换句时整体向左移动；
- 中文 / CJK 使用楷体约 16 px；
- 英文使用 Times New Roman 约 12 px，每个字形单独旋转 90°后沿纵向排列；
- 外文歌曲优先支持“中文译文 + 原文”双语；
- 字体资源优先放 SD。

Color ASCII Cover：

- PC 端批量生成彩色 ASCII Cover；
- ADV 不实时转换图片；
- PC 端预渲染 RGB565，ADV 直接读取；
- 初始目标网格约 30×24，真机允许一次性校准。

息屏：

- 息屏后所有按键原功能失效；
- 第一次任意键只唤醒并吞掉该按键；
- 第二次按键才执行；
- 因此息屏时第一次按 `V` 只唤醒，不切换 View；
- 唤醒后约 5 秒无操作重新息屏；
- 正常播放时默认约 15 秒无 UI 操作自动息屏。

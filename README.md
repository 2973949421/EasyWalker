# ADV Walkman

> 工作名：ADV Walkman  
> 当前阶段：V0.2 V1 Design Baseline（V1 设计基线）  
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
- 关机/重启恢复：歌曲 + 播放位置 + 队列 + 播放模式；恢复后保持 Pause（暂停），不自动出声
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

正式播放器开发前，先完成 Audio Backend Benchmark（音频输出底层基准测试）。

候选路线：

1. ESP8266Audio → Cardio 风格 M5.Speaker → ES8311
2. ESP8266Audio → Direct I2S（直接 I2S）→ ES8311
3. BackgroundAudio → I2S → ES8311

先用同一首 320 kbps / 44.1 kHz MP3 和同一副旧耳机比较稳定性、内存、卡顿、爆音、主观声音和实现复杂度，再冻结 V1 音频底层。

详见 [`docs/AUDIO_BENCHMARK.md`](docs/AUDIO_BENCHMARK.md)。

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

### 字母快捷键：页面与播放模式

```text
H  Now Playing / Home
L  Library
Q  Queue
R  Repeat Mode
S  Shuffle
```

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
有歌词 → 竖排同步歌词
无歌词 → 彩色 ASCII Cover

Footer
进度 / Sound Preset / Volume
```

歌词：

- 逐行 LRC，不做逐字 Karaoke；
- 上一组在左、当前组居中、下一组在右；
- 换句时整体向左移动；
- 中文 / CJK 使用楷体约 16 px；
- 英文使用 Times New Roman 约 12 px，每个字形单独旋转 90°后沿纵向排列；
- 外文歌曲优先支持“中文译文 + 原文”双语；
- 字体资源优先放 SD。

无歌词：

- PC 端批量生成彩色 ASCII Cover；
- ADV 不实时转换图片；
- PC 端预渲染 RGB565，ADV 直接读取；
- 初始目标网格约 30×24，真机允许一次性校准。

息屏：

- 息屏后所有按键原功能失效；
- 第一次任意键只唤醒并吞掉该按键；
- 第二次按键才执行；
- 唤醒后约 5 秒无操作重新息屏；
- 正常播放时默认约 15 秒无 UI 操作自动息屏。

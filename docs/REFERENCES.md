# ADV Walkman References

> 作用：记录已经研究过的官方资料和开源方案，以及“值得借什么 / 不借什么”。  
> 注意：本文件是参考资料，不是最终技术决策。最终方案以 `TECH_DESIGN.md` 和真机 Benchmark 为准。

## 1. 参考原则

研究现有项目的目标不是：

> 找一个项目直接改皮肤。

目标是：

> 复用成熟方案中已经验证的设计，减少重复踩坑，同时保持我们自己的产品和架构边界。

优先级：

```text
M5Stack 官方资料 / 官方源码
↓
芯片 Datasheet
↓
本项目可复现的真机验证
↓
高质量开源实现
↓
明确标注的推断
```

本项目可复现的真机结果高于社区项目的口头或 README 声称；如与官方资料冲突，应核对硬件版本、软件版本和测试条件后重新验证。

---

## 2. Cardio

Repository:

https://github.com/ZUENS2020/Cardio

### 定位

针对 M5Stack Cardputer ADV 的本地音乐播放器。

已公开能力包括：

- MP3 / FLAC / WAV
- ES8311
- 5-band EQ
- M5Launcher
- 无 PSRAM 约束下运行
- 外置立体声 Backend 规划 / 实现尝试

### 值得重点借鉴

#### AudioEngine

- 音源 sample rate 跟随
- M5.Speaker 参数调校
- DMA 配置思路
- Triple Buffer（三缓冲）
- 正确 Stereo → Mono 下混
- 音量曲线处理
- Gain ceiling（增益上限）
- EQ
- 输出 Backend 抽象

#### UI / Input

- Dirty redraw
- Hold-to-repeat
- 立即按键反馈
- 进度条限频更新
- Launcher 返回逻辑

### 不直接采用

- 开机全量音乐库扫描
- 只围绕较浅目录构建的 Library 逻辑
- BLE 通知
- RSS
- MQTT / 服务端
- 与纯播放器无关的网络体系

### 我们的结论

Cardio 是 V1 内置音频链路的第一参考，但不作为整个项目的直接底座。

---

## 3. BrokenSignal

Original:

https://github.com/MarcoRR/BrokenSignal

Next fork:

https://github.com/Rythlan/BrokenSignal-Next

### 定位

Cardputer ADV 音乐播放器 + Web Radio。

### 值得重点借鉴

#### Library

BrokenSignal Next 当前最值得研究的是：

- Lazy Scan（按需扫描）
- 多层目录
- Pagination（分页）
- Windowed cache（窗口缓存）
- LRU 风格缓存淘汰
- 大目录处理
- Recent tracks
- Settings persistence
- Screen off
- Seek

这套设计更适合 ADV：

- 无 PSRAM；
- 几 GB 音乐；
- 约 1000 首甚至更多音乐。

### 不直接采用

- Web Radio
- Wi-Fi 体系
- 全局 State 数量较多的组织方式
- 音频核心不作为最终最高参考
- 浏览时部分行为和播放状态耦合较紧

### 许可证注意

Original BrokenSignal 的 README 对许可证表述非常非正式。

如果未来公开发布本项目：

- 可以学习设计；
- 不应在未确认许可证边界时大段直接复制 Original 代码。

BrokenSignal Next 标示 MIT，但其来源关系仍应在正式发布前再次核对。

### 我们的结论

主要作为 Library / Browse / State Persistence 的设计来源。

---

## 4. SomaFM for CardPuter

Repository:

https://github.com/dr3d/SomaFM-for-CardPuter

### 定位

Cardputer / Cardputer ADV 网络电台。

### 值得重点借鉴

- Direct I2S
- 直接初始化 ES8311
- Audio Task 与 UI / Network 分离
- DMA 参数
- sample rate 动态调整
- 简单可读的 I2S output 实现
- 音频可视化数据从 Audio → UI 的低成本共享方式

### 不直接采用

- 网络电台业务
- Wi-Fi 获取频道
- 网络图片
- Station 管理

### 我们的结论

SomaFM 是 Direct I2S Backend 的主要实验参考。

---

## 5. BackgroundAudio

Repository:

https://github.com/earlephilhower/BackgroundAudio

### 定位

同一作者在 ESP8266Audio 之后提供的后台音频架构。

### 值得重点借鉴

- Background decode（后台解码）
- Buffer-driven playback（缓冲驱动播放）
- Application 只负责持续喂入压缩数据
- 通过后台音频减少 UI 主循环对播放的干扰
- underflow 时恢复机制

### 风险 / 限制

- ESP32 版本依赖较新的 I2S 支持
- PlatformIO 可能需要 pioarduino / IDF5
- 与当前稳定 Cardputer 工具链存在环境差异
- 当前正式主路径仍是已验证的 MP3 / Candidate A；V1 新增 FLAC / WAV 产品目标也不构成提前迁移整个工具链或重开 Backend 选型的理由

### P0 固定实验版本

- BackgroundAudio 1.4.4
- pioarduino `55.03.38-1`
- Arduino-ESP32 3.3.8
- ESP-IDF 5.5.4
- Candidate C packages 使用 `B:\PlatformIO\isolated\adv-walkman-c\packages`，不替换 A/B 的 Arduino-ESP32 2.0.16 packages

### 我们的结论

作为 P0 Benchmark 的第三候选。

只有实测明显优于 A/B，才考虑迁移主线。

---

## 6. ESP8266Audio

Repository:

https://github.com/earlephilhower/ESP8266Audio

### 定位

ESP8266 / ESP32 社区成熟音频解码库。

### 本项目价值

- MP3
- FLAC
- AAC
- AudioFileSource
- AudioGenerator
- AudioOutput 抽象
- 社区案例丰富

### 需要注意

传统调用模式需要频繁运行 decoder `loop()`。

如果主循环被 UI、SD 或其他任务长时间占用，可能造成播放断续，因此需要：

- 合理 Buffer；
- task / core 策略；
- 或使用 M5.Speaker 的异步播放；
- 或 Direct I2S；
- 或评估 BackgroundAudio。

### License

库本身涉及 GPL 等许可证。

如果项目未来公开发布，需要在发布阶段专门处理许可证，而不是现在为了许可证问题阻塞个人开发。

---

## 7. M5Stack 官方资料 / M5Unified / M5Cardputer

主要来源：

- M5Stack Cardputer ADV 官方文档
- M5Stack 官方示例 / UserDemo
- M5Unified
- M5Cardputer
- Cardputer ADV 原理图
- ES8311 Datasheet

### 作用

用于确认：

- Pin
- ES8311 初始化
- I2S
- Speaker
- Display
- Keyboard
- SD
- Power
- Launcher 相关硬件事实

### 规则

遇到社区项目和官方实现冲突时：

1. 先核对硬件版本；
2. 核对源码版本；
3. 用真机实验；
4. 不直接假定社区项目或官方抽象一定适合音乐播放。

---

## 8. 已形成的组合思路

当前不 fork 任一完整项目作为最终产品底座。

计划组合：

```text
Cardio
→ Audio tuning / EQ / M5.Speaker / Backend idea

BrokenSignal Next
→ Library / lazy scan / cache / recent / persistence

SomaFM
→ Direct I2S / ES8311 / audio task

BackgroundAudio
→ Next-generation background audio candidate

M5Stack official
→ Hardware truth
```

最终形成自己的：

```text
Library
Player
Decoder
DSP
Audio Backend
UI
```

---

## 9. 后续更新方式

当新增参考项目时，只需要记录：

- 项目是什么；
- 它解决了什么问题；
- 哪些值得借；
- 哪些不适合；
- 对当前技术设计有什么影响。

不要把 `REFERENCES.md` 变成源码摘抄仓库。

如果某项参考最终变成正式设计，应把结论同步到 `TECH_DESIGN.md`。

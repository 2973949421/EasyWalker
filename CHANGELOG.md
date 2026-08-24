# Changelog

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

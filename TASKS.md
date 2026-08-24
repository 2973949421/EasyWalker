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

Status: TODO

AC：

- [ ] MP3 320 kbps / 44.1 kHz 可播放
- [ ] 正确 L+R downmix
- [ ] sample rate 处理正确
- [ ] 使用合理 triple buffer / M5.Speaker 参数
- [ ] Pause / Resume 可用
- [ ] 切歌可用
- [ ] 记录 heap / underrun
- [ ] 真机旧耳机听感记录

---

## P0-03 Candidate B — Direct I2S

Status: TODO

AC：

- [ ] MP3 320 kbps / 44.1 kHz 可播放
- [ ] ES8311 init 有官方 / M5Unified 依据
- [ ] Direct I2S 生命周期正常
- [ ] 正确 L+R downmix
- [ ] Pause / Resume 可用
- [ ] 切歌可用
- [ ] 记录 heap / underrun
- [ ] 真机旧耳机听感记录

---

## P0-04 Candidate C — BackgroundAudio

Status: TODO

AC：

- [ ] 独立环境构建成功
- [ ] 不破坏当前稳定 PlatformIO 主环境
- [ ] ADV + ES8311 可正常输出
- [ ] MP3 320 kbps / 44.1 kHz 可播放
- [ ] 记录 heap / underrun
- [ ] 真机旧耳机听感记录

如果当前库 / IDF 兼容成本明显超过收益，可记录为 DEFERRED，不为了“必须三套都完成”强行迁移。

---

## P0-05 Stress Test

Status: TODO

AC：

- [ ] 每个可用候选完成 30 min 连续测试
- [ ] 最优候选完成 2 h 连续测试
- [ ] UI Stress
- [ ] SD Browse Stress
- [ ] Pause / Resume 循环
- [ ] 切歌循环
- [ ] Seek
- [ ] 记录明显爆音 / 卡顿 / 崩溃
- [ ] 记录 Heap 是否持续下降

---

## P0-06 Select V1 Audio Backend

Status: TODO

AC：

- [ ] `docs/AUDIO_BENCHMARK.md` 填写结果
- [ ] 写明最终选择和原因
- [ ] 更新 `docs/TECH_DESIGN.md`
- [ ] 不因为“更底层”而自动选择复杂方案
- [ ] 形成可进入 P1 的冻结 Audio Backend

---

# P1 — Player Core

P0-06 完成后进入。

## P1-01 MP3 Playback Core

Status: TODO

AC：

- [ ] MP3 CBR
- [ ] MP3 VBR
- [ ] 最高目标 320 kbps
- [ ] 44.1 kHz
- [ ] 48 kHz
- [ ] Track end 自动结束
- [ ] 播放结束可通知 Player 进入下一首

---

## P1-02 Transport Controls

Status: TODO

AC：

- [ ] Play
- [ ] Pause
- [ ] Resume
- [ ] Stop
- [ ] Next
- [ ] Previous
- [ ] Seek
- [ ] 不因连续操作崩溃
- [ ] 无频繁明显爆音

---

## P1-03 Queue & Playback Modes

Status: TODO

AC：

- [ ] Queue
- [ ] Sequential
- [ ] Shuffle
- [ ] Repeat One
- [ ] Repeat All
- [ ] 模式切换状态正确
- [ ] 重启后可恢复

---

## P1-04 Playback State Persistence

Status: TODO

AC：

- [ ] 保存歌曲
- [ ] 保存位置
- [ ] 保存队列
- [ ] 保存播放模式
- [ ] 重启恢复
- [ ] 恢复后保持 Pause
- [ ] 不自动出声

---

# P2 — Music Library

## P2-01 Recursive Folder Browser

Status: TODO

AC：

- [ ] `/Music/`
- [ ] 多层目录
- [ ] 中文路径
- [ ] 隐藏文件过滤
- [ ] 非 MP3 文件不进入 V1 可播放列表
- [ ] 播放时浏览目录不主动 Stop Audio

---

## P2-02 Lazy Scan / Cache

Status: TODO

AC：

- [ ] 不整库常驻 RAM
- [ ] 当前目录快速显示
- [ ] 大目录有界内存
- [ ] 缓存可淘汰
- [ ] 约 1000 首库规模可正常使用
- [ ] 播放中扫描不造成明显断音

---

## P2-03 Metadata

Status: TODO

AC：

- [ ] Title
- [ ] Artist
- [ ] Album
- [ ] Track Number
- [ ] 中文可显示
- [ ] Metadata 解析不长时间阻塞播放
- [ ] 无 Metadata 时合理回退到文件名

---

## P2-04 Recent Tracks

Status: TODO

AC：

- [ ] 保存最近播放
- [ ] 路径不存在时安全忽略
- [ ] 不重复制造大量状态文件
- [ ] 数量上限简单明确

---

# P3 — UI

## P3-01 Portrait UI Shell

Status: TODO

AC：

- [ ] 逻辑画布以 135×240 竖屏为主
- [ ] 耳机孔朝上为主要播放器姿态
- [ ] 不启用 IMU 自动旋转
- [ ] Header / Content Stage / Footer 三段结构可正常渲染
- [ ] UI 更新不造成可感知音频卡顿

---

## P3-02 Now Playing Header / Footer

Status: TODO

AC：

- [ ] Title / Artist 可显示
- [ ] 长 Title 静止约 5 秒后滚动一遍
- [ ] 滚动完成后再次静止约 5 秒
- [ ] Footer 显示当前时间 / 总时长、进度条、Sound Preset、Volume
- [ ] Header 动画不持续抢占歌词视觉

---

## P3-03 Lyrics Renderer

Status: TODO

AC：

- [ ] 支持逐行 LRC 和可选 `song.zh.lrc`
- [ ] 时间戳初始容差约 300 ms
- [ ] 中文 / CJK 竖排
- [ ] 英文字形逐 glyph 旋转 90°后纵向排列
- [ ] 上一句在左、当前句居中、下一句在右
- [ ] 当前句高亮，前后句弱化
- [ ] 换句时整体向左移动
- [ ] 不做逐字 Karaoke
- [ ] 中文默认楷体约 16 px，英文 Times New Roman 约 12 px
- [ ] 字体大小允许真机约 ±2 px 微调
- [ ] UI 不自动旋转

---

## P3-04 Font Loading

Status: TODO

AC：

- [ ] 主要 CJK / Latin 字体可从 SD 加载
- [ ] Flash 保留最小 fallback
- [ ] 缺少 SD 字体时系统不崩溃
- [ ] 字体加载失败不影响 Audio Core
- [ ] 字体原文件不默认提交到公开 repo

---

## P3-05 Color ASCII Cover Tool

Status: TODO

目标：建立 PC 端机械批处理，不使用 Agent 一张一张生成。

AC：

- [ ] 递归扫描音乐库
- [ ] 优先识别 `cover.jpg` / `folder.jpg`
- [ ] 无独立封面时可尝试从 MP3 ID3 APIC 提取
- [ ] 生成彩色 ASCII
- [ ] 同时输出电脑 Preview
- [ ] 输出 ADV 直接读取的预渲染 RGB565
- [ ] 按专辑生成一次
- [ ] 初始支持 26×20 / 30×24 / 34×26 三档
- [ ] 默认先测试 30×24
- [ ] 批处理可重复运行，不需要 Agent 逐张介入

---

## P3-06 ASCII Cover Renderer

Status: TODO

AC：

- [ ] 无歌词时 Content Stage 显示 ASCII Cover
- [ ] ADV 不实时做图片→ASCII 转换
- [ ] 可直接读取 RGB565
- [ ] 读图不造成明显音频卡顿
- [ ] 缺少 Cover 时有简单 fallback

---

## P3-07 Library UI

Status: TODO

AC：

- [ ] 文件夹 / 歌曲简单列表
- [ ] Arrow 导航、Enter 进入 / 播放、Esc 返回
- [ ] 当前选择高亮
- [ ] 浏览不主动停止当前音乐

---

## P3-08 Queue UI

Status: TODO

AC：

- [ ] 当前歌曲高亮
- [ ] 可浏览前后队列
- [ ] 可选择某首直接播放
- [ ] V1 不要求复杂重排

---

## P3-09 Sound UI

Status: TODO

AC：

- [ ] 显示 Original / Tape / Radio / Vocal Clear
- [ ] 当前 Preset 高亮
- [ ] 与数字 1–4 的全局切换保持一致

---

## P3-10 Settings UI

Status: TODO

AC：

- [ ] Brightness
- [ ] Screen Timeout
- [ ] About / Version
- [ ] Return to Launcher
- [ ] 不为了填充页面增加无实际用途的设置

---

## P3-11 Screen-off Soft Lock

Status: TODO

AC：

- [ ] 正常播放默认约 15 秒无 UI 操作息屏
- [ ] Screen Off 时所有按键原功能失效
- [ ] 第一次任意键只唤醒并吞掉该事件
- [ ] 第二次按键才执行正常功能
- [ ] 唤醒后约 5 秒无操作再次息屏
- [ ] 息屏 / 唤醒不打断音频
- [ ] V1 不单独实现复杂 Lock 系统

---

# P4 — Keymap

## P4-01 Global Navigation Semantics

Status: TODO

AC：

- [ ] `↑ ↓ ← →` 保持 UI 导航语义
- [ ] `Enter / OK` 保持确认 / 进入语义
- [ ] `Esc` 保持返回 / 退出语义
- [ ] 不用媒体快捷键覆盖上述核心导航键

---

## P4-02 Numeric Playback Strip

Status: TODO

目标：

实现耳机孔朝上竖持时的数字列高频控制。

Keymap：

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

AC：

- [ ] `0` 调高音量
- [ ] `6` 调低音量
- [ ] `9` Previous
- [ ] `8` Play / Pause
- [ ] `7` Next
- [ ] `5` V1 不分配功能
- [ ] `1–4` 直接切换对应 Sound Preset
- [ ] 普通 UI 页面中快捷键可控制当前播放，不强制跳转页面
- [ ] 快捷键不直接耦合 Audio Backend

---

## P4-03 Letter Shortcuts

Status: TODO

Keymap：

```text
H  Now Playing / Home
L  Library
Q  Queue
R  Repeat Mode
S  Shuffle
```

AC：

- [ ] `H` 跳转 Now Playing
- [ ] `L` 跳转 Library
- [ ] `Q` 跳转 Queue
- [ ] `R` 按 Off → Repeat All → Repeat One → Off 循环
- [ ] `S` 切换 Shuffle On / Off
- [ ] 不额外占用大量字母键

---

## P4-04 Text Input Context

Status: TODO

AC：

- [ ] 进入真正的文本输入状态时，数字和字母恢复普通输入
- [ ] 文本输入时不触发媒体快捷键
- [ ] 方向键、Enter、Esc 继续按 UI 语义工作
- [ ] 退出文本输入后恢复播放器快捷键

---

## P4-05 Screen-off Input Behavior

Status: TODO

AC：

- [ ] Screen Off 时全部快捷键原功能失效
- [ ] 任意键第一次只唤醒屏幕
- [ ] 唤醒按键事件不继续传递
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

# Later / Deferred

当前不做：

- FLAC
- AAC / M4A
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

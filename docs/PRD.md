# ADV Walkman PRD

> 版本：V0.2  
> 状态：V1 Design Baseline  
> 工作名：ADV Walkman

## 1. 产品定位

ADV Walkman 是一个运行在 M5Stack Cardputer ADV 上的本地随身音乐播放器固件。

项目的主要价值：

1. 提供复古随身播放器的情绪价值和实体交互体验；
2. 通过真实硬件项目学习嵌入式音频、I2S、Codec、microSD、UI、按键、缓存和 DSP；
3. 在现有 Cardputer ADV 硬件上尽量获得稳定、完整、顺手的播放器体验；
4. 保留未来扩展到外置立体声输出的可能，但不让未来需求拖慢 V1。

项目不以替代 iPhone + AirPods 为目标，也不追求跨时代硬件性能。

---

## 2. 产品原则

### 2.1 情绪价值优先，但不牺牲基本可用性

它可以“没有必要”，但不能“难用”。

目标是：

> 做出来以后自己愿意揣在兜里、插有线耳机、按实体键听歌。

### 2.2 专用播放器，而不是小手机

V1 不做万能工具箱。

功能和界面应围绕：

- 找歌；
- 播放；
- 控制；
- 音效；
- 状态；
- 省电。

### 2.3 原生硬件先榨干，再决定是否扩展

第一阶段只用：

```text
Cardputer ADV
→ ES8311
→ 原生 3.5mm
→ 现有旧耳机
```

不预先购买新耳机或外置 DAC。

### 2.4 音频连续性优先

UI、目录浏览、元数据解析、设置保存等不能明显干扰播放。

### 2.5 不过度设计

V1 只实现已经明确有价值的能力。

---

## 3. 目标用户

当前唯一明确目标用户是项目作者本人。

因此允许：

- 强个人审美；
- 强个人按键习惯；
- 针对现有音乐库设计；
- 不追求大众适配；
- 不为了“通用产品”增加无实际收益的复杂度。

---

## 4. V1 用户场景

### 4.1 本地听歌

用户将音乐放入：

```text
/Music/
```

可使用任意多层文件夹组织。

播放器打开音乐库、进入目录、选择歌曲并播放。

### 4.2 口袋使用

设备放入口袋后：

- 屏幕可关闭；
- 音乐继续播放；
- 基本控制仍应可靠；
- 熄屏按键遵循本文件 8.9 的 Screen-off / Soft Lock 规则。

### 4.3 重启恢复

设备重新启动时恢复：

- 上次歌曲；
- 播放位置；
- 当前播放队列；
- 播放模式；
- 用户上次选择的 Now Playing 视图偏好。

恢复后默认保持 Pause，不自动播放。

### 4.4 音效

V1 采用最小可行音效方案。

规则：

- 同一时刻只能选择一个音效；
- 音效不能叠加；
- 用户不需要手动调参数；
- 参数由固件内置；
- 音效实现优先选择低 CPU / RAM 成本的方法；
- 如果某个效果影响播放稳定性，优先简化效果，不牺牲连续播放。

V1 固定四种预设：

1. **Original（原声）**：不主动增加声音染色，只做原生单声道输出所必需的 Stereo → Mono 下混，以及必要的安全处理。
2. **Tape（复古磁带）**：轻度 EQ 染色 + 高频衰减 + 很轻的 Soft Saturation（软饱和），目标是温暖、稍暗、轻微模拟味。
3. **Radio（收音机）**：Band-pass（带通）+ 轻度 Compression（动态压缩）+ 很轻的 Soft Saturation，推荐带通约 200–5000 Hz，最终参数以真机听感微调。
4. **Vocal Clear（人声清晰）**：轻度削弱低频、适度提升中频和 2–4 kHz 存在感区域，让人声和咬字更靠前、更清楚。

V1 不做 Surround / Spatial Audio（环绕 / 空间音频），因为原生 ES8311 输出为单声道。后续如增加外置立体声音频 Backend，再重新评估。

---

## 5. V1 功能范围

### 5.1 Audio（音频）

- MP3 播放
- 目标支持最高 320 kbps
- 常见 44.1 kHz / 48 kHz 音源
- CBR / VBR 作为兼容目标
- 原生 ES8311
- 原生 3.5mm
- 正确 Stereo → Mono 下混
- 播放 / 暂停
- 上一首 / 下一首
- Seek
- 音量控制
- 切歌
- 播放结束自动进入下一首
- 基础 DSP 管线
- 固定四种互斥音效预设：Original / Tape / Radio / Vocal Clear
- Limiter / Gain Safety（限幅 / 增益保护）
- 用户不手动调参

### 5.2 Library（音乐库）

- `/Music/` 根目录
- 任意多层目录
- Lazy Scan（按需扫描）
- Cache（缓存）
- 有界内存使用
- 不要求整库常驻 RAM
- 隐藏文件与非音乐文件过滤
- 中文路径
- 基础 Metadata
- Recent（最近播放）

### 5.3 Player（播放器）

- Queue
- 顺序播放
- Shuffle（随机）
- Repeat One（单曲循环）
- Repeat All（列表循环）
- 状态恢复
- 记忆上次播放信息

### 5.4 UI

计划保留五个核心页面：

```text
Now Playing（正在播放）
Library（音乐库）
Queue（播放队列）
Sound（音效）
Settings（设置）
```

V1 视觉方向、主要布局和导航以本文件第 8 章为设计基线；只允许在真机原型中做小范围校准。

Now Playing 的 Content Stage 在歌曲有可用歌词时允许用户在 Lyrics 与 Color ASCII Cover 间切换；歌曲无可用歌词时只显示 Cover。

### 5.5 System（系统）

- 熄屏播放
- 基础电量显示
- 设置持久化
- Launcher 兼容
- 可返回 M5Launcher
- 不依赖网络完成核心播放功能

---

## 6. V1 Out of Scope

以下不作为 V1 目标：

- FLAC
- AAC / M4A
- 24bit / 96kHz
- 外置 DAC
- 外置 Codec
- 蓝牙耳机
- A2DP
- Web Radio
- Wi-Fi 音乐流
- 手机 Companion App
- AI
- 云服务
- 歌词联网
- 大型专辑数据库
- 空间音频
- HRTF
- 复杂混响
- 大规模动画系统

其中部分可进入 Later Backlog，但不是 V1 完成条件。

---

## 7. 产品级成功标准

V1 至少应达到：

1. 320 kbps / 44.1 kHz MP3 可稳定播放。
2. 常规 UI 操作和目录浏览不造成明显音频断流。
3. 暂停、恢复、切歌不存在频繁明显爆音。
4. 原生单声道输出采用正确下混，不丢失任一声道内容。
5. 约 1000 首规模音乐库不需要全部常驻 RAM。
6. 支持多层目录。
7. 中文文件名和主要 Metadata 可正常显示。
8. 可熄屏继续播放。
9. 重启可以恢复歌曲、位置、队列和播放模式，恢复后保持暂停。
10. 实体按键操作应达到“专用播放器”的可用水平。
11. 设备可在 Launcher 工作流中正常安装、启动和返回。
12. 不存在持续性明显 Heap 泄漏或长时间播放后崩溃。
13. 有歌词歌曲可在 Lyrics / Color ASCII Cover 间切换；无歌词歌曲不会进入空白 Lyrics 页面，切换不干扰播放。

---

## 8. UI / UX 设计

### 8.1 主要使用姿态

主要播放器使用姿态：

```text
Portrait（竖持）
耳机孔朝上
逻辑画布约 135 × 240
```

V1 UI 不使用 IMU 自动旋转。用户如需更自然地阅读旋转后的英文歌词，可自行物理旋转设备，系统不跟随旋转 UI。

### 8.2 Now Playing

Now Playing 是 V1 最重要的界面。

基本结构：

```text
Header        约 26 px
歌曲身份

Content Stage 约 184 px
歌词 / ASCII Cover

Footer        约 30 px
进度 / 音效 / 音量
```

以上像素是设计基线，真机允许小范围调整。

#### Header

显示 Song Title 与 Artist。

长标题：

```text
能完整显示 → 静止
超出宽度 → 静止约 5 秒 → 缓慢向左滚动一遍 → 静止约 5 秒 → 循环
```

滚动应克制，避免持续与歌词横向移动争夺注意力。

#### Content Stage

```text
有可用歌词 + preferred = LYRICS → Lyrics
有可用歌词 + preferred = COVER  → Color ASCII Cover
无可用歌词                         → Color ASCII Cover
```

用户在 Now Playing 按 `V` 时，只在 Lyrics 与 Cover 间切换 Content Stage，并更新：

```text
preferred_now_playing_view = LYRICS | COVER
```

默认值为 `LYRICS`，该偏好跨歌曲并在重启后恢复。实际显示视图按当前歌曲是否有可用歌词计算：当偏好为 Lyrics 但当前歌曲没有可用歌词时，临时显示 Cover，不得把用户偏好改写为 Cover；下一首重新有歌词时自动恢复 Lyrics。

无歌词时按 `V` 不切换，也不进入空白 Lyrics 页面。最低实现可以直接无动作；允许短暂显示非阻塞的 `No lyrics` 提示，但提示不是 V1 必做项。

View 切换不得改变当前歌曲、播放 / 暂停、进度、Queue、Sound Preset 或 Volume。Header 与 Footer 不随 Content Stage 切换而变化。

#### Footer

至少显示：

- 当前时间 / 总时长
- 进度条
- 当前 Sound Preset
- Volume

Footer 在歌词 / ASCII 两种状态下保持稳定。

### 8.3 Lyrics（歌词）

歌词是 V1 强需求。

要求：

- 中文歌曲至少正常显示中文歌词；
- 外文歌曲优先支持“中文译文 + 原文”双语；
- 使用本地离线歌词；
- 采用逐行同步，不做逐字 Karaoke。

推荐文件：

```text
song.mp3
song.lrc
song.zh.lrc
```

- `.lrc`：原歌词或歌曲主要歌词；
- `.zh.lrc`：中文翻译；
- 中文歌曲可以只有 `.lrc`。

双语配对：

- 以时间戳为主；
- 时间差约 `≤300 ms` 可视为同一句；
- 配不到译文时只显示原文；
- 不在 ADV 上做语义匹配；
- 如有必要，PC 端预处理统一时间轴。

#### 竖排与横向推进

```text
左侧      中间       右侧
上一组    当前组     下一组
暗        高亮       暗
```

换句时整体向左移动一组，不做上下滚动。

双语当前组由原文列和中文列组成。具体微小间距允许 UI Prototype 调整，但中文译文必须清晰可读。

#### 字体

设计基线：

- 中文 / CJK：楷体约 `16 px`
- 英文：Times New Roman 约 `12 px`
- Song Title：约 `12 px`
- Artist：约 `10 px`
- Footer：约 `10 px`

真机原型允许约 `±2 px` 微调。

中文 / 日文等适合竖排的字符直接竖排。

英文不使用“整句旋转”，而是每个英文字形单独旋转 90°，再沿纵向排列。V1 不自动旋转 UI。

#### 字体资源

大字体优先放 microSD，例如：

```text
/ADVWalkman/fonts/
  kaiti_16.vlw
  times_12.vlw
```

Flash 仅保留最小 fallback 字体，以便 SD 字体失败时仍能显示基本状态和错误信息。字体文件不要直接纳入公开 Git repo，除非许可证明确允许。

### 8.4 Color ASCII Cover

Color ASCII Cover 是 Now Playing 的第二种 Content Stage：有歌词时可由用户选择；无歌词时作为唯一可用视图。

目标：

- ASCII / ANSI Art 气质；
- 保留原专辑主色；
- 使用字符密度、字符形状和颜色表达图像；
- 接近细致的彩色字符画，不是粗糙黑白 `@#%` 图。

转换不在 ADV 上实时完成。

PC 批处理：

```text
扫描 Music
→ 找 cover.jpg / folder.jpg
→ 如无则尝试从 MP3 ID3 APIC 提取封面
→ 缩放
→ 转彩色 ASCII
→ 预渲染
→ 输出 ADV 直接使用的数据
```

初始测试网格：

- 26×20
- **30×24（默认候选）**
- 34×26

真机选出合适密度后批量统一处理。

推荐输出：

```text
cover_ascii_preview.png
cover_ascii.rgb565
```

ADV 直接读取预渲染 RGB565，不现场执行图片→ASCII 转换。按专辑生成一次即可。Pixel Cover 作为 Later 选项。

### 8.5 Library

V1 使用简单列表 UI：Folder / Subfolder / Track / 当前选择高亮。

- Arrow：移动
- Enter：进入文件夹 / 播放歌曲
- Esc：返回上一级

浏览 Library 时当前音乐继续播放，不得因为打开目录而主动 Stop Audio。

### 8.6 Queue

简单列表：当前歌曲高亮，可查看前后歌曲，选择某首可直接播放。V1 不做复杂拖拽或大型队列编辑系统。

### 8.7 Sound

只显示四个固定 Preset：Original / Tape / Radio / Vocal Clear。当前 Preset 高亮，数字 `1–4` 仍可全局直选。

### 8.8 Settings

V1 只保留实际需要的设置：

- Screen Timeout
- Brightness
- About / Version
- Return to Launcher

不为了填满页面增加无实际用途的设置。

### 8.9 Screen-off / Soft Lock

V1 不增加复杂 Lock 系统。

Screen Off 时：

- 所有按键原功能暂时失效；
- 第一次任意键只负责唤醒屏幕；
- 该按键事件被吞掉；
- 屏幕亮起后第二次按键才执行正常功能。

因此息屏状态第一次按 `V` 只唤醒并吞掉事件，不切换 View；屏幕亮起后再次按 `V` 才按正常规则处理。

建议基线：

- 正常播放约 15 秒无 UI 操作自动息屏；
- 息屏被唤醒后约 5 秒无后续操作再次息屏。

### 8.10 Text Input Context

真正进入文本输入状态时：

- 数字 / 字母恢复正常输入；
- 媒体快捷键暂时关闭；
- Arrow / Enter / Esc 继续保持 UI 导航语义。


## 9. 后续方向

只有在 V1 原生 3.5mm 已经稳定且实际体验证明单声道成为主要限制后，再评估：

```text
外置 Stereo DAC / Codec
→ 真立体声
→ 独立 Audio Backend
```

播放器上层、音乐库、UI 和 Player 不应因此推倒重来。

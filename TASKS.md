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

Status: DONE

AC：

- [x] MP3 320 kbps / 44.1 kHz 可播放
- [x] 正确 L+R downmix
- [x] sample rate 处理正确
- [x] 使用合理 triple buffer / M5.Speaker 参数
- [x] Pause / Resume 可用
- [x] 使用同一 Fixture 完成 Restart / Flush / Reopen 生命周期验证
- [x] 固定 320 kbps Fixture 的定点 Seek 可用
- [x] 记录 heap / underrun
- [x] 真机旧耳机听感记录

---

## P0-03 Candidate B — Direct I2S

Status: DONE — NOT SELECTED

AC：

- [x] MP3 320 kbps / 44.1 kHz 可播放
- [x] ES8311 init 有官方 / M5Unified 依据
- [x] Direct I2S 启动与播放生命周期正常
- [x] 正确 L+R downmix
- [ ] Pause / Resume 可用
- [ ] 使用同一 Fixture 完成 Restart / Flush / Reopen 生命周期验证
- [ ] 固定 320 kbps Fixture 的定点 Seek 可用
- [ ] 记录 heap / underrun
- [x] 真机旧耳机听感记录

Candidate B 已完成构建、真机播放与等响度听感比较。按“只对听感胜者执行短压力测试”的既定简化协议，未勾选的生命周期 / 指标项不再追加人工测试；B 保留为可工作的备选实现。

---

## P0-04 Candidate C — BackgroundAudio

Status: DEFERRED

AC：

- [ ] 独立环境构建成功
- [ ] 不破坏当前稳定 PlatformIO 主环境
- [ ] ADV + ES8311 可正常输出
- [ ] MP3 320 kbps / 44.1 kHz 可播放
- [ ] 使用同一 Fixture 完成 Restart / Flush / Reopen 与定点 Seek
- [ ] 记录 heap / underrun
- [ ] 真机旧耳机听感记录

Candidate C 已完成独立环境构建和真机出声，但用户未听到相对 A/B 的明确收益，同时它占用最大 App 分区并引入 IDF5 工具链维护成本。P0 收敛为 A/B 二选一；C 代码与固件保留为备用，不继续投入人工测试。

---

## P0-05 Stress Test

Status: DONE

AC：

- [x] Candidate A 固件提供 `T` 键一键自动压力测试；无需依赖当前无响应的应用串口
- [x] Candidate A 真机完成约 3 min 自动压力测试并显示 `PASS`
- [x] 用户人工观察到的暂停、跳转与重播均对应脚本步骤，未报告额外异常
- [x] 记录明显爆音 / 卡顿 / 崩溃结果
- [x] 记录 Heap 是否持续下降

自动流程固定为：Baseline 30 s → UI Stress 60 s → UI + SD Stress 60 s → Pause 3 s → Resume 10 s → `seek 60` 后 10 s → Restart 后 10 s，总计约 3 分钟。压力阶段只读取既有 Benchmark MP3，不写 Flash；流程开始前单次写入 `RUNNING` 标记，结束并关闭 SD Stress 后单次覆盖最终摘要 `/ADVWalkman/logs/p0-a-stress-last.txt`，测试负载期间不写日志。

结果页显示 `PASS/FAIL`、state / sample rate、heap delta / sampled minimum heap、backpressure、service max、UI frames、SD KiB 与日志保存状态。`Listen: manual` 明确表示自动结果不能判断主观听感；用户仍需人工报告声音是否正常。

取消 A/B 各 30 min 和胜者 2 h 的人工硬门槛。Candidate A 已完成约 183 s 真机短测并通过；长期稳定性在 P1 实际开发与使用中继续验证。

首次自动短测暴露 Candidate A 的 Decoder service 不归还主循环：M5.Speaker 队列等待后持续返回成功，使 ESP8266Audio 单次 `loop()` 长期运行。`0.2.0-p0.a-stresslog2` 改为每提交一个 768-sample Buffer 后合作式让出主循环，保留未消费样本并且不伪增 Backpressure；修复版真机压力测试已通过。

---

## P0-06 Select V1 Audio Backend

Status: DONE

AC：

- [x] `docs/AUDIO_BENCHMARK.md` 填写结果
- [x] 写明最终选择和原因
- [x] 更新 `docs/TECH_DESIGN.md`
- [x] 不因为“更底层”而自动选择复杂方案
- [x] 形成可进入 P1 的冻结 Audio Backend

V1 冻结选择 Candidate A：`ESP8266Audio 1.9.7 → triple-buffer M5.Speaker → ES8311`。等响度后 A 同样具备用户认可的空间感，短压力测试通过，并且比 Direct I2S 更成熟、维护成本更低；B 保留为备选，C 保持 Deferred。

### 固定比较方式

- 只使用 `/Music/ADVWalkmanBenchmark/benchmark.mp3`，不再派生 M4A、48 kHz 或 VBR 测试矩阵。
- A/B 分别生成独立 M5Launcher App；耳机线控保持最大，完整原曲各听一次，听完立即记录印象。
- 等响度基准采用用户已听过的原 B 响度：A 使用 M5.Speaker `128/255`，B 使用线性增益 `0.25`。此前向 A=`64/255` 下调的方案导致两者均不舒适，已放弃。
- C 保留为 Deferred 备用，不参与本轮听感和压力测试。
- “Restart”只验证 Decoder / File / Output 生命周期，不能冒充正式 Player 的 Next / Previous 功能。

---

# P1 — Player Core

P0-06 已完成，正式 Player 固定沿用 Candidate A；P1 不重新选择 Backend，也不提前实现 Library、正式 UI、歌词、ASCII Cover 或 DSP。

## P1-01 MP3 Playback Core

Status: DONE

AC：

- [x] MP3 CBR
- [x] MP3 VBR
- [x] 最高目标 320 kbps
- [x] 44.1 kHz
- [x] 48 kHz
- [x] Track end 自动结束
- [x] 播放结束可通知 Player 进入下一首
- [x] 自然 EOF 先排空尾部 Buffer，且每首只发一次 TrackEnded
- [x] 缺文件、损坏文件和 Decoder 错误稳定进入 Error，不自动重启
- [x] 生成的 CBR 44.1 kHz、VBR 44.1 kHz、CBR 48 kHz Fixture 真机通过
- [x] Gate A 在设备端计算合法 / 截断 Fixture SHA-256，并与 Manifest 一致

2026-08-26，`0.3.0-p1.gate-a2` 真机 Gate A 通过：三份合法 Fixture 产生三次且仅三次 `TrackEnded`，截断文件与故意缺失文件各产生一次预期 Error，四份 Fixture SHA-256 全部匹配。`p1-01-last.txt` 的最终 `state=ERROR / file_open_failed` 是缺失文件负向测试的通过现场，不是 Gate 残留故障。

---

## P1-02 Transport Controls

Status: DONE

AC：

- [x] Play
- [x] Pause
- [x] Resume
- [x] Stop
- [x] Next
- [x] Previous
- [x] Seek
- [x] 不因连续操作崩溃
- [x] 无频繁明显爆音
- [x] Pause 中 Seek / Next / Previous 后仍保持 Pause
- [x] Previous 在播放超过 5 秒时回到本曲开头，否则进入上一首
- [x] CBR 与带 Xing/VBRI 的 VBR Seek 目标误差约不超过 1 秒
- [x] VBR Seek 后实际恢复 Decoder 并持续播放，不能只核对估算位置

同一次 Gate A 完成 Transport 真机验收：最终 `STOPPED / NONE`，最大 Seek 误差 60 ms，Backpressure 0；用户确认测试音播放正常、无异常爆音或重启。测试音量刻意保守，偏小不代表正式音频链路异常。

---

## P1-03 Queue & Playback Modes

Status: DONE

AC：

- [x] Queue
- [x] Sequential
- [x] Shuffle
- [x] Repeat One
- [x] Repeat All
- [x] 模式切换状态正确
- [x] 重启后可恢复
- [x] Repeat 与 Shuffle 独立；Repeat One 不阻止手动 Next / Previous
- [x] Shuffle 单轮不重复，Previous 按真实历史返回
- [x] 空队列、单曲、队首和队尾安全

2026-08-26，`0.3.0-p1.gate-b` 真机 Gate B 通过：Sequential、单轮 Shuffle、Repeat One、Repeat All、手动导航、模式切换保持当前歌曲及 Previous history 全部满足断言；最终 `STOPPED / NONE`，三次自然 EOF、零 Audio Error、零 Backpressure。

---

## P1-04 Playback State Persistence

Status: DONE

AC：

- [x] 保存歌曲
- [x] 保存位置
- [x] 保存队列
- [x] 保存播放模式
- [x] 重启恢复
- [x] 恢复后保持 Pause
- [x] 不自动出声
- [x] Queue / Session 使用 CRC32 与 A/B 双槽，新槽回读通过后才生效
- [x] 恢复时选择最新完整 Queue / Session 配对，并保留 order / cursor / Previous history
- [x] Session 播放中约每 10 秒 checkpoint，Queue 只在队列变化时重写
- [x] 未知版本、CRC 错误、截断文件和无 SD 时不重启循环

同一次 Gate B 完成一次真实 SD 保存与受控软件重启。重启后恢复第 2/3 首、`4003 ms`、source offset `61248`、Repeat All、Shuffle On、完整 order/cursor/history，并保持 `PAUSED` 且至少 3 秒无 Speaker 输出。首代有效状态写入 `queue-a.bin / session-a.bin` 属于正常 A/B 轮换起点；主机只读解析确认 schema、generation 与 CRC32 有效。

---

# P2 — Music Library

## P2-01 Recursive Folder Browser

Status: DONE

AC：

- [x] `/Music/`
- [x] 多层目录
- [x] 中文路径
- [x] 隐藏文件过滤
- [x] P2 已实现阶段非 MP3 文件不进入可播放列表；FLAC / WAV 暴露由 P6-04 在 Decoder 可用后统一扩展
- [x] 播放时浏览目录不主动 Stop Audio

实现与 PC Fixture 验收已通过。2026-08-26 的 `0.4.2-p2.gate` 真机日志独立确认：根目录边界、多层中文路径、过滤、自然排序、当前文件夹 Queue、Queue pin 与播放中浏览全部通过；P2-02 后续失败不回滚该项结果。

---

## P2-02 Lazy Scan / Cache

Status: DEVICE TEST

AC：

- [x] 不整库常驻 RAM
- [x] 当前目录按页显示
- [x] 大目录有界内存
- [x] 缓存可淘汰
- [x] 约 1000 首库规模可完成扫描、排序、分页和 Queue pin
- [ ] 播放中扫描不造成明显断音

4 个 SD session cache slot、3×32 项 RAM LRU page 与 Queue pin 已实现。`0.4.2-p2.gate` 已把首个真实失败收敛到千文件扫描：单次 Library 调用 92.6 ms，使 Player service 间隔擦线达到 101 ms，但当时仍为 Playing / 44.1 kHz、无 Audio Error、无 Backpressure，PCM 最大间隔 98.397 ms。

`0.4.3-p2.gate` 真机在约 22 秒内完成 1,000 项扫描、排序、分页、LRU 与 Queue pin，P2-02 逻辑通过；但测得 PCM 最大提交间隔 53.942 ms，超过原 `3 × 768` 约 52 ms 的实际缓冲余量，用户听到卡顿，证明旧 100 ms Gate 门槛过宽。最终 Gate 保持 Candidate A 与三缓冲模型，只把正式 Player 单 Buffer 增至 1536 samples，并以 70 ms 缓冲感知门槛验收；完成前状态保持 `DEVICE TEST`。

---

## P2-03 Metadata

Status: DEVICE TEST

AC：

- [x] Title
- [x] Artist
- [x] Album
- [x] Track Number
- [x] 中文 Metadata 可规范解析为 UTF-8（CJK 字形显示留给 P3）
- [x] Metadata 解析在长曲播放期间完成且未增加 Audio Error / Backpressure
- [x] 无 Metadata 时合理回退到文件名

ID3v2.3 / v2.4、四种文本编码、unsynchronization、extended header 与损坏标签 fallback 已通过 PC Fixture；`0.4.3-p2.gate` 又在长曲播放期间完成全部 10 个案例，P2-03 独立 PASS。最终仍随整轮 P2 Gate 收口状态。

---

## P2-04 Recent Tracks

Status: DEVICE TEST

AC：

- [x] 播放累计 5 秒后保存最近播放，Pause 时间不计
- [x] 路径不存在时安全忽略
- [x] 仅使用 `recent-a.bin / recent-b.bin` A/B 双槽，不为每首歌制造状态文件
- [x] 32 首上限、按完整路径去重且最新在前

`0.4.3-p2.gate` 已通过真实播放 5 秒记录、Pause 排除和 pending-path 切歌保护；唯一失败来自把启动时 31 条 Recent 冷加载错误放在播放期，产生 0.89 秒同步 SD 阻塞。最终 Gate 在播放中只验证一次真实发布，随后停止音频，再验证 32 项 MRU、缺失路径过滤与 A/B CRC 冷加载；格式与 host validator 不变，完成前保持 `DEVICE TEST`。

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

- [ ] 从 `/Lyrics/` 按 `/Music/` 相对路径和 basename 机械匹配逐行 LRC
- [ ] 支持基础 `<basename>.lrc` 以及 `zh-Hans / zh-Hant / en / ja / ko` 语言后缀
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

- [ ] 递归扫描 `/Music` 并按相同相对路径 / basename 查找 `/CoverSource` 中的 JPG / PNG
- [ ] 生成彩色 ASCII
- [ ] 同时输出电脑 Preview
- [ ] 为每首歌曲输出独立 `/ADVWalkman/covers/<relative>/<basename>.cover.adv`
- [ ] `.cover.adv` 包含可验证的 Magic / Width / Height / Pixel Format 与 RGB565 Pixels
- [ ] 不建立 Album / Folder 公共封面、模糊匹配、Hash 数据库或 JSON Manifest
- [ ] 同目录重复 basename 明确报冲突，不猜测应该绑定哪一个音频版本
- [ ] 初始支持 26×20 / 30×24 / 34×26 三档
- [ ] 默认先测试 30×24
- [ ] 约 120×144 px 仅作为候选设备画布，真机允许校准
- [ ] 批处理可重复运行，不需要 Agent 逐张介入

---

## P3-06 ASCII Cover Renderer

Status: TODO

AC：

- [ ] 有歌词时可作为 View Selector 的 Cover 视图
- [ ] 无歌词时 Content Stage 只显示 ASCII Cover
- [ ] ADV 不实时做图片→ASCII 转换
- [ ] 可直接读取 RGB565
- [ ] 读图不造成明显音频卡顿
- [ ] 缺少 Cover 时有简单 fallback

---

## P3-07 Library UI

Status: TODO

AC：

- [ ] 上半区显示当前曲库的独立大封面
- [ ] 下半区显示横向叠放的黑胶唱片选择带
- [ ] Left / Right 切换曲库，Enter 进入对应播放列表
- [ ] 当前唱片以上浮为主要高亮，并可辅以更亮 / 露出更多标签
- [ ] 曲库短名支持沿圆弧排版；字号、角度和重叠比例允许真机校准
- [ ] `S` 进入设置，Esc 在曲库页不继续退出
- [ ] 曲库封面与歌曲封面相互独立，不隐式继承第一首歌曲封面
- [ ] 浏览不主动停止当前音乐
- [ ] 切换动画短、轻，不造成可感知音频卡顿

---

## P3-08 Playlist UI

Status: TODO

AC：

- [ ] 标准列表显示当前曲库名称、歌曲序号、Title 和选择高亮
- [ ] 正在播放歌曲可有独立标识；Artist 仅在不牺牲可读性时显示
- [ ] Up / Down 选择，Enter 播放并进入播放器页面
- [ ] Esc 返回曲库并尽量保持原曲库位置
- [ ] 底层复用 P1 Queue，但不建设独立 Queue 页面或复杂重排 UI

---

## P3-09 Page Navigation & Cross-page Playback

Status: TODO

AC：

- [ ] 实际页面固定为播放器 / 播放列表 / 曲库 / 设置，无额外主页
- [ ] 播放器 Esc → 播放列表；播放列表 Esc → 曲库
- [ ] 曲库 `S` → 设置；设置 Esc → 曲库；曲库 Esc no-op
- [ ] 页面返回尽量保持当前歌曲、曲库和列表选择位置
- [ ] 音频跨页面持续，不因退出播放器或列表而 Stop
- [ ] 完整播放控制只在播放器页面，不复制到其他页面
- [ ] 有有效恢复歌曲时启动进入播放器并保持 Pause；无有效状态时进入曲库

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

## P3-12 Now Playing View Selector

Status: TODO

AC：

- [ ] 有可用歌词时可通过播放器 3×4 区的 `View` 在 Lyrics / Color ASCII Cover 间切换
- [ ] 无可用歌词时始终显示 Cover，按 `View` 不进入空白 Lyrics 页面
- [ ] `preferredNowPlayingView` 默认 Lyrics，只在用户成功切换时更新
- [ ] View 偏好跨歌曲保留
- [ ] 无歌词导致的临时 Cover 不覆盖用户偏好；下一首有歌词时按偏好恢复 Lyrics
- [ ] View 偏好通过现有 cooperative Session A/B 生命周期持久化，重启后恢复
- [ ] Header / Footer 在切换时保持不变，只重绘 Content Stage
- [ ] 切换不影响当前歌曲、播放状态、进度、Queue、Sound Preset 或 Volume
- [ ] 切换和 Cover / Lyrics 资源读取不造成可感知音频卡顿
- [ ] 其他页面不派发 `View` Action，也不暗中改变偏好
- [ ] 无歌词提示若实现，必须轻量且非阻塞；提示本身不是 V1 必做项

---

# P4 — Keymap

## P4-01 Input Context Router

Status: TODO

AC：

- [ ] 3×4 专用映射只在播放器页面生效
- [ ] 离开播放器页面立即恢复普通键盘映射
- [ ] 播放列表、曲库、设置中 Arrow / Enter / Esc 保持 UI 语义
- [ ] 曲库页面单独识别 `S → Settings`，其他页面的 `S` 不进入设置
- [ ] 旧数字列和 `H/L/Q/R/S/V` 全局快捷键不再生效

---

## P4-02 Now Playing 3×4 Blind Zone

Status: TODO

目标：

实现耳机孔朝上竖持时、仅属于播放器页面的顶部三排四列盲操区。以下数字是物理位置编号，不代表键帽字符。

Keymap：

```text
1  Volume +       2  Play/Pause  3  Play/Pause  4  Previous
5  Volume -       6  View        7  Play Mode   8  Next
9  Original      10  Tape       11  Radio      12  Vocal Clear
```

AC：

- [ ] 1 / 5 为 Volume + / -，4 / 8 为 Previous / Next
- [ ] 2 / 3 均为 Play / Pause，形成较大的盲操命中区域
- [ ] 6 为 View，7 为 Play Mode
- [ ] 9–12 直接选择 Original / Tape / Radio / Vocal Clear
- [ ] Esc 不属于 3×4 区，仍返回播放列表
- [ ] 离开播放器页后这 12 个物理位置恢复普通键盘语义
- [ ] 所有 Action 不直接耦合 Audio Backend

---

## P4-03 Play Mode Action

Status: TODO

循环顺序：

```text
Normal → Repeat One → Repeat All → Shuffle → Normal
```

AC：

- [ ] UI 四态原子映射到既有 `RepeatMode + Shuffle` 两维模型
- [ ] Normal = Off + Shuffle Off
- [ ] Repeat One = One + Shuffle Off
- [ ] Repeat All = All + Shuffle Off
- [ ] Shuffle = Off + Shuffle On，一轮结束后停止
- [ ] 不暴露 Repeat + Shuffle 的复杂组合
- [ ] 切换不重启当前歌曲，Footer 明确反馈当前模式

---

## P4-04 Normal-page Input

Status: TODO

AC：

- [ ] 播放列表 Up / Down 选择、Enter 播放、Esc 返回曲库
- [ ] 曲库 Left / Right 选择、Enter 进入列表、S 进入设置、Esc no-op
- [ ] 设置 Arrow / Enter 操作、Esc 返回曲库
- [ ] 未来若出现文本输入，数字 / 字母恢复普通输入且不派发播放器 Action

---

## P4-05 Screen-off Input Behavior

Status: TODO

AC：

- [ ] Screen Off 时全部快捷键原功能失效
- [ ] 任意键第一次只唤醒屏幕
- [ ] 唤醒按键事件不继续传递
- [ ] Screen Off 时第一次按 `View` 只唤醒，不切换 View
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

## P6-04 V1 Audio Format Compatibility

Status: TODO

目标：在不重新选择 Candidate A Audio Backend 的前提下，把当前已验证的 MP3 主路径扩展为 V1 的 MP3 / FLAC / WAV 格式集合。

AC：

- [ ] Format Detector 与 format-specific Decoder 不写入 PlayerController 状态机
- [ ] MP3 既有 320 kbps / CBR / VBR / Seek / Persistence 行为不回归
- [ ] FLAC 在无 PSRAM 内存预算内稳定解码并正确 Stereo → Mono 下混
- [ ] WAV 支持最终确认的常见 PCM 组合，不承诺 Hi-Res
- [ ] Library 只在对应 Decoder 可用后把 `.flac` / `.wav` 暴露为可播放文件
- [ ] FLAC / WAV Metadata 使用各自适配器，不把 ID3 假装成通用格式
- [ ] 三种格式共享 DSP、Limiter、Volume 和 Candidate A Backend
- [ ] 格式切换、Pause / Resume、Next / Previous 不造成崩溃或持续 Heap 下降

---

# Later / Deferred

当前不做：

- AAC / M4A
- OGG / Opus
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

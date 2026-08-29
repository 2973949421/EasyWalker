# EasyWalker Audio Backend Benchmark

> 状态：P0-01 / P0-02 / P0-03 / P0-05 / P0-06 — DONE；P0-04 Candidate C — DEFERRED；V1 Audio Backend 已冻结为 Candidate A。
> 目标：在正式播放器开发前，选出 V1 最合适的原生 3.5mm 音频底层。

## 1. 为什么先做 Benchmark

当前存在三条合理路线。

不应该凭：

- “这个更底层”
- “这个更新”
- “这个作者说更好”
- “这个代码看起来更专业”

直接决定。

统一真机测试后再冻结。

---

## 2. 共同测试条件

### Hardware

- M5Stack Cardputer ADV
- 原生 ES8311
- 原生 3.5mm
- 同一副现有旧耳机
- 同一张 microSD

### Test Audio

固定使用一首：

```text
MP3
320 kbps
44.1 kHz
Stereo
```

P0 Backend 选型只使用这一首固定文件。48 kHz、VBR 和额外版权音乐不再作为本阶段测试矩阵；未来遇到真实兼容问题时再按需验证。

P0-01 固定路径：

```text
/Music/ADVWalkmanBenchmark/benchmark.mp3
```

文件由用户从自己的音乐库选择，不进入 Git。首次真机测试前使用：

```powershell
B:\PlatformIO\penv\Scripts\python.exe tools\inspect_mp3.py D:\Music\ADVWalkmanBenchmark\benchmark.mp3
```

将工具输出的大小、SHA-256、bitrate、sample rate 和 channel mode 记录到下表。测试文件来自用户自有音乐，原始 M4A 已由用户从 SD 删除，项目不依赖它。

| Fixture Field | Value |
|---|---|
| SD Path | `/Music/ADVWalkmanBenchmark/benchmark.mp3` |
| Size | 11,972,484 bytes |
| SHA-256 | `4003b057b19ca95bae78e66b3536557e342d1105315c2a6217f4475c0db51d63` |
| Bitrate | 320 kbps CBR |
| Sample Rate | 44.1 kHz |
| Channel Mode | Stereo |

### Runtime

- 关闭与测试无关的 Wi-Fi / BLE
- 不人为给某一 Backend 更轻的 UI
- Benchmark Harness（基准固件）尽量共用同一套 UI / SD / Logger

---

## 3. Candidate A — Cardio-style M5.Speaker

```text
MP3
→ ESP8266Audio
→ custom AudioOutput
→ triple buffer
→ tuned M5.Speaker
→ ES8311
→ 3.5mm
```

### 参考

Cardio:

https://github.com/ZUENS2020/Cardio

### 重点验证

- sample rate 跟随
- DMA 参数
- triple buffer
- sqrt 音量曲线思路
- 320 kbps 稳定性
- UI 压力下是否断音

---

## 4. Candidate B — Direct I2S

```text
MP3
→ ESP8266Audio
→ Direct I2S Output
→ ES8311
→ 3.5mm
```

### 参考

SomaFM for CardPuter:

https://github.com/dr3d/SomaFM-for-CardPuter

### 重点验证

- Direct I2S 是否比 M5.Speaker 更稳定
- DMA 参数
- pause / resume
- sample rate change
- ES8311 init
- 是否出现爆音
- 实现复杂度是否值得

---

## 5. Candidate C — BackgroundAudio

```text
MP3
→ BackgroundAudio
→ ESP32 I2S
→ ES8311
→ 3.5mm
```

### 参考

https://github.com/earlephilhower/BackgroundAudio

### 环境隔离

如果必须使用 IDF5 / pioarduino：

- 建独立 PlatformIO environment；
- 不先升级 / 替换当前稳定主环境；
- 先证明可以在 ADV 编译、运行、控制 ES8311。

### P0 处理结论

Candidate C 已通过独立构建并在 ADV 真机正常出声，但首轮听感没有体现相对 A/B 的明确收益；同时它需要 IDF5 隔离工具链并占用最大的 Launcher App 分区。C 保留代码与固件作为备用，本轮不再投入人工听感或压力测试。

---

## 6. 统一 Downmix

三个候选必须使用同一逻辑。

基础：

```text
Mono = (Left + Right) / 2
```

要求：

- 使用至少 32-bit 中间值；
- 不丢某一声道；
- 不因为 Backend 差异使用不同增益策略；
- 如果某 Backend 有额外内部音量曲线，要记录。

---

## 7. Benchmark Metrics（测试指标）

| 指标 | A M5.Speaker | B Direct I2S | C BackgroundAudio |
|---|---|---|---|
| Build | PASS — actual backend | PASS — actual backend | PASS — actual backend |
| Boot | PASS | PASS | PASS — then deferred |
| 320k / 44.1k 播放 | PASS | PASS | PASS — then deferred |
| Free Heap | Stress start / end 280,800 bytes | Not run — not selected | Not run — Deferred |
| Minimum Heap | 276,412 bytes | Not run — not selected | Not run — Deferred |
| Heap 是否持续下降 | PASS — delta 0 | Not run — not selected | Not run — Deferred |
| Underrun / Dropout | Hardware counter `NA`; backpressure delta 0 | Hardware counter `NA` | Counters available; not run — Deferred |
| 高频 UI 刷新 | PASS — 3,870 frames / 120 s active stress | Not run — not selected | Not run — Deferred |
| 播放中浏览 SD | PASS — 19,988,356 bytes extra read | Not run — not selected | Not run — Deferred |
| Pause / Resume | PASS | Not run under final protocol | Not run — Deferred |
| Restart / flush / reopen | PASS | Not run under final protocol | Not run — Deferred |
| Seek | PASS | Not run under final protocol | Not run — Deferred |
| Sample rate | PASS — 44,100 Hz | PASS — 44,100 Hz playback | PASS — 44,100 Hz playback, then Deferred |
| 最大干净音量 | A/B 均以原 B 响度完成比较 | 同左 | Not pursued — Deferred |
| 主观底噪 | 未发现决定性差异 | 未发现决定性差异 | 无特别优势；Deferred |
| 主观声音 | 等响度后同样有空间感，整体表现获用户认可 | 有空间感，但相对 A 无决定性优势 | 无特别优势；Deferred |
| 实现复杂度 | 较低；M5Unified 集成 | 较高；自管 I2S / Codec | 最高；额外 IDF5 隔离工具链 |
| 维护成本 | 最低 | 高于 A | 最高 |

首轮听感使用了不同的 Backend 固定增益，因此只用于将范围收敛到 A/B，不能直接作为最终音质排名。耳机线控在非最大位置存在偏音，后续固定保持最大。首次等响度尝试将 B 降到 `0.0625`、对齐 A=`64/255`，真机反馈两者均不舒适，立即停止。正式 A/B 比较改为保留用户已听过的原 B=`0.25` 响度，并将 A 提升到 M5.Speaker `128/255`。

### P0-01 Historical Build Record

验证日期：2026-08-24

```text
pio run -e bench-a -e bench-b -e bench-c
```

| Environment | Role | Firmware Size | Conservative Launcher Limit |
|---|---|---:|---:|
| `bench-a` | Candidate A 最小可播放基线 | 642,048 bytes | 1,310,720 bytes |
| `bench-b` | Direct I2S 明确占位 | 463,136 bytes | 1,310,720 bytes |
| `bench-c` | BackgroundAudio 明确占位 | 463,136 bytes | 1,310,720 bytes |

三个环境均使用 Espressif32 6.7.0 / Arduino-ESP32 2.0.16。只有 `bench-a` 引入 ESP8266Audio 1.9.7；B/C 未迁移工具链，也不编译 A 的音频源文件。

### P0-02～P0-04 Current Build Record

验证日期：2026-08-25

```powershell
.\tools\build_p0_backends.ps1
```

| Environment | Backend | Toolchain / Framework | Firmware Size | SHA-256 |
|---|---|---|---:|---|
| `bench-a` | M5.Speaker | Espressif32 6.7.0 / Arduino-ESP32 2.0.16 / ESP8266Audio 1.9.7 | 644,304 bytes | `325dc81e145084e82916e35a9952cac3d4da2b16af1a9911d0e7421c641069f4` |
| `bench-b` | Direct I2S | Espressif32 6.7.0 / Arduino-ESP32 2.0.16 / ESP8266Audio 1.9.7 | 636,448 bytes | `d857713f9eb09c43b44ff5890726e93c53b9a0bbf698bef55047ba43ade7b250` |
| `bench-c` | BackgroundAudio | pioarduino 55.03.38-1 / Arduino-ESP32 3.3.8 / ESP-IDF 5.5.4 / BackgroundAudio 1.4.4 | 693,856 bytes | `ab6681c570a1090ca97f88c74ff3644c488fd4eb5351a1e640951fdc7e490400` |

三个生成物均通过 1,310,720 bytes 的 Launcher 保守尺寸检查。A/B 使用 `B:\PlatformIO\packages` 的稳定 packages；C 使用 `B:\PlatformIO\isolated\adv-walkman-c\packages`，未替换 A/B 的 framework。

同日已将三个生成物复制到 microSD 的 `/firmware/`，复制后逐项重新计算 SHA-256，均与上表一致。旧的 `/firmware/ADV-Walkman-P0-A.bin` 保留未动。

### A/B Low-Reference Build Record（Rejected）

验证日期：2026-08-25

```text
pio run -e bench-a -e bench-b
```

本轮只重新构建 A/B；A 保持 M5.Speaker `64/255`，B 将 Direct I2S 线性增益从 `0.25` 调整为 `0.0625`，C 未重新构建。真机发现该低响度基准令 A/B 均不舒适，以下固件不再用于最终比较；记录仅作为实验历史保留。

| Environment | Firmware Size | `0xA0000` Slot Margin | SHA-256 |
|---|---:|---:|---|
| `bench-a` | 644,320 bytes | 11,040 bytes | `dd1985d1bacce18a485d49146265a57f09a34153bf3750fb6c4b4a9da793b547` |
| `bench-b` | 636,432 bytes | 18,928 bytes | `0bef3905fdc9e35d6e9e0f9dedde8f6661cb0d839c8c76e3f1f639963ab8bb93` |

两者均小于现有 655,360 bytes App 槽。生成物已覆盖到 microSD 的 `/firmware/ADV-Walkman-Bench-A.bin` 与 `/firmware/ADV-Walkman-Bench-B.bin`；复制后重新计算的大小和 SHA-256 均与上表一致。测试音频仍为唯一 Fixture，SHA-256 为 `4003b057b19ca95bae78e66b3536557e342d1105315c2a6217f4475c0db51d63`。

### A/B Original-B Reference Build Record（Current）

验证日期：2026-08-25

真机否决低响度基准后，依据 M5Unified 0.2.20 的 `_master_volume * _master_volume` 实现，将 A 提升为 M5.Speaker `128/255`，并将 B 恢复为用户已听过的 Direct I2S `0.25`。两者相对低响度版本均提高约 12 dB，目标是保持原 B 的可听响度再比较 Backend，而不是把 B 降到 A。

| Environment | Firmware Size | `0xA0000` Slot Margin | SHA-256 |
|---|---:|---:|---|
| `bench-a` | 644,320 bytes | 11,040 bytes | `e8bbc09230ca45ced1f26bca19ad3526a6c730b77fccbde808f1b73b2d0c62b6` |
| `bench-b` | 636,448 bytes | 18,912 bytes | `2b76d65feba68e0aa29125bc4e21009ad479ef7b4fddb209b425b3ba0b07ad46` |

新版 A/B 已覆盖到 microSD `/firmware/` 并通过复制后 SHA-256 核对。按用户要求，SD 上的旧 `/firmware/ADV-Walkman-Bench-C.bin` 与 `/firmware/ADV-Walkman-P0-A.bin` 已删除；最终 `firmware` 目录只保留上表两个 Benchmark 固件。

### Candidate A Auto Stress Build and Device Record（DONE）

验证日期：2026-08-25

当前已安装应用的串口没有返回可用响应，无法可靠地由 Codex 远程逐条发送压力命令。因此 Candidate A Benchmark 固件增加 `T` 键一键自动压力测试，让用户只需启动固件并按一次 `T`。

| Field | Value |
|---|---|
| Firmware version | `0.2.0-p0.a-stresslog2` |
| Build | PASS |
| Firmware size | 648,224 bytes |
| Existing `0xA0000` slot margin | 7,136 bytes |
| SHA-256 | `db2e2e76644ecaf40b04afb4a829fce77949a4edac70f7bc0f9bbf565c4d87eb` |
| Device validation | PASS |

构建后的文件已覆盖到 microSD `/firmware/ADV-Walkman-Bench-A.bin`，复制前后大小与 SHA-256 一致；同一固件随后完成真机自动压力测试。用户此前已经完成等响度 A/B 听感比较，本结果用于完成 P0-05 并冻结 `TECH_DESIGN.md` 的 V1 Backend；`PRD.md` 产品范围不变。

Launcher 尺寸检查已按环境绑定到真实槽位：A/B 使用 `0xA0000`，Deferred 的 C 保留其历史 `0x3F0000` 上限。后续 A/B 若超过 640 KiB 将直接构建失败，不再被旧的 1,310,720-byte 通用上限漏过。

### Current Runtime Contract

- 串口：115200 baud。
- 自动尝试播放固定 MP3；SD 或文件缺失时进入 `ERROR`，不反复重启。
- 每 5 秒输出 `backend`、`state`、`sample_rate`、`free_heap`、`minimum_heap`、`elapsed_ms`、`decode_calls`、`backpressure`、`bytes_read`、`track_loops`、`decoder_errors`、`decoder_underflows`、`output_underflows`、`service_max_us` 和错误状态。
- 不可取得的计数统一输出 `NA`，不伪报为 0。
- 支持串口命令：`play`、`pause`、`resume`、`stop`、`status`、`restart`、`seek <seconds>`、`loop on|off`、`stress ui on|off`、`stress sd on|off`。
- UI Stress 固定约 30 Hz；SD Stress 使用第二个只读句柄循环读取同一 benchmark MP3，不创建文件。
- 普通状态页仅显示 Backend、状态、循环次数和错误；自动压力测试另有运行 / 结果页。两者都不属于正式播放器 UI。
- Candidate A 的 `0.2.0-p0.a-stresslog2` 固件可按一次 `T` 启动约 3 分钟自动压力流程；压力阶段只读测试 MP3，不写 Flash。
- 启动流程先覆盖写入并关闭 `/ADVWalkman/logs/p0-a-stress-last.txt`，内容为 `result=RUNNING`；Restart 成功后才开始采集本轮指标。正常结束或已捕获失败时，关闭 SD Stress 句柄后再一次性覆盖最终摘要。压力负载期间不写日志；若意外重启，遗留的 `RUNNING` 可识别未完成测试。
- 自动结果页显示 `PASS/FAIL`、state / sample rate、heap delta / sampled minimum heap、backpressure、service max、UI frames、SD KiB 与日志保存状态。日志额外记录版本、Fixture SHA-256、失败阶段和 Pause / Resume / Seek / Restart 结果。
- 结果页的 `Listen: manual` 是明确边界：固件只能检查机器可观测状态，不能冒充用户对爆音、卡音、断音、偏音或音质的主观判断。

### First Auto Stress Attempt and Cooperative Yield Fix

首次真机运行 `0.2.0-p0.a-stresslog` 时，屏幕停留在 `BASELINE 30s / PLAYING / SR=0` 超过 60 s，且没有进入 UI Stress。SD 日志正确保留 `result=RUNNING`、`sample_rate=0`、`service_max_us=0`、`ui_frames=0`，证明 T 键、日志写入和 Restart 已完成，但第一次 Decoder service 没有归还主循环。

只读核对 M5Unified 0.2.20 与 ESP8266Audio 1.9.7 后确认：`M5.Speaker.playRaw()` 在普通队列满时会等待空位并最终返回成功，而 `AudioGeneratorMP3::loop()` 会在 `ConsumeSample()` 持续返回 `true` 时继续解码。因此旧 A 输出层会将主循环长期留在单次 Decoder service 内，计时、UI 与串口无法运行。

`0.2.0-p0.a-stresslog2` 在每成功提交一个 768-sample Buffer 后让 `ConsumeSample()` 返回 `false`，合作式地将控制权交还 Harness。ESP8266Audio 会在下一轮重新提交尚未消费的 `lastSample`，因此不丢样、不重复；只有 `playRaw()` 真正拒绝时才增加 Backpressure。修复版随后通过真机验证。

### Candidate A Auto Stress Device Result

- 日志：microSD `/ADVWalkman/logs/p0-a-stress-last.txt`。
- 结果：`PASS`；版本 `0.2.0-p0.a-stresslog2`；总时长 183,044 ms。
- 最终状态 `PLAYING`，sample rate 44,100 Hz，`failure=none`。
- Heap start / end 均为 280,800 bytes，采样最低值 276,412 bytes，delta 0。
- Backpressure delta 0；最大单次 service 26,401 us。
- UI Stress 产生 3,870 frames；UI 激活阶段约 120 s，约 32 frames/s。
- SD Stress 额外只读 19,988,356 bytes。
- Pause / Resume / Seek / Restart 全部为 1；用户观察到的短暂停顿、进度跳转和重新播放与规定脚本一致，没有报告额外崩溃或异常状态。

### P0-01 Launcher Device Test Result

- 日期：2026-08-24。
- M5Launcher 正常安装 `/firmware/ADV-Walkman-P0-A.bin`；未执行 PlatformIO upload、erase、partition 修改或 eFuse 操作。
- 用户确认 Cardputer ADV 正常启动，内置扬声器与原生 3.5mm 耳机均正常出声，无明显启动异常。
- 串口确认 `backend=A_M5SPEAKER`、`state=PLAYING`、`sample_rate=44100`、`error=none`。
- 设备端文件 SHA-256 为 `4003b057b19ca95bae78e66b3536557e342d1105315c2a6217f4475c0db51d63`，与 PC 记录一致。
- 两次周期记录：`free_heap=286044`、`minimum_heap=285764`；约 40–45 秒时保持一致。
- `underrun=NA` 符合当前 API 限制；`backpressure` 已单独记录，未伪报 underrun 为 0。
- B/C 仍只是占位；本结果只完成 P0-01，不代表 P0 Backend 最终选型完成。

### P0-02～P0-06 落地方式

- `bench-a`、`bench-b` 是两个独立 Launcher App，不在运行时动态切换底层驱动；`bench-c` 保留为 Deferred 备用。
- A/B 使用相同 Fixture、串口命令、UI/SD 压力负载与指标格式。
- Restart / Seek 使用同一 MP3 验证文件、Decoder、Buffer 与 I2S 生命周期，不复制音乐来伪造“切歌库”。
- 等响度 A/B 使用完整原曲各听一次；Candidate A 胜出并完成约 3 分钟的一键自动压力测试。取消 A/B 各 30 分钟与胜者 2 小时的人工硬门槛，长期稳定性在 P1 实际使用中继续验证。

### Launcher Flash 只读诊断（清理前）

2026-08-25 从 `0x8000` 只读取 4 KiB 分区表，确认安装失败并非固件格式问题，而是当前只剩 320 KiB 未分配空间，无法再创建 `0xA0000`（640 KiB）App 分区。

| Label | Role | Size | Action |
|---|---|---:|---|
| `app0` | M5Launcher | 1344 KiB | Keep |
| `advwa4` | Candidate C | 4032 KiB | Delete through Launcher |
| `spiffs` | Ownership not fully confirmed | 448 KiB | Keep |
| `advwal` | Old Candidate A baseline | 640 KiB | Delete through Launcher |
| `advwa1` | Current Candidate B slot | 640 KiB | Delete for clean reinstall |
| `advwa2` | Current Candidate A slot | 640 KiB | Delete for clean reinstall |

用户决定从 SD 全新安装 A/B，因此只通过 Launcher 的已安装 App 删除流程清理全部四个 `advwa*` App；不由 esptool 写分区表，也不主动删除关联 SPIFFS/FAT。

### Launcher Flash 清理结果

删除后再次从 `0x8000` 只读取 4 KiB 分区表：`advwa4`、`advwal`、`advwa1`、`advwa2` 均已消失；`app0`、NVS、otadata、phy_init 与 coredump 保留。`spiffs` 标签和 448 KiB 大小仍在，Launcher 清理时将其表项从 `0x560000` 整理到 `0x170000`；未读取或推断其内容。当前 `spiffs` 结束于 `0x1E0000`，到 8 MiB Flash 末尾有 `0x620000`（6272 KiB）连续可用空间，足够全新创建两个 `0xA0000` A/B App 分区。

---

## 8. 自动记录建议

串口至少能看到：

```text
backend
sample_rate
bitrate / file info
free_heap
minimum_free_heap
underrun_count
playback_time
state
```

不要为了 Benchmark 搭建大型 Telemetry 系统。

只记录足够支持决策的数据。

---

## 9. Stress Test（压力测试）

### UI Stress

测试 Harness 约 30 Hz 重绘整块屏幕，模拟未来歌词、进度条和动画带来的显示负载；这不是正式 UI 或视觉验收。

目标：

- 不出现可感知卡音。

### SD Stress

播放期间使用第二个只读文件句柄，每约 8 ms 读取 4 KiB 并循环读取同一个 Benchmark MP3，模拟未来目录浏览、Metadata、歌词和资源读取；不创建或修改文件。

目标：

- 播放不断流；
- 如果卡顿，能够定位是 SD / Decoder / Output 哪一层。

### State Stress

当前应用串口没有返回可用响应，因此不再要求 Codex 远程逐条发送命令。Candidate A 固件启动并正常播放后，用户按一次 `T`；其余流程由固件自动完成：

```text
Baseline 30 s
→ UI Stress 60 s
→ UI + SD Stress 60 s
→ Pause 3 s
→ Resume 10 s
→ Seek 60 s，继续 10 s
→ Restart，继续 10 s
```

总时长约 3 分钟。SD Stress 始终只读同一个 Benchmark MP3，负载阶段不创建或修改 SD 文件，也不写 Flash。测试前后的 `RUNNING` / 最终摘要只在 SD Stress 关闭时写入固定日志文件，不参与压力负载。

观察：

- 爆音；
- 崩溃；
- Heap；
- Driver 状态。

固件自动检查最终 state、44.1 kHz sample rate、Backend error、UI frame 数和 SD 读取量，并在屏幕显示 `PASS` 或 `FAIL`。结果页同时显示 state / SR、heap delta / minimum heap、backpressure、service max、UI frames 和 SD KiB。

`PASS` 只代表这些自动条件通过。结果页固定显示 `Listen: manual`，因为固件不能自行判断用户是否听到爆音、卡音、断音、异常偏音或声音是否好听；这部分由用户的等响度听感和现场观察补足。本次自动日志与用户观察一致，P0-05 完成。

---

## 10. 决策规则

不是单纯“跑分最高者胜”。

优先级：

1. 音频稳定
2. 无明显爆音 / 异常
3. UI / SD 压力下稳定
4. 内存健康
5. 主观音质
6. 实现与维护复杂度

如果 A 和 B 实际体验基本相同：

> 优先选择更简单、更成熟、更容易维护的方案。

如果 C 只带来轻微收益但要求整个工具链迁移：

> V1 不迁移。

---

## 11. Benchmark 结束后必须更新

完成 P0 后：

1. 在本文件填写真实结果；
2. 写出明确 Decision；
3. 把最终 Backend 同步到 `TECH_DESIGN.md`；
4. 更新 `TASKS.md`；
5. 进入正式 Player / Library 开发。

---

## 12. Decision

```text
V1 Audio Backend:
Candidate A — ESP8266Audio 1.9.7 → triple-buffer M5.Speaker → ES8311

Reason:
Equal-loudness listening found no decisive B advantage; A passed the short UI/SD/state stress test with stable heap and zero backpressure, while retaining the lowest implementation and maintenance cost.

Rejected / Deferred:
Candidate B — working fallback, not selected
Candidate C — Deferred unless A fails during P1 development
```


---

## 13. 与 UI 的关系

P0 Audio Backend Benchmark 不等待最终 UI 实现。

UI、歌词、ASCII Cover、Keymap 和 Sound Preset 已有 V1 设计基线，但 P0 只需要最小测试 Harness。禁止为了“顺便把 UI 做好”拖慢 Backend 选型。

2026-08-26 的 V1 增量把最终格式方向扩展为 MP3 / FLAC / WAV，但不改变本文件结论：P0 比较和 Candidate A 冻结均以 MP3 PCM 输出为证据。FLAC / WAV 后续只增加 Format Detector / Decoder adapter，并继续复用已选定的 DSP、Stereo → Mono 与 M5.Speaker / ES8311 Backend；不得借格式扩展重新开启 A/B/C 选型。

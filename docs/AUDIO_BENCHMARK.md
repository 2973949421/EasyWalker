# ADV Walkman Audio Backend Benchmark

> 状态：P0-01 — DONE；P0-02 / P0-03 / P0-05 — DEVICE TEST；P0-04 Candidate C — DEFERRED；P0-06 最终选型仍待 A/B 等响度结果。
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
| Free Heap | 286,044 bytes（约 40–45 s） | TBD | TBD |
| Minimum Heap | 285,764 bytes | TBD | TBD |
| Heap 是否持续下降 | TBD | TBD | TBD |
| Underrun / Dropout | Hardware counter `NA` | Hardware counter `NA` | Decoder / output counters available; runtime TBD |
| 高频 UI 刷新 | TBD | TBD | TBD |
| 播放中浏览 SD | TBD | TBD | TBD |
| Pause / Resume 爆音 | TBD | TBD | TBD |
| Restart / flush / reopen | TBD | TBD | TBD |
| Seek | TBD | TBD | TBD |
| Sample rate 切换 | TBD | TBD | TBD |
| 最大干净音量 | TBD | TBD | TBD |
| 主观底噪 | TBD | TBD | TBD |
| 主观声音 | 首轮：较平、音量最低 | 首轮：有空间感、音量居中 | 首轮：音量最高、无特别优势；Deferred |
| 实现复杂度 | TBD | TBD | TBD |
| 维护成本 | TBD | TBD | TBD |

首轮听感使用了不同的 Backend 固定增益，因此只用于将范围收敛到 A/B，不能直接作为最终音质排名。耳机线控在非最大位置存在偏音，后续固定保持最大；A 使用 M5.Speaker `64/255`，B 改用约等效的 `0.0625` 线性增益，再各完整播放原曲一次。

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

### A/B Equal-Loudness Build Record

验证日期：2026-08-25

```text
pio run -e bench-a -e bench-b
```

本轮只重新构建 A/B；A 保持 M5.Speaker `64/255`，B 将 Direct I2S 线性增益从 `0.25` 调整为 `0.0625`，C 未重新构建。

| Environment | Firmware Size | `0xA0000` Slot Margin | SHA-256 |
|---|---:|---:|---|
| `bench-a` | 644,320 bytes | 11,040 bytes | `dd1985d1bacce18a485d49146265a57f09a34153bf3750fb6c4b4a9da793b547` |
| `bench-b` | 636,432 bytes | 18,928 bytes | `0bef3905fdc9e35d6e9e0f9dedde8f6661cb0d839c8c76e3f1f639963ab8bb93` |

两者均小于现有 655,360 bytes App 槽。生成物已覆盖到 microSD 的 `/firmware/ADV-Walkman-Bench-A.bin` 与 `/firmware/ADV-Walkman-Bench-B.bin`；复制后重新计算的大小和 SHA-256 均与上表一致。测试音频仍为唯一 Fixture，SHA-256 为 `4003b057b19ca95bae78e66b3536557e342d1105315c2a6217f4475c0db51d63`。

### Current Runtime Contract

- 串口：115200 baud。
- 自动尝试播放固定 MP3；SD 或文件缺失时进入 `ERROR`，不反复重启。
- 每 5 秒输出 `backend`、`state`、`sample_rate`、`free_heap`、`minimum_heap`、`elapsed_ms`、`decode_calls`、`backpressure`、`bytes_read`、`track_loops`、`decoder_errors`、`decoder_underflows`、`output_underflows`、`service_max_us` 和错误状态。
- 不可取得的计数统一输出 `NA`，不伪报为 0。
- 支持串口命令：`play`、`pause`、`resume`、`stop`、`status`、`restart`、`seek <seconds>`、`loop on|off`、`stress ui on|off`、`stress sd on|off`。
- UI Stress 固定约 30 Hz；SD Stress 使用第二个只读句柄循环读取同一 benchmark MP3，不创建文件。
- 屏幕仅显示 Backend、状态、循环次数和错误，不属于正式播放器 UI。

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
- 等响度 A/B 使用完整原曲各听一次；听感胜者只执行约 3–4 分钟短压力测试。取消 A/B 各 30 分钟与胜者 2 小时的人工硬门槛，长期稳定性在 P1 实际使用中继续验证。

### Launcher Flash 只读诊断（清理前）

2026-08-25 从 `0x8000` 只读取 4 KiB 分区表，确认安装失败并非固件格式问题，而是当前只剩 320 KiB 未分配空间，无法再创建 `0xA0000`（640 KiB）App 分区。

| Label | Role | Size | Action |
|---|---|---:|---|
| `app0` | M5Launcher | 1344 KiB | Keep |
| `advwa4` | Candidate C | 4032 KiB | Delete through Launcher |
| `spiffs` | Ownership not fully confirmed | 448 KiB | Keep |
| `advwal` | Old Candidate A baseline | 640 KiB | Delete through Launcher |
| `advwa1` | Current Candidate B slot | 640 KiB | Keep / reuse for B |
| `advwa2` | Current Candidate A slot | 640 KiB | Keep / reuse for A |

只通过 Launcher 的已安装 App 删除流程清理 `advwa4` 和 `advwal`；不由 esptool 写分区表，不删除关联 SPIFFS/FAT，不移动或调整其他分区。清理完成后再次只读核对分区表并补充结果。

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

只对听感胜者执行一次：

```text
Pause
Resume
Restart
Seek
```

观察：

- 爆音；
- 崩溃；
- Heap；
- Driver 状态。

短测顺序固定为：正常播放 30 s → UI Stress 60 s → UI + SD Stress 60 s → 两种压力下各执行一次 Pause/Resume、`seek 60`、Restart → 关闭压力并记录最终状态。用户只需听是否出现非预期卡音、爆音或断音，串口命令与日志由 Codex 负责。

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
TBD

Reason:
TBD

Rejected / Deferred:
Candidate C — Deferred unless A/B both fail
```


---

## 13. 与 UI 的关系

P0 Audio Backend Benchmark 不等待最终 UI 实现。

UI、歌词、ASCII Cover、Keymap 和 Sound Preset 已有 V1 设计基线，但 P0 只需要最小测试 Harness。禁止为了“顺便把 UI 做好”拖慢 Backend 选型。

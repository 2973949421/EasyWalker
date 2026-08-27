# P3C validation record

Current fix baseline: `991d54c`; target: `0.7.1-p3c.fix`.

## 0.7.1 修复与验证

### 已确认的失败证据

- 初版 `0.7.0`：A 导航及 `ADVWalkmanBenchmark` 两行 PASS，B SKIPPED；C 在开始
  连续测量前报告 `real_track_media_missing_or_bad`，不能把其零值当作实测音频指标。
- 三份原日志已保存在 PC 的 Git 忽略目录 `test-data/local/p3-media/failure-0.7.0/`。
- SD 媒体与交付包一致，不能归因于用户漏拷文件。5 个默认文件名额存在并发不足风险，
  但旧日志无 errno / 组件证据，未证明它是唯一根因。
- UI 的确用 Fn 作为导航条件，并依赖只比较按键数量的旧接口；提示也的确画在歌词
  上。逐条带等待字模会产生长时间旧句 / 新句混杂，这些均有代码证据。

### 自动验证与边界

- `check_p3c.py` 15 项、`check_p3b.py` 10 项、`check_p3abc_fix.py` 8 项通过；Session
  自测通过。覆盖 29 组实际字模 / 分页容量、资源错误 / 日志契约、无提示压字等。
- PC 参考与源码检查不是设备执行；真正 C++ 按键自检、只读文件名额耗尽 / 恢复、
  Cover CRC / LRC / 代表字模与显示计时均须在本次 Gate 上运行。
- 使用原来的 135×18 行缓冲、Candidate A、Queue/Session；没有新增媒体或修改 MP3。
- 严格保持 70 ms PCM，新增显式 100 ms 整帧 / 200 ms 到期刷新检查；漏过自然歌词
  deadline 也会失败，不通过丢掉慢帧来隐藏问题。至少一个自然换句必须实际测量。
- `max_files=12` 的全局 FatFs 表相较默认值增加 **28,959 bytes**；本地 SDK/GDB 确认
  `sizeof(FIL)=4136`，加每槽 1-byte o_append。它独立于受 ≤48 KiB 断言约束的媒体
  工作集，不能称为“全系统只多用了48 KiB”。真实剩余 Heap / 泄漏仍要真机确认。
- 固件 / 日志匹配版本 `0.7.1-p3c.fix`。原始失败现场先快照，再 Pause / close；未测量
  用 NA，后续未执行用 SKIPPED。日志保留完整路径、组件、操作、errno 和读长。

### 最终构建 — 2026-08-27

六环境全部成功，`.pio` 与 `artifacts` 逐项匹配，全部小于 `0x140000`。Dev / P3A /
联合 Gate 版本为 `0.7.1-p3c.fix`；历史 P1/P2 只做共享 SD 挂载代码的编译回归，
保留原 Gate 版本。未重建 Benchmark A/B/C，也未更换依赖。

| Artifact | Bytes | Static RAM bytes | SHA-256 |
|---|---:|---:|---|
| `ADV-Walkman-Dev.bin` | 734912 | 114264 | `e5577d20328ea3a0e7871b3a2fbd172922fa136295acb83c09dfba2d7af76b64` |
| `ADV-Walkman-P3ABC-Gate.bin` | 761168 | 117560 | `48cc428e0fb3cac2a68b61d7086bbc08564c68124b7e04c6a243420692b20f60` |
| `ADV-Walkman-P3A-Gate.bin` | 741616 | 115144 | `15cdfbbe83d57b33ca8c5c3663b79ec97f237dc8c2c707517242662bd414a08d` |
| `ADV-Walkman-P1-Gate-A.bin` | 673984 | 55088 | `83d1302305c94c374540ac5c5d3a10428035e799fcdf4343c81ad7e4fd6bb971` |
| `ADV-Walkman-P1-Gate-B.bin` | 674656 | 55088 | `3852245c9d0a4c55bce3772d687608225d95d8ca4f1ad4ed5785112353368161` |
| `ADV-Walkman-P2-Gate.bin` | 724352 | 152664 | `b20743a805ac70a074770a44cefbbcc84a1811b65fb70bac879d01356128bf06` |

联合固件占现有预算的 58.1%。静态 RAM 不包括运行时分配，不等于剩余 Heap。
最终 BIN 已检查包含新版本及短行单键说明卡；人工确认卡在 123 px 内框中不会把
末尾的确认键提示挤掉。33 项 PC 检查、Session 自测及 `git diff --check` 通过。

### 本轮 SD 单文件交付

- 重新确认 D 盘现有联合 BIN 和原曲路径 / 大小后，仅覆盖
  `D:\firmware\ADV-Walkman-P3ABC-Gate.bin`。
- SD 文件为 **761168 bytes**，SHA-256 与上表联合固件完全一致。
- 媒体包未变化，未重复复制字体、歌词、封面或音乐；未删除其他 BIN、日志或状态。
- 未操作 COM、设备 Flash、分区或 eFuse。由用户通过 M5Launcher 安装同名 BIN。
- 新版本尚未在 ADV 上运行。A/B/C 继续 `DEVICE TEST`，待本次三份日志与人工显示 /
  听感确认，不能以编译或 PC 参考检查代替真机 PASS。

## 0.7.0 历史交付记录

以下为初版 `61692f0` 起点的历史结果，不代表当前修复版生成物。

## Local validation

- `tools/check_p3c.py`：15 项资源 / 像素 / 格式 / 分页 / Session 与日志校验器检查通过。
- `tools/check_p3b.py`：既有 10 项几何 / 时钟 / 接口契约检查通过。
- `tools/preview_p3_lyrics.py`：29 组真实双语歌词及其分页使用实际 SD 字模，全部位于
  内容内框；预览不是 ADV 真机结果。
- 字体必需字符覆盖通过；CJK 每种字号 28,595 个 glyph，737 个 fallback 来源有 TSV。
  不支持的 BMP 字和 C1 控制码明确报告，不伪装成可用字形。
- 构建期 assert 检查媒体工作集 + 7 KiB FS reserve ≤48 KiB，Launcher ≤0x140000。
- `inspect_player_state.py --self-test`、Python 工具语法检查通过。

## Build artifacts — 2026-08-27

以下六环境均构建成功，脚本已核对 `.pio` → `artifacts` 大小与 Hash；构建阶段未复制 SD。
Dev / P3A / 联合 Gate 的版本为 `0.7.0-p3c.media`，历史 P1/P2 保留原 Gate 版本，
只验证共享 Session 兼容，不表示重新进行了 P1/P2 真机测试。

| Artifact | Bytes | Static RAM bytes | SHA-256 |
|---|---:|---:|---|
| `ADV-Walkman-Dev.bin` | 730496 | 113752 | `0375ab014c38c2ad33a833cbc9e049e1fd509cac404d1d476e8dea73162808d2` |
| `ADV-Walkman-P3ABC-Gate.bin` | 750224 | 115616 | `afd6c8c12310a28f878c26aa41b47a05bf6be27f2ec6c534c44a78ca38750bd4` |
| `ADV-Walkman-P3A-Gate.bin` | 736832 | 114632 | `edc394b3d052d3496a773b3885eed4c3fd2b8998fdd22a504a5abe42d30d8398` |
| `ADV-Walkman-P1-Gate-A.bin` | 673984 | 55088 | `e67716306141d2fc793dd0ae8cab6cb9e9b2d3a850c14dcfa0be04553ab34054` |
| `ADV-Walkman-P1-Gate-B.bin` | 674656 | 55088 | `ca55d7b5f7893b6a0e7f30f0af73691018c1ea94852c936bfc11be46a8d68a5d` |
| `ADV-Walkman-P2-Gate.bin` | 724352 | 152664 | `d1ea06f737925951973e0304835a0b7b875217941549e850f856ab93facfd561` |

联合 Gate 为现有 `0x140000`（1,310,720 bytes）预算的 57.2%；没有改分区。
静态 RAM 不包括运行时 Decoder / Library / 媒体工作集，不能当作最低空闲 Heap。
实际 Heap 和音频连续性由设备日志验证。依赖图保持 M5Cardputer 1.1.1 / M5Unified 0.2.20 /
M5GFX 0.2.27 / ESP8266Audio 1.9.7 / Arduino 2.0.16。

## Private resources

- `test-data/local/p3-media/package/`：SD 布局资源包（约 20 MiB）；`PACKAGE.sha256` 记录复制清单。
- `test-data/local/p3-media/previews/`：三档真实 ASCII 字符图的原尺寸 / 4× 预览，以及歌词字模预览。
- 原 MP3 不在 package；生成的 `no-lyrics.mp3` 是低幅度双声道测试音，不是原曲副本。
- 本地日文原稿与绑定 LRC 字节一致；图片、LRC、Windows 字体及预览均未加入 Git。
- 微软字体来源仅供本地私用，不能将本项目工具输出视为公开再分发授权。

## SD delivery — 2026-08-27

用户确认 SD 已插入 PC，并要求清理旧 BIN。已完成：

- `sync_p3_media.py --sd-root D:\` 同步 14 项资源及联合固件，全部复制 Hash 核对通过。
- `/firmware/ADV-Walkman-P3ABC-Gate.bin`：750224 bytes，SHA-256 与上述构建表一致。
- 只读确认原曲大小和 SHA-256 一致，没有重写 `benchmark.mp3`。
- 旧 `ADV-Walkman-Dev.bin`、`ADV-Walkman-P2-Gate.bin`、`ADV-Walkman-P3A-Gate.bin`
  已复制到 PC 并核对 Hash 后，从 SD 删除；可从下列目录恢复：
  `B:\sharewithlight\ESP\firmware\adv-walkman\sd-retired\2026-08-27-p3abc`。
- SD `/firmware` 顶层只保留联合 Walkman BIN；Bruce / UIFlow2 子目录及其他用户文件不动。
- 未操作 COM3 或设备内部 Flash；SD 上删 BIN 不等于删除 Launcher 已安装的 App。

## Device validation — 原计划及当前待验项

初版已实际测试，结果见顶部 A PASS / B SKIPPED / C FAIL。修复版仍需新的联合
日志与人工确认，不能将以下历史构建或计划当作真实显示 / 连续播放通过。

- A：方向 / 导航 / 跨页音频与 `ADVWalkmanBenchmark` 两行。
- B：真实 Header/Footer、长标题、浮层 / 局部刷新。
- C：中文右 / 原文左、右起多列、Latin 顺时针、View、Cover、资源取消 / 缺失与偏好。
- 连续窗口：44.1 kHz、Audio Error/Backpressure=0、PCM gap≤70ms，资源读取 / 动画在窗口内。
- 脚本 Pause/Seek 与重启恢复独立验收；重启至少 3 秒静音，偏好恢复。
- 需三份日志与人工显示 / 听感确认后才能 DONE；P3D/P4 未提前实施。

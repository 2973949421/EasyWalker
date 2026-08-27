# P3C validation record

Baseline: `61692f0`; target version: `0.7.0-p3c.media`.

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

## Device validation — pending

SD 已交付，但尚未安装或读取本轮真机日志。不能将以上结果当作真实显示或连续播放通过。

- A：方向 / 导航 / 跨页音频与 `ADVWalkmanBenchmark` 两行。
- B：真实 Header/Footer、长标题、浮层 / 局部刷新。
- C：中文右 / 原文左、右起多列、Latin 顺时针、View、Cover、资源取消 / 缺失与偏好。
- 连续窗口：44.1 kHz、Audio Error/Backpressure=0、PCM gap≤70ms，资源读取 / 动画在窗口内。
- 脚本 Pause/Seek 与重启恢复独立验收；重启至少 3 秒静音，偏好恢复。
- 需三份日志与人工显示 / 听感确认后才能 DONE；P3D/P4 未提前实施。

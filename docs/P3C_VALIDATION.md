# P3C validation record

Current baseline: `0b185f8`; target: `0.7.5-p3c.closure`.

## 0.7.5 P3ABC收尾 — 2026-08-27

### 基线与证据边界

- 原0.7.4日志保存在`test-data/local/p3-media/trial-0.7.4/p3-free-last.txt`，65273 bytes，
  SHA-256 `929d8257513f1ed4cee481af7768db18662abb2b9776cca3477664fe292c2c5a`。
- 41份完整检查点，最终elapsed596024ms；Audio Error0 / Backpressure0，歌词更新69ms，
  完整呈现80551µs；PCM70494µs仍超过70000，绝不计为本版或已通过证据。
- 本轮双布局、字体、曲库和自由日志实现见`P3ABC_CLOSURE.md`。没有新版设备运行数据，
  不把PC排版参考测试、编译期断言或构建成功当成实际显示与音频验收。

### 构建结果

六环境成功；稳定依赖不变；Benchmark A/B/C不重建。P1/P2保持各自历史Gate版本号，
只重编译共享代码兼容性，不作为本次SD安装项。

| 产物 | Bytes | Static RAM bytes | SHA-256 |
|---|---:|---:|---|
| Dev | 750736 | 122992 | `9b4d4bc43730ea63c3c0025b4367add82fdaa8984217d8f119c9fe2dd7ce3fa9` |
| P3ABC | 750800 | 122992 | `fb04750d616a04b594671ca6d5dc50e6c3e5e7974225b1180d3d7ba4e7a1a608` |
| P3A | 749232 | 115688 | `ced145f77869b7faa7df8c95fa1b4908efea7ef9d7a79f01bb2ba6dd90bbc145` |
| P1A | 674048 | 55088 | `3440014b00df3d31ab49e9e39c7c819e6c229e0ba7752833570012ec41519f6c` |
| P1B | 674704 | 55088 | `efe40840268745f8e7519222c6df3b83340a918178aa7848f05d645cdb43a2ad` |
| P2 | 724416 | 152664 | `8554438cbc72653e3c0bf3bf82867c7fa1c6d436aaadc68821377a8ba80041d1` |

均小于0x140000（1310720）bytes；媒体48KiB编译断言通过。Static RAM不是可用Heap，
实际最低Heap和持续增长/下降仍由新设备日志判断。

### 资源及检查

- 10首MP3格式、时长、Title/Artist/Album、绑定及暗黑天国无LRC检查。
- 298组实际歌词全分页的像素边界/整词检查；最多50个不同字形、5485 bytes压缩位图。
- 检查不只覆盖歌词：实际Title/Artist/Album也纳入字体覆盖。末轮抓到标题△/▽缺字，
  生成器已把Metadata纳入必需字集，不能带着缺字错误交付。
- 95字符mask、40×32默认及新旧网格、RGB565逐像素一致性/坏CRC；看过原尺寸与放大预览。
- 音量、视图、跨启动日志比较、完整性和历史失败隔离有自动检查；门槛保持70/100/200ms。
- 私有逐句译文与疑义见SONG_REVIEW；字体与补字明细见FONT_REPORT。版权媒体不入Git。

最终48项PC检查全部通过（P3B10 / P3C15 / fix8 / free9 / closure6），另外重新生成并
检查29组Crucifix排版预览。补字后CJK各28599字形，实际歌词与Metadata均无缺字；
非实际使用的320个BMP码位不支持，不能宣称覆盖全部Unicode。

### SD交付

- D盘仍为已确认的SD，FAT32；按旧manifest核对受管归属后同步成功，所有拷贝Hash一致。
- 仅覆盖同一个`D:\firmware\ADV-Walkman-P3ABC-Gate.bin`（750800 bytes，Hash见表）。
- 63个变化资源：11封面（含benchmark新ASCII）、14字体文件、10封面源图、18中日LRC、
  10新MP3；同时更新受管资源校验清单。未删除文件，不增加其他Walkman安装BIN。
- 原benchmark音频11972484 bytes / `4003b057…db51d63`在同步前验证不变；原LRC、
  状态、日志及其他用户歌曲不改。旧自由日志已留存PC，SD原记录不清空。
- 字体/源图/歌词/音频均未进入Git；无Flash写入、分区、eFuse、完整备份或push。

### 真机待验

普通界面自由使用，不强制顺序Gate。至少60秒自然连续播放，覆盖歌词/Cover、
暗黑天国无歌词、跨歌偏好、导航及音量；T保存后由用户手动重启，比较暂停静音、
歌曲/位置/偏好。播放中Seek仍单列未验，不以启动无声定位检查替代。
A/B/C继续DEVICE TEST；P3D/P4范围不扩展。

## 0.7.4 用户真机校准 — 2026-08-27

### 本次读到的0.7.3证据（不是新版结果）

- 原始自由试用日志已留存 `test-data/local/p3-media/trial-0.7.3/p3-free-last.txt`，
  73292 bytes，45份CRC有效检查点；SD日志不删除。
- 最后elapsed640597ms：Lyrics / Cover Ready，122歌词帧、3封面帧、42次自然换句；
  完整呈现最大91890µs、到期更新最大141ms；没有字体 / 媒体加载失败。
- Audio Error0 / Backpressure0，最长连续播放113473ms；PCM提交间隔先到92054µs，
  后到93100µs。两个上升段最近操作有Back，但旧日志不能证明单一阻塞源；不能写PASS。
- A导航覆盖不足、B/C主路径有覆盖；用户认可基本效果，提出仅当前句、增字、降音量、
  更细ASCII、英文整词换列。资源准备最大2101777µs为预取墙钟时间，不等同显示迟到。
- minimum heap86132 bytes；静态媒体预算48700 bytes。不把最低值本身当内存泄漏证据。

### 本轮修改与自动验证

- 只显示当前双语cue，首句前留空；CJK16、六列、英文整词优先换列，极长词安全拆分。
  29组真实字模全分页边界检查；首次长句完整，当前组续列不变暗。
- VolumePolicy逻辑0～255→raw0～63，启动raw32；真实setter及显示事件保持分离，
  日志增加speaker_volume_raw / speaker_volume_cap / heap_free。
- ASCII默认34×26，收紧共用字格空白和密度拟合，仍120×144 / ACOV v1 / 34588 bytes。
  已检查放大PC预览；不宣称设备观感或听感已通过。
- 重复Esc等待RestorePlaylist完成，不重复openPath打断同一目录扫描；保留70ms阈值。
- 本地检查覆盖字模 / 所有29组分页、英文整词和极长词、封面RGB565 / CRC与字符网格、
  全256音量映射、启动限幅路径、日志完整性、旧调度失败反例。无新工具链或全屏Sprite。

### 最终构建与SD交付

- 42项PC检查通过（P3B10 / P3C15 / fix8 / free9）；生产C++头的编译期断言覆盖
  单词边界 / 限幅 / 布局 / 输入 / 公平调度，旧调度反例必须失败。旧计时器13断言继续通过。
- 最终Dev与联合自由试用构建成功，0.7.4-p3c.tune；共享Runtime的P1 Gate A亦构建通过。
  P3A / P1B / P2 / Benchmark不重复构建或交付，本次不声称历史构建产物已更新。

| 最终产物 | Bytes | Static RAM bytes | SHA-256 |
|---|---:|---:|---|
| Dev | 746096 | 120040 | `84cdc181e81f11c093291ed2a8b185c18f915ac1b1725e1fb3e694ac146c9654` |
| P3ABC自由试用 | 746160 | 120040 | `505629f58ffc2e1d9308550926437542eda8ef5fc1876923ae171aec80e91eb0` |

- 均通过1310720 bytes上限与48KiB媒体预算编译断言；静态RAM不等同剩余Heap。
- 已覆盖 `D:\firmware\ADV-Walkman-P3ABC-Gate.bin`，PC构建 / artifacts / SD Hash一致。
- 唯一变化的媒体是 `D:\ADVWalkman\covers\ADVWalkmanBenchmark\benchmark.cover.adv`，
  34588 bytes，SHA-256 `cc23d15c33d14521b023b6eaf85be2783d0445dd2335bf688e9173616bbcec22`。
  同步更新资源校验清单，按旧清单验证文件归属；字体16px已在SD，不重复复制字体。
- 原benchmark大小及Hash仍符合既有11972484 / 4003b057…db51d63；音乐、两份LRC、
  原图、状态、日志和其他BIN均未改。无设备Flash / 分区 / eFuse或Git push操作。
- A/B/C仍DEVICE TEST；这次没有新版ADV运行证据，不宣称93ms音频峰值已解决，
  也不宣称新响度、可读性和封面已获人工确认。继续自由使用、15秒后台日志、T保存。

## 0.7.3 自由试用修复 — 2026-08-27

### 本次证据与修复范围

- 0.7.2 原始三份日志已保存 `test-data/local/p3-media/failure-0.7.2/`，SD 原日志不动。
- C 首因 `media_not_exercised / continuous_60s`，测量60010ms，44100Hz、Error0、
  Backpressure0、PCM gap42404µs、1723buffers。字体没有I/O错误，但歌词一直Loading。
- 381454资源字节 = Cover头及CRC34588 + 十帧像素10×34560 + 日文LRC1266；
  中文1357字节未进入读取。旧cover-frame优先分支饿死timeline，是代码调度根因。
- 初始单列截断、40×106不透明音量底、NORM Original占整行也是代码问题，非用户操作。
  旧Gate在失败时主动pause；现有日志不能将其解释为芯片重启。
- 改为公平resource worker、Loading不等于Missing、有界16ms / 64步UI burst；
  完整双语首句、CJK14七列、28/188/24分区、透明3px条、局部恢复。
- 只前置固定Play/Pause及Vol+/−真实功能，View不变；无额外DSP或音频架构修改。
  新FreeSession观察而不控制用户播放，后台日志不自动切视图 / Seek / 暂停 / 重启。

### 自动验证

- check_p3b：10项；check_p3c：15项；check_p3abc_fix：8项；check_p3_free：8项，
  共41项PC检查通过，包括真实资源格式、字模容量、源码契约、日志CRC和不伪报PASS。
- 现有ESP32 C++编译器执行15项free_contracts constexpr断言；注入旧封面优先算法
  必须失败。直接检查生产chooseMediaWork / PlayerKeys / MediaLayout，不是只重写Python。
- 旧计时器13项同一C++函数断言及旧算法反例继续通过；Session自测通过。
- 29组真实双语的全部分页用实际SD字模检查像素边界；完整首句44字在5列内。
  已人工查看PC放大预览，仍不是ADV实屏或音频验收。媒体预算静态断言保持≤49152bytes。
- P3B模型 / 像素 / 无面板 / 背景恢复检查已接入UI启动（RAM行缓冲内），
  实际设备运行结果将写入新日志，不能把编译成功说成已运行。
- 70ms PCM / 零错误 / 零Backpressure、100ms歌词呈现 / 200ms自然换句延迟均未放宽。

### 验收方式与未验范围

- 同名BIN启动普通UI，自由播放、浏览、View和音量；不再强制11步提示或60秒封面阶段。
- 每15秒/T追加`p3-free-last.txt`，CRC保护完整checkpoint、保留首因；T完成后显示
  LOG SAVED。自动记录自然连续播放时长与实际媒体负载，没有测到的写INCOMPLETE。
- READY_FOR_REVIEW只表示主路径覆盖，不是整阶段PASS；可读性、方向、封面观感与
  听感仍待用户确认。脚本Seek / 重启偏好恢复明确not_exercised，未提前勾选。
- 字体/歌词/封面缺失、错误、加载停滞分别记录；不通过暂停播放隐藏显示错误。
- 只同步变化的中文LRC（标点清理）与同名固件，更新资源清单；日文、音乐、原图、
  字体、Cover和历史状态保留。详细最终构建/SD数据见下一节。

### 最终构建与SD交付

三个环境均构建成功，版本0.7.3-p3c.free；保留原工具链和0x140000上限。

| App | Bytes | Static RAM bytes | SHA-256 |
|---|---:|---:|---|
| Dev | 745856 | 120384 | `48a6ed0f2a5513ac753ddbfd795b891232a2be73802195dbe9eaec36d88a3ea7` |
| P3A历史回归 | 746752 | 115680 | `0f415f6591a9437b3baa68efb7f52a1fe915381365d4dc3f3bca085e1f628489` |
| P3ABC自由试用 | 745920 | 120384 | `b2947076a22ae974ebf0c25738f02337b7731f855e2ca0ff6b95adad0a009e4a` |

- .pio输出与artifacts逐项一致。静态RAM不是剩余Heap；媒体≤48KiB编译断言通过，
  真实Heap / 音频仍需运行记录。P1/P2 source filter排除UI；本轮不重建无关历史Gate。
- 已确认D盘原曲大小11972484及SHA-256仍为4003b057…db51d63，未改写。
- 已覆盖`D:\firmware\ADV-Walkman-P3ABC-Gate.bin`，复制Hash与表中一致。
- 仅变化资源为`Lyrics/ADVWalkmanBenchmark/benchmark.zh-Hans.lrc`，SHA-256：
  `71b165537b3fefe119fca4740007b66cd765bac7822dc0169f34699aed3769c3`；已按旧清单核对
  归属后同步并更新清单。字体、日文、原图、ASCII Cover、原MP3不重复复制。
- 未删除其他SD文件，未操作COM或设备Flash；Launcher由用户安装同一个BIN。
- git diff --check通过，Git无MP3 / LRC / VLW / BIN / Fixture / 构建缓存或本地日志。
- **A/B/C仍DEVICE TEST**，本次没有ADV运行证据，不宣称实际音量、刷新速度或稳定性通过。

## 0.7.2 阶段计时修复

- `0.7.1` 实机日志：`automatic_ms=105`，`preflight / phase_timeout`；A/B 为 SKIPPED。
  输入自检通过；12 个文件打开达到预期上限，关闭后重开通过；媒体仍 Loading，无已报
  资源错误，连续测量未开始。三份日志已保存在 `test-data/local/p3-media/failure-0.7.1/`。
- 根因：service 入口旧 now 减去 transition 新 phaseAt，uint32 下溢后大于 45000。
  现修为阶段身份检查 + 工作后新时刻，终态不再落入超时判定；不放宽任何限值。
- `check_p3abc_timer.py` 用现有 ESP32 C++ 编译器执行同一判定函数的 13 项 constexpr
  断言；注入旧算法时，明确在 `phase transition underflow regression` 处失败。另检查
  Gate 接线确实使用工作后的 millis。不是仅有 Python 参考计算，也不是 ADV 已通过。
- 原有 33 项本地检查通过。本次只改联合 Gate；Dev/P3A 仍为 0.7.1，历史 P1/P2 和
  Benchmark 不重建。音频、资源、字体、输入及渲染代码不改。

### 构建与交付 — 2026-08-27

- `player-p3abc-gate` 构建成功；版本 `0.7.2-p3c.timer`。
- `ADV-Walkman-P3ABC-Gate.bin`：**761360 bytes**，小于原 `0x140000` 上限；静态 RAM
  117568 bytes。没有调整媒体内存预算、资源、音频、分区或依赖。
- SHA-256：`081b67b58fd8dfc00889e81cb93e44b87ab0747026adf506b33a2870390d29c6`。
- 最终 BIN 已确认包含新版本和阶段耗时诊断；D 盘仍在线，仅覆盖
  `D:\firmware\ADV-Walkman-P3ABC-Gate.bin`，复制后 Hash 一致。
- 原 SD 日志仍保留；音乐、字体、歌词、封面、状态和其他 BIN 均未改。
- 13 项 C++ 编译期断言、旧缺陷拒绝检查、调用接线检查及既有 33 项 PC 检查通过。
  本次修复尚未在 ADV 上运行，A/B/C 保持 DEVICE TEST，不宣称媒体 / 音频已通过。

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

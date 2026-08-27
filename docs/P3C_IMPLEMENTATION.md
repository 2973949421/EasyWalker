# P3C media contract — 0.7.1-p3c.fix

这是 `TECH_DESIGN.md` 的媒体格式 / 实现细节附件，不改变 P3 分阶段路线。

## 1. Resource compiler

独立 `.venv-media` 固定 Pillow 12.3.0 / fontTools 4.63.0，不安装到 PlatformIO。
`prepare_p3_media.py` 保留日文原稿，将两份 LRC 机械绑定到
`Lyrics/ADVWalkmanBenchmark/benchmark[.zh-Hans].lrc`，不修改 MP3。

官方单曲图：https://bushiroad-music.com/musics/crucifixx/ 。原图、字体、LRC 和
预览仅在 `test-data/local/p3-media/`，不提交版权媒体。字体仅供本机私用；不能据此
推定有公开分发 Windows 字体的许可。

CJK 来自 Windows simkai.ttf，缺字依次检查 msgothic.ttc、simsun.ttc、seguisym.ttf；
Latin 来自 times.ttf。fontTools cmap 检查及逐码位 fallback TSV；当前歌词必需字缺失
会使工具失败。每个字号独立生成；少数超出标称 em 的轮廓完整缩入该 cell，不裁笔画。

VLW 为标准 24-byte 大端头、28-byte glyph records、连续 8-bit coverage bitmap。
配套 IDX：16-byte 小端头 `<4sHHII>` = FIDX / version1 / record24 / count / VLW size。
每条 `<IIHHhhhHI>` = Unicode / bitmapOffset / width / height / advance / dx / dy /
fontPixels / reserved。按 Unicode 排序，设备每轮最多四条索引读取或到 750 µs 软预算，
每条 24 bytes，不扫描整套 VLW。一次不可分割 SD 调用的耗时另记。

ASCII 使用真实字符 mask 的形状 / 密度与原图颜色拟合，保持 contain 比例和留白，
不是直接缩小 JPG。三档 26×20、30×24、34×26；默认 30×24，120×144 canvas。
批处理按相对路径和 basename，冲突报错。每曲独立文件，无封面 JSON 数据库。

ACOV v1 Header 为 28 bytes，小端 `<4sHHHHHHHHII>`：magic / version / headerSize /
width120 / height144 / gridColumns / gridRows / pixelFormat1(RGB565LE) / reserved0 /
payloadLength34560 / CRC32。设备先分步校验，再按行预读并绘制；坏头或 CRC 显示 fallback。
`PACKAGE.sha256` 只用于本次拷贝校验与文件归属，不参与设备资源查找。

## 2. Runtime and memory

`NowPlayingMedia` 组合 `LyricsTimeline`、`LyricsRenderer`、`CoverRenderer`，共享
`FontCache`。一次 service 至多一个资源 worker，单次 read 不超过 512 bytes。
渲染函数只访问 RAM。没有全屏 Sprite、额外音频任务、全局 M5GFX 修改。

字体 bitmap 使用 16 KiB 变长 arena（在 24 KiB 预算内），每字按实际 width×height
存储；240 项 16-byte metric 加 256-byte Latin advance 表恰为 4 KiB。空格零面积
不参与 arena 搬移，避免与下一字模地址相同导致错误压缩。LRC 两语各
512 个 time/offset 加当前前后组文本、512-byte I/O 和行缓冲，低于 16 KiB。
类对象、工作集、封面行缓冲和 7 KiB 文件句柄 / stdio 预留一起由 static_assert 限制为
48 KiB。这个媒体工作集预算不等于全程序 Heap，也不包括 SDK 的全局挂载表。
本机 Arduino 2.0.16 VFS 默认 stdio buffer 为 4096 bytes，资源文件显式设置为
index=128、VLW=256、LRC/Cover=512 bytes，不改全局库。Latin 排版只读 metric，
advance 独立保留；显示前要求当前完整布局的全部 glyph bitmap 就绪并 pin，按字体分组
预取，显示结束再解除 pin。密集 Latin 每页最多 240 glyph，
使用 8-byte 紧凑位置记录。离开 Player 释放
歌词工作集和资源句柄，字体缓存供其他页面使用；不可见动画不推进。

横排 UI 保留 Font0 ASCII 度量以保持 Benchmark 两行回归；非 ASCII 使用原生
CJK em cell。只预取可见文本窗口，避免长 Title 超过缓存容量时反复逐出、永远无法绘制。
歌词采用真实 Latin advance / bitmap extent 与 CJK 原生 16 px cell；先整帧准备字模，
不能边等字模边擦除旧句。缓存容量不足明确报错，不静默扩容。

SD 挂载显式 `max_files=12`。当前 SDK `sizeof(FIL)=4136`，含 4096-byte FatFs cache；
全局表由挂载时一次分配，与 resource stdio buffer 不是同一份内存。
相比原默认 5 槽，增加 `7×(4136+1)=28,959 bytes`（含 o_append），必须单独报告，
不能塞进或谎称已包含在 7 KiB 媒体 I/O reserve。依据为本地 SDK 类型大小及
[Espressif v4.4.7 VFS 源码](https://github.com/espressif/esp-idf/blob/v4.4.7/components/fatfs/vfs/vfs_fat.c#L152)。
媒体工作集仍 ≤48 KiB；总 Heap 的可用余量 / 持续下降由真实 Gate 检查，构建不能证明。
Cover CRC 完成、帧结束时关闭文件，下一显示帧按需重开；LRC 索引及窗口读完关闭；
取消 / 换歌释放资源。FontCache 只保留当前 font 的 index/VLW，切换前关闭旧文件。

LRC UTF-8/BOM、多时间戳、1–3 位精度和全局 offset；128 KiB / 512 records / 1024-byte
line 上限，错误明确。基础文件优先；缺基础时扫描唯一非中文语言候选，冲突报错。
简体优先繁体，最近未用时间戳 ≤300ms 配对，等距取较早项。基础时间轴驱动显示；
同时间多记录稳定保存，播放定位取最后一个同时间记录。缺译文不影响原文显示。

没有歌词横移动画，也没有“前奏”标签；前奏只显示暗色首句预览。正常播放最多提前
2 秒准备下一句 / 下一分页。同一逻辑帧冻结布局与字模，使用 generation / frame id
取消 Seek、换歌、View 后的旧任务；准备时保留上一完整画面。
准备期与其他 UI 公平调度；完整呈现期优先完成 18px 条带，每条带之间仍服务音频，
媒体不读 SD，Header 不能淘汰 pin 的字模。每次完整呈现 ≤100 ms，正常预取后的
歌词到期后 ≤200 ms 更新；记录耗时、missed deadline、取消和呈现中 I/O 违规。
Volume overlay 隐藏时重绘
当前媒体，不恢复过期截图；帧内发生的浮层更新会在下一完整帧兑现。135×18 行缓冲仍唯一。
歌词每条带至多 18 行，Cover 每次预读 / 绘制 2 行（480 bytes）。前后组必须连同译文
完整容纳才显示，不只显示半组；无可用空间就隐藏预览。

## 3. View / persistence / keys

只在 Player 接通 portrait 顶部 3×4 第二排第二颗，官方键盘矩阵 (12,1)，按下边沿
且无 Fn / Shift / Ctrl / Alt。旧字母 V 不恢复；其余盲操仍在 P4。

Session v1 固定区 byte21：0=Lyrics、1=Cover，旧零值或非法值为 Lyrics。用户切换
只标记原有 cooperative A/B checkpoint；无歌词 Cover-only 不回写偏好。
不改变 Queue、位置、音量、恢复暂停或 Header/Footer。

## 4. Combined Gate and limits

先停播进行资源前置检查：实际 LRC、Cover Header/CRC、代表中日文字模及只读文件名额
耗尽 / 关闭后重开检查；文件名额检查期间暂停 Library/UI 后台 I/O，避免故意耗尽
波及业务。缺资源失败必须发生在用户导航前，且不替代稍后的冷加载。
P3A 使用单键物理位图与每键 25 ms 去抖；人工简化导航，未改变的页面返回由 Gate
自动回归并以 n0 / x-1 / y-1 标记，不能伪造物理按键证据。
P3B 模型 / 像素检查使用同一行缓冲。媒体冷加载、整帧和 View / 浮层
进入真实长曲的 60 秒连续窗口，44100 Hz、Audio Error/Backpressure=0、PCM gap≤70ms。
脚本 Pause/Seek/重启独立检查，不混入连续播放统计。用户确认实际可读性 / 方向 /
封面 / 音频，未确认不 PASS。自动部分目标 3–5 分钟，不人为延时凑时长。

分别写 p3a-last.txt / p3b-last.txt / p3c-last.txt。C 在重启前保存 RUNNING 证据，
重启后追加 final_result 和暂停恢复检查，不用重启后的零计数覆盖之前音频数据。
同版本 PASS marker 防止下次自动重跑；PASS 后 Enter 回到 Library，保持暂停。
日志验证和人工显示确认前所有 A-fix/B/C 状态仍为 DEVICE TEST。

`0.7.2` Gate 计时修正：service 入口保存阶段身份和开始时刻；本轮工作结束后重新
读取 millis，仅对未切换的、非终态、非人工等待阶段执行 45 秒检查。Measure / A
仍由原有专属逻辑控制。不能拿入口的旧 now 减 transition 新写入的 phaseAt；正常
millis 回绕仍按 uint32 差处理。失败日志增加阶段开始 / 观测时刻与实际已用毫秒。
`test/p3abc/phase_timing.cpp` 通过现有 ESP32 编译器执行同一 constexpr 判断函数的
13 项断言；旧算法注入测试必须失败，不用 Python 重写参考算法来代替 C++ 判定。

打开失败只在明确 ENOENT 时归类 Missing；否则记录 Error、组件、路径、操作、errno、
期望 / 实际读取长度。先快照失败现场，再暂停并清理。连续测量未开始时写 NA，
失败现场与测量数据不得混用；保留首因，后续未执行写 SKIPPED。Gate 使用独立提示卡，
真实展示阶段清除提示，不在歌词上画 STEP 或确认文字。

只生成本地 package；`sync_p3_media.py --sd-root D:\` 必须在用户确认 SD 在 PC 后
才执行。同步预检所有目标，未知或用户改过的同名文件停止；不清理其他音乐、旧固件、
状态或日志。始终不 upload/erase/修改分区/eFuse/full-flash。

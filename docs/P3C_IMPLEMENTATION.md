# P3C media contract — 0.7.0-p3c.media

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
fontPixels / reserved。按 Unicode 排序，设备每轮一次二分索引读取，不扫描整套 VLW。

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

字体 bitmap 68×256=17 KiB（在 24 KiB 预算内），96 项 RAM metric cache 加 256-byte
Latin advance 表（合计低于 4 KiB）；LRC 两语各
512 个 time/offset 加当前前后组文本、512-byte I/O 和行缓冲，低于 16 KiB。
类对象、工作集、封面行缓冲和 7 KiB 文件句柄 / stdio 预留一起由 static_assert 限制为
48 KiB。本机 Arduino 2.0.16 VFS 默认 stdio buffer 为 4096 bytes，资源文件显式设置为
index=128、VLW=256、LRC/Cover=512 bytes，不改全局库。Latin 排版只读 metric，
advance 独立保留，不要求整句所有 glyph bitmap 同时驻留。密集 Latin 每页最多 240 glyph，
使用 8-byte 紧凑位置记录。离开 Player 释放
歌词工作集和资源句柄，字体缓存供其他页面使用；不可见动画不推进。

横排 UI 保留 Font0 ASCII 度量以保持 Benchmark 两行回归；非 ASCII 使用原生
CJK em cell。只预取可见文本窗口，避免长 Title 超过缓存容量时反复逐出、永远无法绘制。
歌词采用真实 Latin advance / bitmap extent 与 CJK 原生 16 px cell；逐条带准备字模。

LRC UTF-8/BOM、多时间戳、1–3 位精度和全局 offset；128 KiB / 512 records / 1024-byte
line 上限，错误明确。基础文件优先；缺基础时扫描唯一非中文语言候选，冲突报错。
简体优先繁体，最近未用时间戳 ≤300ms 配对，等距取较早项。基础时间轴驱动显示；
同时间多记录稳定保存，播放定位取最后一个同时间记录。缺译文不影响原文显示。

同一逻辑帧冻结布局，多轮画完再推进；内容 / Header / overlay 交替获得工作机会。
从 20 ms/stripe 改成动画逻辑帧限频，不反复重置 y=0。Volume overlay 隐藏时重绘
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

复用 P3A 原导航和文本断言；P3B 模型 / 像素检查使用同一行缓冲。媒体冷加载和动画
进入真实长曲的 60 秒连续窗口，44100 Hz、Audio Error/Backpressure=0、PCM gap≤70ms。
脚本 Pause/Seek/重启独立检查，不混入连续播放统计。用户确认实际可读性 / 方向 /
封面 / 音频，未确认不 PASS。自动部分目标 3–5 分钟，不人为延时凑时长。

分别写 p3a-last.txt / p3b-last.txt / p3c-last.txt。C 在重启前保存 RUNNING 证据，
重启后追加 final_result 和暂停恢复检查，不用重启后的零计数覆盖之前音频数据。
同版本 PASS marker 防止下次自动重跑；PASS 后 Enter 回到 Library，保持暂停。
日志验证和人工显示确认前所有 A-fix/B/C 状态仍为 DEVICE TEST。

只生成本地 package；`sync_p3_media.py --sd-root D:\` 必须在用户确认 SD 在 PC 后
才执行。同步预检所有目标，未知或用户改过的同名文件停止；不清理其他音乐、旧固件、
状态或日志。始终不 upload/erase/修改分区/eFuse/full-flash。

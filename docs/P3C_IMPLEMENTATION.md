# P3C media contract — 0.7.4-p3c.tune

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
不是直接缩小 JPG。三档26×20、30×24、34×26；默认34×26，120×144 canvas。
裁去共用字格空白边，调整密度拟合；不按单字bbox放大标点，输出格式与大小不变。
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
预取，显示结束再解除 pin。密集 Latin 每页最多258 glyph，
使用 8-byte 紧凑位置记录。离开 Player 释放
歌词工作集和资源句柄，字体缓存供其他页面使用；不可见动画不推进。

横排 UI 保留 Font0 ASCII 度量以保持 Benchmark 两行回归；非 ASCII 使用原生
CJK em cell。只预取可见文本窗口，避免长 Title 超过缓存容量时反复逐出、永远无法绘制。
歌词采用真实Latin advance / bitmap extent与CJK原生16 px cell；先整帧准备字模，
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

没有歌词横移动画，也没有“前奏”标签；只显示当前双语组，首句前Content留空。正常播放最多提前
2 秒准备下一句 / 下一分页。同一逻辑帧冻结布局与字模，使用 generation / frame id
取消 Seek、换歌、View 后的旧任务；准备时保留上一完整画面。
准备期字体 / 歌词 / 封面按轮次公平服务，不让封面帧反复抢占冷歌词；Loading 不等于
Missing。完整呈现期优先完成 18px 条带，UI burst 最多64步 / 16ms软预算后回到音频，
媒体不读 SD，Header 不能淘汰 pin 的字模。每次完整呈现 ≤100 ms，正常预取后的
歌词到期后 ≤200 ms 更新；记录耗时、missed deadline、取消和呈现中 I/O 违规。
Volume overlay 只有3px细条和透明背景数字；若同歌曲代次 / 同歌词页则只重建25×82
覆盖区域，否则显示当前整帧，不恢复过期截图。帧内浮层更新下一帧兑现。135×18行缓冲仍唯一。
歌词每条带至多18行，Cover每次预读 / 绘制2行（480 bytes）。不呈现前后句预览；
内部相邻窗口仅用于预取。当前句完整续列均高亮，六列放不下才分页。
英文整词按实际advance预量：剩余列高不足则换列，只有词长超过174px才允许拆分。
计列与放置共享advanceColumn / VerticalWords；无新的数组或动态分配。

## 3. View / persistence / keys

Player 使用官方物理坐标、按下边沿，不按 Fn / Shift / Ctrl / Alt。View (12,1)，
本次确认前置Vol+ (13,0)、Vol− (12,0)、两个Play/Pause (13,1)/(13,2)。逻辑音量±8，
PlayerRuntime统一用VolumePolicy映射到raw0～63，启动逻辑128 / raw32；先真实调整
再触发显示。日志另记raw/cap；不存档音量。不是绝对安全声压保证。
旧字母V不恢复，其余盲操 / DSP仍在P4。

Session v1 固定区 byte21：0=Lyrics、1=Cover，旧零值或非法值为 Lyrics。用户切换
只标记原有 cooperative A/B checkpoint；无歌词 Cover-only 不回写偏好。
View 不改变 Queue、位置、音量或恢复暂停；音量键只走已存在的 Speaker volume 路径。

## 4. 当前自由试用日志（0.7.3）

正常 UI 启动恢复 Paused；没有顺序 Gate 卡片，不自动控制播放、Seek、View 或重启。
FreeSession 只观察同一主循环中的 Player / UI / 媒体状态与实际动作，不能调用 transport。
记录采用4KiB固定缓冲，512B分步append；避开完整歌词呈现和Player checkpoint写入。
每15秒 / T生成带sequence、CRC32及END行的checkpoint；半截尾部不会覆盖之前完整证据。
日志 `/ADVWalkman/logs/p3-free-last.txt`；旧p3a/b/c日志不覆盖。实际SD写入负载也在
PCM统计中，不暂停音频来制造好看的测量值；写入耗时、UI burst、最低heap另记。

启动使用同一个RAM行缓冲执行P3B模型 / 像素 / 透明浮层检查，不等同人工实际显示确认。
分别报告A/B/C主路径覆盖；未操作是INCOMPLETE，真实异常是FAIL，全部覆盖也只能为
READY_FOR_REVIEW。后台自然观察至少60秒连续44100播放，不强制用户停留哪个视图。
保留70ms PCM、零Backpressure / Error、100ms歌词整帧与200ms到期延迟门槛。
第一失败不被后续健康状态冲掉；字体 / 歌词 / 封面分组件、路径、操作、errno记录。
旧Gate脚本Seek / 重启偏好验证不自动执行，记录not_exercised，任务保持待验。

显示常量：Header28、Content188、Footer24；内框123×174、上6下8、CJK16、列距2，
双语间距6、最多六列。仅当前双语组，无前后句预览；英文整词换列，过长才拆字。
保留0.7.3已清理的中文标点，本轮歌词内容不改。小标点右上，括号 / 引号旋转。

## 5. 历史 Combined Gate and limits（0.7.0～0.7.2，不是当前操作步骤）

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

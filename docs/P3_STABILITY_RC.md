# P3 Stability RC — 0.8.5-p3d.stability-rc

基线：`3efeb01` / `0.8.4-p3d.renderfix`。本文件记录Stage A实现和验收；它不是P3完成声明。

## 修复范围

- Library以页面、请求、资源三重Token约束异步结果。快速左右切换只保留最新请求；旧读取只能幂等取消并归还条带。
- `LibraryFrameCommit`分别核对名称、轮盘和174行封面。只有同一Token连续提交完整区域才发布`displayedSelection`。
- 显式`UiWorkScheduler`替代布尔轮转。浏览模型在旧ready条带之前推进；一次步骤最多承载一个可能阻塞的文件操作。
- 连续2秒没有文件阶段、读取、条带或区域进展时执行一次局部重建；第二次失败显示“封面读取失败”，但Tab、Esc、左右和设置仍有效。
- 每次启动保留上一/当前/下一三项LCOV验证缓存；首次仍严格CRC，暖返回不重复整文件校验。
- Playlist六行最终文本、正式Title、光标和局部脏区由独立控制器持有。Player、Playlist、Library使用独立字体租约，暖往返不清除列表字模。
- 每个主循环只调用一次`M5Cardputer.update()`；事件仍进入16项固定队列，短按、按住一次、组合键、页面代次和唤醒吞键规则不变。
- T生成递增SaveTicket，捕获Player/Display修订。Player状态、显示设置和诊断分步完成并回读校验；重复T可合并当前快照，但较新修订形成尾随事务。10秒无结果明确TimedOut。
- 日志使用1KiB固定格式化缓冲、每步最多512bytes和滚动CRC。Stage A每15秒写紧凑摘要；T、错误和自愈写完整快照，保留32项RAM事件。

## 自动证据边界

生产纯状态类型由目标编译器覆盖旧卡死顺序、精确Token、连续条带、1000次快速重定向、一次恢复/二次错误、保存尾随及超时。其他P3回归继续检查真实字体资产、298组歌词、11首绑定、图像CRC、导航、View和Session兼容。

这些检查不能证明LCD物理提交时序、SD实际延迟或音频连续性。固件构建只进入`DEVICE TEST`。

## 本地构建与容量证据

六环境均已构建通过，且全部低于`0x140000`：

| 环境 | BIN bytes | 静态RAM bytes |
|---|---:|---:|
| player-dev | 792128 | 128568 |
| player-p3abc-gate | 792192 | 128568 |
| player-p3a-gate | 782080 | 122456 |
| player-p1-gate-a | 674736 | 55184 |
| player-p1-gate-b | 675408 | 55184 |
| player-p2-gate | 725120 | 152768 |

联合BIN SHA-256：`2b91291d11f5e76ad2b65efd7afb16037fbc1222b9cbaf3eb1910bbc0ed8c26c`。媒体加事件预算为49080/49152 bytes，其中媒体48528 bytes、事件队列232 bytes；只余72 bytes，后续不得在该预算内无核算增加状态。静态RAM相对0.8.4约减少15KiB。最低Heap和最大连续块必须由Stage A真机日志提供，本地构建不伪造这两项。

## Stage A真机步骤

### 测试中新增、尚未修复的回归

- Playlist出现以方便命名的文件名替代MP3正式Title；后续修复必须恢复“正式Title优先、文件名只作冷Metadata临时回退”，并覆盖未选中行和暖返回。
- 从有LCOV曲库切到无LCOV曲库时仍保留上一曲库封面。后续修复必须把Missing占位图作为新目标的完整帧提交，不能把“验证期间保留旧完整帧”延伸到Missing终态。

这两项已确认问题使当前0.8.5不能通过Stage A；按用户要求先记录，继续收集本次压力日志，不在设备测试途中替换固件。

### 2026-08-29 boot 11日志诊断

SD日志在媒体同步前保存为私有只读副本`test-data/local/p3-media/stability-085/pre-cantopop-sync-p3-free-last.txt`，与SD原文件SHA-256一致。当前版本共有11个boot；boot 11包含103份完整检查点，不能通过Stage A：

- 音频主路径仍稳定：`pcm_gap_max_us=48555`，`audio_errors=0`，`backpressure=0`。
- 输入、局部选择、暖返回和暖View分别达到75ms、421ms、2934ms和631ms，均超过50/100/300/300ms门槛；冷View 1358ms仍在1500ms内。
- 歌词完整呈现峰值142806us、到期迟到653ms，超过100ms/200ms门槛。
- Library累计64次请求、10次stall、6次recover和4次failure；但`library_cover_errors=0`。这证明“Missing LCOV保留旧图”没有被封面错误计数覆盖，且页面事务已有真实无进展/恢复负担，不能只归因为人工观感。
- 同期`metadata_fallbacks=0`，但人工看到未选中行显示basename。日志只能证明Metadata层没有声明fallback，不能证明六行最终显示模型使用了正式Title；验收必须增加逐行显示来源证据或生产控制器像素/文本断言。
- 保存Ticket 1～6已完成；日志结束时Ticket 7仍为active、`save_status=4`，因此最后一次T没有终态证据。不能把前六次成功概括成“每次都能保存”。

随后SD曲库整理为仅`AveMujica`和`粤语迷幻`，两者均提供LCOV；这会消除本次“资源确实缺失”的触发条件，但不会修复固件的Missing终态，相关回归仍保持未通过。

1. 使用耳机正常播放至少20分钟。
2. 播放5分钟后进入Library，做三轮快速左右切换，每轮至少20次；封面变化中反向切换。
3. 每轮后分别验证Tab、Enter、Esc和T仍响应；不得靠重启恢复。
4. Playlist停留和翻页后往返Player/Library；Cover/Lyrics单次与连续View均检查完整画面。
5. Lyrics、Cover、Playlist、Library分别设置15秒息屏并唤醒；首组键只唤醒，全部松开后再操作。
6. Player、Playlist、Library、Settings各按T一次；封面读取中快速按T两次。正常3秒、繁忙5秒内结束，10秒必须明确失败/超时。
7. 最后T保存，手动重启，确认暂停、歌曲、位置、View和显示设置与最后成功Ticket一致。

## Stage A通过条件

- 永久UI卡死、旧Token提交、条带所有权冲突、正常操作自愈、看门狗/异常复位均为0。
- 输入≤50ms；列表和Library标题/轮盘≤100ms；暖返回/暖封面/暖View≤300ms；冷封面/冷View≤1500ms。
- 歌词完整呈现≤100ms、正常预取更新≤200ms、PCM提交间隔≤70ms、Audio Error/Backpressure=0。
- T动作≤50ms接受；空闲≤3秒、繁忙≤5秒；每个Ticket都有Succeeded/Failed/TimedOut终态。
- 人工确认显示完整、导航和保存可用、耳机播放无异常。

Stage A任何业务代码修改都要求重跑。全部通过后才生成`0.8.6-p3.closure`，唯一允许的差异是周期摘要从15秒改为60秒。扬声器破音保持DEFERRED。

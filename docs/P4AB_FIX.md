# P4A+B Fix — 0.9.1-p4ab.fix

## 范围与状态

基线为`efb5b19` / `0.9.0-p4ab.controls`。本轮只修复保存、日志负载、Playlist暖返回、歌词布局、Tab路由和两项KINO资源；P3继续保持`FROZEN / UNVERIFIED / KNOWN ISSUES`，扬声器破音继续`DEFERRED`。构建成功只进入`DEVICE TEST`。

## 实现结果

- T先写≤256-byte、带CRC的`SAVE_BEGIN`，等待Player/Display修订落盘，再写分段完整快照，最后独立写`SAVE_END`。结果分为`Succeeded`、`StateSavedLogFailed`、`StateFailed`和`TimedOut`；状态成功但详细日志失败时不会再误报为全部失败。
- Transport、15项存档阶段和事件环分别输出；每段设计上限≤768 bytes，事件每段4项。周期摘要改为60秒；完整快照只在T、首次新错误或自愈时产生。
- 新日志为`/ADVWalkman/logs/p4-free-current.txt`与`p4-free-previous.txt`，单文件1MiB。旧P3日志不删除、不再追加。删除16-byte RTC breadcrumb，只把`esp_reset_reason()`写入启动摘要。
- 同目录Player→Playlist返回不调用`openPath`或`entryAt`，保留六行最终文本、Metadata位图、光标和字体租约；同窗口移动只标脏旧/新两行。
- Lyrics先计算当前页实际列数与最大使用高度，在原有Glyph数组里写入居中坐标；没有新增数组、缓存或绘制期SD访问。
- Playlist、Library、Settings的Tab走同一返回Player路由；Player Tab仍打开当前歌曲目录。所有导航保持Transport不变，息屏首组键仍只唤醒。
- 《Группа крови／血液型》以SD同版284.003秒音频为基准，将俄中24组统一到LRCLIB 284秒时间轴；《渴望变革》不改。该版本曾把KINO单张肖像误生成为135×173；0.9.2已将其作为尺寸合同缺陷修正为全曲库统一135×154，详见`P4AB_TRANSPORT_FIX.md`。

## 本地证据

`tools/check_p4_fix.py`覆盖独立保存终态、日志段上限、RTC清除、暖返回、局部高亮、Tab、歌词居中、双语时间轴和KINO LCOV；既有P4控制、P3稳定性及29组真实歌词边界检查均通过。

六环境构建全部成功：

| Environment | BIN bytes | Static RAM bytes |
|---|---:|---:|
| player-dev | 796960 | 128624 |
| player-p3abc-gate | 797024 | 128624 |
| player-p3a-gate | 783776 | 122480 |
| player-p1-gate-a | 674800 | 55184 |
| player-p1-gate-b | 675488 | 55184 |
| player-p2-gate | 725184 | 152768 |

联合BIN SHA-256为`be454cf94424dccd70c1cce08ed49b4b238ced55645b896b682f9b3dd4831325`，低于`0x140000`。P3ABC静态RAM较0.9.0减少32 bytes；媒体+事件保持49080/49152 bytes，事件队列232 bytes，RTC诊断0 bytes。最低Heap和最大连续块只能由新真机日志取得，目前为`PENDING`。

SD交付文件及核验值：

- `/firmware/ADV-Walkman-P3ABC-Gate.bin`：797024 bytes，SHA-256 `be454cf94424dccd70c1cce08ed49b4b238ced55645b896b682f9b3dd4831325`
- `/Lyrics/KINO/01 - Группа крови.lrc`：SHA-256 `83d010899019021187ee0f4b66363ce118840e3f0caecef62fbfe12f8c68b6d9`
- `/Lyrics/KINO/01 - Группа крови.zh-Hans.lrc`：SHA-256 `84333e46a0e232e31e649c70cb49a982153e93a71056247e4f688cadf74f1b9c`
- `/ADVWalkman/library-covers/folders/KINO/cover.adv`：135×173，SHA-256 `627bedeb065eb1246fdec887a1c9c1ad197dd6cb606e8144b24204ea36854bcc`
- 原P3日志`/ADVWalkman/logs/p3-free-last.txt`保持3610676 bytes，未删除或覆盖。

## DEVICE TEST

1. 在四页面分别按T，确认即时“保存中”及明确终态；列表忙时连按两次，SD中每个Ticket必须有有效CRC的BEGIN/END。
2. Player/Playlist往返10次并快速上下移动；检查正式Title不回退、无整页闪烁，暖返回完整首屏目标≤300ms。
3. 检查短句居中、多列从中心扩展；歌词完整呈现≤100ms、到期更新≤200ms。
4. 完整试听《血液型》首段、两遍副歌和第二段，与《渴望变革》对照；主要人声起点误差目标≤300ms。
5. 在Library和Settings用Tab返回Player，歌曲、位置和Pause保持；息屏首次Tab仅唤醒。
6. 检查KINO为单张肖像。耳机连续播放至少15分钟并穿插列表、Tab、T；PCM≤70ms且Audio Error/Backpressure为0/0。
7. 最后T保存并手动重启，核对歌曲、位置、Pause、模式和设置。未取得新SD日志与人工确认前，本轮不写DONE。

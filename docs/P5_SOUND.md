# P5A+B Sound — 0.10.2-p5.monochrome

Status: `DEVICE TEST`

## 交付边界

P5只修改耳机验收使用的PCM音效、播放器页9～12号键、Footer第二枚图标及Session保留字节。音频Backend、3×1536缓冲、音量映射、Queue、Session v1长度、页面、音乐和资源均未改变。扬声器持续噪声／破音及P3/P4既有性能问题继续留在P6。

## 信号路径与Preset

处理位置固定为：MP3解码 → Stereo→Mono下混 → `PcmDsp`原地处理 → 既有M5.Speaker缓冲提交。

| Preset | 固定处理 |
|---|---|
| Original | int16逐样本直通 |
| Tape | -2.5dB预留、180Hz低架+1.5dB、4.2kHz高架-4.5dB、较明显但有界的软饱和、1.15输出补偿 |
| Radio | 200Hz高通、5kHz低通、-18dBFS/2.5:1轻压缩、轻软饱和 |
| Vocal Clear | -2.5dB预留、180Hz低架-1.5dB、1.2kHz+2dB、3kHz+3.5dB、1.15输出补偿 |

三个处理Preset末端均使用-1dBFS、即时Attack、80ms Release的无Look-ahead峰值保护。Preset切换运行新旧两链20ms线性交叉淡化；暂停切换只更新目标，Seek、换歌、采样率变化和Stop重置滤波历史。

## 输入、显示与恢复

播放器页9／10／11／12分别选择Original／Tape／Radio／Vocal Clear。其他页面沿用自己的导航语义，不产生音效Action；息屏第一组键仍只唤醒。Footer第二枚圆形标识以纯黑白显示O／T／R／V，变化只使Footer状态区失效。当前字母会与播放模式图标一起显式预取并按真实字宽居中；图标绘制后显式恢复黑白文字状态，不再依赖偶然缓存命中，也不能把音效样式泄漏到时间或进度信息。

Session v1头长度保持24 bytes：字节22保存0～3，字节23继续保留。旧Session的零值自然恢复Original；非法值回退Original并记录诊断，但不让歌曲、位置、Queue、Pause或播放模式恢复失败。一次有效Preset变化只请求一个checkpoint。

## 本地证据

- 16项可直接执行的`tools/check_*.py`主机回归全部通过（PlatformIO专用`check_launcher_size.py`由构建调用）。
- Original边界样本、44.1/48kHz及非标准采样率、频响参考、快速切换、Session byte22和页面隔离均有检查。
- 完整DSP运行状态为244 bytes，并有编译期`<=256 bytes`断言；没有新增PCM、文件、图片或全屏缓存。
- `player-dev`静态RAM为128880 bytes，较0.9.2基线128624 bytes净增256 bytes；媒体+事件预算保持49080/49152 bytes。

0.10.1业务代码曾通过六环境构建；0.10.2只撤回Footer配色并增加颜色隔离断言，因此按最小有效范围重建实际交付使用的两个环境：

| 环境 | BIN bytes |
|---|---:|
| player-dev | 805296 |
| player-p3abc-gate | 805360 |

联合BIN SHA-256为`5320884ea5a1eff0fd4cc88f0c44d47e741cf48f973c2cf17fc8e7317c44ed15`。`player-dev`静态RAM仍为128880 bytes，普通RAM净变化为0；两个BIN均低于`0x140000`。最低Heap、最大连续可用块、DSP实际耗时、PCM提交峰值与听感必须由新P5真机日志取得。构建通过不代表这些项目已经通过。

0.10.0真机日志确认12次Preset请求均被接受，Footer反馈峰值3ms、首个PCM生效37ms、PCM提交峰值51.818ms、Audio Error／Backpressure为0／0；同时记录到`face=4, U+0054`缺字，直接支持Footer字模修复。0.10.1真机截图进一步暴露彩色图标的文字颜色泄漏，本轮据此恢复黑白并增加回归。2026-08-31已仅覆盖SD的`/firmware/ADV-Walkman-P3ABC-Gate.bin`为0.10.2，复制后为805360 bytes且SHA-256与上述联合BIN一致；彩色0.10.1 BIN已保存到Git忽略的本地恢复目录。音乐、歌词、封面、字体、Queue、Session和历史日志均未改动。

## 真机验收重点

固定音量依次比较O/T/R/V；快速交替至少20次；播放与暂停分别切换；在切歌、Previous/Next、Repeat One、Shuffle、View及息屏唤醒中确认Preset保持。最后选择非Original，按T取得明确终态并重启检查完整恢复。验收同时要求PCM提交≤70ms、Audio Error／Backpressure为0／0、Footer和首个实际PCM生效≤100ms。

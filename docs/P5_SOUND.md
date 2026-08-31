# P5A+B Sound — 0.10.0-p5.sound

Status: `DEVICE TEST`

## 交付边界

P5只修改耳机验收使用的PCM音效、播放器页9～12号键、Footer第二枚图标及Session保留字节。音频Backend、3×1536缓冲、音量映射、Queue、Session v1长度、页面、音乐和资源均未改变。扬声器持续噪声／破音及P3/P4既有性能问题继续留在P6。

## 信号路径与Preset

处理位置固定为：MP3解码 → Stereo→Mono下混 → `PcmDsp`原地处理 → 既有M5.Speaker缓冲提交。

| Preset | 固定处理 |
|---|---|
| Original | int16逐样本直通 |
| Tape | -2dB预留、180Hz低架+1dB、4.5kHz高架-3dB、轻软饱和 |
| Radio | 200Hz高通、5kHz低通、-18dBFS/2.5:1轻压缩、轻软饱和 |
| Vocal Clear | -1.5dB预留、180Hz低架-1dB、1.2kHz+1dB、3kHz+2dB |

三个处理Preset末端均使用-1dBFS、即时Attack、80ms Release的无Look-ahead峰值保护。Preset切换运行新旧两链20ms线性交叉淡化；暂停切换只更新目标，Seek、换歌、采样率变化和Stop重置滤波历史。

## 输入、显示与恢复

播放器页9／10／11／12分别选择Original／Tape／Radio／Vocal Clear。其他页面沿用自己的导航语义，不产生音效Action；息屏第一组键仍只唤醒。Footer第二枚圆形标识显示O／T／R／V，变化只使Footer状态区失效。

Session v1头长度保持24 bytes：字节22保存0～3，字节23继续保留。旧Session的零值自然恢复Original；非法值回退Original并记录诊断，但不让歌曲、位置、Queue、Pause或播放模式恢复失败。一次有效Preset变化只请求一个checkpoint。

## 本地证据

- 全部`tools/check_*.py`主机回归通过（PlatformIO专用`check_launcher_size.py`由构建调用）。
- Original边界样本、44.1/48kHz及非标准采样率、频响参考、快速切换、Session byte22和页面隔离均有检查。
- 完整DSP运行状态为244 bytes，并有编译期`<=256 bytes`断言；没有新增PCM、文件、图片或全屏缓存。
- `player-dev`静态RAM为128880 bytes，较0.9.2基线128624 bytes净增256 bytes；媒体+事件预算保持49080/49152 bytes。

六环境均通过，BIN尺寸如下：

| 环境 | BIN bytes |
|---|---:|
| player-dev | 805296 |
| player-p3abc-gate | 805360 |
| player-p3a-gate | 791616 |
| player-p1-gate-a | 681968 |
| player-p1-gate-b | 682832 |
| player-p2-gate | 732304 |

联合BIN SHA-256为`0e24da3f61b8bda0444526a79e6bf8c09472247237f881ce6262e5b878fe7993`。全部固件均低于`0x140000`；最低Heap、最大连续可用块、DSP实际耗时、PCM提交峰值与听感必须由新P5真机日志取得。构建通过不代表这些项目已经通过。

2026-08-31已仅覆盖SD的`/firmware/ADV-Walkman-P3ABC-Gate.bin`；目标为805360 bytes，复制后SHA-256与上述联合BIN完全一致。音乐、歌词、封面、字体、Queue、Session和历史日志均未改动。

## 真机验收重点

固定音量依次比较O/T/R/V；快速交替至少20次；播放与暂停分别切换；在切歌、Previous/Next、Repeat One、Shuffle、View及息屏唤醒中确认Preset保持。最后选择非Original，按T取得明确终态并重启检查完整恢复。验收同时要求PCM提交≤70ms、Audio Error／Backpressure为0／0、Footer和首个实际PCM生效≤100ms。

# P4A+B Controls — 0.9.0-p4ab.controls

## 范围与P3移交

基线为HEAD `7177052`和SD上的`0.8.5-p3d.stability-rc`。P3状态固定为
`FROZEN / UNVERIFIED / KNOWN ISSUES`，证据与遗留项见`P3_DELIVERY.md`；本轮不生成0.8.6，
不把P4成功写成P3通过。

本轮只实现Player盲操区1～8、上下文输入、Previous/Next和原子播放模式。9～12保持
no-op，等待P5 DSP；音频Backend、3×1536缓冲、Queue/Session格式、音量、页面、媒体和
封面均不改，扬声器问题继续DEFERRED，使用耳机验收。

## 实现合同

- `InputRouter`直接接收`UiPage`；Esc和开发T保持原规则。0.9.1起Playlist、Library、Settings的Tab统一回Player。
- Player坐标：`x=13,y=0..3`为Volume+、Play、Play、Previous；`x=12,y=0..3`
  为Volume-、View、Play Mode、Next；`x=11`整排无动作。
- Playlist只产生Up/Down/Enter/Esc/Tab；Library产生Left/Right/Enter/Esc/Tab及S；Settings
  产生方向/Enter/Esc/Tab，S无动作。页面代次仍丢弃旧动作，息屏仍吞掉首组按键。
- 0.9.2起手动导航与自然EOF严格分离。Previous大于5秒回本曲0秒；否则顺序模式按源索引
  后退并在首项回末项，Shuffle优先走真实History、无History时选择新的随机项。Next按
  当前order前进并在边界开始下一轮。Pause保持，Repeat One不拦截手动导航。
- `setPlaybackMode(RepeatMode,bool)`在一次Action内同时修改Repeat/Shuffle，并只请求一次
  checkpoint。四态为Normal、Repeat One、Repeat All、Shuffle；自然EOF分别为停止、重播
  本曲、列表回首和随机重排下一轮。
- Footer只更新模式区域并记录实际绘制延迟；播放模式统一使用方形循环箭头，列表循环无
  内字、单曲循环`1`、随机循环`R`、列表单次`S`；第二图标继续显示Original。

## 自动验证与构建

`tools/check_p4_controls.py`覆盖1～12坐标、页面隔离、成功才checkpoint、单次原子模式保存及
诊断字段；`P4Controls.h`把生产映射和四态循环编译成static_assert。既有P1 Gate继续覆盖
5秒Previous、顺序边界、随机History、Pause切歌、Repeat One手动Next、Repeat All和随机循环及
Session非法旧组合恢复。

构建目标：Dev、P3ABC、P3A、P1 Gate A、P1 Gate B、P2 Gate。最终结果：

```text
LOCAL_CHECKS=100 unittest cases + 13 C++ timer assertions PASS
PLAYER_BIN_SIZE=794880 bytes
PLAYER_BIN_SHA256=56497cb931a43192dbad01b59c1419d9135e74a291cf4072a97deaafdc2b7035
STATIC_RAM=128656 bytes
MEDIA_PLUS_EVENTS=49080 / 49152 bytes
SD_DELIVERY=794880 bytes; SHA-256 matches build
```

六个构建环境均通过：Dev 794816、P3ABC 794880、P3A 783552、P1 Gate A 674784、
P1 Gate B 675472、P2 Gate 725168 bytes；全部低于`0x140000`。Transport日志另外记录
成功切歌后的歌曲索引、播放状态、首个PCM延迟、暂停切歌次数及快速切歌造成的待测量覆盖次数。
SD只覆盖`/firmware/ADV-Walkman-P3ABC-Gate.bin`；覆盖后大小794880 bytes、SHA-256为
`56497cb931a43192dbad01b59c1419d9135e74a291cf4072a97deaafdc2b7035`。旧0.8.5同名BIN已在
Git忽略的恢复目录按原Hash保存；未写入媒体、字体、Queue、Session或历史日志。

以上本地证据不代表真机性能或功能通过。

## 10～15分钟自由验收

1. Player逐一短按1～8，每次只触发一次；在其他三页按同位置，不得漏出播放器Action。
2. 播放超过5秒按Previous回本曲开头，5秒内再按进入History；播放和暂停中分别测Prev/Next。
3. 用`熱・情`36秒Overture验证Repeat One自然重播；Normal自然播完末项停止，手动Next仍回首；Repeat All末项自然回首。
4. Shuffle连续Next确认一轮内不重复、轮末自动开始新随机轮；Previous按真实History返回；切模式不得重置歌曲、位置或Pause。
5. Footer模式反馈目标≤100ms；9～12全部无动作，音效图标仍为Original。
6. 设置15秒息屏，Previous/Next/Play Mode第一次只唤醒，全部释放后第二次才执行。
7. 快速交替Previous/Next至少10次，保持可操作且无Audio Error/Backpressure。
8. 最后T保存并手动重启，确认歌曲、位置、Pause、Queue和最后成功Ticket的模式一致。

严格门槛：输入≤50ms、Footer≤100ms、PCM≤70ms、Audio Error/Backpressure=0/0；正常T
空闲≤3秒、繁忙≤5秒、10秒必有终态；新Action永久卡死、旧页动作、重复派发和唤醒泄漏均为0。

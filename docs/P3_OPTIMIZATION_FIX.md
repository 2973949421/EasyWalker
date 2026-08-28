# P3 修复与优化交付记录

日期：2026-08-28；基线`88bd676`；版本`0.8.1-p3d.fix`。
这是既有P3修复，不进入P4，不变更Candidate A、3×1536缓冲、Queue/Session或音量映射。

## 修复依据

0.8.0 SD日志：boot 3，139个CRC有效检查点，约17.56分钟；8次选歌/6首不同歌曲、
队列10首。音频错误/Backpressure为0，但PCM最大305885µs、UI burst212763µs、
复合audio service291421µs、歌词准备6062802µs、呈现124780µs、到期迟到4170ms。
这些是真实未通过证据。不同最大值不一定来自同一次调用，不能直接相减归因。

Sophie和Black Birthday的SD封面存在且CRC/Hash正确；不是用户漏放图片。
代码确认的缺陷：占位帧结束关闭仍在CRC校验的文件；列表上下移动重建整个六行窗口；
加载提示位于歌曲行；离页释放同曲索引/校验；换句重读前/当前/后共六份文本，
下一句仅剩两秒才准备，且单一布局/全局字模固定容易与其他显示工作相互干扰。
LRC与原包一致，没有统一偏移；本轮不改歌词时间戳。

## 实现

- Tab物理(0,1)单键：Player打开当前歌曲父目录并定位，Playlist/Library返回Player。
  不调用selectTrack/replaceQueue/play/stop/seek。Settings和无当前歌曲时无动作；
  原始位图唤醒吞键仍在动作派发之前。Enter仍选歌并重播，包括当前歌曲。
- 同一六行窗口上下移动仅更新新旧行；保留basename与Metadata分离。新Metadata按
  请求路径核对，只重绘对应行。只准备当前区域及最终省略文本的字模，绘制后解除UI固定。
  冷加载超过250ms才在底部显示提示；旧画面先清除，目录取消/过期请求保护不变。
- 暖返回保留六行上下文，验证目录和generation后复用；同曲媒体离页只暂停、关闭闲置
  句柄，保留歌词索引/封面验证。重新进入按真实位置准备，不补画旧歌词。
- LyricsTimeline仅存当前/下一组四份文本。自然换句复用已读槽，补读新下一组。
  两份4byte/glyph布局共享位图；当前/下一/UI三种pin独立，提升下一帧时保留UI固定。
  当前帧后立即准备下一分页/句；浮层恢复使用已显示帧，不覆盖正在准备的下一帧。
- 字体按face分组加载；增加一个512byte索引页，实际读取21条/504bytes，命中后RAM查询。
  仍为15KiB位图、200项metrics，准备/呈现隔离，歌词呈现阶段不读取SD。
- 前奏灰色首组第一页，到首句时间点亮；无“前奏”文字，正常播放不显示前后组。
- 到期歌词的字模/文本优先取得三个资源工作槽，第四槽仍推进封面校验，不让低优先级资源饿死。
- Cover的finishFrame仅在不处于校验阶段时关闭文件；有效CRC跨View/暖返回保留。
  校验和绘制串行使用游标，按实际宽度/高度读取，每次≤512bytes；兼容旧120×144。
  11张既有绑定从原图重制40×32字符mask，10张135×135、Octagram Dance135×133。
  无拉伸、无裁切、没有替换源图或歌曲归属。
- Cover标题/歌手居中；14px时间在x17、宽84px，右侧模式箭头/1/A/S/?和Original小圆标。
  Lyrics无Header，底栏和透明音量条保留。

## 日志与测量

保留全局首错，同时独立保存lyrics/cover/font/navigation首错、路径、操作、errno、
期望/实际读取长度、发生代次/时间和计数。字模绘制缺失记录实际face/codepoint/page。
新增Tab播放/暂停次数、前后position/state、暖返回/高亮反馈耗时、窗口重建次数、
歌词目标/准备/提交播放位置、封面/字体打开次数、索引命中以及PCM峰值页面/资源代次。
PCM与歌词延迟峰值分别保存歌曲路径；媒体单步峰值另区分字体、歌词、封面和布局准备。
transport service与persistence service分开计时；现有目录、字体、绘制、日志峰值保留。
峰值页面/代次用于定位现场，不冒充某一模块已被证明是阻塞原因。

周期日志仍15秒、手动T仍等待存档完成。页面/高亮变化只积累RAM证据，不逐次触发写SD。
日志缓冲14,336bytes，覆盖四组件最长路径首错、两项峰值歌曲路径及十二条事件；
最坏检查点预算13,750bytes。它独立计入静态DRAM，
不是增加媒体预算。旧日志不删除；Host只接受当前版本，Tab播放/暂停未测判INCOMPLETE。

## 本地验证

命令：`.venv-media/Scripts/python.exe -B -m unittest discover -s tools -p "check_p3*.py"`。

- 67项检查通过：生产C++编译期策略、PC资源/字模/布局参考、源码接口和日志边界。
- 新生产策略断言：Tab目标、Settings/无歌曲、唤醒整组吞键、文本槽复用、pin提升、
  新旧封面尺寸/512byte读取。注入旧“占位帧总是关文件”策略会编译失败，拒绝旧缺陷。
- 298组真实歌词逐页及相邻帧，加入真实底栏/音量字模后最大9974bytes、110项；
  小于15360bytes/200项，不用扩大缓存解决。非法/非BMP或超容量仍显式记录错误。
- 查看了Sophie全宽ASCII及灰色首组的PC像素预览；它们不是ADV截图或实测时序。
- 编译器记录：基础媒体46596bytes，含LibraryVisual共47900bytes≤49152bytes。
  尚余1252bytes；不新增全屏Sprite或音频任务。

最终静态DRAM及SD复制结果记录于下节。没有设备时序数据时，
不把编译/参考测试当成PCM、按键手感、歌词同步或视觉验收。

## 构建与SD交付

代码提交：`0c24c70`。六环境最终构建均成功；其中P1 A/B和P2仅回归共享Runtime诊断计时，
没有修改音频/Library语义或重新执行历史真机Gate。最终三份UI产物使用修正后的C++11兼容调度代码，
不使用过程中留下的旧BIN；Benchmark未重建。

| 环境 | BIN bytes | 静态DRAM bytes | SHA-256 |
|---|---:|---:|---|
| player-dev | 773664 | 141136 | `4e7b694a6eca28d011f5aa6a5c0600fe6fdd11f4be4b1c107883b793ead0b330` |
| player-p3abc-gate | 773728 | 141136 | `75219f76e9d0e1d190e48b89df946b7d0816286e59418f4efdb33641b11d896f` |
| player-p3a-gate | 767136 | 122136 | `e87c0f5900387c11e15e652c104ca2973eb88340a50fe12811f2e25b8929d598` |
| player-p1-gate-a | 674112 | 55096 | `37fbb1173fa4c86087359320ef973180063a91e52a98c4b65c92e61f0aba5e5d` |
| player-p1-gate-b | 674768 | 55096 | `fec83280be64a5dbc24a16600535486eea243f6eff30011b74f4535f0170fe4d` |
| player-p2-gate | 724416 | 152680 | `5fd3b84fe33e514d252558a0452ee6cd2064509d66a40fb9fa0d819e06f18099` |

全部小于`0x140000`（1310720bytes）。相对0.8.0，Dev/P3ABC静态DRAM增加12856bytes、
P3A增加4248bytes，包含六行basename缓存、双布局、诊断路径/事件和有界日志缓冲。
媒体独立上限仍49152bytes，实际47900bytes；尚待真机核对最低Heap和长时间操作趋势。

SD确认仍为D盘后，仅覆盖：

- `/firmware/ADV-Walkman-P3ABC-Gate.bin`，773728bytes，Hash与表中一致。
- `/ADVWalkman/covers/`下原有11个歌曲封面，每个复制后核对Hash。
  对应尺寸/Hash保存在Git忽略的`test-data/local/p3-media/p3d-fix-covers.json`。

字体逐文件核对与SD一致，无需复制。音乐、LRC、Queue/Session、曲库封面均未改动；
不删除任何旧文件。历史`p3-free-last.txt`仍635801bytes，复制前后SHA-256均为
`f22eecef458e0b1ed7d2853d3b71ef306a1b67771c9f6845c08f0edc2abde447`。
三份UI本地产物已更新到`artifacts/`，资源/日志/二进制未加入Git。

## 自由验收（无规定操作顺序）

1. 正常安装同一个`/firmware/ADV-Walkman-P3ABC-Gate.bin`，不创建额外Walkman安装文件。
2. 播放/暂停各用Tab往返；进度状态不变。Enter仍重播，Esc仍逐级返回。
3. 浏览翻页，观察歌曲行无加载提示/旧歌词残留；看前奏和Sophie、Black Birthday、
   Symbol 1、Crucifix X封面/歌词。不要求逐首听完，不自动Seek/切歌/重启。
4. 自由操作自然积累至少60秒播放；息屏后Tab首次只唤醒，松开再按才能导航。
5. T保存后取SD，结合日志及人工观感判断。设置/息屏/Launcher及恢复仍按实际覆盖分别验收。

严格标准仍是零Audio Error/Backpressure、PCM≤70ms、整帧歌词≤100ms、正常到期更新≤200ms。
高亮≤100ms、暖返回首屏≤300ms是响应目标，日志分别记录；实际超限须继续定位。
A/B/C/D继续DEVICE TEST。未确认的听感或显示不会自动标DONE；不扩大分区、不改Backend、
不上传/擦除Flash、不碰eFuse，也不为达到PASS排除真实资源、页面、浮层或日志负载。

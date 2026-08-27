# P3ABC 收尾交付 — 0.7.5-p3c.closure

基线：`0b185f8`；实施日期：2026-08-27。状态：**DEVICE TEST**。
代码、本地资源、六构建及48项PC检查完成；同名BIN及63个变化资源已同步D盘并核对Hash。
这是现有A/B/C的收尾，不进入P3D黑胶曲库/设置/息屏，不扩展P4控制或DSP。
父子AGENTS不变；产品规则见PRD，阶段顺序见P3_DELIVERY。

## 1. 本版内容

| 项目 | 实现 |
|---|---|
| Cover | Header28 / 内容188 / Footer24；歌名14、歌手12 |
| Lyrics | 不显示Header，内容216 / Footer24；内框123×202 |
| 歌词 | 18px微加粗楷体、14px Times；中文右/原文左；只显示当前组；整词换列 |
| 正常UI | 楷体/Times实际字模度量；底栏时间10px；字体故障才允许备用字体 |
| 音量 | `raw=(level*102+127)/255`；启动level80/raw32；步长8；不存档音量 |
| ASCII | 默认40×32；另有34×26、48×40预览；120×144 RGB565 / ACOV v1不变 |
| 日志 | 自由操作、按启动编号追加；T等待状态保存；比较手动重启前后证据 |

无歌词保持Cover，View无动作且不改偏好；歌词页停止不可见标题动画。
切换视图不改变歌曲、播放进度或音量。纯自造语言保留原文，不填重复中文栏。
新版100%对应最初未限幅版40%，是刻度校准，不代表声压或听力安全保证。

7种字体为CJK12/14/16/18、Latin10/12/14。PC四倍栅格化后缩小，18px楷体微加粗；
SD保留8-bit VLW/IDX，RAM使用4-bit覆盖缓存。15KiB bitmap arena，200项metric及
Latin度量表共3968 bytes；总媒体预算仍受48KiB编译断言约束。保持135×18行缓冲。
绘制只遍历字模与条带相交的像素；歌词呈现期固定字模，不读取SD。

UI资源读取和绘制分轮，burst最多64次小工作或6ms软预算，随后回到音频服务。
日志避开歌词完整呈现和状态写入，实际日志/字体/浏览/浮层负载不排除出测量。
各模块耗时单列；不把旧70.494ms峰值未经实测归因于某一个模块。

## 2. 私有媒体准备

源目录`B:\sharewithlight\SONG`保持不动。10首输出平铺到`/Music/AveMujica/`；
320kbps/44.1kHz/Stereo MP3，无响度归一化，不宣称转码改善音质。
Title/Artist/Album保留；仅缺失的Octagram标签按已确认曲名/发行资料补齐。

ASCII basename：ankokutengoku、blackbirthday、choirschoir、ether、masuerade、
sophie、symbol1、symbol3、twomoons、octagramdance。明确对应表在私有SONG_MAPPING.json。
`symbo3.lrc`明确对应symbol3，不用模糊文件名匹配。暗黑天国故意不提供任何LRC。
现有`/Music/ADVWalkmanBenchmark/benchmark.mp3`不复制、不重命名、不改标签。

9张封面复用已核对的内嵌图；Octagram Dance使用
[Completeness通常盘官方发行页](https://bang-dream.com/discographies/4025/)的对应封面。
Crucifix X继续使用已确认的官方单曲图。ASCII使用95个可打印字符mask，匹配轮廓、
密度、边缘与颜色；不是直接缩小照片。每曲独立文件，无设备媒体数据库。

私有交付入口（均在`test-data/local/p3-media/`，不提交Git）：

- `SONG_REVIEW.md`：9份逐句原文/译文及疑义，含源文件、输出路径、大小和Hash。
- `translations/`：中文LRC审阅稿；原文/时间戳不静默纠错。
- `FONT_REPORT.md`、`cjk-*-fallback.tsv`：字模数量、补字来源与体积。
- `previews/`：各封面的原尺寸/放大预览、字符表，以及实际歌词排版预览。
- `package/`、`PACKAGE.sha256`：SD受管文件与拷贝校验清单。

复现命令：

```powershell
& '.\.venv-media\Scripts\python.exe' -B tools/prepare_song_library.py --extract
# 私有中文LRC存在后运行；不改源音频
& '.\.venv-media\Scripts\python.exe' -B tools/prepare_song_library.py --build
& '.\.venv-media\Scripts\python.exe' -B tools/prepare_p3_media.py --fonts
& '.\.venv-media\Scripts\python.exe' -B tools/prepare_p3_media.py --manifest-only
```

Windows字体和版权媒体仅作本机私用，未推定任何公开再分发许可。

## 3. 自动验证与尚未验证的边界

- PC检查覆盖10首格式/Metadata/绑定、298组实际原文歌词及所有分页、字模边界、
  英文整词、CRC、7字体及4-bit缓存。当前实际页面最多50个不同字形，位图5485 bytes。
- P3B模型、P3C资源、历史fix、free日志、closure检查分别10/15/8/9/6项，48项全部通过；
  closure包含实际歌曲Metadata字形覆盖检查；Python参考/源代码契约检查
  不等于在ESP32实际跑过绘制。
- 启动无声自检使用真实字体和媒体模块定位、布局及取消；不打开Decoder、
  不自动Seek正在播放的歌曲、不自动写播放状态。播放中Seek依然单列未验。
- Dev、P3ABC、P3A、P1A、P1B、P2六个构建及尺寸/Hash详见P3C_VALIDATION。
- 阈值不变：PCM≤70ms、Error/Backpressure=0、歌词呈现≤100ms、预取更新≤200ms。
  编译成功不是这些真机指标已通过；没有新版设备日志前不标DONE。

原0.7.4日志已保存在`trial-0.7.4/p3-free-last.txt`：41份有效检查点、约596秒试用，
Error/Backpressure=0，歌词更新69ms；PCM70.494ms仍失败。原SD日志保留。

## 4. 一次安装，自由验收

只通过Launcher安装同一个`/firmware/ADV-Walkman-P3ABC-Gate.bin`，不是USB upload。
没有顺序提示卡，没有脚本自动切歌、暂停、Seek或重启。下列项目顺序自由：

- 浏览曲库/列表：看长名称换行，试方向、Enter、Esc；曲库S进设置再返回。
  顺便显示一次ADVWalkmanBenchmark卡片，供A文本回归记录；不必再次完整听旧歌。
- 自选有歌词歌曲：看18px双语当前句、切换封面看歌名/歌手，低音量起步调节。
- 选暗黑天国：只有Cover；按一次View不应跳空白页。再选有歌词歌，偏好应保留。
- 任意页自然连续播放至少60秒；不用盯屏、无需逐首听完。试一次播放/暂停。
- 最后选好歌曲和视图，按T，等`LOG SAVED`后手动重启一次。
  重启应暂停、不自动出声；先不按键至少3秒，随后再按T保存启动记录，等保存成功后取SD。
  也可等待至少15秒的自动检查点，无需第二次T。
- SD插回PC后读取日志；人工仅反馈可读性、封面观感、音量手感及异常声音。

操作仍为单键：`] }`切View；Backspace/del加音量，`= +`减音量；
`\`或Enter播放/暂停；Esc返回。不恢复旧全局字母V或新增完整P4控制。
T保存不要求当前正在播放；静音启动阶段也可记录。日志会指出未覆盖项，
`READY_FOR_REVIEW`仍不是人工PASS；本版任一启动的真实FAIL不会被后一次健康日志抹掉。

## 5. 完成条件

新版本日志及人工显示/听感均通过后才收口A/B/C对应任务。P3-07黑胶视觉仍留P3D。
本轮不清理其他音乐、旧BIN、存档或日志；不push、不改Backend/分区、不erase/eFuse。
若实际性能或观感不满足，按具体模块/指标修复，不改宽松阈值、不恢复顺序Gate。

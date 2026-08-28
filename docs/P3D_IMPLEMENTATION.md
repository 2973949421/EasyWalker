# P3D 实施与合并验收记录

日期：2026-08-28。版本：`0.8.0-p3d.ui`。入口基线：`0eac03f`，P3C `0.7.6`仅本地。

## 范围与事实

实现黑胶曲库、独立普通彩色封面、四项中文设置、显示偏好双槽、息屏整组吞键及
标准Launcher返回。保留Candidate A、Queue/Session、音量raw102上限/启动raw32、
歌词18px/Times14px、歌曲ASCII与135×240 rotation2。没有新音频任务或全屏Sprite。
没有修改歌曲/歌词/歌曲封面，也不添加实体上一首/下一首；必须到列表选择另一首。

P3C前置修复和本轮P3D仍需合并真机确认。旧0.7.5日志PCM154.195ms、歌词呈现109.328ms、
延迟687ms仍是未通过证据；本地修改不抹掉旧记录，不宣称这些峰值已消失。

## 主要实现

- `LibraryCoverReader`：LCOV v1、120×120 RGB565/CRC，最多512bytes读取，2行RAM，
  退出/换库关闭旧文件。缺图占位，坏图具体报错。与歌曲ACOV完全分开。
- `LibraryVisual`：两行完整名称，弧形短名以actual advance控制容量；0/1/2项不凑数量；
  只绘制有效前后项，选择过期动画不排队。复用135×18行缓冲并重设高度，以pushSprite提交。
- `DisplaySettingsStore`：DSPL v1独立24byte A/B记录，generation/CRC/范围校验；1秒防抖保存，
  write/flush/close/reopen/verify分轮，与Player/日志错开。失败RAM值保留且显示未保存。
- `ScreenPowerController`：raw56键位图在派发前处理；Player180秒/其他30秒，七档可选。
  任意整组唤醒键直到全释放前均不派发；输入去抖继续更新。只灭背光，后台音频/存档照常。
- `SettingsPanel`：中文四项及内部子面板；默认取消返回Launcher。运行时选择非当前TEST App，
  Pause而非Stop，等待checkpoint/显示设置/日志成功，再标准启动API验证image并返回。
  目标/保存/启动失败不会盲目重启，也不回退Factory。
- 自由日志按boot_id追加，新增D覆盖、显示恢复、实际唤醒位图/吞键计数、设置/封面/Launcher
  错误与耗时。T等待状态保存；返回Launcher同样先保存日志。没有自动Seek/切歌/重启测试。

## 资源来源与预览

官方动画主页：<https://anime.bang-dream.com/avemujica/>。
实际原图：<https://anime.bang-dream.com/avemujica/wordpress/wp-content/themes/avemujica_0102/assets/images/common/index/img_hero_2.jpg>。
原图SHA256：`1a67656f7d48776746e2ce487a6c4996f3020aec11b00d3d635557504de2b6af`。

已人工查看原图和120px/放大预览，五位动画角色全部保留；等比缩小留边，无裁切。
输出：`/ADVWalkman/library-covers/folders/AveMujica/cover.adv`，28824bytes，SHA256：
`8329320d6c110abbde318f5c51db35394308f20b2f3469a947a7500e252aba4b`。

私有资源/预览位于`test-data/local/p3-media/p3d/`，Git忽略。
`tools/prepare_p3d.py`还生成135×240排版参考，使用交付字模；检查了AveMujica、
ADVWalkmanBenchmark两行长名和设置页，未见边界裁字。它是PC像素参考，不冒充ADV截图。
新菜单73种CJK字符均已在cjk-12/14，英文数字沿用latin-12；无需重制字体。

## 本地验收

运行：`.venv-media/Scripts/python.exe -m unittest discover -s tools -p "check_p3*.py"`。

- 62项PC检查通过，含298组实际歌词/分页及字模回归（最大固定字模5485bytes/50字形）。
- 生产C++函数的编译期断言覆盖正常超时/唤醒/组合键/页面/时钟回绕/永不，DSPL CRC、
  版本、范围和generation回绕；旧5秒规则与旧导航取消有故意失败反例。
- 独立PC参考覆盖LCOV头/长度/CRC坏数据、RGB565、等比留边、真实字体度量、日志判定。
- 新增最长路径日志容量检查，上界8503bytes；日志缓冲8→9KiB（独立于媒体预算），
  避免新增D字段令合法长路径的完整检查点溢出。保存错误会结束等待并报告失败。
- 源码契约检查无渲染SD读取/全屏图像、无Factory fallback/Stop归零/Flash地址猜测。
  保存失败/取消和屏幕实际效果仍须真机验证，编译期及源码检查不能替代它。
- 未重建未受影响的P1/P2/Benchmark；历史Gate的source filter仍排除新UI代码。

最终三环境构建成功，静态DRAM：Dev/P3ABC 128280bytes，P3A 117888bytes；媒体48KiB
编译期断言通过。日志容量增加计入上述DRAM，不靠扩大媒体预算实现。阈值保持
`0x140000`、PCM70ms、歌词呈现100ms/正常预取延迟200ms。尚无0.8.0真机数据，不标DONE。

| Environment | App bytes | SHA-256 |
|---|---:|---|
| player-dev | 767360 | `d4c0505e699cd9ccea946c8d728f97975871a2123d9231a0812080c2e88964c7` |
| player-p3a-gate | 763104 | `897fc67990af7d59b929f429986db1caa2199ec47a2b499521422c3238cc8b85` |
| player-p3abc-gate | 767424 | `09d5a67a11f46a81e71d487ddcc8bd38e10b4c1e3f4fef8ddff1a207d9bcedd8` |

本地artifacts与构建输出逐个核对一致；上限1310720bytes，联合BIN占58.5%。

## SD 交付完成

2026-08-28确认D盘为约32GB FAT32且原Walkman目录存在后，仅写入两项：

- 覆盖`D:/firmware/ADV-Walkman-P3ABC-Gate.bin`，767424bytes，SHA-256与上表一致。
- 新增`D:/ADVWalkman/library-covers/folders/AveMujica/cover.adv`，28824bytes，SHA-256与资源节一致。

复制后两项均核对一致。SD现有cjk-12/cjk-14/latin-12的IDX和VLW与本地一致，没有复制字体。
没有删除文件，没有改音乐/歌词/歌曲封面/Queue/Session/旧日志，没有执行烧录或设备重启。
代码/工具提交：`95f07b1`。A/B/C/D均保持DEVICE TEST，用户安装后才有本版本真机证据。

## 一次安装后的自由验收

1. 只通过M5Launcher安装同名`/firmware/ADV-Walkman-P3ABC-Gate.bin`，无USB upload。
2. 自由从Lyrics/Cover退列表，进AveMujica选至少两首（含无歌词暗黑天国），确认不残留、可选歌。
3. 看普通彩色曲库封面/黑胶/长名称；曲库按S进入设置，调亮度和两类时限。
4. 可暂改15秒，分别试View和音量键首次只唤醒，全松开再按才执行；组合键也只唤醒。
5. 自然累计≥60秒播放，任意页面均可，不锁定歌词或封面，不自动打断。
6. 恢复希望保留的设置，在设置确认返回Launcher；按Launcher原倒计时Enter提示进入菜单。
   再启动Walkman，先静置≥3秒确认暂停静音/歌曲/视图/显示设置；再按T或等待15秒日志完成。
7. 关机取SD交回PC，读`/ADVWalkman/logs/p3-free-last.txt`；正常UI是否清晰、是否卡音由用户确认。

Host：`B:/PlatformIO/penv/Scripts/python.exe tools/validate_p3_free.py D:/ADVWalkman/logs/p3-free-last.txt`。
`INCOMPLETE`表示缺覆盖，`READY_FOR_REVIEW`仍需人工；早期错误不能被后续记录覆盖。
P3A/B/C/D按真实对应结果收口，不用新黑胶视觉掩盖基础导航和音频失败。

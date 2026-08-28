# P3C 0.7.6 navigation repair — local delivery

日期：2026-08-28。基线：`e7f45bd` / `0.7.5-p3c.closure`，开始时Git干净。
目标版本：`0.7.6-p3c.navfix`。A/B/C继续 **DEVICE TEST**，P3D **TODO**。
代码与测试提交：`2f37b4c` — `Fix P3C browser navigation and text rendering`。

## 事实与修复边界

- 0.7.5用户反馈Player可用，但Esc后列表残留歌词。旧日志Esc被接受、页面仍Player；
  缺少目录失败细节，不能断言OOM或SD是唯一根因。
- SD有10首AveMujica歌曲，保存队列仍是benchmark单曲。选歌成功才建立当前目录队列，
  不把SD文件存在当作Queue已切换，也不提前实现实体上一首/下一首。
- `actual_font_layout`确系自检使用错误画布：把y76的屏幕区域放入18px行缓冲。
- 旧峰值PCM154.195ms、歌词呈现109.328ms、更新687ms保留，不能写已通过。
- 音频Backend、Queue/Session格式及语义、音量、歌词布局、封面/字体资源均不改。
  不访问设备、不写SD、不清理文件、不安装固件。本次新增日志字段不改变存档。

## 实现

1. `UiCoordinator`先切换目标页面、取消媒体代次、释放工作集与字模固定状态，先清屏，
   下一轮才打开目录。Loading期间显示干净框架和按需加载的提示；Esc不依赖Ready。
2. `NavigationLoad`分别处理Pending/Ready/Error；请求代次和Library代次/路径同时核对。
   同目标加载中不重启扫描。连续5秒无扫描/排序/索引写入进展报告停滞；分页定位也
   记录进展。错误留在可返回页面，Enter重试、Esc返回，不困在Player。
3. 新`cancelOpen()`仅清理浏览器临时工作，不动活动Queue的pin；`cancelSelection()`
   取消旧待选歌。冷启动首次返回定位当前歌曲，正常返回保留最近目录/选择。
   通过原`selectTrack()`选歌成功才进入Player；浏览不Stop、不调音量、不跳进度。
4. 六行窗口每次准备一个目录项，Metadata仍按歌曲路径核对；字体只准备固定标签和
   可见文本。绘制只消费RAM，标题/行/Footer分区更新，首屏不等待完整字模才清旧内容。
   字体不可用时用最小故障字体显示恢复指引；正常UI仍使用SD楷体/Times。
5. 时间使用已有Latin14：`59:59/59:59` advance宽68px，框96px，Footer仍24px。
   音量百分比仍Latin10。曲库名称用真实显示对象测量；行缓冲只测合法局部字模墨迹。
6. 字模bitmap被淘汰后的重载，先检查当前字体文件再排队；原phase4已有文件归属检查，
   因此这项是减少跨字体重载绕路，不作为旧导航失败或错读字体的唯一解释。
7. 保持6ms UI软预算；慢Library调用后先补音频service，输入导航与补充音频service
   也计入模块峰值。禁止排除加载、浏览、View或日志负载来获得合格数据。

## 日志与验证语义

仍追加 `/ADVWalkman/logs/p3-free-last.txt`，按版本/boot_id/CRC分组，不覆盖旧证据。

- `nav_state/generation/target/error`、`browser_path/state/error`、分配失败与最大空闲块；
- `page_frame_complete`、`playlist_frames/library_frames`、`track_selections`、
  `different_track_selections`、`queue_count/selected_queue_count/current_index`；
- `media_track/generation/active`、`time_font_px=14`、独立字体失败字段；
- 导航工作、列表准备/绘制、读目录项、字体、输入、音频、日志各自峰值；
- 首项失败不覆盖；`audio_limits_ok/media_limits_ok`独立保留，避免自检错误遮住性能问题。

出现页面完成/选歌/导航错误时标记后台保存，不在按键处理里同步写日志。
A覆盖要求真实列表/曲库成帧和不同歌曲选择，不再仅要求nav_mask；没有覆盖是INCOMPLETE。
READY_FOR_REVIEW仍需人工确认，不能自动标DONE。

## 自动验证与生成物

本地检查包含生产NavigationLoad的C++编译期正反例（取消/过期/Ready/Pending/Error、
重试、进展、5秒停滞与时钟回绕），GNU++11固件语法兼容，以及页面生命周期源码检查。
真实Latin14字模宽度、298组歌词/媒体旧回归、日志超限/未覆盖检查一起复验。
这些不是模拟ADV执行：真实显示墨迹和目录/Queue/文件系统交互仍需要合并真机确认。

构建目标为Dev、P3ABC、P3A、P2；P2因Library新增取消接口/计数而补构建。
不重建P1或Benchmark；不新增主机C++工具链、不删除PlatformIO缓存。
54项PC检查通过（导航6 / P3B10 / P3C15 / fix8 / free9 / closure6）；另有timer的13项
编译期断言及旧实现反例测试。真实歌词共298组，最大固定字模bitmap5485bytes；
媒体总预算仍由≤49152bytes编译断言约束。正常用户环境构建，未删除缓存。

所有输出只在PC `artifacts/`；无SD副本。本地源BIN与artifact拷贝Hash一致：

| 环境 | BIN bytes | 静态RAM bytes | SHA-256 |
|---|---:|---:|---|
| player-dev | 754784 | 125728 | `1c2aea62a52f394649c375dc4f735579bba40c57e0a859459413553acb27ad3e` |
| player-p3abc-gate | 754848 | 125728 | `040910aaeb9a3162828059052ed9a078dec8fe8dd401fbbe92d309344194c5d7` |
| player-p3a-gate | 752112 | 116368 | `f3b6b654ac6d5cc2d07f97087987ba5132a61f14733a9cb587c38d8ba6ab87fb` |
| player-p2-gate（历史回归） | 724368 | 152672 | `896a92a53aaf813816715f99542b3cd296b82827e4e112c19d0a8b2857d1c893` |

全部低于`0x140000 = 1310720 bytes`。静态RAM不是运行时最低Heap，也不代表真机
音频/显示已过关。P2保持原历史Gate版本，当前Dev/P3ABC/P3A使用0.7.6。

## 后续合并验收

**P3C修复与自动验证 → 单独规划并实施P3D → 一次安装、合并真机自由试用。**
本轮结束即停在P3D规划入口，不自动实施P3D，不要求用户现在安装。

- 从Lyrics和Cover均能返回干净列表，加载/错误时仍可返回或重试。
- 返回曲库、进入AveMujica，选择至少两首不同歌曲（含无歌词暗黑天国），队列10首。
- 真实歌曲Metadata/歌词/封面正确，跨页不断音；时间14px可读、光标/目录返回合理。
- 自由播放累计至少60秒，PCM≤70ms、歌词呈现≤100ms、正常预取更新≤200ms。
- 不自动暂停、切歌、Seek或重启；未覆盖/超限单列。现有重启恢复待验项不偷偷勾选。
- P3D完成也不能掩盖上述失败；只有日志与人工确认满足后才收口对应任务。

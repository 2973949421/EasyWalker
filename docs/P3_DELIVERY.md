# P3 UI Delivery Roadmap

> 本文是 P3 的唯一分阶段执行明细。产品行为以 `PRD.md`、正式技术接口以
> `TECH_DESIGN.md`、任务状态以 `TASKS.md` 为准。P3 不重新选择 Audio Backend，
> 不改变 P1 Queue / Session 或 P2 Library Engine 语义。

## 当前验收方式 — 0.7.3（覆盖下方历史 Gate 操作顺序）

工程仍按 A → B → C → D，不跳阶段；用户只安装一份同名 P3ABC BIN，但改为普通
界面自由试用 + 后台日志，不再要求先过 A 提示卡才能看到 B/C。可以自由浏览、播放、
看歌词、切 View、调音量；不自动 Seek / 切封面 / 暂停 / 重启。

按用户本轮授权只前置冻结的两个 Play/Pause 及 Vol+/−，其他 P4 控制 / DSP 不做。
当前显示校准为 28 / 188 / 24 px、CJK14、完整双语前奏预览、透明细音量条。

日志 `/ADVWalkman/logs/p3-free-last.txt` 每 15 秒分块追加并带 CRC，T 主动保存。
分别记录 A/B/C 主路径覆盖；连续播放 60 秒是被动覆盖项，不是固定测试阶段。
结果为 INCOMPLETE / FAIL / READY_FOR_REVIEW，最后一项仍需人工可读性和听感确认，
不得自动改为 DONE。旧 Gate 额外的脚本 Seek / 重启偏好恢复不在本次后台自动执行，
显式记为 not_exercised；后续用小范围验证补足，不要求重跑全套。

保留严格 70 ms PCM、100 ms 完整歌词呈现、200 ms 到期更新，错误不被后续日志
覆盖。0.7.2 的 media_not_exercised 已定位到冷歌词被封面优先级饿死，不是音频失败。
实施 / 构建 / SD 记录以 P3C_VALIDATION 为准；A/B/C 继续 DEVICE TEST。

## 1. Frozen Delivery Order

| 阶段 | 对应任务 | 交付内容 | 真机 Gate |
|---|---|---|---|
| P3A | P3-01、P3-08、P3-09；P3-07 功能骨架 | 竖屏 Shell、四页路由、可用曲库 / 播放列表、跨页播放 | `0.5.1` 功能 Gate 已 PASS；文本修复并入 Gate A-fix+B+C 回归 |
| P3B | P3-02 | Now Playing Header / Footer、歌曲信息、进度与状态反馈 | Gate A-fix+B+C |
| P3C | P3-03～06、P3-12 | 字体、歌词、ASCII Cover 工具与 Renderer、View Selector | Gate A-fix+B+C |
| P3D | P3-07 视觉完成、P3-10、P3-11 | 黑胶曲库、独立曲库封面、设置、息屏 Soft Lock、最终 UI 校准 | Gate D |

执行顺序固定为 `P3A → P3B → P3C → P3D`。更改顺序、合并产品范围或提前实现
后续阶段必须先获得用户确认。`0.5.1` 已完成原 Gate A 的功能与音频验收，唯一待收口
问题是曲库长名称换行。用户当前无法实机操作，因此工程继续按
`P3A text fix → P3B → P3C` 分开实施和提交，但不要求单独重装 P3A 修正版；下一次
真机安装使用一份 Gate A-fix+B+C 固件，同时回归 P3A 文本修复并验收 P3B/C。之后
仅剩 Gate D。这个合并只减少实操次数，不合并代码职责，也不允许用 B/C 的完成掩盖
P3A 文本回归失败。

## 2. P3A — UI Foundation

P3A 建立真实可操作的 UI 主路径，不追求最终视觉：

- 固定 `135×240` 逻辑竖屏、耳机孔朝上，不启用 IMU 自动旋转；
- 页面固定为 Player / Playlist / Library / Settings；
- `/Music` 一级可见目录映射为曲库，根目录 MP3 映射为合成曲库“未分类”；
- Playlist 支持进入子目录，选歌继续使用当前文件夹非递归 Queue；
- Player / Settings 仅提供清楚的功能占位，不提前实现 P3B/C/D；
- 浏览和跨页不停止音频；
- 只接通普通页面方向键、Enter、Esc 和 Library `S`；Player 3×4 完整控制不在 P3A 实现；
- 建立基础文本布局能力：按实际可用像素宽度和当前字体度量，而不是固定字符数，
  对 UTF-8 文本安全换行 / 截断，并以绘制区域裁剪兜底；曲库名、目录名等内容不得
  越出 `135×240` 画布。无空格长名称（例如 `ADVWalkmanBenchmark`）也必须能在
  两行内换行，末行仍放不下时才显示省略号；
- 不使用全屏 RGB565 Sprite，只使用 Dirty Region 与小缓冲。

Gate A-fix+B+C 真机确认文本不越界后，P3-01 / P3-08 / P3-09 才完成；P3-07 只完成
可用骨架，保持 `DOING`，最终黑胶视觉在 P3D 验收。

## 3. P3B — Now Playing Chrome

- 实现 Title / Artist Header、克制的长标题滚动；
- 当前 Header / Content / Footer 为 28 / 188 / 24 px，边距 6 px；Title14 / Artist12 / 时间10 px；
- 真实时间、进度、状态与紧凑模式标识，不常驻 NORM Original；透明细音量浮层 3 秒；
- 复用 P3A 文本布局能力：Header / Footer 先保证不越界，Title / Artist 再按本阶段
  规则决定单行静态或 24 px/s 滚动、首尾各停 5 秒；Artist 单行省略，暂停不冻结标题；
- 建立 Player 页面 Dirty Region，Content Stage 仍允许使用明确 fallback；
- 不在该阶段伪造歌词、封面或字体资源。

P3B `0.6.0-p3b.chrome` 已完成代码、10 项 PC 检查、编译期时钟 / 音量公式断言，
Dev / P3A Gate / P2 Gate 三环境构建均通过。只生成本地固件，没有复制 SD 或安装。
按路径 Metadata、局部行缓冲、动画时钟与浮层测试支持由 B 完成；完整时钟 / 像素
检查仅编译，实际显示和音频条件仍待 Gate A-fix+B+C。
P3A/B/C 均为 DEVICE TEST；P3C 本地实现与六环境构建完成，不得提前标记 DONE。

## 4. P3C — Media Resources（含 0.7.0～0.7.2 历史实施）

2026-08-27 初版实施基线为 `61692f0`；修复基线为 `991d54c`。当前选择：长句先展开多列；暗色
首句预览；同语言从右向左续列；仅极长句自动阅读分页；使用 Crucifix X 官方单曲
封面；中文右 / 原文左；联合 PASS 后 Enter 回到暂停的普通曲库界面，无需另装 Dev。
联合验收保持 Gate A-fix+B+C，自动部分目标 3–5 分钟，用户确认时间另计。
本轮先生成本地资源和固件，用户确认 SD 在 PC 后才同步，不改变 MP3 或清理其他文件。

- 完成 LRC 解析、双语时间轴配对、竖排歌词与 SD 字体；
- 用正式中日文字体的实际 glyph metrics 完成 UTF-8 / CJK 排版适配；不得按字节数
  截断，也不得切断多字节字符；Lyrics Renderer 继续使用其独立竖排规则；
- 完成 PC Color ASCII Cover 批处理、Preview、`.cover.adv` 和设备 Renderer；
- 完成 `preferredNowPlayingView`、Lyrics / Cover Selector 与 Session 持久化；
- 使用用户提供的 Crucifix X 日文 LRC 与本地中文翻译作为首个真实资源 Gate；
- 歌词、封面源图与字体默认保存在 Git 忽略的本地资源区，许可证明确后才考虑提交。

P3C 实现说明见 `P3C_IMPLEMENTATION.md`，构建记录见 `P3C_VALIDATION.md`。
本地 15 项媒体检查 + 10 项 P3B 检查、Session 自测与 29 组真实歌词字模检查通过；
2026-08-27 用户确认 SD 在 PC 后，联合包已同步并核对 Hash；下一步是真机验收，
不重新规划或提前实施 P3D。
字体采用独立 B 盘工具环境、SD VLW + 索引；媒体读取分步执行，绘制不访问 SD。
联合 Gate 同时包含冷资源加载和完整帧刷新负载，不放宽 44.1 kHz / Error=0 /
Backpressure=0 / PCM gap≤70ms。脚本 Pause/Seek/重启前提示，重启后验证 Paused 和
View；PASS 后 Enter 进入普通曲库，同版本不再自动跑 Gate。按用户追加的清理要求，
SD 上旧 Dev / P2 Gate / P3A Gate 三个 BIN 已备存 PC 后移除，只保留联合 Walkman
安装项；其他固件、原曲和状态不动。备存位置及交付记录见 `P3C_VALIDATION.md`。

`0.7.0` 首轮联合真机为 A PASS / B SKIPPED / C 资源失败，不能按初版构建结果收口。
`0.7.1` 先前置检查资源，再一张提示卡说明单键导航；A 只人工 Left/Right/Enter、
Up/Down/Enter 和播放后 Esc，其余页面返回由 Gate 自动回归。B/C 的提示卡与真实
Header / 歌词 / 封面互斥，真实展示时没有横向确认文字。取消歌词横移及“前奏”标签。
仍只更新同一个 P3ABC BIN，不新增安装项、不重抄媒体，不进入 P3D/P4。

`0.7.1` 实机在 105 ms 时因 Gate 阶段时间下溢误报 preflight 超时，A/B 未执行；
`0.7.2-p3c.timer` 仅修复这一计时缺陷，并覆盖同阶段 / 换阶段 / 真实超时 / 时钟回绕。
阶段 45 秒、自动总计 5 分钟和既有通过条件不变；A-fix+B+C 仍待本版本真机通过。

## 5. P3D — Product UI Completion

- 将 P3A 曲库骨架升级为上方独立曲库封面 + 下方黑胶堆叠选择带；
- 完成短动画、上浮高亮、圆弧短名和真机像素校准；P3D 只校准字号、行距、角度、
  留白和截断阈值，不承担修复基础文字越界；
- 完成 Brightness、Screen Timeout、About / Version、Return to Launcher；
- 完成 Screen-off Soft Lock：首次按键只唤醒并吞掉事件；
- 做最终 UI / Audio 联合回归，不扩大到 P4 DSP 或完整 Player 3×4 控制。

## 6. P3A Historical Gate and Combined Closure

Gate 使用真实 Library / Playlist / Player，不依赖串口命令。屏幕逐步提示：

```text
确认竖屏方向
→ Library Left / Right / Enter
→ Playlist Up / Down / Enter
→ 播放 10 秒
→ Player Esc
→ Playlist Esc
→ Library S
→ Settings Esc
→ PASS
```

最终日志为 `/ADVWalkman/logs/p3a-last.txt`。Build Success 只进入 `DEVICE TEST`；
必须确认显示方向、真实按键、跨页播放、日志以及真实长曲库名不越出画布后才能完成
P3A。功能 Gate 即使为 `PASS`，若仍存在可见文字越界，P3A 仍保持 `DEVICE TEST`。

`0.5.1-p3a.gate` 已完成上述功能步骤并产生 `PASS` 日志；后续不再要求用户单独安装
一个只修换行的 P3A 固件。Gate A-fix+B+C 必须先执行 P3A 回归项：显示真实
`ADVWalkmanBenchmark` 曲库名、确认两行布局 / 末行省略和画布裁剪正常，再继续验收
P3B Header / Footer 以及 P3C 字体、歌词、Cover 和 View Selector。任一 P3A 回归项
失败时，P3A 继续保持 `DEVICE TEST`，但日志必须将其与 B/C 失败分开归因。

## 7. Stop Conditions

- 不更改 P1 Queue / Session 与 P2 current-folder Queue 语义；
- 不重新选择 Audio Backend；
- 不为 UI 扩大 Launcher 分区；
- 不猜 Flash offset，不 erase，不修改 Partition Table 或 eFuse；
- P3A 失败只诊断本阶段 UI / Input / Library 接口，不重建烧录环境。

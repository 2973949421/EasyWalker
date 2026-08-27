# P3 UI Delivery Roadmap

> 本文是 P3 的唯一分阶段执行明细。产品行为以 `PRD.md`、正式技术接口以
> `TECH_DESIGN.md`、任务状态以 `TASKS.md` 为准。P3 不重新选择 Audio Backend，
> 不改变 P1 Queue / Session 或 P2 Library Engine 语义。

## 1. Frozen Delivery Order

| 阶段 | 对应任务 | 交付内容 | 真机 Gate |
|---|---|---|---|
| P3A | P3-01、P3-08、P3-09；P3-07 功能骨架 | 竖屏 Shell、四页路由、可用曲库 / 播放列表、跨页播放 | 独立 Gate A |
| P3B | P3-02 | Now Playing Header / Footer、歌曲信息、进度与状态反馈 | 与 P3C 合并 |
| P3C | P3-03～06、P3-12 | 字体、歌词、ASCII Cover 工具与 Renderer、View Selector | Gate B+C |
| P3D | P3-07 视觉完成、P3-10、P3-11 | 黑胶曲库、独立曲库封面、设置、息屏 Soft Lock、最终 UI 校准 | Gate D |

执行顺序固定为 `P3A → P3B → P3C → P3D`。更改顺序、合并产品范围或提前实现
后续阶段必须先获得用户确认。为减少用户安装次数，工程提交仍按 A/B/C/D 分开，
真机安装采用 A、B+C、D 三个 Gate。

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

P3A 真机通过后，P3-01 / P3-08 / P3-09 完成；P3-07 只完成可用骨架，保持
`DOING`，最终黑胶视觉在 P3D 验收。

## 3. P3B — Now Playing Chrome

- 实现 Title / Artist Header、克制的长标题滚动；
- 实现时间、总时长、进度、Play Mode、Sound Preset、Volume Footer；
- 复用 P3A 文本布局能力：Header / Footer 先保证不越界，Title / Artist 再按本阶段
  规则决定静态、两行或克制滚动；状态和数值字段保持单行并在必要时省略；
- 建立 Player 页面 Dirty Region，Content Stage 仍允许使用明确 fallback；
- 不在该阶段伪造歌词、封面或字体资源。

## 4. P3C — Media Resources

- 完成 LRC 解析、双语时间轴配对、竖排歌词与 SD 字体；
- 用正式中日文字体的实际 glyph metrics 完成 UTF-8 / CJK 排版适配；不得按字节数
  截断，也不得切断多字节字符；Lyrics Renderer 继续使用其独立竖排规则；
- 完成 PC Color ASCII Cover 批处理、Preview、`.cover.adv` 和设备 Renderer；
- 完成 `preferredNowPlayingView`、Lyrics / Cover Selector 与 Session 持久化；
- 使用用户提供的 Crucifix X 日文 LRC 与本地中文翻译作为首个真实资源 Gate；
- 歌词、封面源图与字体默认保存在 Git 忽略的本地资源区，许可证明确后才考虑提交。

## 5. P3D — Product UI Completion

- 将 P3A 曲库骨架升级为上方独立曲库封面 + 下方黑胶堆叠选择带；
- 完成短动画、上浮高亮、圆弧短名和真机像素校准；P3D 只校准字号、行距、角度、
  留白和截断阈值，不承担修复基础文字越界；
- 完成 Brightness、Screen Timeout、About / Version、Return to Launcher；
- 完成 Screen-off Soft Lock：首次按键只唤醒并吞掉事件；
- 做最终 UI / Audio 联合回归，不扩大到 P4 DSP 或完整 Player 3×4 控制。

## 6. P3A Gate

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

## 7. Stop Conditions

- 不更改 P1 Queue / Session 与 P2 current-folder Queue 语义；
- 不重新选择 Audio Backend；
- 不为 UI 扩大 Launcher 分区；
- 不猜 Flash offset，不 erase，不修改 Partition Table 或 eFuse；
- P3A 失败只诊断本阶段 UI / Input / Library 接口，不重建烧录环境。

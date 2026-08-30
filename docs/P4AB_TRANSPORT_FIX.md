# P4A+B Transport Fix — 0.9.2-p4ab.transportfix

## 范围与真机诊断

基线为`f08b0b9` / SD上的`0.9.1-p4ab.fix`。用户在AveMujica末项
`/Music/AveMujica/twomoons.mp3`复现Previous/Next多次无响应并按T保存。当前P4日志证明：

- Queue共11首，当前索引10，`Repeat Off + Shuffle Off`；
- 初始统计Previous请求28、接受6，Next请求13、接受1，Transport失败34；
- 事件环包含对应按键捕获，故障不在键盘采样或页面路由；
- Audio Error / Backpressure为0 / 0，PCM峰值54.990ms，音频仍健康；
- 故障附近Ticket 3和4均有`SAVE_BEGIN`、`SAVE_END Succeeded`，本次保存证据完整。

根因是手动导航错误复用了自然EOF边界：Normal末项Next被拒绝；直接从列表选中末项时
History为空，Previous在5秒回零规则之后也被拒绝。

## 修复合同

手动按键与自然播完分离：

| 模式 | 手动Previous / Next | 自然播完 |
|---|---|---|
| 列表单次（Normal） | 顺序首尾循环 | 末项停止 |
| 单曲循环（Repeat One） | 顺序首尾循环，模式保持 | 当前曲重播 |
| 列表循环（Repeat All） | 顺序首尾循环 | 末项回首 |
| 随机循环（Shuffle） | Next沿随机order；Previous优先真实History，无History时选择随机项 | 一轮完成后重排下一轮 |

所有模式继续保留“播放位置大于5秒时Previous先回本曲0秒”的既有产品规则。手动切歌
保持Pause状态，成功后才请求checkpoint；Queue / Session格式、音频Backend和缓存均不变。

Footer播放模式使用同一方形循环箭头轮廓：列表循环无内字、单曲循环`1`、随机循环`R`、
列表单次`S`；非法旧Repeat+Shuffle组合仍为`?`。仅重绘现有模式区域，不增加RAM。

## 曲库图片尺寸合同

0.9.1媒体脚本错误地为KINO保留了单独的135×173常量，而AveMujica、粤语迷幻和`熱・情`
均为135×154。0.9.2删除该例外：PC生成、验证和SD交付统一为135×154；顶部页面舞台仍为
174px，名称固定从y=174开始。今后新增曲库也必须通过全曲库同尺寸检查。

## 本地证据

- 107项主机状态、资源与回归检查通过；C++计时器13项编译期断言通过且旧实现负例被拒绝。
- `tools/prepare_expanded_libraries.py --verify`通过：37首、4张曲库图，四张LCOV均为135×154。
- 六个PlatformIO环境全部构建成功：

| Environment | BIN bytes | Static RAM bytes |
|---|---:|---:|
| player-dev | 797104 | 128624 |
| player-p3abc-gate | 797168 | 128624 |
| player-p3a-gate | 783920 | 122480 |
| player-p1-gate-a | 674880 | 55184 |
| player-p1-gate-b | 675760 | 55184 |
| player-p2-gate | 725264 | 152768 |

联合BIN为797168 bytes，SHA-256
`f800b8797cbbba20ed2da7ef0731962671b53f7d10dbeaf6321a1dffeb2503f8`，低于
`0x140000`。P3ABC静态RAM与0.9.1同为128624 bytes；媒体+事件仍为49080 / 49152 bytes，
没有增加图片缓存、Queue或Session字段。

## SD交付与DEVICE TEST

SD仅覆盖同名联合BIN和KINO曲库`cover.adv`；音乐、歌词、歌曲封面、Queue、Session和历史
日志不改。复制后核对为：

- `/firmware/ADV-Walkman-P3ABC-Gate.bin`：797168 bytes，SHA-256 `f800b8797cbbba20ed2da7ef0731962671b53f7d10dbeaf6321a1dffeb2503f8`；
- `/ADVWalkman/library-covers/folders/KINO/cover.adv`：135×154、41604 bytes，SHA-256 `42a745ca3eafee365ba77a06a5ec34d17287bb471a04f06a5e7d01a2da458188`。

旧0.9.1联合BIN和135×173 KINO LCOV按原Hash保存在Git忽略的精确恢复目录。

真机必须验证：

1. 在双月末项、列表单次模式下，播放不足5秒时Previous进入前一首，Next回到首项；两键不能再静默失败。
2. 播放超过5秒时Previous先回0秒，再次在5秒内按才进入前一首。
3. Repeat One下手动Previous/Next确实换歌且模式仍为Repeat One；自然播完只重播当前歌。
4. Repeat All末项自然回首；列表单次末项自然停止。
5. Shuffle连续Next覆盖完整一轮后继续下一轮，Previous按实际历史返回；刚进入Shuffle且无History时Previous也有响应。
6. 依次切换四态，确认方形循环图标分别为`S`、`1`、无内字、`R`，且歌曲、位置、Pause不因切模式重置。
7. 检查KINO与其他曲库图片高度一致，名称和轮盘位置不移动。
8. 最后按T保存并手动重启，核对最后成功Ticket对应的歌曲、位置、Pause、Queue与模式。

构建和主机测试不能代替以上真机操作。0.9.1日志中的输入70ms、模式Footer113ms、暖View
1257ms等既有超限继续保留，不能因本次边界修复写成已通过。

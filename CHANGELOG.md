# Changelog

## V0.10.0 — Fixed sound presets

Date: 2026-08-31

- 在Stereo→Mono下混后、既有3×1536 PCM提交前增加原地DSP：Original严格直通，Tape轻暖化，Radio带限/轻压缩，Vocal Clear轻推人声存在感。
- Tape、Radio、Vocal Clear统一使用-1dBFS无Look-ahead峰值保护；切换以20ms双链交叉淡化完成，不新增PCM、SD或全屏缓存。
- 播放器页9～12号键直接选择O/T/R/V；其他页面不泄漏音效Action，Footer只刷新第二枚圆形音效标识。
- Session v1长度不变，使用保留字节22保存音效；旧存档自然恢复Original，非法值只回退Original而不破坏歌曲、Queue、位置或Pause恢复。
- 增加DSP块耗时、首个实际PCM生效延迟、交叉淡化、限幅和安全回退诊断；主机回归及六环境构建完成后仍须耳机真机验收。

## V0.9.2 — Queue boundaries, mode icons and library cover contract

Date: 2026-08-30

- 根据0.9.1真机日志修复`twomoons.mp3`末项手动Next失败及直接选中末项后Previous无History失败；按键捕获正常、34次Transport拒绝与0/0 Audio Error/Backpressure明确区分。
- 手动Previous/Next与自然EOF分离：顺序模式手动首尾循环，随机模式Next沿随机order、Previous优先真实History且无History也不静默失败；Repeat One手动切歌后保持单曲循环。
- 自然EOF固定为列表单次停止、单曲循环重播、列表循环回首、随机循环开始新一轮；Queue/Session格式不变。
- Footer播放模式改为用户定义的方形循环箭头：列表循环无内字、单曲`1`、随机`R`、列表单次`S`。
- 删除KINO的135×173特例，全部曲库图片PC生成与交付合同统一为135×154；不增加图片缓存或设备RAM。
- 主机状态检查和六环境构建通过；双月边界、四模式图标与真机时序仍为`DEVICE TEST`，详见`docs/P4AB_TRANSPORT_FIX.md`。

## V0.9.1 — Reliable saves, warm lists and lyric layout

Date: 2026-08-29

- T保存增加独立、带CRC的`SAVE_BEGIN`/`SAVE_END`；状态保存和详细日志分别给出结果，详细快照溢出不再吞掉Ticket终态。
- 完整快照按≤768-byte段拆分，32项事件按每段4项输出；周期摘要改为60秒，新日志使用1MiB current/previous轮换并停止扩张旧P3日志。
- 删除16-byte RTC breadcrumb及阶段写入，只在SD启动摘要保留`esp_reset_reason()`；联合固件静态RAM较0.9.0减少32 bytes。
- Playlist暖返回保留六行最终文本、正式Title、Metadata状态与字体租约；同页移动仅刷新旧/新高亮，周期日志保持最低优先级。
- Lyrics在既有Glyph数组中按实际列数与最大高度水平/垂直居中；Playlist、Library、Settings的Tab统一回Player，Player Tab语义不变。
- KINO《Группа крови》双语24个时间点按284.003秒音频/LRCLIB对应时间轴重排；KINO曲库图替换为单张公有领域Victor Tsoi 1986肖像。
- 六环境构建和主机回归通过；真机时序、保存终态和《血液型》≤300ms听感仍为`DEVICE TEST`，详见`docs/P4AB_FIX.md`。

## V0.9.0 — Context controls and atomic playback mode

Date: 2026-08-29

- P3冻结为`FROZEN / UNVERIFIED / KNOWN ISSUES`；保留0.8.5尺寸、Hash、性能失败、唤醒/T证据缺口、LCOV/文件名逻辑缺陷和媒体未验收事实，不生成0.8.6。
- `InputRouter`改用明确`UiPage`上下文；Player只派发盲操区1～8，Playlist/Library/Settings在动作产生前隔离，9～12保持no-op，旧全局播放器快捷键不恢复。
- 新增Previous/Next：5秒规则、真实History、Queue order、Pause保持及Repeat One手动绕过沿用P1行为；只有成功动作才请求checkpoint。
- 新增原子`setPlaybackMode(RepeatMode,bool)`，固定Normal→Repeat One→Repeat All→Shuffle→Normal；一次模式Action只产生一次checkpoint，非法旧组合在明确操作时归一化。
- Footer只局部更新真实模式，Original音效标识不伪造P5能力；自由日志增加新Action请求/接受/失败、模式前后、Footer反馈和checkpoint修订。
- 自动检查、六环境构建、固件尺寸/Hash与SD交付结果见`docs/P4AB_CONTROLS.md`。构建成功只进入DEVICE TEST，P3旧问题继续单列。

## V0.8.5 — UI transactions, reliable checkpoints and low-overhead diagnostics

Date: 2026-08-29

- Library请求统一使用页面/选择/资源三重Token；旧封面结果只能释放，不能提交或恢复旧页面。名称、轮盘和174行封面全部同代次完成后才发布显示选择。
- 修复旧ready条带阻塞新浏览模型的调度顺序；显式有界工作调度替代`resourceTurn_`。Library连续2秒无进展执行一次局部恢复，再失败进入仍可Tab/Esc/左右操作的错误状态。
- Library/Playlist页面状态拆出控制器；Playlist六行模型及专属字体租约跨Player暖往返保留，Player清理不再淘汰列表字模。曲库三项CRC验证结果按本次启动缓存。
- 每主循环只做一次官方键盘硬件更新；保留固定16项事件队列、短按、组合键及唤醒吞键语义。
- T改为递增SaveTicket联合事务：等待Player与显示修订写回、流式诊断关闭和校验后才成功；重复T形成尾随事务，10秒无结果明确超时，所有页面统一反馈。
- 诊断由16KiB预格式化改为1KiB流式缓冲、单步最多512bytes、滚动CRC；Stage A每15秒摘要，T/错误/恢复保留完整快照和32项RAM事件。
- 新增生产状态类型编译期回归、旧卡死顺序、1000次切库、保存尾随/超时、日志完整性与字体租约检查。业务代码仍需Stage A真机日志和人工操作确认；P3A/B/C/D保持DEVICE TEST。
- 扩展私有媒体准备流程：新增KINO 8首及`熱・情`完整双碟29轨，同一NetEase ID配对GD音频和同步歌词；统一歌曲封面135×135中心裁切与曲库封面135×154，修正《約會》构图并对当前《禁色》文件组合加入+1500ms经验偏移。媒体同步不改变固件版本、Queue或Session。

## V0.8.4 — Owned image stripes, verified frame commits and three-record wheel

Date: 2026-08-29

- 修复content调用中新占用条带后仍落入Title/Footer的确定缺陷；唯一135×18 canvas保持尺寸，实际提交裁剪有效高度，等待时独占。
- 帧代次/起始行/连续提交合同，完整帧与局部浮层/占位分开；取消归还条带，错误一次受控重建。完整View后才保存偏好，唤醒必须由新完整画面证明。
- 暖返回保留正式Title和六行metadata标志；最终省略文本缓存、按字体分组请求、已知位图地址避免重复打开查询索引；优先实际待办，不机械交替空绘制。
- 曲库固定174px图/22px单行名/44px轮盘，长名复用Player滚动；非空固定三唱片，每张自己的蓝色弧形短名，1/2/多库映射正确；静止不重新播放动画。
- 四套曲库华文行楷/Kunstler字体离线轻加粗，44张实际字模预览；不改其他字体、歌曲封面、音乐、歌词和存档。
- 89项本地检查、生产C++旧缺陷负例、M5GFX实际行缓冲像素自检（仅编入，待真机执行）；分阶段/提交/睡醒日志仍使用16KiB。详细构建与交付见`docs/P3_RENDER_FIX.md`。
- A/B/C/D仍DEVICE TEST，严格性能未取得0.8.4真机数据；历史停音/失去输入UNVERIFIED，扬声器破音DEFERRED。不push、不烧录、不改分区/eFuse。

## V0.8.3 — Cooperative wake recovery, official titles and vinyl wheel

Date: 2026-08-29

- 睡醒取消/排空/恢复不在物理按键采样中执行；每步至多关闭一个资源句柄，旧帧和固定标记统一取消，真实歌曲位置恢复。
- 短按唤醒、整组按键释放后才执行，睡醒代次隔离；增加复位原因、16byte RTC阶段、唤醒/PCM里程碑与分阶段峰值。
- 六行正式标题查询复用12项缓存，选中优先、非选中行后补，播放状态变化不重建整个列表；暖返回与唤醒计时分开。
- 曲库名称华文行楷18/Kunstler22、弧形短名12/14；顶部完整合图不变，底部共同圆弧重叠黑胶，最多4帧，少于三曲库不复制。
- 四首MP3只校正官方Title，无重编码，11首标签/资源绑定已核对；路径、排序、歌词、歌曲封面及存档不改。
- 字模记录仍16bytes；11字体编号与8bit年龄压缩，缓存上限不变；覆盖度行段绘制和已有RGB565条带批量写入。
- 本地检查/构建/交付证据见`docs/P3_REFINEMENT.md`。唤醒停音异常根因尚未真机确认，A/B/C/D仍DEVICE TEST；扬声器破音DEFERRED。

## V0.8.2 — Reliable View requests and bounded media/storage work

Date: 2026-08-28

- 捕获式16项输入队列保留短按；View按最后完整视图请求目标，重复待完成请求合并，无加载提示，完成后保存偏好。
- Cover期间预备歌词，FIDX v2直接索引和配对验证，兼容旧索引；18px图像条带填充/提交互斥，暖曲库不重校CRC。
- 列表逐区准备、选中行优先、去掉冷字模查询20ms节流；音频/存档分离，同长度非当前槽原位写，写/Flush/关/回读各自分步。
- 新增输入/View冷热/存档阶段/音频源读取诊断；严格保留70ms/100ms/200ms要求。
- 全宽完整五人曲库图、居中名称和紧凑唱片；10首中文译文逐句复核，298组原文时间记录不改；Crucifix X无重编码迁入AveMujica，旧benchmark退役但PCFixture保留。
- 本地检查与六环境交付记录见`docs/P3_PERFORMANCE_FIX.md`。P3A/B/C/D保持DEVICE TEST，不以编译声称真机性能已通过；不push、不烧录、不改分区。
- 71项PC检查及编译期回归通过，六环境BIN均≤0x140000；媒体加输入48436 / 49152 bytes。SD核对交付23项，联合BIN778528bytes；旧benchmark5文件退役可从PC恢复，19个存档/日志文件Hash未变。

## V0.8.1 — P3 navigation, media prefetch and stable redraw

Date: 2026-08-28

- Tab仅导航当前歌曲目录/Player，不改transport；Enter保留重播。列表同窗口只画新旧
  高亮行，Metadata按路径核对；加载提示延迟250ms置于底部，最终省略文字先准备字模。
- 同曲离页暂停资源工作、保留索引与已验证封面；当前/下一歌词两槽交换、立即预取，
  两份紧凑布局共享字模，当前/下一/UI各自固定；增加512byte索引页缓存，不扩媒体预算。
- 修复占位绘制结束误关封面CRC文件；动态stride兼容旧图和全宽135px新图。11张ASCII
  从原有源图重制，未重新搜图或更换归属；居中Header、灰色首组、底栏两小标识。
- 独立保留字体/歌词/封面/导航首错，记录Tab前后状态、歌词目标/准备/提交时间、
  文件打开与索引命中、PCM峰值页面/代次；区分transport和persistence耗时。
- 67项本地检查通过，包含298组真实歌词双帧+UI上限9974bytes/110项。构建与SD记录见
  `docs/P3_OPTIMIZATION_FIX.md`；旧305.885ms PCM失败不豁免，A/B/C/D仍DEVICE TEST。
- 六环境最终构建通过；同名联合BIN及11张歌曲ASCII已同步SD并核对，字体无需更新，
  原音乐、歌词、存档和历史日志保持不变。

## V0.8.0 — P3D library records, settings and screen-off

Date: 2026-08-28

- 独立普通彩色LCOV曲库封面、两行名称、按字宽的弧形短名和最多三张黑胶；静止为主，
  选择时约160ms/最多4帧。AveMujica采用官方动画五人合图，不改变歌曲ASCII资源。
- 四项中文设置，默认亮度70%，两类时限默认Player180秒/其他30秒；独立DSPL A/B+CRC
  保存与回读，未改Player Session。已有字体完整覆盖新菜单，无需重新制作资源。
- 完整物理位图在动作层之前处理息屏/唤醒，首组键吞到全部松开；取消旧5秒时限，
  背光关闭仍服务音频与存档。返回Launcher动态查询TEST App，先Pause保存，错误不重启。
- 保留P3C列表返回、目录选歌、14px时间修复；共享行缓冲重设尺寸、页面生命周期释放，
  输入/保存长工作后返回音频服务。未改变PCM70ms/歌词100ms/延迟200ms标准。
- 自由日志新增D覆盖、显示恢复、唤醒位图/抑制动作、封面/设置耗时及返回错误；
  日志写失败结束手动保存等待，避免返回Launcher卡在等待状态。
- 本地检查、预览和构建见`docs/P3D_IMPLEMENTATION.md`。A/B/C/D仍待真机与人工确认；
  同一个P3ABC BIN合并验收，不进入P4、不push、不烧录或改Flash布局。

## V0.7.6 — P3C navigation, clean page lifecycle and readable time

Date: 2026-08-28

- Esc立即进入列表上下文，取消旧媒体并清屏后异步打开目录；加载中可返回，失败显示
  原因并支持Enter重试/Esc返回。导航代次隔离旧结果，5秒无进展单列错误。
- 六行列表分步取项、加载字模和区域绘制，移除每次预热95个ASCII；切换选中项不再
  整屏清空。保持原当前文件夹selectTrack和Queue pin，不增加实体上一首/下一首。
- Footer时间10→14px Times；修复把屏幕坐标用于18px行缓冲的字体自检，保留正反例。
  字模重载在准备阶段先确认字体文件，保留原读取阶段的防错检查。
- 日志增加实际页面完成、目录请求/错误、选歌成功/队列数量、媒体归属及模块耗时；
  不只凭按键事件算导航通过。列表上的字体错误也记录，音频/显示阈值不放宽。
- 本地验证和构建见 `docs/P3C_NAVIGATION_FIX.md`；不复制SD、不烧录、不改媒体/存档。
  A/B/C仍DEVICE TEST；下一步仅到独立P3D规划入口，后续一次安装合并验收。

## V0.7.5 — P3ABC closure, typography and real music library

Date: 2026-08-27

- Lyrics去掉Header，216px内容区配18px微加粗楷体/14px Times；Cover保留28/188/24。
  所有正常UI统一实际SD字体度量，保留整词换列与透明音量浮层，无前后句/前奏标签。
- 7种字体、4-bit RAM覆盖缓存、相交像素绘制，仍≤48KiB媒体预算与135×18行缓冲。
  音量上限raw102，启动逻辑80/raw32；100%对应最初未限幅版40%，不是声压安全保证。
- 新增10首320kbps/44.1kHz MP3、9份中文LRC与逐句审阅稿；暗黑天国无歌词。
  9张匹配内嵌封面、Octagram Dance使用官方Completeness通常盘；默认40×32真实ASCII。
  原输入、benchmark及日文不改；私人媒体和字体不入Git。
- UI读取/绘制分轮，burst软预算6ms，字模只画相交区域；记录分模块耗时。
  0.7.4约10分钟日志PCM70.494ms仍超70ms，阈值不变，不凭代码优化宣称已修复真机峰值。
- 自由日志按boot_id跨重启追加；T先等状态保存，Host比较用户手动重启前后歌曲/位置/
  视图及暂停静音。启动媒体自检不是播放中Seek测试，不自动操纵播放或重启。
- 本地检查、六构建和SD交付见P3ABC_CLOSURE / P3C_VALIDATION；A/B/C保持DEVICE TEST。

## V0.7.4 — Current lyrics, whole words and calibrated volume

Date: 2026-08-27

- 按用户真机反馈去除暗色前后句和前奏首句预览；只显示当前完整双语组，CJK14→16，
  六列、174px可用高度；当前长句正常续列 / 分页。英文单词优先整体换列，修复n / ever
  这类不必要拆词；超长词才逐字折列，保持旋转竖排。
- 音量显示0～100%映射M5.Speaker raw0～63，新100%为旧约25%，启动50%为raw32。
  全部256逻辑档位编译期校验单调 / 限幅；不更换Backend、不保存旧高音量。
- 原官方封面默认30×24→34×26，收紧字格空白 / 密度拟合；仍为真实彩色ASCII，
  不增加ACOV文件大小、设备画布或内存。歌词原文、译文、时间戳、MP3本轮均不改。
- 0.7.3日志45份完整检查点，42次自然换句，呈现91.890ms / 延迟141ms，Audio Error0 /
  Backpressure0；但PCM93.100ms仍超70ms。重复Esc不再重启同一待完成目录扫描，
  不声称此防重入已证明解决音频峰值。阈值不改，A/B/C仍DEVICE TEST。
- 维持普通界面自由试用与后台日志；构建 / SD交付事实见P3C_VALIDATION。

## V0.7.3 — Fair media loading and free-session usability

Date: 2026-08-27

- 按 0.7.2 真实日志定位封面帧优先导致冷歌词饿死；音频仍44100 / 零错误 / PCM42.404ms。
  修复 worker 公平调度和 Loading ≠ Missing，实际 C++ 回归包含旧算法失败反例。
- 完整双语首句复用正式布局；28/188/24 三段，CJK14、七列、下留白8px，标点竖排。
  仅清理本地中文译文冗余破折号 / 标点，不改日文原文、时间戳或 MP3。
- 透明3px音量条与数字，重建覆盖的当前媒体小区域；去常驻 NORM Original。
  按用户确认只前置冻结的 Play/Pause 与 Vol+/−，不扩大到其余 P4 / DSP。
- 资源读取每步 ≤512B，UI burst 16ms软预算 / 最多64个小工作；保留135×18行缓冲。
  音频70ms、歌词100ms呈现 / 200ms更新条件不变，超时仍记录失败。
- 同名联合 BIN 改为普通界面自由试用，后台15秒 / T追加CRC日志；不自动暂停、切视图、
  Seek或重启。INCOMPLETE不是失败或通过；READY_FOR_REVIEW不是人工验收完成。
  脚本Seek / 重启偏好检查显式未执行，保留待验；不凭构建将 A/B/C 标DONE。
- 自动验证、构建和SD交付事实见 `docs/P3C_VALIDATION.md`；不操作设备Flash或推送Git。

## V0.7.2 — Fix P3ABC phase-transition timeout

Date: 2026-08-27

- `0.7.1` 真机累计 105 ms 即失败；输入和文件名额自检通过，媒体仍 Loading，A/B
  SKIPPED。已留存原日志；这是 Gate 计时下溢，不是 SD 或耳机诊断结果。
- 阶段切换后不检查旧阶段时间，超时使用工作结束后的新时刻；不覆盖 PASS/FAIL 终态。
  45 秒阶段 / 5 分钟总时限和 70/100/200 ms 音频 / 显示阈值均未放宽。
- 同一 C++ 超时函数的 13 项编译期断言覆盖阶段切换、真实超时和 millis 回绕；旧算法
  被同一测试拒绝。只重建联合 Gate，资源、Dev/P3A、音频及 Queue/Session 不变。
- 验证 / SD 交付记录见 `docs/P3C_VALIDATION.md`；仍需真机联合验收，不提前 DONE。

## V0.7.1 — P3ABC single-key / complete lyric frames / resource diagnostics

Date: 2026-08-27

- 留存初版 A PASS / B SKIPPED / C 资源 FAIL；资源 Hash 正确，旧日志不能将根因唯一
  归于文件名额。没有擦 Flash、改分区、重做资源或改变音频链路。
- 官方键盘物理位图 + 每键 25 ms 去抖，方向 / Esc 单键，等数量换键不漏报；无完整 P4。
- 字模按实际尺寸缓存，整组准备、pin、固定帧后快速条带显示；取消横移与前奏文字。
  修复空字模在 arena 压缩时的别名风险，以及缺封面时等待不存在像素行的问题。
- Gate 提示卡与真实媒体互斥；资源前置检查先于人工导航，A 简化，B/C 保留严格连续窗口。
- SD max_files=12；新增全局 FatFs 开销精确为 28,959 bytes，和 ≤48 KiB 媒体工作集分开
  核算。Cover/LRC 读完及时关闭；打开、读取、校验错误可归因，失败先快照再清理。
- 保留 70 ms PCM、100 ms 呈现和 200 ms 正常换句门槛；未测量写 NA，未执行写 SKIPPED。
- 自动 / 构建 / SD 交付结果统一见 `docs/P3C_VALIDATION.md`；A/B/C 仍 DEVICE TEST。

## P3ABC SD delivery / old BIN cleanup

Date: 2026-08-27

- 用户确认后将联合固件及字体、歌词、封面和辅助音频同步到 SD，核对复制 Hash。
- 按要求将旧 Dev / P2 Gate / P3A Gate 三个 BIN 备存 PC 后从 SD 移除；其他固件、
  原曲和状态不动。没有操作设备 Flash，P3A/B/C 仍为 DEVICE TEST。

## V0.7 — P3C Media / Combined Gate (device validation pending)

Date: 2026-08-27

- 固化七项已确认选择：长句先多列；前奏提示；同语言右起续列；极长句自动分页；
  官方 Crucifix X 单曲图；中文右 / 原文左；PASS 后回普通曲库试用，不另装 Dev。
- 独立 B 盘 `.venv-media` 固定 Pillow 12.3.0 / fontTools 4.63.0，生成本地私有
  CJK 12/14/16、Latin 12 的 VLW / 索引与缺字报告，保留日文原稿和原 MP3。
- 字符 mask / 密度 / 原图颜色驱动三档 ASCII，120×144 保持比例；ACOV v1 RGB565LE
  头、长度与 CRC32 校验。输出小图 / 放大图，资源不提交 Git。
- 分步 LRC 索引、300ms 最近未用配对、竖排双语与阅读分页；Latin glyph 顺时针旋转。
  缺失 / 坏资源有 fallback，不干扰音频。SD 字模只在 service 读入缓存，渲染不读 SD。
- 修正每条带等待 20ms 的调度，冻结逻辑帧并公平绘制；避免长标题整串预取耗尽缓存，
  浮层变更在下一完整帧恢复当前内容。没有全屏 Sprite 或新的 Audio task。
- 缓存与文件 I/O 一并纳入 48 KiB 媒体预算；限制 Arduino 默认 4 KiB stdio buffer，
  用 17 KiB 字模缓存、独立 Latin advance 与紧凑位置记录避免长句缓存循环等待。
- 仅接通 Player View 实体键；Session v1 预留 byte21 保存偏好，旧状态兼容。
  不改变 Queue/Transport/音量，P3D/P4 保持原路线。
- 联合 Gate 复用 A/B 检查、真实媒体冷加载 / 切换、60 秒连续窗口、Pause/Seek、
  缺资源回退和一次重启。三份日志独立归因，C 重启前后证据保留，未执行标 SKIPPED。
- 本轮只准备本地固件和 package，不复制 SD、不烧录。构建和 PC 检查的最终记录见
  `docs/P3C_VALIDATION.md`；A-fix/B/C 尚需联合真机和人工显示确认，不能提前标 DONE。
- 本地 15+10 项检查、Session 自测和六环境构建通过；P3C 推进至 DEVICE TEST。
  Gate 含真正冷字体 / LRC / Cover 加载，确认时间另计，70 ms 音频条件不放宽。

## V0.6 — P3B Now Playing Chrome

Date: 2026-08-27

- `0.6.0-p3b.chrome` 完成 34 / 168 / 38 px 信息区布局、Title / Artist、真实进度与状态、
  长标题首尾各 5 秒 / 24 px/s / 最多 20 fps 滚动；Pause 不冻结动画。
- 音量从常驻 Footer 改为左侧 3 秒临时显示浮层；不接入 P4 按键、不改变音频音量。
- 复用 P3A 布局并扩展至 LovyanGFX 公共基类；完整标题按 UTF-8 安全小块处理，不在
  128-byte 缓冲边界截掉尾部。单个固定 4,860-byte RGB565 行缓冲替代 Player 整页秒刷。
- 按路径核对异步 Metadata，浏览目录变化不取消独立歌曲请求；保留原目录项接口。
  缺标签 / 标签损坏保留完整文件名回退，避免解析器较小的 fallback 覆盖长文件名。
- 本地检查发现并修正 Original 底栏宽度，完整名称需要 72 px；状态图形和模式独立布局。
  旧 Repeat / Shuffle 非标准组合显示 MODE?、日志保留原值，不更改播放语义。
- 10 项 PC 几何 / 参考 / 源码契约检查通过，构建期实际 timing / volume 公式断言通过；
  完整可注入时钟、M5GFX 像素裁剪、浮层背景恢复及独立 P3B 日志支持已编译，尚未执行真机。
- Dev / P3A Gate / P2 Gate 构建通过，大小 715,552 / 721,744 / 724,256 bytes；均低于
  1,310,720-byte 上限。Dev / P3A 静态 RAM 为 109,176 / 110,040 bytes；SHA-256 见 README。
- 没有改音频 Backend、Queue / Session、Library Engine、AGENTS 或 Flash；未复制 SD、
  未烧录、未运行历史 Gate。P3A/B 保持 DEVICE TEST，P3C 为 TODO，等待 Gate A-fix+B+C。

## V0.5 — P3A UI Foundation Development

Date: 2026-08-26

- 冻结 `P3A → P3B → P3C → P3D` 执行顺序，并新增 `docs/P3_DELIVERY.md` 作为阶段明细入口。
- P3A 范围固定为竖屏 UI Shell、四页路由、可操作的一级目录曲库、支持子目录的 Playlist 与跨页播放；黑胶视觉、歌词、ASCII Cover、Settings 和 Screen-off 按后续阶段推进。
- `/Music` 一级可见目录正式映射为曲库，根目录 MP3 使用合成“未分类”；子目录返回和当前文件夹 Queue 语义完成一致性修正。
- 项目 Agent 路由增加 P3 文档强制读取规则，防止上下文切换后跳步或漂移。
- 用户提供的 Crucifix X 日文 LRC 与中文翻译仅保存为 Git 忽略的 P3C 本地资源，不在 P3A 提前实现歌词渲染。
- P3A 已实现真实 `Player / Playlist / Library / Settings` 路由、ADV 物理坐标输入解析、一级目录曲库、子目录 Playlist、当前文件夹 Queue 与跨页连续播放；异步目录页在 Enter 后会保留用户意图并在数据就绪后继续。
- `player-dev`、`player-p3a-gate`、历史 P1 Gate A/B 与 P2 Gate 均重新构建通过；P3A Gate 为 705,120 bytes，占 `0x140000` Launcher 上限 53.8%。
- SD 中 marker / manifest 完整匹配的 P1/P2 测试夹具目录已安全删除；`/Music/ADVWalkmanBenchmark/benchmark.mp3` 与 Player 状态、缓存和日志均保留。
- `ADV-Walkman-P3A-Gate.bin` 已复制到 SD 并核对 SHA-256 `766d5c489d5a78ca2c00071eab41e8c402be55ee7538a970c72e0f88c6138744`；P3A 当前进入 `DEVICE TEST`，未提前标记完成。
- 首轮 P3A 真机反馈确认 `rotation 0` 在耳机孔朝上时上下颠倒、页面和 Gate 提示字号过小，而且最终状态缺少明确整屏反馈；这属于 UI / Gate 可用性失败，P3A 保持 `DEVICE TEST`。
- `0.5.1-p3a.gate` 将 Portrait 基线改为 `rotation 2`，精简页面文字、放大标题和列表，并把 Gate 提示改为清楚的 `STEP nn / 11` 双行大字。
- Gate 通过或失败后现在锁定整屏大号 `PASS / FAIL`；超时原因包含卡住的具体步骤。44.1 kHz、跨页连续播放、Audio Error、Backpressure 与 PCM gap 验收条件均未放宽。
- 修正版 SD 固件为 706,064 bytes，SHA-256 `d6b093d432f033e6e93d1e599555489d56d1dfbc9860eaeffcc9596239b21c9e`。
- `0.5.1-p3a.gate` 真机日志确认功能 Gate 为 `PASS`：方向键、Enter、Esc、Settings、
  页面路由、44.1 kHz 播放和跨页连续性均通过；但真实曲库名
  `ADVWalkmanBenchmark` 在卡片中央越出屏幕，因此 P3A 暂不标记完成。
- 文本排版职责现已固定：P3A 建立像素宽度 / 字体度量 / UTF-8 安全换行、末行省略
  和区域裁剪；P3B 复用到 Now Playing 并实现长标题滚动；P3C 完成正式中日文字体
  与 CJK glyph 适配；P3D 只进行最终字号、间距和曲库圆弧视觉校准。
- 为减少用户远程期间的等待和回家后的重复安装，后续工程仍按
  `P3A text fix → P3B → P3C` 分开实现与提交，但取消只修换行的独立 P3A 重装；
  下一次 Gate A-fix+B+C 将先回归 `ADVWalkmanBenchmark` 换行，再统一验收 P3B/C。
  这不放宽 P3A AC，也不允许在文本回归失败时提前将 P3A 标为完成。

### P3A Text Layout Fix — 2026-08-27

- `0.5.2-p3a.textfix` 新增无动态分配的 `UiTextLayout`：使用 M5GFX 实际字体度量、
  固定 128-byte 行缓冲、显式 / 语义 / UTF-8 边界换行、末行 ASCII `...` 与区域裁剪。
  无效 UTF-8 字节只在显示副本中替换为 `?`，保留源数据并标记诊断。
- Library 97×38 px 名称区改为两行；Playlist、Player、Footer、版本和错误等动态
  文本统一接入像素布局，保留原左对齐、配色、字号与页面结构，不提前实现 P3B/C/D。
- 修复渲染上下文引用局部 `LibraryDescriptor.name` 的悬空指针；上下文拥有曲库名与
  六个可见列表行的完整有界文本，避免旧 64-byte 截断切开 UTF-8。上下文为固定成员，
  不增加 loop 临时栈压力，也不缓存整个音乐库。
- Gate 等待返回 Library 后实际绘制再检查两行、宽度、截断和 UTF-8 / layout error；
  新日志与 `library_text_layout` 失败归因可由后续联合 Gate 复用，未放宽音频或导航条件。
- Dev / P3A Gate 均构建成功：708,176 / 714,608 bytes，静态 RAM 分别为
  102,536 / 103,400 bytes，固件均低于 1,310,720-byte Launcher 上限。SHA-256 记录于 README。
- 静态 Font0 几何核对、动态文本固定字符截断检查和布局对象无分配器符号检查通过；
  未声称已执行设备显示或音频回归。本轮未复制 SD、未安装、未改 Flash / Launcher，
  P3A 继续为 `DEVICE TEST`，待 Gate A-fix+B+C 统一真机确认。

## V0.4 — P2 Music Library Development

Date: 2026-08-26

### V1 Interaction / UI / Resource Baseline Update

- 用仅在播放器页面生效的顶部 3×4 盲操区替换旧数字列和 `H/L/Q/R/S/V` 全局快捷键；双 Play/Pause 扩大盲操命中区，四个 Sound Preset 改为四颗物理位置直选。
- `Play Mode` 产品动作收敛为 `Normal → Repeat One → Repeat All → Shuffle → Normal`；P1 内部 Repeat / Shuffle 两维模型和 Session schema 保持不变，由 UI 做原子映射。
- 页面层级收敛为播放器 / 播放列表 / 曲库 / 设置四页：播放器 Esc → 播放列表 → 曲库，曲库 `S` → 设置，曲库 Esc no-op；无额外 Home、独立 Queue 或独立 Sound 页面。
- 音频允许跨页面持续，完整播放控制只属于播放器页面；非播放器页面恢复 Arrow / Enter / Esc 普通映射。
- 曲库视觉冻结为上方独立曲库封面 + 下方黑胶唱片堆叠选择带；播放列表 V1 保持清楚的标准列表，具体像素和动画参数留给 P3 真机校准。
- 媒体资源改为 `/Music`、`/Lyrics`、可选 `/CoverSource`、`/ADVWalkman/covers` 分离保存，以相同相对路径 + basename 机械匹配；每首歌曲保存独立 `.cover.adv`，不做 Album / Folder 去重或模糊数据库。
- 多语言歌词采用基础 `.lrc` 加 `zh-Hans / zh-Hant / en / ja / ko` 后缀；曲库封面与歌曲封面相互独立，曲库封面最终命名仍待实现阶段冻结。
- V1 格式方向扩展为 MP3 / FLAC / WAV；现有 P1/P2 MP3 主路径和 Candidate A Backend 不变，FLAC / WAV 作为后续独立兼容任务接入，AAC/M4A 与 OGG/Opus 继续 Deferred。
- 本次只更新产品与技术事实源，不修改正在真机运行的 P2 Gate 固件、P2 状态或 Flash 布局。

### V1 UI Design Increment — Now Playing Dual View

- 将 Now Playing Content Stage 从“有歌词自动显示 Lyrics、无歌词显示 Cover”扩展为：有可用歌词时可通过 `V` 在 Lyrics / Color ASCII Cover 间切换，无歌词时保持 Cover-only。
- Header / Footer、当前歌曲、播放状态、进度、Queue、Sound Preset 与 Volume 不受 View 切换影响。
- 冻结 `preferredNowPlayingView = Lyrics | Cover`：默认 Lyrics，跨歌曲和重启保留；无歌词时临时退化为 Cover 不覆盖用户偏好。
- `V` 仅在 Now Playing 且有可用歌词时改变偏好；其他页面 no-op，Screen Off 时第一次按键仍只唤醒并吞掉事件。
- 技术方案保留现有两个 Renderer，仅增加薄型 View Selector；后续实现可复用 Session v1 预留字段与既有 cooperative A/B 保存，不新增状态文件。
- 本次只同步 V1 设计、Keymap 与 AC，不提前实现 P3 UI，也不改变当前 P2 `DEVICE TEST` 状态或既有产品路线。

### Library Engine

- 新增以 `/Music` 为边界的多层目录浏览、隐藏项与非 MP3 过滤、文件夹优先自然排序，以及当前文件夹非递归 Queue。
- 新增 cooperative `Open → Scan → Sort → Finalize` 扫描、4 个 SD session cache slot、3×32 项 RAM LRU page 与 Queue 生命周期 pin；不把整库路径常驻 RAM。
- 新增 ID3v2.3 / v2.4 Metadata reader，支持 ISO-8859-1、UTF-16 BOM、UTF-16BE、UTF-8、unsynchronization、extended header 与安全 fallback；APIC 只跳过。
- 新增 32 项 Recent Tracks 与 CRC32 A/B 双槽；累计 Playing 5 秒后记录，Pause 不计时，缺失路径读取时忽略；达到阈值后独立保留待发布路径，立即切歌也不会漏记或误记为新曲。
- 正式 Player 主循环从固定四路轮转改为 work-aware 调度：活跃目录扫描至少获得四轮中的三轮，Metadata / Recent 只有实际工作时才占后台轮次；当前曲目路径继续使用 RAM cache。
- 真机失败日志确认 1,000 项目录的旧实现存在确定性吞吐瓶颈：Library 每四轮前进一步，SD 随机读取名称的归并排序每轮仅移动一项，不能在原 Gate 时限内完成。排序改为扫描期动态 `SortKey` Scratch、24-byte prefix 快速比较、精确 SD fallback 和 750 µs 批量归并，缓存文件格式与 Queue pin 不变。
- Repeat One 自然 EOF 改为复用已验证 `Mp3Info` 和首帧位置快速重启，不再对同一歌曲重复完整 Probe / Seek；Open、Restart、PCM 提交和 service 指标改为显式 reset 的累计诊断。

### Validation Harness

- `0.4.4-p2.final-gate` 已通过最终真机与 Host Validator 验收，P2-01～P2-04 均为 `task_executed=1 / PASS`，无 `SKIPPED`、无主失败项；Gate 总时长约 78 秒。
- 连续长曲压力窗口仍使用 70,000 µs PCM 门槛，没有放宽回 100 ms：P2-02 / 03 / 04 最大 PCM 间隔分别为 60.317 / 61.796 / 61.796 ms，Audio Error、Backpressure、意外 TrackEnded 均为 0，最低 Heap 90,148 bytes。
- P2-01 的一次 112.933 ms PCM 间隔来自刻意的短曲 EOF / Track 切换生命周期，单独保留为 WARN；Repeat One 快速重启仅 8.832 ms。该阶段从设计上不冒充连续长曲压力窗口，日志与 Host Validator 均保留完整证据。
- 1,000 项扫描、排序、32 个代表分页样本、LRU / Queue pin、10 个 Metadata 案例以及 Recent 的 live publish / offline cold reload 全部实际执行；Recent A/B generation 37/36 与 CRC32 均通过主机解析。
- SD Gate binary 与本地 artifact SHA-256 一致：`727beed3024f1feab45d153cbfe3339b173104ec63c84ce81301ecc3103aeaa4`。P2-01～P2-04 正式标记 `DONE`。
- `0.4.2-p2.gate` 真机日志首次独立确认 P2-01 完整通过，并将唯一首个失败收敛到 P2-02：Library 单次扫描 92.6 ms、Player service 间隔 101 ms；当时仍为 Playing / 44.1 kHz、无 Audio Error / Backpressure，PCM 最大间隔 98.397 ms，P2-03/P2-04 因早停未执行。
- `0.4.3-p2.gate` 将千文件枚举从每项 `openNextFile()` 的 `stat + fopen` 改为官方 `getNextFileName(bool*)`，并复用 SortScratch 的 4 KiB 合并 `.dat` 小写，缓存格式不变且额外内存峰值为 0。
- `0.4.3-p2.gate` 真机已让 P2-01、P2-02、P2-03 依次 PASS：千文件阶段约 22 秒完成，Audio Error / Backpressure 为 0；但原三组 768-sample Buffer 仅约 52 ms 余量，53.942 ms PCM 间隔被用户实际听到为卡顿，证明旧 100 ms 音频门槛会产生假 PASS。
- 正式 Candidate A 保持 ESP8266Audio、M5.Speaker、三缓冲、downmix 与音量不变，将单 Buffer 从 768 增至 1536 samples，以约 4.5 KiB 静态内存换取约 104 ms 总余量；最终 Gate 使用 70 ms 缓冲感知 PCM 门槛。
- SortKey basename prefix 从 24 bytes 收敛至 20 bytes，2,048 项 Scratch 从 73,728 降至 65,536 bytes；精确 fallback、缓存格式和排序结果不变，回收的 8 KiB 足以覆盖音频缓冲增长并保留原 80 KiB 最低 Heap 门槛。
- P2-04 的 0.89 秒失败确认来自 Gate 在播放期间同步重载并逐个检查 31 条 Recent 路径。最终 Gate 先在长曲播放中验证一次真实 5 秒发布并捕获音频快照，再停止音频执行 32 项 MRU、缺失路径与 A/B CRC 冷启动验证，使测试生命周期与正式产品一致。
- 千文件设备复核由 1,000 次同步 `entryAt()` 改为覆盖首尾、分页边界和远端 LRU 的 32 个代表点，PC Fixture 继续全量校验；减少 Gate 自身制造的 SD 负载。
- 单个 Player service >100 ms 改为明确 `WARN`，连续三次 >100 ms 或单次 >500 ms 才是调度硬失败；PCM submit >100 ms、2 秒无推进、Audio Error、Backpressure 与意外 TrackEnded 继续保持硬失败。新增目录读取、过滤、追加、批量写、close 和 EOF 收尾分项指标，下一次异常可直接归因。

- 新增 marker-owned P2 Fixture 和主机 validator，覆盖多层中文/日文路径、1,000 项大目录、自然排序、过滤、ID3 编码与 Recent/Cache binary CRC；测试数据与 MP3 不提交 Git。
- 新增 `player-p2-gate`：一次按 `T` 自动覆盖 P2-01～P2-04，停止音频后再写四份完整日志。
- 根据连续真机失败日志重构 Gate：P2-01 完成后经正式 Library Queue 切换到约 299 秒 benchmark，清零测量指标后再执行 P2-02～P2-04；测量期间不刷新屏幕，整轮 watchdog 为 240 秒。
- `M5.Speaker.isPlaying(0)` 已确认只反映请求槽占用，不再作为硬件断音硬判据；Gate 改用 PCM Buffer 提交进度、PCM / Player service 间隔、Audio Error、Backpressure 与 TrackEnded，并把请求槽空样本仅保留为诊断。
- 日志增加 task executed/skipped、唯一失败 task/phase/reason 和分任务指标快照；早期失败不再把未运行的任务和 Recent A/B 文件伪报成独立失败。
- `player-dev`、P2 Gate 和两个历史 P1 Gate 均保留独立构建入口；P2 不改变 Candidate A Audio Backend、P1 Queue / Session 语义或 Launcher 分区。
- PC Fixture、静态校验与自动构建已通过；P2-01～P2-04 当前统一处于 `DEVICE TEST`，必须以四份真机日志为准，尚未标记 `DONE`。

## V0.3 — P1 Player Core Development

Date: 2026-08-25～2026-08-26

### Player / Audio

- 新增隔离的 `player-dev` PlatformIO 环境，正式 Player 不编译 P0 Candidate B/C 或 IDF5 实验源码。
- 从 Candidate A 提取 `Mp3PlaybackEngine` 与 3×768-sample `M5SpeakerPcmOutput`，保持 32-bit Stereo → Mono downmix 和 `128/255` 音量。
- 新增 CBR、Xing/Info、VBRI 与无 TOC VBR 的有界 Probe / Seek；保存 source offset 作为恢复重同步提示。
- 自然 EOF 先排空 M5.Speaker 尾部 Buffer，再发送单次 `TrackEnded`；截断文件和 Decoder / Read Error 不再冒充自然结束。
- 新增 Play / Pause / Resume / Stop / Next / Previous / Seek，以及冻结的 5 秒 Previous 行为。

### Queue / Persistence

- 新增最多 1,024 首的有界 Queue、Fisher–Yates Shuffle、Repeat Off / All / One 和 32 项 Previous history。
- 新增按需 `TrackSource`，不把 1,024 条完整路径常驻 RAM。
- 新增 Queue / Session schema v1 与 SD A/B 双槽、CRC32、完整 pair 回退、回读校验和每步不超过 1 KiB 的 cooperative 保存。
- 恢复统一为 Paused；启动不打开 Decoder、不自动出声。缺失当前歌曲时寻找下一首有效 MP3。

### Validation Harness

- 生成并归档到本地 / SD 的无版权 CBR 44.1 kHz、VBR 44.1 kHz、CBR 48 kHz 与真实截断 MP3 Fixture；Fixture 不提交 Git。
- 新增 P1 Gate A / B 开发测试状态机、设备端 Fixture SHA-256 核验、VBR Seek 后实际解码验证、串口 Transport 命令和 `/ADVWalkman/logs/p1-01-last.txt`～`p1-04-last.txt`。
- 新增只读 MP3 / 状态槽检查工具和统一 `ADV-Walkman-Dev.bin` 构建脚本。
- Gate A / Gate B 构建环境均已完成自动构建与 Launcher 体积检查；编译成功不代替 Device Validation。
- 首次 Gate A 真机运行暴露两个 Harness 问题：libmad 在正常 EOF 留下的 `MAD_ERROR_BUFLEN` 被误判为 Decoder Error；无版权 Fixture 约 `-40 dBFS`，叠加 `128/255` 平方音量曲线后几乎不可听。
- `0.3.0-p1.gate-a2` 允许正常 EOF 的 terminal `BUFLEN`，通过 Xing/Info/VBRI 声明字节数提前拒绝本轮截断 Fixture，并将 Fixture 提升到保守但可听的约 `-20.4 dBFS` Mono Peak；正式 Player 音量仍保持 `128/255`。
- Gate 测试画面改为仅在阶段切换时整屏刷新，消除原先每 250 ms 清屏造成的频闪；FAIL 日志在 Stop 前保存快照并增加 `player_error / audio_error`。
- 2026-08-26 Gate A 真机通过：三份合法 MP3、截断/缺失错误处理、Pause/Resume、Stop/Replay、Next/Previous、CBR/VBR Seek 全部通过；Fixture Hash 全匹配，最大 Seek 误差 60 ms、Backpressure 0，用户确认声音正常。P1-01、P1-02 完成，进入 Gate B 的 P1-03、P1-04 真机验收。
- 2026-08-26 Gate B 真机通过：Sequential、Shuffle、Repeat One/All/Off、Previous history 与模式切换断言全部通过；状态写入 SD 后受控重启，正确恢复第 2 首约 4 秒位置、Repeat All、Shuffle order/cursor/history，并保持 Paused 与至少 3 秒静音。
- 主机只读解析 `queue-a.bin / session-a.bin` 确认 schema v1、generation 1、CRC32、三首完整 UTF-8 路径和 Session 字段有效。P1-03、P1-04 完成，P1 Player Core 正式收口；下一阶段按既定顺序进入 P2 Music Library。

## V0.2 — V1 Design Baseline

Date: 2026-08-24

### Product / UI

- 冻结耳机孔朝上竖持的主要播放器姿态，V1 不启用 IMU 自动旋转。
- Now Playing 采用 Header / Content Stage / Footer 三段结构。
- 长标题先静止约 5 秒，再滚动一遍，再静止约 5 秒。
- 有歌词时优先显示歌词；无歌词时显示彩色 ASCII Cover。
- 歌词采用逐行 LRC，不做逐字 Karaoke。
- 上一组在左、当前组居中、下一组在右；换句整体向左移动。
- 外文歌曲支持 `.lrc + .zh.lrc` 的“原文 + 中文译文”双语模式。
- 中文 / CJK 默认楷体约 16 px。
- 英文默认 Times New Roman 约 12 px，每个字形单独旋转 90°后纵向排列。
- 大字体优先从 SD 加载，Flash 只保留最小 fallback。

### ASCII Cover

- 采用 PC 端机械批处理，不使用 Agent 逐张生成。
- 扫描封面文件或 MP3 Embedded Cover。
- 转换为彩色 ASCII / ANSI 风格艺术。
- 同时生成电脑 Preview 和 ADV 预渲染 RGB565。
- ADV 直接读取 RGB565，不实时执行图片→ASCII。
- 初始测试 26×20 / 30×24 / 34×26，默认先测 30×24。
- Pixel Cover 推迟到 Later。

### Other Screens

- Library：简单目录 / 歌曲列表，浏览期间当前音乐继续播放。
- Queue：简单队列列表，可直选歌曲。
- Sound：Original / Tape / Radio / Vocal Clear 四项。
- Settings：Brightness / Screen Timeout / About / Return to Launcher 等最小集合。

### Screen-off

- 息屏状态等同 V1 Soft Lock。
- Screen Off 时全部按键原功能失效。
- 第一次任意按键只唤醒并吞掉事件，第二次按键才执行功能。
- 正常播放默认约 15 秒无 UI 操作息屏；唤醒后约 5 秒无输入重新息屏。
- V1 不增加独立 Lock 系统。

### Documentation

- 修正 `REFERENCES.md` 的硬件事实优先级。
- 清理 PRD / Technical Design 中残留的 V0.1“待确认”表述，以已经冻结的 V0.2 Keymap、UI 和 Screen-off 规则为准。
- 明确 Lyrics 与 PC 预生成 Color ASCII Cover 属于 V1；仅传统图片 Album Art 直接显示推迟到 Later。
- V1 产品侧设计达到可正式交付 Codex 的基线。
- 下一正式开发阶段仍为 P0 Audio Backend Benchmark。

### P0-01 Benchmark Harness

- 建立 `bench-a`、`bench-b`、`bench-c` 三个 PlatformIO 环境。
- Candidate A 采用 ESP8266Audio 1.9.7、三缓冲 M5.Speaker 输出与 32-bit Stereo → Mono 下混。
- B / C 当前为明确的可编译占位，不伪装成已实现 Backend。
- 增加串口指标、最小控制命令、SD 测试文件 SHA-256 与错误状态输出。
- 增加只读 MP3 检查工具和 Launcher app 尺寸硬检查。
- 三环境自动构建通过。
- P0-01 已通过 M5Launcher、固定 320 kbps / 44.1 kHz / Stereo MP3、扬声器、3.5mm 耳机和串口指标真机验收。
- 设备端测试文件 SHA-256 与 PC 记录一致；Candidate A 运行于 44.1 kHz 且无启动错误。

### P0 Backend Route Refinement

- 保持既有 A/B/C Benchmark 和 V1 产品路线不变，将三个候选明确落实为隔离的 M5Launcher App。
- 删除低收益的 M4A 衍生、48 kHz / VBR 测试矩阵；P0 只使用已确认的 320 kbps / 44.1 kHz Fixture。
- 将 Restart、Seek、UI Stress、SD Stress 和统一统计协议纳入 P0-02～P0-06。
- Candidate C 固定为独立 pioarduino / IDF5 环境，不迁移或污染 A/B 稳定环境。
- Candidate A 补齐 Restart、Seek、Loop 和真实 `playRaw()` backpressure 统计。
- Candidate B 落实为 ESP8266Audio Direct I2S Port 1 + M5Unified 0.2.20 Cardputer ADV ES8311 官方初始化序列。
- Candidate C 落实为 BackgroundAudio 1.4.4 + Arduino-ESP32 3.3.8 / ESP-IDF 5.5.4，并提供统一 Mono downmix 包装。
- 增加统一串口控制、30 Hz UI Stress、第二只读文件句柄 SD Stress 和扩展统计字段。
- Candidate C packages 独立放在 `B:\PlatformIO\isolated\adv-walkman-c\packages`，避免替换 A/B 的稳定 framework。
- 三候选已通过自动构建和 Launcher 尺寸检查，状态进入 `DEVICE TEST`；最终 Backend 尚未选择。
- 根据首轮真机听感将 P0 收敛为 A/B 二选一；C 无明确听感收益且工具链、固件与维护成本最高，转为 `DEFERRED` 备用。
- 首次等响度固件将 B 向 A=`64/255` 下调，真机反馈两者均不舒适；改为以用户已听过的原 B 响度为基准，A=`128/255`、B=`0.25`，仍使用完整原曲各听一次。
- 将人工验收缩短为听感胜者约 3–4 分钟的 UI/SD 联合压力测试；长期稳定性转入 P1 实际使用验证。
- 因当前已安装应用串口没有返回可用响应，Candidate A 增加 `T` 键一键自动压力测试：Baseline 30 s → UI 60 s → UI+SD 60 s → Pause 3 s → Resume / Seek / Restart 各观察 10 s，总计约 3 分钟。
- 自动测试的压力阶段只读既有 Benchmark MP3、不写 Flash；测试前先写入并关闭 `RUNNING` 标记，结束并关闭 SD Stress 后再一次性覆盖 `/ADVWalkman/logs/p0-a-stress-last.txt`。结果页显示 `PASS/FAIL`、state / SR、heap delta / sampled minimum heap、backpressure、service max、UI frames、SD KiB 和日志状态，并以 `Listen: manual` 明确保留人工听感验收。
- 首次 `0.2.0-p0.a-stresslog` 真机运行停在 `BASELINE / PLAYING / SR=0`；持久化 `RUNNING` 日志确认首次 Decoder service 未返回主循环。
- 根因是 M5.Speaker 队列等待后通常返回成功，导致 ESP8266Audio 在 `ConsumeSample()` 持续为 `true` 时长期留在单次 `loop()`；A 改为每成功提交一个 768-sample Buffer 后合作式让出主循环，不丢样、不重复，也不伪增 Backpressure。
- 修复版 `0.2.0-p0.a-stresslog2` 构建为 648,224 bytes，在现有 640 KiB App 槽中余 7,136 bytes；SD 副本 SHA-256 为 `db2e2e76644ecaf40b04afb4a829fce77949a4edac70f7bc0f9bbf565c4d87eb`。
- 修复版完成 183,044 ms 真机自动压力测试：44.1 kHz、Heap delta 0、Backpressure delta 0、UI 3,870 frames、SD 额外读取 19,988,356 bytes，Pause / Resume / Seek / Restart 全部通过。
- 根据等响度听感、真机压力结果和维护成本，P0-06 冻结 Candidate A 为 V1 Audio Backend；B 保留为可工作备选，C 继续 Deferred，PRD 产品范围不变。
- Launcher 尺寸检查改为按环境使用真实分区上限：A/B 为 `0xA0000`，Deferred C 为历史 `0x3F0000`，避免 A/B 超过 640 KiB 时仍被构建误放行。

## V0.1.2 — Keymap Freeze

Date: 2026-08-24

### Changed

- 冻结 V1 Keymap 主体。
- 将“耳机孔朝上、设备竖持”确定为播放器的重要使用姿态。
- 方向键、Enter、Esc 等核心功能键保留 UI / 导航语义。
- 数字列用于高频播放和音效直选：
  - `0` Volume +
  - `9` Previous
  - `8` Play / Pause
  - `7` Next
  - `6` Volume -
  - `5` Reserved
  - `4` Vocal Clear
  - `3` Radio
  - `2` Tape
  - `1` Original
- 字母快捷键：
  - `H` Now Playing / Home
  - `L` Library
  - `Q` Queue
  - `R` Repeat Mode
  - `S` Shuffle
- 文本输入状态下，字母和数字恢复普通输入，不触发播放器快捷键。
- Lock、熄屏快捷键和长按 / 组合键细节留到 UI / System 阶段确认。
- UI 视觉设计仍待确认。

## V0.1.1 — Sound MVP Freeze

Date: 2026-08-24

### Changed

- 冻结 V1 最小可行音效方案。
- V1 固定四种互斥 Preset：Original / Tape / Radio / Vocal Clear。
- 用户不手动调 DSP 参数，参数由固件内置。
- Tape：轻度 EQ 染色 + 高频衰减 + 轻软饱和。
- Radio：约 200–5000 Hz 带通 + 轻压缩 + 轻软饱和。
- Vocal Clear：轻度突出中频和 2–4 kHz 人声存在感。
- V1 不做 Surround / Spatial Audio、Stereo Widening、复杂 Reverb、Wow & Flutter、Bitcrusher、Vinyl Noise。
- 音效复杂度不得牺牲连续播放稳定性。
- UI 和 Keymap 仍待确认。

## V0.1 — Development Baseline

Date: 2026-08-24

### Added

- 建立 ADV Walkman 项目文档基线。
- 明确项目定位：复古随身播放器 + 嵌入式音频学习。
- 冻结第一阶段硬件：Cardputer ADV 原生 ES8311 + 3.5mm + 现有旧耳机。
- 明确 V1 以 MP3 为主，目标最高 320 kbps。
- 明确音乐根目录 `/Music/`，允许自由多层目录。
- 明确开机恢复歌曲、位置、队列和播放模式，恢复后保持 Pause。
- 明确 UI、Keymap、具体 DSP 音效暂不冻结。
- 建立 P0 Audio Backend Benchmark：
  - Cardio-style M5.Speaker
  - Direct I2S
  - BackgroundAudio
- 建立开源方案参考：
  - Cardio
  - BrokenSignal / BrokenSignal Next
  - SomaFM for CardPuter
  - BackgroundAudio
  - ESP8266Audio
  - M5Stack / M5Unified / M5Cardputer
- 建立轻量 Backlog + AC。
- 建立 Codex / Agent 工作规约。

### Engineering Rules

新增项目级开发原则：

- 效率优先；
- 不过度设计；
- 不过度防御；
- 不把普通开发做成电脑攻防大战；
- 允许 Codex 自主新增依赖、合理重构、Commit 和正常真机 Flash；
- Build Success 与 Device Validation 分离；
- 小问题 Agent 自行解决，重大产品 / 技术取舍与用户沟通。

### Firmware

- 尚未开始正式 V1 firmware 实现。
- 下一主任务：P0 Audio Backend Benchmark。

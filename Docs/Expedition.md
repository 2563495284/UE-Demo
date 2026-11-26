# 远征岛

玩家从码头出发，读四个搜索圈与岛上地标，决定先走哪条路线、在哪里挖、何时与队友汇合。每局固定埋四只真箱。房主选择额外假箱数量。每只箱首次开启时，全房进入小游戏，比赛结束后揭晓真假。第三枚真箱碎片合成一把队伍钥匙，从四只不同真箱领过奖励后，队内海图画出 A–C 与 B–D 的交点。一名队员用钥匙打开那里的大奖，全房比赛结束。一个人也能用同样的规则通关。

## 从码头开始

1. 打开游戏后会自动取得 EOS 游客身份。房主点击「创建房间」，给房间起名。朋友使用同版试玩包点击「搜索房间」，从公开大厅按房名加入，可使用各自的家庭网络或热点。
2. 默认有八个玩家席位、八支可选队伍，各队可容纳八人。想一起找宝藏就选同一队。想分组比赛就选不同队，空队留在岸上。房主可以限制队数、每队人数和总人数；调整后的席位必须容得下已入座成员。房主也可选择额外假箱数，默认 4 只，可设为 0–24 只。
3. 在码头点击「选择角色」，从小猪、猴子、狐狸和小牛里点选一个。近景显示实际角色，回房间后队友也能看见。允许同款，纯外观选择不影响准备状态、队伍和操作能力。倒计时开始后锁定，返回房间可再次更换。来宾准备，房主确认开始。只有当前成员参与就绪检查。队伍选择可在开局前调整。影响阵容的设置变更后，来宾重新确认。
4. WASD 移动，鼠标观察。默认第三人称，按 V 切换第一人称，再按 V 切回。左键挖、右键丢土，都支持长按。面向脚边丢土可一层层垫高，空格跳跃。按住 Tab 看海图时，可以用鼠标转动视角，同时用 WASD 移动。靠近露出的搜索区箱子按 E。首次开箱会让全房进入小游戏，已揭晓的真箱可直接领取本队的碎片。在最终大奖旁按 E 撬锁，留在箱边。长按 R 回本队码头。Esc 打开菜单，按住 F1 可查看操作。
5. 打开大奖后所有端看到结算。房主选择「返回房间」即可与原成员再来一局。本局线索、宝箱与挖过的地形重新准备。房主解散房间会结束会话，来宾离开已开局的房间后其余人可以继续。

「游戏设置」在首页、房间、结算和 Esc 菜单均可打开。返回时焦点落回原入口，键盘可用 Tab、方向键、Enter 和 Esc 完成设置。房间中的每个人独立保存偏好。这些入口与焦点规则参考了 [Xbox 界面导航指南](https://learn.microsoft.com/en-us/xbox/accessibility/xbox-accessibility-guidelines/112)。

码头与海岛支持简体中文和英语，房间名和玩家名保留原文。挖地时可调视角灵敏度、反转纵轴或关闭凿岩和陷阱的镜头晃动，海图、准星和回音提示仍可提供线索。语言选择和输入设计分别参考了 [Epic 运行时语言管理](https://dev.epicgames.com/documentation/en-us/unreal-engine/managing-the-active-culture-at-runtime) 和 [Xbox 输入指南](https://learn.microsoft.com/en-us/xbox/accessibility/xbox-accessibility-guidelines/107)。

Mac 首次以 1280 × 720 窗口启动。切换窗口、无边框全屏或分辨率时先预览 15 秒，点击「保留并保存」才写入本机配置。可按 Esc 撤销，也可等倒计时自动恢复。实现复用了 [Epic 的游戏用户设置接口](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UGameUserSettings?application_version=5.8)。

## 在岛上怎么挖、怎么抢

抵达 A–D 任一搜索区，自行选择从地面往下挖，或从侧面凿进去。默认岛上每个圈的半径为 18 米。斜线区域表示待搜索的范围。本队领到该区的真箱奖励后，海图标出实心点。每局重新采样藏点，真交点距离四个公开圈心的连线交点至少 10 米。凿到埋箱附近会听见空响，越近声音越清晰，准星下也有提示。回音只给挖掘者。集齐本队四条线索后，交点附近的大奖才会产生回音。

每个搜索区都有一只真箱，同一区还可能挖出房主配置的假箱。首次开启前，两种箱盖外观相同。每只箱只触发一场小游戏。揭晓为真箱时，开箱队和每支小游戏胜队都取得本箱的坐标和一枚钥匙碎片。其他队的任一队员随后到这只箱边按 E，就能直接为全队领取同样的奖励。每队每箱只领一次。

第三枚碎片自动合成队伍钥匙，此时海图仍缺一个点。第四个真箱给出最后的坐标和一枚新碎片，海图才画出 A–C 和 B–D。前往两线交点挖出大奖，持钥的队伍可以开始撬锁。首支完成开锁的队伍消耗钥匙并获胜。

| 大奖箱边的局面 | 玩家可以做什么 |
|---|---|
| 箱子空闲 | 有四点和钥匙的队员按 E 开锁，保持在箱边且无遮挡，独自需要 4 秒。 |
| 队友正在撬 | 靠近按 E 合撬，两人以双倍速度推进，对手无法夺锁。主撬人离开后，助手接手。 |
| 对手独自撬 | 本队也已集齐四点并持钥时，按 E 可以夺锁。夺锁从一半进度开始。所有出力者离开时进度清空。 |

队员分头搜索时可按 G 放置持续 15 秒的集合点。平时头顶的玩家名、形状和颜色可以区分具体队友和对手。

## 开箱后的七种小游戏

任一玩家首次按 E 打开某只搜索区宝箱时，全房同时开始加载小游戏，冷色墨潮从视野边缘卷入。这时仍能跑动和转向。各端画面完全黑后才锁住角色，场地准备好再揭幕。回岛时暖色墨潮反向收拢，同样在黑场中交换角色。其中六种比赛按个人成绩争第一。房间里至少有两支队伍时，抽选池还会加入按阵营决胜的迷宫追捕。比赛种类不透露真假。[开箱与寻宝反馈的交互设计](远征岛开箱交互.md)

小游戏沿用进场时的视角。按 V 可随时在肩后第三人称与看得见双爪的第一人称之间切换，回岛后保留最后选择。小游戏用 WASD 移动、鼠标瞄准、空格跳跃。个人赛中，按住左键会沿准星落点连续凿进岩面，拖动准星可开沟，同一点多挖几次能打穿墙。按住右键可以把土堆高，也能沿地面或桥头铺成坡。其他人能从坡脚挖开你刚堆的土。站在场地上瞄准外岛，准星提示可以拆除时按左键，整座建筑会倒塌。挡在前面的墙会截住这一击。每座外岛的岩台仍留在原处，下次进入同一小游戏会重建建筑。跌出场地会重生，已经获得的分数保留。[准星瞄准高架桥](Screenshots/ExpeditionVenueIntact.jpg) · [同一机位看桥面与桥墩倒下](Screenshots/ExpeditionVenueDemolished.jpg)

| 小游戏 | 场上目标 | 胜负判定 |
|---|---|---|
| 穿礁竞速 · 45 秒 | 三道珊瑚墙横在跑道上，挖一条通道、用对手挖出的洞，或搭坡越墙。 | 先踏上尽头金线获胜，超时看已越过的墙数。 |
| 灯塔登顶 · 65 秒 | 三座石台逐级升高 3 米。沿台边堆可踩的阶梯，依次站上各层。 | 先在第三层站稳获胜，超时看最高已点亮层。 |
| 晶洞寻珠 · 50 秒 | 十六处亮标记下各有一颗金晶。可以抢挖同一矿脉，敲开泥壳后，敲碎金晶的人得 1 分。 | 先取四颗获胜，超时看所得金晶数。 |
| 断桥接力 · 70 秒 | 两道断口各宽四米。从低桩向上堆土，或沿桥头把土面向前延伸，接通两岸后亲自走到对面。别人可以过你的桥，也能挖断它。 | 先连过两段获胜，超时看已过桥数。 |
| 海堤补漏 · 50 秒 | 出生点正前方是自己的四条水渠。往渠中堆土，出水灯从蓝变金。别人挖掉最后一道土塞，水渠会重新通水。 | 自己每堵住一渠，每秒得 1 分，先到 48 分获胜。 |
| 潮滩争旗 · 60 秒 | 三座高台每 14 秒轮换一面亮金旗。搭路登台，独占金旗每秒得 1 分，第二个人站上来便暂停计分。 | 先到 12 分获胜；超时看累计独占分数。 |
| 迷宫追捕 · 180 秒 | 随机迷宫内，一队逃跑，其他队联合追捕。双方都能左键拆内墙、右键补回缺口。追方近身瞄准逃方按右键，一次砸中即抓获。被抓者原地出局，随后跟随存活队友的视角。 | 逃方先行 8 秒，再开始三分钟追捕。抓齐即由全部追方队伍获胜。时间到时只要还有一人逃脱，就由整支逃方队获胜。 |

迷宫从在场队伍中等概率抽一支当逃方。双方从平台两侧出发，追方每秒跑 6.15 米，逃方每秒跑 5.70 米。右键要在 2.1 米内直接瞄到逃方才能抓人，打空后有 0.85 秒恢复。墙被拆开 1.5 秒后才能补回，缺口里有人时保持通行。原生走廊始终连通，因此最后一名玩家也有路可寻。[迷宫平台实机全景](Screenshots/ExpeditionMazeChase.jpg)

要直接试玩迷宫，房主可在项目根目录运行下面的命令，从首页照常开房。至少两支队伍出航后，首次打开搜索区宝箱就会抽到迷宫。

```sh
open -n Builds/TreasureIsland/Mac/远征岛.app --args -ExpeditionSeed=4
```

真人试玩先分开记录 1v1、2v4 和人数悬殊局的首次抓捕时间、最后一人独自逃跑的时长、逃方胜率。策划建议 2v4 的初始目标为首抓中位数 20–60 秒、逃方胜率 40%–60%。若普遍在出生区被抓，先调先行时间。若首抓正常、最后一人却常躲到超时，再试最后 30 秒提示大致区域。

六个个人赛超时时，有正分的玩家按得分排序。同分时先达到该分者优先，同一服务器帧同时达分则使用本场种子裁定。所有人都零分时没有小游戏赢家，真箱仍给开箱队发奖。[六张地图的实机全景](Screenshots/ExpeditionVoxelGames.jpg)

假箱结算时，房主端按仍在场的人数、队伍、成绩与现场地形、设施，从八种事件中筛出可触发的项目，再等概率抽一件。全房看到同一结果和同一个假箱灰叉，下一只假箱仍可抽到相同事件。各队只有领取真箱才增加碎片与坐标。

| 假箱事件 | 何时可以抽到 | 抽中后如何寻宝 |
|---|---|---|
| 船员集合 | 在场至少一支多人队，且有队员能安全靠拢 | 每支多人队以一名队友刚才的勘探位置为集合点。能在附近安全落地的队员集合，单人成队或找不到站位的队员按常规方式返岛。 |
| 翻砂成坡 | 假箱旁有能安全成坡的地面 | 已开假箱旁一次翻起砂坡。全房玩家都可以借坡攀高、挖开坡面或继续垫土。 |
| 风卷帆布 | 岛上至少还有一架可收放的晒帆架 | 所有可收放的帆架各切换一次。放下的帆遮挡视线和名牌，收起的帆让两侧重新互见。谁都能到绳尾按 E 再收放。 |
| 开路好手 | 至少一支胜队 | 每支胜队全员的挖掘和泥团落地堆土半径翻倍。单铲深度、台高及投掷距离沿用常规值。 |
| 乱流冲散 | 至少一支未胜队 | 每名未获胜的队员分别随机返岛，胜队回原处。个人赛全员零分时，抽中此项会让全员散开。 |
| 迷雾换装 | 至少两名玩家 | 全员换上同一装束，隐藏名牌，暂停集合信号。之前取得的线索仍能查阅。 |
| 弹跳潮 | 任意阵容 | 全员在岛上的普通起跳速度变为 1.5 倍，可以从较深的坑里直接跳回岸上。 |
| 停火潮 | 至少两支在场队伍 | 泥球照常落地堆土，打中对手不会积累泥印或造成新眩晕，队伍可趁机接近或争夺箱锁。 |

船员集合与乱流冲散只在返岛时安排一次落点。其他事件按原位安全返岛。砂坡和晒帆一次改变岛上的实物，下一场开赛时保留。迷宫多支追方队同胜时，若抽中开路好手，每支胜队都强化。开路好手、迷雾换装、弹跳潮和停火潮持续到下一场小游戏正式开始。中途补领已揭晓的真箱，或下一场加载中止后回岛，持续效果都保留。整局寻宝结束时清除。[开箱流程与验收用例](远征岛开箱交互.md#每只假箱随机触发一个事件)

## 用土团抢位或自救

左键每 0.5 秒挖一铲。按住横移可以把洞挖宽，沿洞跟进可以继续挖深。右键从开局就有无限土团，每 0.6 秒丢一团。想从坑底上来时，朝脚下连丢；想给队友搭台阶，就依次瞄准几个相邻的落脚点。每团土约垫高 40 厘米，所有人都能踩、挖或继续加高。

相邻两次有效命中在 1.5 秒内，第三团会让对手眩晕 1.8 秒。继续朝倒地处丢两团，真土能在他身边合顶。醒来就能挖，先朝坑壁或斜下方掏一条矮洞，再从侧面反击。队友也可以挖开土盖。多人同时命中同一人，每 0.4 秒至多算一次。醒来起有 4 秒免眩晕，期间命中不累计。开箱人被砸晕会松手，仍在合撬的伙伴接手剩余进度。

远征角色拥有 UE Gameplay Ability System 的技能组件。挖、丢、跳、交互、集合与回港经技能激活；眩晕、恢复保护和工具冷却由 Gameplay Effect 计时。箱锁与线索由房主裁定，土台、土壳和原生地形共享同一套密度、碰撞和编辑序列。[能力定义](../ActDemo/Source/ActDemo/Abilities/ExpeditionAbility.cpp) · [泥团命中](../ActDemo/Source/ActDemo/Island/ExpeditionMudCombat.cpp)

## 中立场景物体

任何队伍都可以长按左键，沿准星逐铲挖掉公共地标、路标、码头和中立机关命中处的体素。红船的船壳、甲板和桅索可以分别开洞。洞口同时影响同行玩家看见的外形和实际通行范围。树根和砂斗木销仍可触发倒树、落砂。泊位损坏后，回港会另选可以站立的落点。

倒树两端间隔约 14.6 米。三端试玩中，距树根 13.2 米处仍有树干阻挡；另一队挖低路面后，从树下实际走过 4.14 米。船首、船尾两处实体碰撞相距 22.63 米，挖通的横穿船舱路线长 10.43 米。砂斗落下的沙在这局抬高了约 10.75 × 4.48 米的地面，角色爬坡上升 0.85 米。实际露出地面的范围随地势变化。

垂帆约 14 米宽，在距中央 6.48 米的侧路仍能遮住视线与姓名牌。潮孔相隔 8.4 米，高喷生效半径为 3.05 米。试玩角色在距孔心 2.5 米处主动跳跃，两次测试都落到前方约 13.5 米的岩台。

| 岛上外观 | 怎么用 |
|---|---|
| 绿冠、棕干的大树 | 靠近树根，用左键敲两下。树倒后从树下挖通，也可以在侧面填坡。 |
| 砖红色旧船壳 | 朝船舷破洞里的沙按左键开挖。用右键沿舱壁丢土堆坡，可以上甲板。 |
| 赭橙色砂斗 | 站在斜槽旁边，用左键敲浅色木销。砂堆落稳后可走、可挖。 |
| 紫色晒帆架 | 靠近米白色绳尾按 E，放下或收起帆布。靠近宝箱时 E 优先开箱。 |
| 湖蓝色双孔潮喷口 | 用右键朝一侧孔口丢土，另一孔冒白泡时跳入水柱。挖开沙塞可恢复双泉。 |

### 每局怎样生成

开新一局时，服务器只选一次本局种子。宝箱布局沿用现有种子入口，中立物体从同一个种子按固定用途派生出独立的随机序列。这样调低树的出现率时，同一个种子的宝箱仍在原处。地图、配置和生成器版本相同时，指定种子应能复现整局初始布局。

想在本版一局看全五种物体，可以在项目根目录运行下方命令，再从首页创建房间，额外假箱使用默认的 4 个。指定种子只需由房主操作。平时直接打开 App 会在每次开局时重新取种子。

```sh
open -n Builds/TreasureIsland/Mac/远征岛.app --args -ExpeditionSeed=25
```

在已保存的海岛上，程序按稳定的地形网格顺序找出符合下列条件的地点和朝向，再用种子抽取本局的物体种类、数量及摆位。候选点由地形产生，关卡作者可以调出现权重、间距和禁放范围。每类都允许缺席，合适位置用尽就少放。例如，同一段海岸这局可能在沙脊上露出半截船壳，下局船壳出现在另一段海岸，原处恢复成一条空沙脊。

先确定本局宝箱和交点，再检查物体自身、落点以及触发后的影响范围。按角色实际通行宽度，确认所有已选物体一起触发后，每个泊位仍有路可走到四个搜索区和大奖附近的可挖地面。比如倒树已经挡了近路，就跳过会把同一条绕路埋住的砂斗摆位。服务器把最终物体清单、位置和朝向同步给所有玩家，局内的倒伏、放砂与挖填也在同一场景中同步。本局种子和隐藏箱位留在服务器。房主返回房间再开一局，才换新种子。

### 岔路大树

生成器在有侧路的浅沟两肩间对齐树根和落点，让树下留有可挖的土。第一击让树冠朝落点摇晃。再敲一次，树干横倒在两处岩肩上，枝杈拦住近路，直到有人拆除。

落稳后，树下仍是可挖的土。后到的人能掏低路面，从树腹下穿过，也能在树侧填坡翻越，或顺海岸绕行。树干两端架在岩肩上，即使下面被掏通，树也留在头顶。太早放倒它，同伴回程时一样得找路。

### 半埋的旧船壳

生成器寻找两侧都有可走岸路的低沙脊，让破船横跨沙脊，两侧船舷的破洞各朝向一条岸路。站在近侧洞口，能从堆沙上方看见对侧的天光。船身固定，舱里塞的是普通体素沙。

从任一头把沙掏低，就能穿舱直达另一条岸路。也可以把沙沿舱壁堆成坡，踩上甲板看清沙脊背面的岔口。后来的人可以走现成的洞、踩现成的坡，还可以挖宽一侧的通道，留下另一侧的沙坡。船头和船尾各留一条露天岸路。

岸边能从船首的青背海鸟木雕和斜挂的破帆辨认沉船。顺着龙骨铺设的长板在舱口断开，近看可以分辨旧红漆、浅色木茬和绑桅杆的铜箍。打开 Mac 试玩包里的「试看沉船.command」即可站在船首旁绕看或开挖。[沉船样板同机位对照](Screenshots/WreckPilotComparison.jpg) · [船舷实挖一铲](Screenshots/WreckPilotCarving.jpg)

### 采石场砂斗

生成器寻找相邻的高台和下层便道，要求砂量足以堆坡接上台面，且原有上行路在落砂范围外。木斗的斜槽顺坡朝向岩壁。敲松槽口的木销，砂先从缝里漏下来，再沿槽滑下，靠墙堆成可走的坡，同时埋住下层便道的一截。放空的木斗保持空置。

低处的人现在能沿砂坡直接上台。想继续走下层的人可以挖开坡脚一侧，把便道拓回来，余下的半边坡仍给别人登高。砂堆和岛上普通地形一样能挖能填，原有石阶始终通行。

### 晒帆架

生成器从有两个可见出口的岔口里选点，让两根帆杆横在来路视线与出口之间，侧面留有观察位置。旧帆卷在横杆上，拉松低处的系绳，帆就垂下来，遮住两个出口。帆布能直接穿过，擦过时会鼓起来，身后的人能看见刚才哪里动了一下。人在帆前回头看，也会被同一幅帆遮住。

绳尾完好时，任何人都可以把帆重新升起、系回木桩。铲开的帆洞可以透视，帆绳挖尽就无法再收放。也可以登上侧坡越过帆顶观察。摆放时让帆面只遮住近处的岔口，两边仍能辨认远处的地标。

### 双孔潮喷口

生成器寻找海岸低岩台，要求两个孔都在海岸线以内可回填的地面上、台上有安全落脚面、侧面有天然上行路线。两个浅潮孔成对生成，溢口朝海。浪来之前，两孔先冒白泡，再各喷出一股矮水柱。用土填住其中一孔，下一涌就集中从另一孔喷出。看准白泡主动跳进水柱，可以借力登上低岩台。喷口朝上，经过岸路的人可以照常走。

后来的人可以借同一股水上台，也能挖掉沙塞，让两个孔恢复原状。挖尽一侧的整圈礁石后，该孔停止喷水。如果两个孔都被填住，水从侧缝排走。岩台旁保留能走上去的天然坡路。

可以在地形 Actor 的设置中将五种权重全部设为零，复验同一种子的空场寻宝。文末的联机命令可在默认配置下重放有物体的场景，检查各端地形、物体状态与角色通行情况。

## 可编辑的体素岛

这一版可以直接从试玩包中的「全岛巡游.command」进岛。镜头从全岛和码头出发，经过倒树、沉船、砂斗、晒帆架和潮孔，再看四个寻宝地标与路肩。按空格即可接手角色，走到想试的物件旁，瞄准一块外露的木板、岩面或树根按左键。沿木板中间削一铲，再沿支柱根部横向挖穿，可以分别看到局部缺口和失去支撑后的脱落。

石拱有楔石与拱肩，信标用分叉桅杆和顶盘，采石场的吊架跨在阶地上，斜鳍露出一段浅色骨脊。码头的铺板、缆绳和桩帽，宝箱的铜扣、锁眼和罗盘，以及沿岸的碎浪，在玩家视角下都可以近看。[四个地标升级前后](Screenshots/IslandFullArtComparison.jpg) · [主岛机关与植被](Screenshots/IslandFullArtField.jpg) · [码头与寻宝道具](Screenshots/IslandFullArtSalvage.jpg)

海岸用浅暖沙，向陆地升高后逐渐出现草坡和灰绿色礁岩。挖低地面会露出赭色土层，深挖能见到岩层，堆高的新土沿用沙土色。木板、立柱、石拱和系缆桩都有削边，远处靠剪影认地标。红船的领航眼、砂斗的箭纹、紫帆的风玫瑰分别提示船首、落砂方向和可收放的帆面。

主岛上，扇叶棕挑高树冠，滨海灌木、裂纹礁和枝状礁沿路肩成簇出现。生成器给玩家留出至少 2.3 米宽的路面，避开本局挖掘点，并从玩家眼高检查通向地标的视线。种子 25、默认四个假箱时，岛上有 4 株扇叶棕、11 丛灌木、4 块裂纹礁和 5 处枝状礁。[路肩近景](Screenshots/RoutePalmShoulder.jpg) · [沿路望向地标](Screenshots/RouteLandmarkSightline.jpg) · [同机位布景前后](Screenshots/RouteIslandComparison.jpg)

准星对着实物按左键，可按落铲位置削掉外叶、礁石、船板、码头或地标的一角。断根的植株和失去支撑的构件会脱落。挖出悬空的宝箱时，箱体会落到下面的洞底。再掏空支撑，箱板会脱落，原发现点仍可以开箱。下面是本版 Mac 游戏中两次普通铲击各自的同机位实拍，以及同机三进程的逐件开挖记录。[裂纹礁与拱柱各挖一铲](Screenshots/IslandFullArtShovel.jpg) · [17 类物件的碰撞与同步结果](../ActDemo/Saved/FullIslandArtPackStructures/neutral/normal/summary.json)

在项目根目录运行以下命令，按平常流程建房、出发，就能回看这局的布景。

```sh
open -n Builds/TreasureIsland/Mac/远征岛.app --args -ExpeditionSeed=25
```

前一版的实机画面来自默认四个假箱、种子 25 的远征。三人局里，船舱里的沙面挖低了 1.30 米，角色从新开的通道穿过 10.37 米。[加入路肩植被前的主岛](Screenshots/LowPolyIsland.jpg) · [岸边红船](Screenshots/LowPolyWreck.jpg) · [挖开后的船舱](Screenshots/LowPolyWreckPassage.jpg) · [岛上机关](Screenshots/LowPolyField.jpg)

从港口开房、在码头组队，再进入小游戏时，木料、削边石块和礁石沿用同一套造型。[港口首页](Screenshots/LowPolyHarbour.jpg) · [队伍码头](Screenshots/LowPolyRoom.jpg) · [六处小游戏场地](Screenshots/LowPolyVenues.jpg) · [迷宫俯视](Screenshots/LowPolyMaze.jpg)

| 设计决定 | 编辑与运行方式 |
|---|---|
| 八队从哪里看见各自的队友、第一处地标和通向内陆的路 | 编辑关卡里的泊位 Actor、队员站位、预览镜头。码头模型与出生位置一起移动。用八人同队检查每个站位能否起步、是否能看见同伴。 |
| 从外围如何发现搜索区、何时走高处或挖捷径 | 用 Voxel Map 调整地形、搜索圈，移动公共地标并实走检查视线。主路线、地标之间的遮挡和岩层厚度由关卡策划确认。 |
| 某局的箱子在哪里 | 服务器在作者批准的搜索圈及可埋岩层中选择箱位，校验交点。地形修改公开复制，精确线索只发给相应队伍。 |
| 某局有哪些中立物体、朝向哪里 | 在地形 Actor 的 Neutral props 配置五种权重、最小间距和禁放范围。服务器按本局种子和地形选点，目标每局 4–6 件。地标与泊位沿用已保存的地图。 |
| 人数变化后如何出生、谁能开始、谁已赢 | 服务端按房间配置和实际名单分配泊位、核对准备并裁定开箱。大厅与结算读同一份权威状态。 |

首次生成器按八个方向铺设码头、道路标记和初始搜索区，结果保存在关卡文件中。之后由关卡策划在 UE 移动泊位、镜头和地标，调整遮挡、坡度及搜索区，再保存关卡。普通启动和校验都读取已保存的布局。

在 UE 编辑 `/Game/TreasureIsland/L_ExpeditionIsland` 并保存。前台布景在 `/Game/TreasureIsland/L_IslandFront`。以下命令首次建立缺少的地图，也可用来检查新岛。远征房首次打开各搜索区宝箱时进入独立小游戏，原六人场仍保留按键挑战的回归入口。

```sh
bash Scripts/actdemo.sh build
bash Scripts/actdemo.sh party-feature
bash Scripts/actdemo.sh island-front
bash Scripts/actdemo.sh island-map -Expedition -VerifyExpedition
bash Scripts/actdemo.sh play
```

每个小游戏的 Game Feature 自带地图、规则类和一份连接两者的 Feature Data。以断桥为例，在 UE 打开 `/PartyBridgeDash/Maps/L_BridgeDash` 可以编辑背景布景，规则类负责桥面、出生点和计分。场地内的挖填会在小游戏卸载时清除。[断桥规则源码](../ActDemo/Plugins/GameFeatures/PartyBridgeDash/Source/PartyBridgeDash/Private/BridgeDashSession.cpp)

## 玩家怎样读懂这一局

| 玩家此时要决定什么 | 提供的线索与反馈 | 由谁作最后判断 |
|---|---|---|
| 加入朋友，还是自己开一局 | 开始页提供建房与找房。公开列表显示房名和人数，可取消、刷新，连接失败后可重试。 | EOS 找到等待中的同版房间，房主在玩家进入时复查容量与比赛阶段。 |
| 和谁一起出发 | 大厅列出已入座的小队，队员身旁显示准备状态。换队时视角转到对应泊位。 | 房主更新名单、队伍和准备状态。开始前等待已入座成员的地形同步。 |
| 先搜索哪个区域 | 外围码头通向八条入口，沿岸路线连接四个搜索区。地面地标和海图用相同的 A–D 标识。玩家可以分头寻找。 | 关卡策划调整视线和路线；地图生成时检查出生净空、路径与可埋深度。 |
| 何时开挖，怎样离开 | 海图提供搜索圈。瞄准地面或坑壁按左键挖。右键往脚边丢能踩着出去，长按回港会显示进度，松手可取消。 | 服务器按真实射线、距离与地形确认挖填。宝箱露出并可触及时才显示开箱提示。 |
| 接下来去哪里 | 本队领取真箱奖励后，海图把搜索圈更新为线索点。第四点使两条线及其交点出现。 | 真实地形由全房共享，精确坐标只复制给对应队员。 |
| 谁赢了，怎样再玩一次 | 取得四点并持钥的队伍开出大奖，所有在线玩家进入结算。房主返回后队员可继续同房准备。 | 服务器只裁定一次胜队，重开保留房名、席位与归队信息并重新初始化本局。 |

## 项目小组

七个常驻角色定义见[项目代理目录](../.codex/agents)。每次改动由主责岗位提出具体的玩家操作，请另一岗位带着反例复核，再把双方结论交给实现者。

| 岗位与代理名 | 主责 | 交叉复核 |
|---|---|---|
| 主美 `island_art_director` | 角色造型、队伍辨识、动作表现与美术交付 | 与系统策划核对动物连续挖、丢土、晕倒、矮身掏洞和合撬的动作。下一轮方案见[动物角色造型](PartyCharacter.md)与[操作和对抗提案](ExpeditionPartyProposal.md)。 |
| 系统策划 `island_system_designer` | 房间准入、组队、开局、胜负、重开 | 与玩法程序逐项核对单人、满房和成员离开 |
| 关卡策划 `island_level_designer` | 泊位、地标、路线、搜索区、岩层净空 | 与 UI 设计师核对地标和海图；与程序策划实走验证可达性 |
| UI 设计师 `island_ui_designer` | 玩家要看见的信息、状态优先级、操作提示 | 与交互程序检查空队、未就绪、地图同步和错误恢复的实际画面 |
| UI 交互程序 `island_interaction_engineer` | Slate 焦点、输入交还、过渡、镜头和即时反馈 | 用独立游戏窗口核对鼠标、键盘和联机等待 |
| 程序策划 `island_technical_designer` | 核对由谁配置、哪些可以生成、哪些得由作者放置 | 用改图、重开和多人复验检查边界；发现例外后交还对应岗位 |
| 系统与玩法程序 `island_gameplay_engineer` | 会话、权威状态、地形复制、开箱和结算 | 与系统策划验证规则；与交互程序核对服务端结果和本机可见反馈 |

本轮房间面板最初把八支空队都列出来，实拍时队员与准备按钮被挤到面板两端。现在大厅直接列出有人入座的队伍，其余队伍通过「选择其他空队」进入。相邻站位用短镜头移动，对岸换队用短淡入淡出；玩家始终留在同一张房间面板上。

每次迭代选择一个可以实际重现的分歧。例如八人同队时，房间列表显示八人只能证明准入成功。关卡策划还要从八个泊位实际起步，UI 设计师要能在码头看见队友，玩法程序要验证其中一人开箱后其他七人同时收到结算。联机探针与带画面的操作记录分别保存。

## 复验入口

在项目根目录运行。Mac 包位于 `Builds/TreasureIsland/Mac/远征岛.app`。UI 脚本从游戏开始页启动，读取游戏窗口的真实视口。房间测试脚本使用独立进程验证服务器裁决与各端收到的数据。

```sh
bash Scripts/actdemo.sh island-package
python3 Scripts/expedition_ui_qa.py --scenario settings --foreground --shots
python3 Scripts/expedition_ui_qa.py --scenario pair --edges --shots
python3 Scripts/expedition_ui_qa.py --scenario recovery
python3 Scripts/expedition_ui_qa.py --scenario walkthrough --record
python3 Scripts/island_qa.py --scenario eight-teams --stress --profile --mode both
python3 Scripts/island_qa.py --scenario crew-eight --mode normal
python3 Scripts/island_qa.py --scenario field --mode both
python3 Scripts/island_qa.py --scenario games --seed 2026 --decoys 2 --mode both --screenshot
python3 Scripts/island_qa.py --scenario games --arena-terrain --arena-views --seed 3 --mode normal --screenshot
python3 Scripts/island_qa.py --scenario games --seed 3 --decoys 2 --venue-demolition --mode emulated --screenshot
python3 Scripts/island_qa.py --scenario games --decoy-events --mode both
python3 Scripts/island_qa.py --scenario solo --decoys 0 --mode normal
python3 Scripts/island_qa.py --scenario games-drop --seed 25 --mode normal --headless-host
python3 Scripts/island_qa.py --scenario games-crew --game-only PartyCrystalHunt --seed 25 --mode emulated --headless-host
python3 Scripts/island_qa.py --scenario games-maze --seed 25 --mode normal --screenshot
python3 Scripts/island_qa.py --scenario games-eight --game-only PartyCrownHold --seed 25 --mode normal --headless-host
python3 Scripts/island_qa.py --scenario mud --mode both --screenshot
python3 Scripts/island_qa.py --scenario solo --neutral-audit --seed 25 --headless-host --mode normal
python3 Scripts/island_qa.py --scenario neutral --seed 25 --decoys 4 --mode both --screenshot
python3 Scripts/island_qa.py --scenario neutral --seed 25 --decoys 4 --structures --mode both
```

实走探针沿真实地面移动，通过普通挖掘、回港和开箱输入完成一局。为使每次地图改动都可复验，开发探针知道服务端本局的箱位。它验证移动、挖掘、操作和结算链路，玩家能否凭地标自行发现埋藏点，仍需真人盲玩评估。录像单独采集，避免截图写盘影响压力数据。

## 公网大厅验证

在安装了同版游戏的两台 Mac 上分别运行对应命令。两端使用不同的网络和不同的游客身份。将示例房名换成本轮唯一的名字，并在两端使用相同拼写。脚本只加入名称匹配的房间。

```sh
# 家庭网络上的房主
python3 Scripts/expedition_ui_qa.py --scenario internet --role host --room-name IslandQA-1125A

# 手机热点上的来宾
python3 Scripts/expedition_ui_qa.py --scenario internet --role guest --room-name IslandQA-1125A
```

房主会从首页建房，来宾从列表入房。开局后，房主探针使用正常移动、挖掘和开箱交互走完整局，两端核对地形修改、小游戏、结算和返回原房结果。房主从码头解散房间，两端重新搜索并核对旧房间已消失。使用另一个房名再跑一轮，在双方命令末加 `--force-relay`。只有 EOS 的实际连接通知报告中继才算该项通过。每端日志与 `summary.json` 保存在 `ActDemo/Saved/ExpeditionUI`。

只有一台 Mac 时可运行 `python3 Scripts/expedition_ui_qa.py --scenario internet --role solo` 检查游客登录和建房。关闭后重新运行，两次日志的 `ISLAND_EOS_GUEST_READY` 身份摘要应一致。再运行 `python3 Scripts/expedition_ui_qa.py --scenario internet --role directory`，独立的观察进程使用同一 macOS 用户搜索本轮房名，核对列表的 1/8 人、开局隐藏和房主销房后的搜索结果，并核对后端的销房回调。观察进程全程停留在列表。双机连接和中继用上面的两台 Mac 检查。八人弱网及其他本机回归命令显式使用开发用局域网模式。

## 四个真箱与建筑复验

迷宫追捕在同一台 Mac 上用三个两人队跑了两轮。发行包在模拟 80 毫秒往返延迟、2% 丢包下，各端读取的两套迷宫均有 81 个互通房间，拆补后的碰撞一致。逃方先行 8 秒后，第一轮经过完整 180 秒仍有一人存活。第二轮抓齐后，两支追方队同时获胜。另用三端检查一名追方来宾离开后仍可回岛。[六端打包版验收](../ActDemo/Saved/MazeChaseMac/games-maze/emulated/summary.json) · [掉线验收](../ActDemo/Saved/MazeChaseDropMac/games-drop/normal/summary.json)

房主分别把额外假箱设为 0、4、24。用同一组 256 个种子对照有无假箱的布局，A–D 四个真箱与最终交点逐一相同。设为 24 时累计核对 6,144 个假箱位置，并在一张岛上检查全部 24 个假箱能否挖出。[三人宝库争锁与 24 假箱验收](../ActDemo/Saved/KeyVaultFieldAcceptedQA/field/normal/summary.json)

单人局先在没有任何线索时挖空宝库位置。角色集齐三点时已经有钥匙，海图和坑内仍没有大奖。第四点补齐后才显示交点、出现大奖，成功开锁后钥匙消耗，并能返回原房。默认假箱数和零假箱各自通过此项检查。[默认假箱的宝库开锁](../ActDemo/Saved/NeutralShipLayoutAudit/solo/normal/summary.json) · [发行版零假箱的宝库开锁](../ActDemo/Saved/FourChestVenueRelease/solo/normal/summary.json)

三进程对局用 87 铲拆除了 48 处静态构件、13 个地标构件、8 座码头和本局生成的 6 座中立设施。铲中树冠、桅索、砂斗腿箍与卷帆也能拆除相应设施。普通网络和模拟 80 毫秒往返延迟、2% 丢包的两局中，三端最终剩余数量均为零。拆光码头后角色仍能在地面安全回场。另一次实走从 13.2 米处的倒树远端开路，穿过 10.43 米船舱，并借水柱前进约 13.5 米。[建筑拆除](../ActDemo/Saved/NeutralAttachmentDemolition/neutral/normal/summary.json) · [弱网同步](../ActDemo/Saved/NeutralAttachmentDemolition/neutral/emulated/summary.json) · [五种中立建筑实走](../ActDemo/Saved/ExpandedLandmarkWalk/neutral/normal/summary.json)

六张小游戏地图的外围共 60 座建筑、210 件可见构件。三进程测试把角色放在有支撑的棋盘格上，用原有挖掘操作逐座拆除。每一击后都核对三端的可见性与碰撞。穿礁赛道上，同一堵墙能挡住远处的拆除射线，走近后先挖到墙块。Mac 包在模拟 80 毫秒往返延迟、2% 丢包下走完七轮，实测拆除距离为 37.18–103.51 米。第七回合再进第一回合已经拆过的断桥场地，十座建筑全部恢复。[打包版六图拆除与奖励](../ActDemo/Saved/VenueDemolitionRelease/games/emulated/summary.json) · [编辑器三端交叉复验](../ActDemo/Saved/VenueDemolitionEditor/games/normal/summary.json)

打包版海图截图展示了两个时刻。[三点已持钥，D 区仍待搜索](Screenshots/ExpeditionTreasureChartThree.jpg) · [四点连线与大奖交点](Screenshots/ExpeditionTreasureChartFour.jpg)。[正常队友与对手名牌](Screenshots/ExpeditionCrewNames.jpg) · [名牌遮挡](Screenshots/ExpeditionCrewOcclusion.jpg)

早期三队测试在模拟 80 毫秒往返延迟、2% 丢包下验证了真箱奖励、假箱换位与身份混淆。旧记录的第七场来自另一队重开 A 箱。现行规则下该队在箱边按 E 直接领取。零假箱的单人房也完成了开库、消耗钥匙并返回原房。[旧版三队开箱和奖励](../ActDemo/Saved/FourChestRelease/games/emulated/summary.json) · [零假箱单人完整对局](../ActDemo/Saved/FourChestRelease/solo/normal/summary.json)

打包版另从首页建房，用普通移动、挖掘和 E 键开箱走完全局。角色走过 531.3 米，四场小游戏都无人得分，开箱队仍逐次获得四个真箱点位。海图在第三点时保持未连线，第四点后画出交点。用钥匙开出大奖后从结算页回到原房，全程 449.42 秒。[实走日志](../ActDemo/Saved/FourChestReleaseWalk/walkthrough-normal-20261125-120812/Walkthrough.log) · [结算画面](../ActDemo/Saved/FourChestReleaseWalk/walkthrough-normal-20261125-120812/Walkthrough_Result.png)

## 小游戏连续地形联机复验

本轮 Mac 试玩包已用房主和两个来宾走完六场比赛，并在第七轮重进断桥。三端都在同一落点测得一铲挖深 40 厘米，回填两次后高出原面 40 厘米；坡脚的采样高度依次降至 30.3、17.6、1.5 和 0 厘米。三端每轮的最终地形和比分一致，重进时地面恢复原高。另一次海堤对局在 80 毫秒往返延迟、2% 丢包下由房主封渠得 2 分，来宾挖通后积分保持到结算。[挖坑与回填实机图](Screenshots/ExpeditionArenaTerrain.jpg) · [六关三端记录](../ActDemo/Saved/PartyDensityRelease/games/normal/summary.json) · [弱网海堤记录](../ActDemo/Saved/PartyDensityFloodMac/games/emulated/summary.json)

## 早期方块版测试记录

小游戏探针把回合时限缩短到 14 秒，逐端核对方块、比分、赢家和私有标记。断桥用例安排一人从近岸搭四格、走过土桥，另一人在远岸原地等到结算，两人的得分分别是 1 和 0。灯塔和争旗用例摆放角色以检查登台与独占计分。另用八个独立进程进入同一局争旗，并让八人都返回原岛。满房复验首次启动时有一个来宾在写出引擎启动日志前退出，整组重启后通过。[断桥与掉线](../ActDemo/Saved/PartySixRelease/games-drop/normal/summary.json) · [八人争旗](../ActDemo/Saved/PartySixRelease/games-eight/normal/summary.json) · [同队领奖](../ActDemo/Saved/PartySixPackagedQA/games-crew/emulated/summary.json)

上一版 Mac 包的两名玩家在正常网络和模拟 80 毫秒往返延迟、2% 丢包的条件下，分别从六只假箱完成六场比赛。每场回岛后，逐端核对的最终体素、比分和第一名均一致。房主与来宾各赢三次。打包版另保存了转场、赢家海图、六张第一人称视图和六张场地全景。[普通网络与实机截图](../ActDemo/Saved/PartySixRelease/games/normal/summary.json) · [模拟丢包](../ActDemo/Saved/PartySixRelease/games/emulated/summary.json)

## 双视角与搜索圈复验

Mac 试玩包在同机房主与来宾双进程下通过视角切换。模拟 2% 丢包时，两端都能用 V 往返切换，第一人称的水平和陡俯视准星都与挖、丢的方向一致。海图打开时按 V，关闭后保留原视角。[第三人称](Screenshots/ExpeditionThirdPerson.jpg) · [第一人称](Screenshots/ExpeditionFirstPerson.jpg)

单人从码头步行 30.2 米，挖开并取得第一条线索。本队海图随即在实际 A 箱位置留下实点，B、C、D 仍显示为搜索区。[开局海图](Screenshots/ExpeditionSearchChartStart.jpg) · [取得 A 线索](Screenshots/ExpeditionSearchChartFirstClue.jpg)

在默认远征岛上复验 256 个种子，相同种子的四箱及大奖坐标逐一重现。用圈心连线猜大奖，最近偏差 10.01 米，中位偏差 12.42 米，最远偏差 20.17 米。改用开箱得到的四个坐标，256 次都命中真交点。另测 64 个种子的中立物体，所有已选布局都在物体同时触发后保留通路。编辑器与 Mac 试玩包对同种子的 321 件物体给出了相同的种类、位置和朝向。种子 25 可以在同一局看到五种物体。

在种子 25 的两队三进程对局中，后来的一队从倒树下穿过 3.41 米，穿过船舱 7.55 米，再用土坡登上甲板。落砂成坡后走了 2.88 米、爬升 0.93 米，坡脚随后被挖低 0.79 米。两队先后收放同一面帆。潮喷实际把角色向前送出 8.58 米。模拟 80 毫秒往返延迟、2% 丢包后，五种互动也全部完成。[五种白模触发前后](Screenshots/ExpeditionNeutralProps.jpg)

在本机多进程复验期间，macOS 记录过游戏窗口打开前的 UE 内存跟踪器启动崩溃。同一试玩包使用相同参数再次启动后，完成了从首页到第一条线索的实玩。

```sh
python3 Scripts/expedition_ui_qa.py --scenario pair --perspective --shots
python3 Scripts/expedition_ui_qa.py --scenario chart --shots
```

## 上一版连挖与投掷的本机验收

打包版在实际岛图复查了 64 个种子。所有布局都能按种子重放，五种中立物体均自然出现，所有泊位到搜索区和大奖附近的可挖地面都通过了累加障碍检查。同一包的单人对局取得四条线索、打开大奖并返回原房。[种子、各局坐标与寻宝结果](../ActDemo/Saved/GasReleaseSeeds/solo/normal/summary.json)

另一次单人实走从首页创建房间开始，走过 505.2 米，用 218.82 秒取得四条线索并打开大奖，再从结算页回到原房。四个线索箱各用了 11、16、12、13 次浅挖。[实走日志](../ActDemo/Saved/GasReleaseWalkQA/walkthrough-normal-20261124-205349/Walkthrough.log) · [结算实机画面](../ActDemo/Saved/GasReleaseWalkQA/walkthrough-normal-20261124-205349/Walkthrough_Result.png)

在另一条实走路线上，角色用四铲挖出一个 159.9 厘米深的坑，朝脚下丢三团土，实际站立面升高 119.9 厘米。剩下 40 厘米由角色正常跨步走出，全程没有跳跃。[挖坑、垫土和出坑记录](../ActDemo/Saved/GasSoloWalkQA/RouteAndPit.log)

倾角 24.34° 的坑底实深 171.6 厘米，角色用四团土走出。移到净空只剩 21.98 厘米的矮檐下，两次向脚下丢土都没有挤动角色，走出矮檐后再次投掷，脚下升高 34.88 厘米。[斜底与矮檐记录](../ActDemo/Saved/GasSoloWalkQA/PackagedSlopedPitAndRoof.log)

三进程复验中，被埋者朝上挖一铲便能掀盖站起，队友从土包外面挖一铲也能救人。选择朝下挖时，脚底下降了 43.4 厘米，原处上方仍有完整土盖。[自行掀盖](../ActDemo/Saved/GasEscapeRoof/mud/normal/summary.json) · [队友救援](../ActDemo/Saved/GasEscapeRescue/mud/normal/summary.json) · [地下挖掘](../ActDemo/Saved/GasEscapeUnder/mud/normal/summary.json)

普通网络与模拟 80 毫秒往返延迟、2% 丢包下，来宾都能一次按住左键、转向另一处继续挖，松开后停止。两人合撬时，无论主撬还是助手中弹，前两次命中都能继续出力；第三次命中后，由另一位队员继续开箱。[正常网络](../ActDemo/Saved/GasFieldAcceptedQA/field/normal/summary.json) · [模拟丢包](../ActDemo/Saved/GasFieldAcceptedQA/field/emulated/summary.json)

另一场三进程对局中，两种网络都复现了单人用五团土封顶、受击者侧挖穿出并反投命中，以及仅用右键垫出队友挖好的窄坑。鼠标左右键可同时按住，松开左键后右键继续投掷。土壳在三个进程测得高度同为 1.20 米。模拟丢包时，两名投手相隔 0.101 秒打中同一角色，只算一次；苏醒保护期间两人各打中三团，保护结束后的第一团重新从第一层累计。[正常网络与实机截图](../ActDemo/Saved/GasMudReleaseQA/mud/normal/summary.json) · [模拟丢包](../ActDemo/Saved/GasMudReleaseQA/mud/emulated/summary.json)

种子 25 的三进程对局中，一队放倒树，另一队用浅铲挖低树下路面，从树下穿过 2.11 米。这条测试路线清出了整个胶囊宽度的通道，共用 29 铲。角色还实际穿过 4.58 米船舱，沿土坡登上甲板，走上砂斗堆出的 78 厘米砂坡后挖开坡脚。两队先后放帆、收帆，堵住一侧潮孔后成功借水柱登台。模拟丢包也通过了全部五种交互。[普通网络](../ActDemo/Saved/GasNeutralFinalQA/neutral/normal/summary.json) · [模拟丢包](../ActDemo/Saved/GasNeutralFinalQA/neutral/emulated/summary.json)

## 此前版本的本机验收

单人从最终打包版首页开房，在自然生成 5 件中立物体的岛上实走 509.7 米。角色用 187.63 秒搜齐四条线索并开出大奖，全程可绕过中立物体。点击结算页「返回房间」后回到原房，线索清零。测试期间 macOS 停在锁屏界面，额外的应用前台焦点检查未通过。[实走与返回日志](../ActDemo/Saved/NeutralFinalWalkQA/walkthrough-normal-20261124-181705/Walkthrough.log) · [结算](../ActDemo/Saved/NeutralFinalWalkQA/walkthrough-normal-20261124-181705/Walkthrough_Result.png) · [返回原房](../ActDemo/Saved/NeutralFinalWalkQA/walkthrough-normal-20261124-181705/Walkthrough_ReturnedLobby.png)

彩色中立物体的最终 Mac 包在实际岛图上检查了 64 个种子。22 局有 4 件，19 局有 5 件，23 局有 6 件。64 个初始布局互不相同，重放结果逐项一致。累加所有触发后的占地后，八个泊位仍可通往四区和大奖附近。[种子明细与单人完整夺宝](../ActDemo/Saved/NeutralFinalQA/solo/normal/summary.json)

上一版使用旧挖填笔刷时也完成过五种中立物体的三进程检查，可用于比较两种笔刷的开路成本。[旧笔刷普通网络](../ActDemo/Saved/NeutralFinalQA/neutral/normal/summary.json) · [旧笔刷模拟丢包](../ActDemo/Saved/NeutralFinalQA/neutral/emulated/summary.json)

上一轮普通凿岩复验在 A 区海侧边缘凿下一击，落点地面下降 193 厘米，四米外的地面高度变化为零。一名来宾从坑沿走到坑底并站稳。三个游戏进程在普通网络和模拟 80 ms 往返、2% 丢包下均得到相同地形。单人另跑完四条线索、开启大奖并返回房间。[普通网络](../ActDemo/Saved/PlainTerrainFinalQA/field/normal/summary.json) · [模拟丢包](../ActDemo/Saved/PlainTerrainFinalQA/field/emulated/summary.json) · [单人寻宝](../ActDemo/Saved/PlainTerrainFinalQA/solo/normal/summary.json)

此前版本跑过八人同队。301 次挖填后，八端地形密度和四处地面碰撞一致，房主帧时间 P95 为 22.34 ms。随后八名成员共同夺宝并返回原房间。[八人同队复验](../ActDemo/Saved/ExpeditionMechanicsFinal/crew-eight/normal/summary.json)

下表为前一版远征岛的八队基线。2026-11-24，在一台 M3 Max Mac 上运行八个独立进程。房主以 Metal 渲染 1280 × 720，七位来宾使用 NullRHI。输入负载持续覆盖四处地形，每端按序应用所有编辑。八端的密度校验值和四处地面碰撞射线均一致。下表列 P95。房主碰撞从服务端收到请求开始计时，来宾碰撞从各自收到已裁定的编辑开始计时。

| 网络条件 | 挖填次数 | 房主整帧 | 房主 GPU | 房主请求至碰撞 | 最慢来宾收包至碰撞 |
|---|---:|---:|---:|---:|---:|
| [正常](../ActDemo/Saved/ExpeditionFinalPackageQA/eight-teams/normal/summary.json) | 301 | 18.21 ms | 8.72 ms | 1.96 ms | 13.54 ms |
| [模拟 80 ms 往返、2% 丢包](../ActDemo/Saved/ExpeditionFinalPackageQA/eight-teams/emulated/summary.json) | 302 | 18.18 ms | 7.10 ms | 3.61 ms | 14.78 ms |

单人、双人合作、两队对抗、八人同队、八人八队均完成夺宝及返回房间。八队用例由第八队夺宝，每局进行 40 次分端线索或结算状态检查。单人实走另覆盖四区移动、远端开挖、回填与长按回港。[八人同队](../ActDemo/Saved/ExpeditionTunedQA/crew-eight/normal/summary.json) · [队伍中途退出](../ActDemo/Saved/ExpeditionRoomQA/crew-drop/normal/summary.json) · [四区实走](../ActDemo/Saved/Logs/ExpeditionWalkFinal.log)

[单人完整实机录像](../ActDemo/Saved/ExpeditionUI/walkthrough-normal-20261124-160352/Walkthrough.mp4)从首页到返回原房间，约 3 分 42 秒。角色实际移动 521 米，打开四只线索箱和一只大奖箱。打包版的两个 Metal 窗口另外通过房名及容量同步、搜索取消、准备与撤销、换队、打开海图和暂停后继续。[房间交互与截图](../ActDemo/Saved/ExpeditionUI/pair-normal-20261124-161337/summary.json) · [旧双人地图回归](../ActDemo/Saved/ExpeditionAcceptanceQA/legacy-voxel/normal/summary.json)

[双 Metal 窗口的完整开局流程](../ActDemo/Saved/ExpeditionUI/pair-normal-20261124-161003/summary.json)单独采样，全程无截图和录像。从按下开始或准备到接管角色分别为 3.41 秒和 3.80 秒，其中包含三秒出航倒计时。含首次进房载图的全流程帧 P95 为房主 40.25 ms、来宾 70.36 ms，最长帧约 0.53 秒。跨世界时由 UE 加载界面接住视口；双窗口共用一台 Mac 的数据仍显示载图阶段有停顿。此口径和上表进入岛后集中挖填的帧时间分别记录。单独激活原生游戏窗口后，也验证了按 Esc 打开暂停面板，再用鼠标点击「返回」恢复角色输入。[前台鼠标复验](../ActDemo/Saved/ExpeditionUI/solo-normal-20261124-161702/summary.json)

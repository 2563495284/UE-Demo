# 任务 02 · 局域网 2v2v2 海岛寻宝

海岛关卡为 `/Game/TreasureIsland/L_TreasureIsland`。房主同时担任 Listen Server，计入六个座位。按加入顺序轮流分配红、蓝、黄三队，六端初始地形校验通过后开局并锁房。来宾离开后对局继续，房主离开则结束会话。

## Mac 启动

在工程根目录执行，默认使用 UE 游戏 UDP 端口 7777。

```sh
bash Scripts/actdemo.sh island-host
bash Scripts/actdemo.sh island-join 192.168.1.42
```

将示例 IP 换成房主的局域网 IP。其他 Mac 只需同一份完整 `ActDemo.app`，可在它所在的目录运行以下命令。

```sh
open -n -a "$PWD/ActDemo.app" --args '/Game/TreasureIsland/L_TreasureIsland?listen' -windowed -ResX=1280 -ResY=720
open -n -a "$PWD/ActDemo.app" --args '192.168.1.42:7777' -windowed -ResX=1280 -ResY=720
```

WASD 移动，鼠标控制镜头，空格跳，左键凿岩，右键挖脚下，Q 回填，按住 Shift 攀爬。按住 Tab 看藏宝图，靠近已露出的箱子按 E，长按 R 两秒回本队码头。角色在岛外水面下连续三秒会溺亡，一秒后回码头，已获得的队内线索和全场地形保留。结算后由房主按 Enter 重开，重新生成宝箱并恢复原始地形。

四个搜索圈公开。每只线索箱可被红、蓝、黄三队各开一次，精确平面坐标只下发给开过箱子的队伍。集齐才显示 A–C 与 B–D 的交点。大奖须挖露并打开，最先满足条件的队伍获胜。挖露的实体箱与地形变化对六端可见。

## 地图编辑

在 UE 打开 `L_TreasureIsland`。从顶部模式菜单选择 **Voxel Map**，也可以在编辑器底部的 Cmd 输入框执行 `Act.VoxelMap`。点击 **Frame Island** 可快速切到海岛鸟瞰。

1. 选择 **Add Rock** 或 **Carve Rock**，在面板设置半径。用左键在视口拖动笔刷，一次拖动对应一次撤销。
2. 选择 **Search Circles**。拖动视口里 A–D 对应的圆心，在面板填写半径并按回车。编辑后面板会提示无法埋箱的区域或交点。可以随时保存草稿，布局合法后才能开局。
3. 用普通的 **保存当前关卡** 写入 `.umap`。联机测试时，六端使用带有同一版保存关卡的工程或打包版本。
4. 在大纲选中普通 Static Mesh Actor，点击 **Make Selection Breakable**。用 Details 里的 **Destruction → Effect** 选择碎裂或树倒下。桥和建筑可用多件这样的 Actor 拼装。三个码头和示例树已经支持破坏。对局里左键攻击物件，宝箱沿用 E 键交互。

地图编辑限定在现有体素网格及海岸边界内。选中 **Voxel Map** 模式时才显示编辑预览。复活默认尝试原出生点，码头拆掉后会搜索安全地面。重开从保存的关卡重新加载地形和物件。

跨区块挖填、搜索圈保存和埋箱诊断可以通过 `bash Scripts/actdemo.sh island-map -VerifyAuthoring` 检查。它生成独立的 `QA_Authoring` 关卡供联机回归使用。打开该关卡后，可运行 UE 自动化测试 `ActDemo.Editor.VoxelStroke` 检查整笔撤销、重做和搜索圈编辑。使用以下命令跑编辑后的关卡。

```sh
PYTHONDONTWRITEBYTECODE=1 python3 Scripts/island_qa.py --editor --map /Game/TreasureIsland/QA_Authoring --mode both
```

修改后的测试关卡在同一台 Mac 上用六个独立进程跑通了寻宝、同时砸树、拆码头后复活及重开恢复。正常网络接受 304 次压力挖填，模拟约 80 ms 往返及 2% 丢包时接受 300 次。两局六端的 576 个区块均一致。只改搜索圈、地形校验值相同的客户端也被开局校验拦下。 [正常网络](../ActDemo/Saved/VoxelAuthoringQA/AuthoredHunt/normal/summary.json) · [模拟弱网](../ActDemo/Saved/VoxelAuthoringQA/AuthoredHunt/emulated/summary.json) · [圈位不一致](../ActDemo/Saved/VoxelAuthoringQA/Mismatch/summary.json)

## 假箱与派对玩法

房主读 `ActDemo/Config/DefaultGame.ini` 的 `[/Script/ActDemo.IslandGameMode]`。将 `DecoyChestCount` 设为 0–24，默认四个。假箱分散在各线索箱的搜索圈内，外观与线索箱相同，按 E 打开后触发全房间派对。成功开局后该假箱消失。小玩法期间按提示用空格确认，结束后全员返回海岛。已挖地形和队内线索会保留。Mac Development 包也可以用房主启动参数 `-ini:Game:[/Script/ActDemo.IslandGameMode]:DecoyChestCount=0` 临时关闭假箱。

`DecoyMinigameFeature` 选择小玩法插件，默认 `PartyButtonChallenge`。每个插件在 `Plugins/GameFeatures` 中声明 `ExplicitlyLoaded` 和初始状态 `Installed`，使用与插件同名的 `UPartyMinigameFeatureData` 资源指定会话类。会话提供自己的 Pawn，输入、视角和 HUD 随该 Pawn 启停。房主与客户端各自激活插件，所有在线玩家准备好后开局，返回海岛后卸载资源。新增玩法时，将插件和资源一同打包到所有端。

## 构建和复测

```sh
bash Scripts/actdemo.sh build
bash Scripts/actdemo.sh island-map
bash Scripts/actdemo.sh island-package
PYTHONDONTWRITEBYTECODE=1 python3 Scripts/island_qa.py --mode both
PYTHONDONTWRITEBYTECODE=1 python3 Scripts/island_qa.py --scenario party --mode both
PYTHONDONTWRITEBYTECODE=1 python3 Scripts/island_qa.py --scenario party-drop --mode normal
PYTHONDONTWRITEBYTECODE=1 python3 Scripts/island_qa.py --scenario party-timeout --mode normal
```

派对测试通过玩家控制器注入空格键，覆盖玩法确认及返回海岛后的跳跃。需要重建默认插件的 GameFeatureData 时，先编译再运行 `bash Scripts/actdemo.sh party-feature`。

默认归档在 `Builds/TreasureIsland/Mac/ActDemo.app`，自动化日志在 `ActDemo/Saved/IslandQA`。六进程回归通过真实 NetDriver 测试挖箱、开箱、结算、重开和可靠编辑复制。探针直接布置角色和压力测试的笔刷落点，以便重复跑相同情景。探针使用正常房主编辑队列、角色挖地入口和客户端开箱 RPC。队内隔离根据每个独立进程持有的线索坐标和交点验证。帧时间由一台启用 Metal 的房主进程采样，五个客户端使用 NullRHI。

弱网模式为六个进程分别配置 `-PktLag=40 -PktLoss=2`，模拟约 80 ms 往返、每端 2% 出站丢包。碰撞延迟从进程收到请求或编辑指令计到该进程的碰撞生效。性能门槛为房主帧时间 P95 ≤ 33.3 ms，房主和每个来宾的碰撞延迟 P95 ≤ 150 ms。

## 实测 · 2026-11-23

UE 5.8 Mac Development，Apple Silicon arm64，最低 macOS 14.0。归档包通过 `codesign --verify --deep`。以下两局在同一台 Apple M3 Max 上运行六个独立进程，经本机局域网 IP 直连。房主为 1280 × 720 Metal 窗口，来宾为 NullRHI。

| 网络 | 压力挖填 | 最终序号 / 全局 CRC | 房主帧 P95 | 房主碰撞 P95 | 最慢来宾碰撞 P95 |
| --- | ---: | --- | ---: | ---: | ---: |
| 正常 | 301 次 | 316 / `6f7d19db` | 17.676 ms | 84.578 ms | 34.632 ms |
| 约 80 ms RTT、2% 丢包 | 301 次 | 316 / `182a46ab` | 17.830 ms | 68.102 ms | 67.769 ms |

- 两局的六端都按序应用了全部压力编辑，全部 576 个区块的密度校验值和跨区块接缝的碰撞射线落点一致。
- 两局各检查了 17 次六端状态，包括三队各开四只线索箱。每局验证 12 组同队重复开箱被拒，敌队收不到本队的精确线索，未集齐四点时交点为空。
- 三线索队伍面对露出的大奖无法开箱。两支符合条件的队伍争抢时只结算一次。两局均验证溺亡返回本队码头并保留进度和地形。房主重开后六端的线索、编辑序号和地形全部重置，并生成新宝箱布局。
- 正常网络额外验证了第七人被拒、来宾退出后对局继续、房主退出后其余会话结束。
- 首轮弱网最慢来宾的 P95 为 155.135 ms。客户端接收可靠指令及提交碰撞后直接启动下一笔按序重建，重包后两种网络的性能门槛均通过。
- 最终包中的任务 01 地图也通过了双进程回归。跨洞、踩回填、占位拒绝、攀爬松手和长按 R 均通过。100 次压力编辑结束后，两端序号为 104，全部 24 个区块的密度校验和一致。见 [双人回归汇总](../ActDemo/Saved/IslandQA/Task01Final/normal/summary.json)。

六端自动化覆盖了联机规则、数据和碰撞。跨两台物理 Mac 和六位真人操作尚未实测。

## 派对玩法复测 · 2026-11-24

同一台 Mac 上用六个独立进程验证。正常网络和模拟弱网都连续触发两轮，六端每轮从 `Installed` 进入 `Active`，返回原角色后卸载。另测了玩法中掉线和加载等待超时。六端保留地形与队内线索，回海岛后空格恢复跳跃。 [正常网络](../ActDemo/Saved/IslandQA/party/normal/summary.json) · [模拟弱网](../ActDemo/Saved/IslandQA/party/emulated/summary.json) · [掉线](../ActDemo/Saved/IslandQA/party-drop/normal/summary.json) · [加载超时](../ActDemo/Saved/IslandQA/party-timeout/normal/summary.json)

关闭假箱后，正常网络和模拟弱网都跑通挖箱、大奖结算和重开。正常网络通过原性能门槛。两轮模拟弱网复测的房主帧 P95 分别为 183.761 ms 和 485.170 ms，房主碰撞延迟 P95 分别为 494.785 ms 和 2100.789 ms，均超出原门槛。 [正常寻宝](../ActDemo/Saved/IslandQA/normal/summary.json) · [弱网寻宝复测 1](../ActDemo/Saved/IslandQARetry/emulated/summary.json) · [弱网寻宝复测 2](../ActDemo/Saved/IslandQAFinal/emulated/summary.json)

## 八泊位远征岛 · 白模编辑

新图为 `/Game/TreasureIsland/L_ExpeditionIsland`。它有约 160 米的陆地跨度、八个靠岸区和四块公共搜索区。岛内辐射状低地通往深岩区，沿岸的下凹环路绕过较高的开挖台地。低矮的双石堆标出外环，单根高石柱引向内圈。四区分别用断拱 A、双柱 B、采石台阶 C、斜岩 D 辨认，地标设置在搜索圈外沿。

- **首次创建与验图**　编译编辑器后，执行 `bash Scripts/actdemo.sh island-map -Expedition -VerifyExpedition`。关卡已存在时只读取和验证，保留设计师摆放。命令校验八座码头、四个地标、寻宝布局，以及一次从粗地表切换到可开挖岩层的碰撞变化。测试挖痕只留在该次验证进程内。如基础地形算法更新，在上述命令中显式追加 `-RebakeExpeditionSurface`，即可只重烘这张图的基础表面与码头号码，同时保留已有笔刷、搜索圈、泊位及地标的摆放。
- **编辑路线和宝藏区**　从编辑器打开新图，在 **Voxel Map → Frame Island** 观察全岛。Add Rock / Carve Rock 调坡、打通隘口或加高观察点。Search Circles 移动 A–D、调整半径。维持 A–C 与 B–D 在中央深岩内部相交，为所有候选埋箱点的上方保留可挖岩土。保存当前关卡即可写入地形笔刷与搜索圈。
- **编辑起点与镜头**　选中 **Crew landings** 下的 Landing，沿本地 X 轴正方向面朝岛内。整体移动 Landing 会同步码头、队伍落脚位和海图图钉。在 Details 的 Expedition 中调八个局部 Berths、Preview Eye 和 Preview Target。调完从 1 人、同队多人及相对岸 1v1 三种开场检查队友入镜、镜头无遮挡与通岸坡度，最后从八处实走到最近搜索圈比较时间。
- **编辑地标**　**Excavation landmarks** 内的四个 Actor 同时提供海图图钉和白模，调整 Actor 或其可编辑网格组件。**Trail markers** 存放转向石堆与信标。用正常第三人称对局镜头从海岸及内圈两侧识别地标。码头可以破坏，保留邻近安全岸线。
- **首圈路线测量**　2026-11-24 重烘新图后，从八个码头中心采样甲板、入岸坡与最近圈边缘的可行走地表，按角色默认步速 370 cm/s 换算得到 10.54–10.55 秒，最大差为 0.01 秒。该测量不计转镜头与玩家选择路线的时间。
- **基础地表与开挖**　常态地表与碰撞已随关卡烘焙。首次凿入一个四米宽地形列时，以 25 厘米采样重建局部洞壁、地表与碰撞。其他未开挖列沿用基础表面，运行时按序同步笔刷。原来的寻宝图与双人体素场景保留既有网格和启动流程。

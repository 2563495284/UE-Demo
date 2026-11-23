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

## 构建和复测

```sh
bash Scripts/actdemo.sh build
bash Scripts/actdemo.sh island-map
bash Scripts/actdemo.sh island-package
PYTHONDONTWRITEBYTECODE=1 python3 Scripts/island_qa.py --mode both
```

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

逐端数据与原始日志见 [正常网络汇总](../ActDemo/Saved/IslandQA/normal/summary.json) 和 [弱网汇总](../ActDemo/Saved/IslandQA/emulated/summary.json)。

# 双人可挖掘地形原型

在两台同一局域网内的 Mac 上使用同一份 Development 包。房主启动后，另一名玩家用房主的局域网 IPv4 地址加入。默认端口为 UDP 7777。

```bash
bash Scripts/actdemo.sh voxel-host
bash Scripts/actdemo.sh voxel-join <房主局域网IP>
```

把 `Builds/VoxelPlayground/Mac/ActDemo.app` 单独拷到另一台 Mac 时，可以通过系统的 `open` 直接传入启动参数。

```bash
open -n -a /path/to/ActDemo.app --args '/Game/VoxelPlayground/L_VoxelPlayground?listen' -windowed -ResX=1280 -ResY=720
open -n -a /path/to/ActDemo.app --args <房主局域网IP> -windowed -ResX=1280 -ResY=720
```

两端加载并核对初始地形后开始。先在 `WALL` 砸穿岩壁，让另一名玩家走过洞口。到 `SHAFT` 对着脚下砸出竖井。到 `CLIMB / FILL` 贴墙攀爬或把岩石补成踏板。回填会避开玩家，长按 R 两秒返回起点。重新开图会恢复初始地形。

| 操作 | 键位 |
| --- | --- |
| 移动、镜头、跳跃 | WASD、鼠标、空格 |
| 砸墙、向下挖 | 鼠标左键、鼠标右键 |
| 攀爬 | 按住左 Shift，W/S 上下移动 |
| 回填 | Q |
| 脱困 | 长按 R 两秒 |

编辑器中可打开 `L_VoxelPlayground`，将 PIE 设为 2 名玩家和 `Play As Listen Server`。构建与复测命令如下。`voxel-map` 在缺少地图时生成白模场景。打包输出在 `Builds/VoxelPlayground/Mac`。

```bash
bash Scripts/actdemo.sh build
bash Scripts/actdemo.sh voxel-map
bash Scripts/actdemo.sh voxel-package
python3 Scripts/voxel_qa.py --mode both
python3 Scripts/voxel_qa.py --render --mode normal --output ActDemo/Saved/VoxelQA/Metal
```

验收脚本默认读取 Mac 的 `en0` 地址。可用 `--ip` 指定其他局域网地址，或用 `--render` 测试带 Metal 渲染的同一流程。脚本用独立游戏进程测试挖洞通行、回填承重、占位拒绝、攀爬面消失、向下挖掘和远端长按脱困，然后用固定笔刷参数交替提交 100 次编辑来验证同步。带网络模拟的一轮在两端分别配置 40 ms 出包延迟和 2% 出包丢失率，目标往返延迟 80 ms。

2026-11-23 在 Apple M3 Max 上用归档包验收。两个独立进程通过本机局域网地址连接，限帧 60，Metal 窗口为 960 × 540。这次使用一台物理机。

三轮均完成 4 笔玩家操作验证和 100 笔交错编辑，最终序号 104，主客端 24 个区块的密度校验值在每轮内一致。两端复测了洞口、踏板和跨区块接缝。普通连接还验证了第三人入房被拒绝、房主退出时客机断开，以及重新开图后岩壁恢复初始碰撞。

| 归档包验收 | 房主帧时间 P95 | 房主收到请求到碰撞生效 P95 | 客机收到指令到本地碰撞生效 P95 |
| --- | ---: | ---: | ---: |
| 普通连接 · NullRHI | 18.69 ms | 35.43 ms | 20.56 ms |
| 80 ms RTT、2% 丢包 · NullRHI | 18.81 ms | 35.33 ms | 21.16 ms |
| 普通连接 · Metal | 19.03 ms | 35.98 ms | 19.89 ms |

两列碰撞耗时从各自进程的消息入口起算，不含消息在网络中的传输时间。原始日志和 JSON 位于 `ActDemo/Saved/VoxelQA/PackagedFinal` 和 `ActDemo/Saved/VoxelQA/PackagedMetalFinal`，Metal 目录中保留了砸穿岩壁后的截图。

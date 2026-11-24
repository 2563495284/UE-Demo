# ActDemo · 体素海岛寻宝

UE 5.8 项目，入口为 `ActDemo/ActDemo.uproject`。游戏从白模码头启动。在同一局域网内创建或搜索房间，1–8 名玩家自由组队，在远征岛上挖掘、收集线索、争夺大奖。单人可以直接开局。

- [远征岛与房间](Docs/Expedition.md)包含新玩家流程、设计分工和地图调试入口。
- [原六人寻宝场](Docs/TreasureIsland.md)包含经典 2v2v2、体素编辑器、Mac 打包和六进程回归命令。
- [双人可挖掘地形原型](Docs/VoxelPlayground.md)包含 `L_VoxelPlayground` 地图、操作和双进程复测命令。

## Mac 构建与启动

安装 Unreal Engine 5.8 和 Xcode，运行以下命令编译编辑器目标，再从源码启动游戏前台。也可打开工程，在编辑器中选择关卡和 PIE 联机设置。

```sh
bash Scripts/actdemo.sh build
bash Scripts/actdemo.sh play
```

引擎路径可通过 `UE_ROOT` 覆盖。`island-package` 将前台、远征岛及原型关卡一同 Cook，归档到 `Builds/TreasureIsland/Mac/ActDemo.app`。在 Finder 里双击即可进入开始页面。

## Git 与资源协作

Git LFS 管理 UE 地图、资源与可编辑素材。克隆后在仓库根目录执行以下命令，确认资源下载成功后再打开 UE。

```sh
git lfs install --local
git lfs pull
git lfs locks
```

修改已有二进制资源前先同步分支，并用 `git lfs lock` 锁定对应路径。资源与依赖的代码、配置随提交一起推送，合入主分支后用 `git lfs unlock` 解除锁定。生成命令使用 `-Force` 覆盖地图时也采用同样的锁定流程。`.lfsconfig` 启用推送前的锁检查。

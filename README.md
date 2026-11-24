# ActDemo · 体素海岛寻宝

UE 5.8 项目，入口为 `ActDemo/ActDemo.uproject`，默认打开 `/Game/TreasureIsland/L_TreasureIsland`。房主可启动局域网 2v2v2 对局，六名玩家分队挖掘地形、收集线索并抢夺大奖。

- [海岛寻宝](Docs/TreasureIsland.md)包含玩法、Mac 开房加入、打包和六进程复测命令。
- [双人可挖掘地形原型](Docs/VoxelPlayground.md)包含 `L_VoxelPlayground` 地图、操作和双进程复测命令。

## Mac 构建与启动

安装 Unreal Engine 5.8 和 Xcode，运行以下命令编译编辑器目标，再从源码启动海岛房主。也可打开工程，在编辑器中选择关卡和 PIE 联机设置。

```sh
bash Scripts/actdemo.sh build
bash Scripts/actdemo.sh play
```

引擎路径可通过 `UE_ROOT` 覆盖。`island-package` 和 `voxel-package` 都会 Cook 两张联机地图，分别归档到对应的 `Builds` 目录。

## Git 与资源协作

Git LFS 管理 UE 地图、资源与可编辑素材。克隆后在仓库根目录执行以下命令，确认资源下载成功后再打开 UE。

```sh
git lfs install --local
git lfs pull
git lfs locks
```

修改已有二进制资源前先同步分支，并用 `git lfs lock` 锁定对应路径。资源与依赖的代码、配置随提交一起推送，合入主分支后用 `git lfs unlock` 解除锁定。生成命令使用 `-Force` 覆盖地图时也采用同样的锁定流程。`.lfsconfig` 启用推送前的锁检查。

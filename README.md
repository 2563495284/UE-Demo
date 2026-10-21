# ActDemo — Ashen Court

UE 5.8.2 第三人称单机动作灰盒。入口是 `ActDemo/ActDemo.uproject`，默认地图为 `/Game/ActDemo/Maps/L_AshenCourt`。原有 `SampleLevel` 保留。

## Git 与资源协作

代码、配置和文档使用普通 Git；UE 资源、FBX、Blend、PSD 和 WAV 由 `.gitattributes` 指定使用 Git LFS。`Content` 必须完整同步，缓存和构建产物继续按 `.gitignore` 排除。

先安装 Git LFS（macOS：`brew install git-lfs`；Windows 可使用 Git for Windows 安装器中的 Git LFS），确认 `git lfs version` 可用。克隆后，在仓库根目录执行：

```sh
git lfs install --local
git lfs pull
git lfs locks
```

每个新克隆都要执行初始化；已有克隆在首次拉取 LFS 改动前执行 `git lfs install --local`。打开 UE 前确认 `git lfs pull` 成功，避免编辑器读取到文本指针。正常提交后，`git push` 的 LFS hook 会上传资源。

修改二进制资源前，先同步分支并锁定对应文件，例如：

```sh
git lfs lock ActDemo/Content/ActDemo/Kwang/ABP_Kwang.uasset
```

完成编辑后，把资源及其依赖的代码、配置一起提交并推送；使用功能分支时，等合入主分支再解锁：

```sh
git lfs unlock ActDemo/Content/ActDemo/Kwang/ABP_Kwang.uasset
```

`.lfsconfig` 开启推送前锁检查；锁服务不可用或文件被他人锁定时应先解决问题。资源锁按文件路径协调，同一资源在不同分支上也不要同时编辑。`assets -Force`、`kwang` 等生成命令同样需要先锁定它们会覆盖的已有资产。

第三方原包（如 `Content/ParagonKwang`）尽量保持原样，适配、派生和修改后的副本放到 `Content/ActDemo`。不要把整个资源目录压成一个 ZIP 提交。LFS 的每个新资源版本都会占用存储，下载会消耗远端流量额度；不执行全历史资源下载作为日常同步。

## 运行

Mac 需要安装 Unreal Engine 5.8 与 Xcode。打开工程，等待编译和着色器准备，点击 Play。也可在仓库根目录运行：

```sh
bash Scripts/actdemo.sh build
bash Scripts/actdemo.sh play
```

`UE_ROOT` 可覆盖脚本中的引擎路径。独立包生成到 `Builds/Mac`：

```sh
bash Scripts/actdemo.sh smoke
bash Scripts/actdemo.sh pie
bash Scripts/actdemo.sh package
bash Scripts/actdemo.sh packaged-smoke
```

打包后也可直接打开 `Builds/Mac/ActDemo.app`。`package` 包含 Mac 的 Cook、Stage、Package 和 Archive；`packaged-smoke` 检查实际独立包，不依赖编辑器世界。

## 操作与流程

| 操作 | 键位 |
| --- | --- |
| 移动 / 自由镜头 | WASD / 鼠标 |
| 冲刺 | 按住左 Shift，消耗体力 |
| 三段轻击 | 鼠标左键；连段窗口内再次点击 |
| 重击 | 鼠标右键 |
| 闪避 | Space；有方向输入时朝该方向，否则朝前 |
| 锁定 / 切换目标 | Tab / E |
| 恢复道具 | Q，初始 3 次，满血不能使用 |
| 暂停 / 继续 | Esc |
| 失败或暂停时重试 | R，或点击界面按钮 |
| 调试显示 | F1：命中轨迹、感知范围、AI 状态和属性 |

从练习场出发，经过两组巡逻敌人，靠近中央琥珀色信标激活检查点，再穿过第二道门挑战 Warden。检查点当次回满生命、体力与恢复次数；死亡后从信标前重生，前半场不再刷新。Boss 半血后加速并增加重击频率。胜利或暂停界面可以开始新一轮。

## 实现分工

`输入 / AI → GameplayTag 请求能力 → GAS Ability → Montage / Notify / Sweep → GameplayEffect → 属性事件 → UMG / 表现`。

| 目录 | 职责 |
| --- | --- |
| `Source/ActDemo/Character` | 共用战斗角色、ASC、移动、输入缓存、锁定、玩家输入上下文 |
| `Source/ActDemo/Combat` | 原生标签、属性与瞬时 GE、四类能力、动画窗口 Notify、数据资产类型 |
| `Source/ActDemo/AI` | AIController、视觉/听觉感知、StateTree 原生条件与任务、技能结束回调 |
| `Source/ActDemo/UI` | UUserWidget 属性/流程订阅、敌人血条、锁定标记、暂停/失败/胜利界面 |
| `Source/ActDemo/World` | 敌人生成、检查点、死亡/重试/胜利与对象销毁 |
| `Source/ActDemo/Tests` | `-ActDemoSmoke` 可选真实游戏世界回归检查，Shipping 不自动启用 |
| `Source/ActDemoEditor` | 仅编辑器使用的资源生成 Commandlet，不进入运行时模块 |

StateTree 包含 Patrol、Investigate、Chase、Attack、Recover、Search、Return、Dead。感知保存目标与最后位置；Attack 任务订阅能力结束，退出时清理订阅并取消其发起的技能。AI 与玩家使用相同伤害、体力、动作互斥和死亡路径。

## 配置入口

内容浏览器中的 `/Game/ActDemo`：

- `Data/DA_Combat`：三段轻击、重击、伤害、体力、蒙太奇、闪避与恢复参数。
- `Data/DA_Player / DA_Scout / DA_Brute / DA_Boss / DA_Dummy`：生命、伤害倍率、速度、感知距离、巡逻与返回参数；可指定兼容骨架的 Mesh、AnimClass、Combat。
- `Input/DA_Input`、`IMC_Gameplay`、`IMC_Menu`：Enhanced Input 资源和键位。
- `Animation/AM_*`：每段动作使用 `DefaultSlot`、`Action` Section；NotifyState 0 为伤害窗口，1 为无敌窗口，2 为连段窗口。
- `AI/ST_Enemy`：可打开查看状态与原生任务。
- `UI/WBP_HUD`：可编辑布局。命名控件由 C++ 绑定，修改时保留绑定名。
- `Maps/L_AshenCourt`：灰盒测试关卡；`ActSpawnPoint` 可调整敌人类型、组别与角色数据。

**调整攻击 Duration 后，要同步检查蒙太奇 Notify 的归一化位置。** 窗口由 Notify 驱动；没有蒙太奇时才使用时间参数兜底。取消或死亡会关闭伤害、连段和无敌窗口，并清除缓冲输入。

资源已保存进工程，正常打开无需重新生成。以下命令仅用于复建开发基线：

```sh
bash Scripts/actdemo.sh assets          # 已有地图时跳过
bash Scripts/actdemo.sh assets -Force   # 覆盖 ActDemo 生成资源，包括地图和 HUD
```

`-Force` 不用于保留手工美术修改的场景。不要在编辑器同时修改这些资源时执行生成。

## 资源与边界

灰盒采用随 UE 安装的 Manny、Unarmed AnimBP、基础动作、Level Prototyping 与 Combat Niagara 示例；使用范围遵循 Epic 对应许可。`SourceAudio` 中两段 WAV 为本项目合成占位音效。

当前模板轻击是徒手动作，手持发光方块表示武器。命中同时使用武器轨迹及前方短距离 Sweep，保留遮挡检测和单段目标去重。正式剑动画接入后应校准武器插槽、根运动和 Notify，再去掉徒手占位用的前方补偿 Sweep。恢复道具暂用动作锁定与数值反馈。

Kwang 资源的下载、骨架适配和正式剑动画验收另见 `Docs/Acceptance.md` 的实际状态。没有把模型包视作完整动作包。项目尚不包括联网、格挡、弹反、装备背包或跨会话存档。

编译、运行检查、PIE、独立包与人工试玩分别记录在 `Docs/Acceptance.md`；自动战斗检查不等同于玩家完整通关或手感验收。

#include "Island/IslandMessageText.h"

#include "Containers/Map.h"
#include "ExpeditionText.h"

namespace
{
    struct FMessage {
        const TCHAR* Chinese;
        const TCHAR* English;
    };

    // The room and terrain protocols predate localization and use both native languages on the wire.
    constexpr FMessage Messages[] = {
        {TEXT("本队已取得这个箱子的碎片"), TEXT("Your crew already has this chest's fragment.")},
        {TEXT("钥匙已合成，还需集齐四个真箱点位"), TEXT("Key assembled. Chart all four genuine chests to open the vault.")},
        {TEXT("需要集齐四个真箱点位和三片碎片合成的钥匙"), TEXT("Chart all four genuine chests and assemble the key from three fragments.")},
        {TEXT("身份混淆期间无法发送集合信号"), TEXT("Rally signals are unavailable while identities are hidden.")},
        {TEXT("箱锁已被占用"), TEXT("The lock is occupied.")},
        {TEXT("箱锁已易手"), TEXT("The lock changed hands.")},
        {TEXT("该房间已开局，请选择其他房间。"), TEXT("This room has already started. Choose another room.")},
        {TEXT("房间已满，请选择其他房间。"), TEXT("This room is full. Choose another room.")},
        {TEXT("上一局的成员正在返回码头，请稍后刷新。"), TEXT("Players are returning to the dock. Refresh in a moment.")},
        {TEXT("该房间已开局。"), TEXT("This room has already started.")},
        {TEXT("游戏版本不同，请使用与房主相同的版本。"), TEXT("Version mismatch. Use the same game version as the host.")},
        {TEXT("连接未成功，请刷新房间或检查局域网地址。"), TEXT("Connection failed. Refresh the list or check the LAN address.")},
        {TEXT("请填写房名，并为每位玩家留出可用的队伍位置。"), TEXT("Enter a room name and provide enough team slots for all players.")},
        {TEXT("局域网服务未就绪。"), TEXT("LAN services are not ready yet.")},
        {TEXT("此版本尚未配置 Epic 在线服务。"), TEXT("Epic Online Services is not configured in this build.")},
        {TEXT("Epic 在线服务暂不可用，请稍后重试。"), TEXT("Epic Online Services is unavailable. Try again later.")},
        {TEXT("正在连接互联网大厅…"), TEXT("Connecting to the online lobby…")},
        {TEXT("此版本缺少互联网联机配置。"), TEXT("Online play is not configured in this build.")},
        {TEXT("在线服务尚未就绪，请重试。"), TEXT("Online services are not ready. Try again.")},
        {TEXT("游戏尚未就绪，请重试。"), TEXT("The game is not ready yet. Try again.")},
        {TEXT("同一进程已使用游客身份，请在另一台设备上联机。"), TEXT("This game instance already has a guest. Use another device for the other player.")},
        {TEXT("无法启动在线服务，请检查网络后重试。"), TEXT("Could not start online services. Check your connection and try again.")},
        {TEXT("互联网连接初始化失败，请重试。"), TEXT("Could not start the internet connection. Try again.")},
        {TEXT("在线服务已关闭。"), TEXT("Online services have closed.")},
        {TEXT("无法连接在线服务，请检查网络后重试。"), TEXT("Could not reach online services. Check your connection and try again.")},
        {TEXT("正在搜索互联网房间…"), TEXT("Searching for internet rooms…")},
        {TEXT("无法搜索互联网房间。"), TEXT("Could not search for internet rooms.")},
        {TEXT("无法创建互联网房间。"), TEXT("Could not create the internet room.")},
        {TEXT("互联网房间连接超时，请刷新后重试。"), TEXT("The internet room connection timed out. Refresh the list and try again.")},
        {TEXT("正在取消…"), TEXT("Cancelling…")},
        {TEXT("正在同步房间信息…"), TEXT("Syncing room details…")},
        {TEXT("正在创建房间…"), TEXT("Creating room…")},
        {TEXT("无法在本机创建房间。"), TEXT("Could not host a room on this device.")},
        {TEXT("无法创建局域网房间。"), TEXT("Could not create the LAN room.")},
        {TEXT("正在前往码头…"), TEXT("Travelling to the dock…")},
        {TEXT("正在搜索局域网…"), TEXT("Searching the LAN…")},
        {TEXT("无法搜索局域网。"), TEXT("Could not search the LAN.")},
        {TEXT("搜索失败，请检查局域网连接。"), TEXT("Search failed. Check your LAN connection.")},
        {TEXT("尚未找到可加入的房间"), TEXT("No rooms available to join")},
        {TEXT("房间已失效，请刷新。"), TEXT("This room is no longer available. Refresh the list.")},
        {TEXT("正在加入房间…"), TEXT("Joining room…")},
        {TEXT("未能加入房间，请刷新。"), TEXT("Could not join the room. Refresh the list.")},
        {TEXT("房间已满。"), TEXT("The room is full.")},
        {TEXT("房间已关闭或无法连接。"), TEXT("The room is closed or unreachable.")},
        {TEXT("端口应在 1 到 65535 之间。"), TEXT("Use a port between 1 and 65535.")},
        {TEXT("请输入局域网 IPv4 地址，可附端口。"), TEXT("Enter a LAN IPv4 address, optionally followed by a port.")},
        {TEXT("控制器尚未就绪。"), TEXT("The player controller is not ready.")},
        {TEXT("正在连接码头…"), TEXT("Connecting to the dock…")},
        {TEXT("正在离开房间…"), TEXT("Leaving room…")},
        {TEXT("本机未能打开联机端口，请返回后重试。"), TEXT("Could not open the network port. Go back and try again.")},
        {TEXT("与房主失去连接，请刷新房间列表。"), TEXT("Lost connection to the host. Refresh the room list.")},
        {TEXT("连接超时，请检查房主地址和局域网连接。"), TEXT("Connection timed out. Check the host address and LAN connection.")},
        {TEXT("正在返回码头…"), TEXT("Returning to the dock…")},
        {TEXT("正在等待原房间成员返回。"), TEXT("Waiting for returning room members.")},
        {TEXT("未返回成员的预留席位已开放。"), TEXT("Unused reserved slots are now open.")},
        {TEXT("地图版本不一致，请使用同一版本后重新加入。"), TEXT("Map versions differ. Rejoin with the same game version.")},
        {TEXT("这张地图没有足够的独立出生位，请检查码头布局。"), TEXT("Not enough spawn positions. Check the dock layout.")},
        {TEXT("搜索圈种子复验失败"), TEXT("Could not verify the search circles.")},
        {TEXT("中立场景物体种子复验失败"), TEXT("Could not verify the island props.")},
        {TEXT("有成员离开，请重新确认出航。"), TEXT("A player left. Start the expedition again when ready.")},
        {TEXT("该队已满或已关闭。"), TEXT("This team is full or closed.")},
        {TEXT("有成员取消准备。"), TEXT("A player is no longer ready.")},
        {TEXT("只能在候船时更换角色。"), TEXT("Characters can only be changed while waiting to depart.")},
        {TEXT("房间设置必须能容纳当前所有玩家。"), TEXT("The room must have space for everyone already here.")},
        {TEXT("请先调整队伍，当前席位超出新设置。"), TEXT("Move players between teams to fit the new limits first.")},
        {TEXT("房间设置已更新，请重新准备。"), TEXT("Room settings changed. Ready up again.")},
        {TEXT("当前没有同队玩家可以集合"), TEXT("You have no teammates to rally.")},
        {TEXT("正在返回房间…"), TEXT("Returning to the room…")},
        {TEXT("返回房间失败，请重试。"), TEXT("Could not return to the room. Try again.")},
        {TEXT("小游戏未能就绪，已回到寻宝岛"), TEXT("The minigame could not start. Back on Treasure Island.")},
        {TEXT("此箱的小游戏暂时无法进入"), TEXT("This chest’s minigame is unavailable.")},
        {TEXT("先离开正在撬的宝箱"), TEXT("Step away from the chest you are unlocking first.")},
        {TEXT("已有两人合撬"), TEXT("Two teammates already have the lock.")},
        {TEXT("对手正在合撬，箱盖已稳住"), TEXT("Two rivals have secured the lock.")},
        {TEXT("对手已经接锁，守住了这次开箱"), TEXT("The rival has already taken and secured the lock.")},
        {TEXT("对手夺走箱锁"), TEXT("A rival stole the lock.")},
        {TEXT("地形同步失败，请使用同一版本重新加入房间"), TEXT("Terrain sync failed. Rejoin using the same game version.")},
        {TEXT("请靠近并瞄准岩面"), TEXT("Aim at rock within reach")},
        {TEXT("宝箱周围不能回填"), TEXT("Cannot fill over a chest")},
        {TEXT("有玩家站在回填区域"), TEXT("Someone is in the way")},
        {TEXT("有玩家进入回填区域，本次回填已取消"), TEXT("Someone moved into the fill area")},
        {TEXT("这里没有可改变的岩面"), TEXT("No rock changed")},
        {TEXT("请靠近已露出且无遮挡的箱子"), TEXT("Reach an exposed chest")},
        {TEXT("集齐四条线索才能打开宝藏"), TEXT("Collect all four clues to open the prize")},
        {TEXT("本队已经取得这条线索"), TEXT("Your crew already has this clue")},
        {TEXT("此小游戏暂时无法进入"), TEXT("This party challenge is unavailable")},
        {TEXT("同一成员已在房间内。"), TEXT("Duplicate room member.")},
        {TEXT("有成员离开，重新打开地图可以再玩一局。"), TEXT("Player left. Reopen the map to play again.")},
        {TEXT("地形不一致，两位玩家需要使用相同版本。"), TEXT("Terrain mismatch. Both players need the same build.")},
    };
}

FString IslandMessageText::Display(const FString& Message)
{
    if (Message.IsEmpty()) return {};
    static const TMap<FString, const FMessage*> Index = []
    {
        TMap<FString, const FMessage*> Lookup;
        for (const FMessage& Entry : Messages)
        {
            Lookup.Add(Entry.Chinese, &Entry);
            Lookup.Add(Entry.English, &Entry);
        }
        return Lookup;
    }();
    if (const auto* Entry = Index.Find(Message))
        return ExpeditionText::IsEnglish() ? (*Entry)->English : (*Entry)->Chinese;
    return Message;
}

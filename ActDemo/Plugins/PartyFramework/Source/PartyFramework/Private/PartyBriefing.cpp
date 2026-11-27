#include "PartyBriefing.h"
#include "ExpeditionText.h"
#include "Algo/Find.h"

namespace
{
    struct FCopy
    {
        const TCHAR* Chinese;
        const TCHAR* English;
        const TCHAR* Get(bool bEnglish) const { return bEnglish ? English : Chinese; }
    };

    struct FGame
    {
        FName Feature;
        FCopy Title, Rules, Controls, Goal, Result;
        bool bIndividual = false;
    };

    const FGame Games[] = {
        {
            TEXT("PartyReefRun"),
            {TEXT("穿礁竞速"), TEXT("Reef Run")},
            {TEXT("三道珊瑚墙横在跑道上。挖出通道、借用别人的洞，或堆坡翻墙。"), TEXT("Three reef walls block the course. Dig a tunnel, use another runner’s opening, or build a ramp.")},
            {TEXT("WASD 移动，空格跳。左键挖墙，按住右键堆坡。"), TEXT("WASD to move, Space to jump. Left-click to dig, hold right-click to build.")},
            {TEXT("45 秒内越过三道墙，踏上终点金线。"), TEXT("Clear all three walls and step onto the gold finish line within 45 seconds.")},
            {TEXT("先到终点获胜。超时比已越过的墙数。"), TEXT("First to finish wins. At timeout, most walls cleared wins.")},
            true
        }, {
            TEXT("PartyBeaconClimb"),
            {TEXT("灯塔登顶"), TEXT("Beacon Climb")},
            {TEXT("三座灯台逐级升高。沿台边堆土搭阶，每登上一层就记一分。"), TEXT("Build steps beside the rising beacon platforms. Land on each level in order to score.")},
            {TEXT("瞄准台边按住右键堆土，WASD 移动，空格跳上石台。"), TEXT("Aim at a platform edge and hold right-click to pile soil. Move with WASD and jump with Space.")},
            {TEXT("65 秒内依次站稳三层灯台。"), TEXT("Reach all three platforms in order within 65 seconds.")},
            {TEXT("先登顶获胜。超时比已登上的层数。"), TEXT("First to the summit wins. At timeout, most levels reached wins.")},
            true
        }, {
            TEXT("PartyCrystalHunt"),
            {TEXT("晶洞寻珠"), TEXT("Crystal Hunt")},
            {TEXT("每处发光矿脉里都藏着一颗金晶。敲开泥壳，打碎金晶的人得分。"), TEXT("Each glowing vein hides a crystal. Break away its clay shell, then strike the crystal to claim it.")},
            {TEXT("WASD 靠近矿脉。瞄准后点左键挖掘，按住再松开可蓄力。"), TEXT("Use WASD to approach. Aim and left-click to dig. Hold and release for a charged strike.")},
            {TEXT("50 秒内抢到四颗金晶。"), TEXT("Claim four crystals within 50 seconds.")},
            {TEXT("先得四颗获胜。超时比金晶数。"), TEXT("First to four wins. At timeout, most crystals wins.")},
            true
        }, {
            TEXT("PartyBridgeDash"),
            {TEXT("断桥接力"), TEXT("Bridge Relay")},
            {TEXT("在低桩上堆土，把两岸连成可走的桥。任何人都能过桥，也能挖断桥。"), TEXT("Pile soil on the low posts to connect both banks. Anyone can cross or dig through the bridge.")},
            {TEXT("按住右键铺桥，WASD 从近岸走上已接通的桥，再踏上对岸。"), TEXT("Hold right-click to build. Use WASD to walk from the near bank onto the connected deck, then onto the far bank.")},
            {TEXT("70 秒内亲自走过两段断桥。"), TEXT("Cross both gaps yourself within 70 seconds.")},
            {TEXT("先过两段获胜。超时比已过桥数。"), TEXT("First across both bridges wins. At timeout, most bridges crossed wins.")},
            true
        }, {
            TEXT("PartyFloodGates"),
            {TEXT("海堤补漏"), TEXT("Floodgates")},
            {TEXT("你面前有四条水渠。堵严一条，出口灯就从蓝变金，每秒得一分。"), TEXT("Your four channels are straight ahead. Seal a channel to turn its outlet gold and earn one point per second.")},
            {TEXT("WASD 移动，按住右键向渠中填土。瞄准对手的土塞点左键能挖开它。"), TEXT("Move with WASD. Hold right-click to fill a channel across its width. Left-click to dig through a rival’s plug.")},
            {TEXT("50 秒内累计 48 分。多堵几条，得分更快。"), TEXT("Earn 48 points within 50 seconds. Seal more channels to score faster.")},
            {TEXT("先到 48 分获胜。超时比分数。"), TEXT("First to 48 wins. At timeout, highest score wins.")},
            true
        }, {
            TEXT("PartyCrownHold"),
            {TEXT("潮滩争旗"), TEXT("Capture the Flag")},
            {TEXT("金旗每 14 秒换台。独自在亮金旗台上每秒得一分，有人同台就暂停计分。"), TEXT("The gold flag changes platforms every 14 seconds. Earn one point per second alone on its platform. Scoring pauses when contested.")},
            {TEXT("WASD 移动，按住右键搭阶，空格跳上亮金旗台。"), TEXT("Move with WASD. Hold right-click to build steps and jump onto the gold platform with Space.")},
            {TEXT("60 秒内累计独占 12 秒。"), TEXT("Accumulate 12 seconds of sole control within 60 seconds.")},
            {TEXT("先到 12 分获胜。超时比累计分数。"), TEXT("First to 12 wins. At timeout, highest score wins.")},
            true
        }, {
            TEXT("PartyMazeChase"),
            {TEXT("迷宫追捕"), TEXT("Maze Chase")},
            {TEXT("一队逃跑，其余队联手追捕。逃方先行 8 秒，被抓后可观战队友。"), TEXT("One crew runs and the others hunt. Runners get an 8-second head start. Tagged runners spectate.")},
            {TEXT("WASD 移动。左键拆内墙，右键补缺口。追方在 2.1 米内瞄准逃方按右键抓人。"), TEXT("Use WASD to move. Left-click to dig through inner walls, right-click to patch. Hunters aim and right-click within 2.1 m to tag.")},
            {TEXT("追方要抓齐所有逃方。逃方要撑过三分钟追捕。"), TEXT("Hunters must tag every runner. Runners must survive three minutes of pursuit.")},
            {TEXT("抓齐则所有追方队获胜。时间到仍有一人逃脱，整支逃方队获胜。"), TEXT("Tag everyone and all hunting crews win. If any runner survives, the whole running crew wins.")}
        }, {
            TEXT("PartyButtonChallenge"),
            {TEXT("按键挑战"), TEXT("Button Challenge")},
            {TEXT("站在自己的台座上，等三秒倒数结束。所有人都收到确认提示。"), TEXT("Wait on your podium for the three-second countdown. Everyone then gets a confirmation prompt.")},
            {TEXT("看见「按空格确认」后，按一次空格。"), TEXT("Press Space once when the “Press Space” prompt appears.")},
            {TEXT("八秒内完成自己的确认。"), TEXT("Confirm within eight seconds.")},
            {TEXT("全员确认或时间到就结算，显示确认人数，然后一起返岛。"), TEXT("The round ends when everyone confirms or time runs out. See the final count, then return to the island.")}
        }
    };
}

TOptional<FPartyBriefing> PartyBriefings::Find(FName Feature)
{
    const auto* Game = Algo::FindBy(Games, Feature, &FGame::Feature);
    if (!Game) return {};

    const bool bEnglish = ExpeditionText::IsEnglish();
    FString Result(Game->Result.Get(bEnglish));
    if (Game->bIndividual)
        Result += bEnglish ? TEXT(" Earlier scores win ties. Simultaneous ties are broken by lot. All zero means no winner.")
                           : TEXT("同分先达到者胜，同时达到则抽签。全员零分则无人获胜。");
    return FPartyBriefing{Game->Title.Get(bEnglish), Game->Rules.Get(bEnglish), Game->Controls.Get(bEnglish),
        Game->Goal.Get(bEnglish), MoveTemp(Result), Game->Feature.ToString() + TEXT(".mp4")};
}

#pragma once

#include "Brushes/SlateColorBrush.h"
#include "Styling/CoreStyle.h"

namespace IslandMenuStyle
{
    inline const FLinearColor Ink(.9f, .93f, .91f);
    inline const FLinearColor Muted(.62f, .69f, .68f);
    inline const FLinearColor Alert(.98f, .76f, .50f);
    inline const FSlateColorBrush Backing(FLinearColor(.018f, .032f, .040f, .95f));
    inline const FSlateColorBrush Accent(FLinearColor(.61f, .74f, .70f));

    inline FButtonStyle MakeButtonStyle(bool bActive)
    {
        FButtonStyle Style = FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("Button");
        const FLinearColor Normal = bActive ? FLinearColor(.60f,.74f,.69f) : FLinearColor(.075f,.103f,.113f);
        Style.SetNormal(FSlateColorBrush(Normal));
        Style.SetHovered(FSlateColorBrush(bActive ? FLinearColor(.79f,.89f,.83f) : FLinearColor(.15f,.21f,.22f)));
        Style.SetDisabled(FSlateColorBrush(bActive ? FLinearColor(.42f,.49f,.46f) : FLinearColor(.12f,.15f,.15f)));
        Style.SetPressed(FSlateColorBrush(bActive ? FLinearColor(.47f,.61f,.57f) : FLinearColor(.10f,.15f,.17f)));
        Style.SetNormalPadding(FMargin(14,9));
        Style.SetPressedPadding(FMargin(14,10,14,8));
        return Style;
    }
}

#pragma once

#include "CoreTypes.h"
#include "Misc/StringFormatArg.h"

namespace ExpeditionText
{
    PARTYFRAMEWORK_API bool IsEnglish();
    PARTYFRAMEWORK_API void SetEditorPreviewLanguage(bool bEnglish);
}

// Resolve at presentation time. The local player chooses the language, including on a remote host.
#define EXPEDITION_TEXT(Chinese, English) (ExpeditionText::IsEnglish() ? TEXT(English) : TEXT(Chinese))

#include "ExpeditionText.h"

#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "CoreGlobals.h"

namespace
{
    bool bEditorPreviewEnglish = false;
}

bool ExpeditionText::IsEnglish()
{
    if (GIsEditor) return bEditorPreviewEnglish;
    return FInternationalization::Get().GetCurrentLanguage()->GetTwoLetterISOLanguageName() == TEXT("en");
}

void ExpeditionText::SetEditorPreviewLanguage(bool bEnglish)
{
    bEditorPreviewEnglish = bEnglish;
}

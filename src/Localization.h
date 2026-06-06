#pragma once

#include "AppTypes.h"

// The desktop settings UI now lives in the WebView2 frontend (web/app.js carries its own
// EN/RU strings), so the only localized strings left in native code are the two overlay
// button labels drawn by VrOverlay.
enum class TextId {
    OverlayRec,
    OverlayStop
};

inline const wchar_t* Tr(Language language, TextId id)
{
    const bool ru = language == Language::Russian;
    switch (id) {
    case TextId::OverlayRec:
        return ru ? L"ЗАПИСЬ" : L"REC";
    case TextId::OverlayStop:
        return ru ? L"СТОП" : L"STOP";
    }
    return L"";
}

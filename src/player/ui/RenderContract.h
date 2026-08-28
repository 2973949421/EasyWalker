#pragma once
#include <cstdint>

namespace adv_walkman { namespace player {
#if __cplusplus >= 201402L
#define ADV_RENDER_CONSTEXPR constexpr
#else
#define ADV_RENDER_CONSTEXPR inline
#endif
enum class RenderStep : uint8_t { Idle, Waiting, Submitted, Failed };
constexpr bool validStripeDestination(int y,int height){return y>=0&&height>0&&height<=18&&y+height<=240;}

// Used by the real presenter as well as injected regressions. In particular,
// ownership may START inside content(); checking only at entry is insufficient.
template<class Content, class Owned>
ADV_RENDER_CONSTEXPR RenderStep dispatchContent(bool forced, bool preferred, bool dirty,
                                     Content content, Owned owned) {
    if (!forced && !(preferred && dirty)) return RenderStep::Idle;
    const auto result = content();
#ifdef REPRODUCE_OLD_RENDER_FALLTHROUGH
    return forced || result == RenderStep::Submitted ? result : RenderStep::Idle;
#else
    return forced || result != RenderStep::Waiting || owned() ? result : RenderStep::Idle;
#endif
}

// Only accepted, contiguous submissions can complete a frame. A partial
// overlay has its own range and must never be counted as a full view.
struct FrameCommit {
    uint32_t generation=0, id=0;
    uint16_t first=0, next=0, end=0;
    bool active=false, partial=false;
    ADV_RENDER_CONSTEXPR void begin(uint32_t g,uint32_t f,unsigned start,unsigned limit,bool patch) {
        generation=g;id=f;first=next=start;end=limit;partial=patch;active=start<limit;
    }
    constexpr bool accepts(uint32_t g,uint32_t f,unsigned y,unsigned h)const {
#ifdef REPRODUCE_OLD_FRAME_COMPLETION
        return active&&h>0;
#else
        return active && g==generation && f==id && y==next && h>0 && h<=18 && y+h<=end;
#endif
    }
    ADV_RENDER_CONSTEXPR bool submit(uint32_t g,uint32_t f,unsigned y,unsigned h) {
        if(!accepts(g,f,y,h))return false;
        next+=h;return true;
    }
    constexpr bool complete()const{return active && next==end;}
    ADV_RENDER_CONSTEXPR void cancel(){active=false;}
};
#undef ADV_RENDER_CONSTEXPR
} }

#pragma once
#include "MediaTypes.h"
#if __cplusplus >= 201402L
#define ADV_VIEW_CX constexpr
#else
#define ADV_VIEW_CX
#endif
namespace adv_walkman { namespace player {
struct ViewTransition {
    MediaView displayed=MediaView::Cover,requested=MediaView::Lyrics;
    bool pending=false;
    uint32_t generation=0,coalesced=0;
    ADV_VIEW_CX bool toggle(bool lyrics){
        if(!lyrics)return false;
        const auto target=displayed==MediaView::Lyrics?MediaView::Cover:MediaView::Lyrics;
        if(pending&&requested==target){++coalesced;return true;}
        requested=target;pending=true;++generation;return true;
    }
    ADV_VIEW_CX void commit(MediaView view){displayed=view;if(view==requested)pending=false;}
    ADV_VIEW_CX void cancel(){requested=displayed;pending=false;}
};
} }

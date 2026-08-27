#include "player/ui/media/MediaLayout.h"
#include "player/ui/PlayerKeys.h"
#include "player/app/VolumePolicy.h"
#include "player/ui/media/VerticalWords.h"
using namespace adv_walkman::player;
constexpr bool coldLyricsProgressWithContinuousCover(){
    unsigned lyrics=120,fonts=120;
    for(unsigned i=0;i<360;++i){
#ifdef REPRODUCE_OLD_COVER_PRIORITY
        const auto job=MediaWork::Cover;
#else
        const auto job=chooseMediaWork(i,fonts!=0,lyrics!=0,true);
#endif
        if(job==MediaWork::Lyrics && lyrics)--lyrics;
        if(job==MediaWork::Font && fonts)--fonts;
    }
    return lyrics==0 && fonts==0;
}
static_assert(coldLyricsProgressWithContinuousCover(),"cold lyrics starvation regression");
static_assert(chooseMediaWork(0,false,true,true)==MediaWork::Lyrics,"skip idle worker");
static_assert(chooseMediaWork(2,true,false,false)==MediaWork::Font,"skip idle cover");
static_assert(chooseMediaWork(1,false,false,false)==MediaWork::None,"all idle");
static_assert(playerKeyAt(13,0)==PlayerKey::VolumeUp,"portrait volume plus");
static_assert(playerKeyAt(12,0)==PlayerKey::VolumeDown,"portrait volume minus");
static_assert(playerKeyAt(13,1)==PlayerKey::TogglePlayback && playerKeyAt(13,2)==PlayerKey::TogglePlayback,"double play pause");
static_assert(playerKeyAt(12,1)==PlayerKey::View,"physical view");
static_assert(playerKeyAt(11,0)==PlayerKey::None,"no premature DSP key");
static_assert(adjustedVolume(128,8)==136 && adjustedVolume(128,-8)==120,"real volume step");
static_assert(adjustedVolume(254,8)==255 && adjustedVolume(3,-8)==0,"volume saturation");
static_assert(MediaLayout::top+MediaLayout::lyricHeight+MediaLayout::bottom==216,"lyric gutters");
static_assert(MediaLayout::columns==6 && MediaLayout::cell==18,"current cue larger type");
static_assert(MediaLayout::columns*MediaLayout::pitch-2+MediaLayout::bilingualGap<=123,"six columns fit");
static_assert(MediaLayout::coverTop+144<=188,"cover fits without stretching");
static_assert(rotateVerticalPunctuation(0x300A)&&smallVerticalPunctuation(0xFF0C),"vertical punctuation");
constexpr bool volumeRangeIsBounded(){
    for(unsigned v=0;v<=255;++v){
        if(VolumePolicy::toRaw(v)>102)return false;
        if(v && VolumePolicy::toRaw(v)<VolumePolicy::toRaw(v-1))return false;
    }
    return true;
}
static_assert(volumeRangeIsBounded(),"all 256 UI levels capped and monotonic");
static_assert(VolumePolicy::toRaw(0)==0 && VolumePolicy::toRaw(255)==102,"mute and user-calibrated ceiling");
static_assert(VolumePolicy::toRaw(VolumePolicy::initialLevel)==32,"conservative startup");
struct FixedLatinAdvance {constexpr int operator()(uint32_t)const{return 6;}};
static_assert(englishWordHeight("never die",174,FixedLatinAdvance{})==30,"whole word not phrase");
static_assert(englishWordHeight("don't",174,FixedLatinAdvance{})==30,"contraction kept");
static_assert(nextVerticalColumn(160,6,30,174),"move never instead of n / ever");
static_assert(!nextVerticalColumn(144,6,30,174),"exact fit stays");
static_assert(!nextVerticalColumn(160,6,180,174),"overlong word may split");
static_assert(nextVerticalColumn(172,6,0,174),"overlong continuation still bounded");

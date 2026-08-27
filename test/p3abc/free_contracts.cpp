#include "player/ui/media/MediaLayout.h"
#include "player/ui/PlayerKeys.h"
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
static_assert(MediaLayout::top+MediaLayout::lyricHeight+MediaLayout::bottom==188,"lyric gutters");
static_assert(7*MediaLayout::pitch-2+MediaLayout::bilingualGap<=123,"seven columns fit");
static_assert(MediaLayout::coverTop+144<=188,"cover fits without stretching");
static_assert(rotateVerticalPunctuation(0x300A)&&smallVerticalPunctuation(0xFF0C),"vertical punctuation");

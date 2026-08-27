#include "MediaTypes.h"
#include <cstring>
#include <cstdio>
namespace adv_walkman { namespace player {
bool mediaResourcePath(const char* track, const char* root, const char* suffix,
                       char* output, size_t capacity) {
    if (!track || std::strncmp(track,"/Music/",7)!=0 || std::strstr(track,"/../") ||
        std::strstr(track,"/./") || std::strstr(track,"//") || std::strchr(track,'\\')) return false;
    const char* tail=track+7;
    const char* dot=std::strrchr(tail,'.');
    if (!dot || dot<=tail || std::strchr(dot,'/')) return false;
    const int n=std::snprintf(output,capacity,"%s/%.*s%s",root,int(dot-tail),tail,suffix);
    return n>0 && size_t(n)<capacity;
}
} }

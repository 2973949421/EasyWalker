#include "UiCoordinator.h"
namespace adv_walkman { namespace player {
// Link-visible values allow host ELF inspection without running firmware.
const uint32_t p3MemoryReport[6]={kP3DMediaBudgetBytes,kP3DMediaAndEventsBytes,
    sizeof(InputEdges),135*18*2,FontCache::workBytes(),LyricsTimeline::workBytes()};
} }

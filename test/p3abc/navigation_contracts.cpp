#include "player/ui/NavigationLoad.h"
using namespace adv_walkman::player;
constexpr bool resultsAndCancel() {
    NavigationLoad n;
    const auto old=n.begin(0);
    n.observe(old,10,1,NavigationObservation::Pending);
    if(n.state!=NavigationState::Loading)return false;
    n.cancel();const auto current=n.begin(20);
    n.observe(old,30,2,NavigationObservation::Ready);
    if(n.state!=NavigationState::Loading)return false;
    n.observe(current,40,2,NavigationObservation::Ready);
    if(n.state!=NavigationState::Ready)return false;
    n.begin(50);n.observe(n.generation,60,0,NavigationObservation::Error);
    return n.state==NavigationState::Error&&!n.stalled;
}
constexpr bool progressAndTimeout() {
    NavigationLoad n;const auto request=n.begin(0xFFFFFF00U);
    n.observe(request,4743U,0,NavigationObservation::Pending);
    if(n.state!=NavigationState::Loading)return false;
    n.observe(request,4744U,0,NavigationObservation::Pending);
    if(n.state!=NavigationState::Error||!n.stalled)return false;
    const auto retry=n.begin(6000);
    n.observe(retry,10999,1,NavigationObservation::Pending);
    n.observe(retry,15000,2,NavigationObservation::Pending);
    n.observe(retry,19999,2,NavigationObservation::Ready);
    return n.state==NavigationState::Ready&&!n.stalled;
}
static_assert(resultsAndCancel(),"navigation result/cancellation isolation");
static_assert(progressAndTimeout(),"5s no-progress, retry, millis wrap");
#ifdef REPRODUCE_STALE_NAVIGATION
constexpr bool staleCompletion(){NavigationLoad n;const auto stale=n.begin(0);n.cancel();n.begin(1);n.observe(stale,2,0,NavigationObservation::Ready);return n.state==NavigationState::Ready;}
static_assert(staleCompletion(),"old request must not complete current navigation");
#endif

"""P4A+B source and production-contract checks.

The constexpr assertions in P4Controls.h are compiled by every P3/P4 UI
firmware build. These host checks ensure the runtime wiring cannot silently
fall back to the old global shortcuts or split mode checkpoint path.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def source(path):
    return (ROOT / path).read_text(encoding="utf-8-sig")


class P4ControlChecks(unittest.TestCase):
    def test_all_blind_zone_coordinates_are_production_asserted(self):
        controls = source("src/player/ui/P4Controls.h")
        for x, y in ((13, 0), (13, 1), (13, 2), (13, 3),
                     (12, 0), (12, 1), (12, 2), (12, 3)):
            self.assertIn(f"playerKeyAt({x}, {y})", controls)
        for y in range(4):
            self.assertIn(f"playerKeyAt(11, {y})", controls)
        self.assertIn("legacy invalid mode normalizes only on explicit action", controls)

    def test_router_uses_explicit_page_context(self):
        header = source("src/player/ui/InputRouter.h")
        router = source("src/player/ui/InputRouter.cpp")
        self.assertNotIn("bool playerPage", header + router)
        self.assertIn("UiPage page", header)
        self.assertIn("routedActionAt(page,raw.x,raw.y)", router)
        controls = source("src/player/ui/P4Controls.h")
        self.assertIn("UiPage::Playlist", controls)
        self.assertIn("UiPage::Library", controls)
        self.assertIn("UiPage::Settings", controls)
        self.assertIn("settings Tab returns to Player", controls)

    def test_transport_and_mode_have_one_checkpoint_boundary(self):
        runtime = source("src/player/app/PlayerRuntime.cpp")
        previous = runtime.split("bool PlayerRuntime::previous()", 1)[1].split(
            "bool PlayerRuntime::seekToMs", 1)[0]
        next_ = runtime.split("bool PlayerRuntime::next()", 1)[1].split(
            "bool PlayerRuntime::previous()", 1)[0]
        atomic = runtime.split("bool PlayerRuntime::setPlaybackMode", 1)[1].split(
            "bool PlayerRuntime::setSoundPreset", 1)[0]
        self.assertIn("if (result) requestCheckpoint();", previous)
        self.assertIn("if (result) requestCheckpoint();", next_)
        self.assertEqual(atomic.count("requestCheckpoint()"), 1)
        coordinator = source("src/player/ui/UiCoordinator.cpp")
        self.assertIn("nextPlaybackMode(before.repeatMode,before.shuffleEnabled)", coordinator)
        self.assertIn("player_->setPlaybackMode(next.repeat,next.shuffle)", coordinator)

    def test_manual_navigation_and_automatic_end_are_separate(self):
        policy = source("src/player/core/PlaybackPolicy.h")
        controller = source("src/player/core/PlayerController.cpp")
        queue = source("src/player/core/PlaybackQueue.cpp")
        self.assertIn("manual Previous/Next wrap in every playback mode", policy)
        self.assertIn("list once stops at natural queue end", policy)
        self.assertIn("random loop starts a new shuffled round", policy)
        self.assertIn("queue_.advance(manualTrackNavigationWraps())", controller)
        self.assertIn("manualPreviousUsesHistory(queue_.shuffleEnabled())", controller)
        self.assertIn("queue_.retreatSequential(true)", controller)
        self.assertIn("bool PlaybackQueue::retreatSequential", queue)

    def test_user_defined_mode_icons_are_mapped(self):
        controls = source("src/player/ui/P4Controls.h")
        presenter = source("src/player/ui/NowPlayingPresenter.cpp")
        for name in ("ListLoop", "RepeatOne", "ShuffleLoop", "ListOnce"):
            self.assertIn(name, controls)
        for label in ("'1'", "'R'", "'S'"):
            self.assertIn(label, controls)
        self.assertIn("drawPlaybackModeIcon", presenter)

    def test_old_global_player_shortcuts_are_absent(self):
        router = source("src/player/ui/InputRouter.cpp")
        for shortcut in ("H/L/Q/R/S/V", "keyChar", "isKeyPressed"):
            self.assertNotIn(shortcut, router)
        controls = source("src/player/ui/P4Controls.h")
        self.assertRegex(controls, r"page == UiPage::Player")

    def test_diagnostics_cover_p4_actions_and_footer_latency(self):
        log = source("src/player/p3abc/FreeSession.cpp")
        for field in ("previous_requests", "previous_accepted", "next_requests",
                      "next_accepted", "play_mode_requests", "play_mode_accepted",
                      "mode_footer_max_ms", "checkpoint_revision",
                      "last_transport_index", "last_transport_state",
                      "transport_first_pcm_max_ms", "transport_pcm_completed"):
            self.assertIn(field, log)


if __name__ == "__main__":
    unittest.main(verbosity=2)

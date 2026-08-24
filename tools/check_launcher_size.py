Import("env")

from pathlib import Path


# Conservative limit used by existing Cardputer ADV Launcher-compatible apps.
# The current Launcher may expose a larger OTA slot, but P0 keeps the smaller
# bound until the installed partition layout is explicitly verified.
LAUNCHER_APP_LIMIT_BYTES = 1_310_720


def check_launcher_size(source, target, env):
    firmware = Path(str(target[0]))
    size = firmware.stat().st_size
    print(
        "Launcher app size: "
        f"{size} / {LAUNCHER_APP_LIMIT_BYTES} bytes "
        f"({size / LAUNCHER_APP_LIMIT_BYTES:.1%})"
    )
    if size > LAUNCHER_APP_LIMIT_BYTES:
        raise RuntimeError(
            "firmware.bin exceeds the conservative M5Launcher app limit; "
            "do not install it before reviewing the Launcher partition layout"
        )


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", check_launcher_size)

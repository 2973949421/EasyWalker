Import("env")

from pathlib import Path


# A/B reuse verified 0xA0000 Launcher slots. Candidate C historically used a
# separate 0x3F0000 slot, selected through its PlatformIO environment.
DEFAULT_LAUNCHER_APP_LIMIT_BYTES = 0xA0000


def check_launcher_size(source, target, env):
    firmware = Path(str(target[0]))
    size = firmware.stat().st_size
    configured_limit = env.GetProjectOption(
        "custom_launcher_app_limit", hex(DEFAULT_LAUNCHER_APP_LIMIT_BYTES)
    )
    launcher_app_limit_bytes = int(str(configured_limit), 0)
    print(
        "Launcher app size: "
        f"{size} / {launcher_app_limit_bytes} bytes "
        f"({size / launcher_app_limit_bytes:.1%})"
    )
    if size > launcher_app_limit_bytes:
        raise RuntimeError(
            "firmware.bin exceeds the conservative M5Launcher app limit; "
            "do not install it before reviewing the Launcher partition layout"
        )


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", check_launcher_size)

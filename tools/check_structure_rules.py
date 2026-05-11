#!/usr/bin/env python3
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="ignore")


def collect_sources(base: Path):
    for p in base.rglob("*"):
        if p.suffix in {".h", ".hpp", ".c", ".cc", ".cpp"}:
            yield p


def is_under(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
        return True
    except ValueError:
        return False


def main() -> int:
    violations = []
    core_base_logging = SRC / "core" / "base" / "logging.h"
    core_rtc_bridge_files = {
        SRC / "core" / "rtc" / "hw" / "backend_registry.cpp",
    }

    for p in collect_sources(SRC / "core"):
        t = read_text(p)
        if 'common/internal/' in t:
            violations.append(f"{p}: core should not include common/internal headers")
        if 'public/logger_api.h' in t and p != core_base_logging:
            violations.append(f"{p}: core should include core/base/logging.h instead of public/logger_api.h")

        # Core layering rule:
        # - rtc(domain) must not depend on platform implementation details
        # - backend_registry.cpp is the only bridge point that can reference platform/*
        if is_under(p, SRC / "core" / "rtc"):
            if '"platform/' in t and p not in core_rtc_bridge_files:
                violations.append(
                    f"{p}: core/rtc domain cannot include platform/* directly "
                    f"(only backend_registry.cpp may bridge platform bindings)")

    for p in collect_sources(SRC / "client"):
        t = read_text(p)
        if '"service/' in t:
            violations.append(f"{p}: client must not include service headers")

    for p in collect_sources(SRC / "service"):
        t = read_text(p)
        if '"client/' in t:
            violations.append(f"{p}: service must not include client headers")

    for p in collect_sources(SRC):
        t = read_text(p)
        if '../internal/' in t:
            violations.append(f"{p}: avoid relative ../internal include style")
        if 'common/internal/' in t:
            violations.append(f"{p}: common/internal has been removed; use common/base, common/abi, or common/media")
        if 'common/util/' in t:
            violations.append(f"{p}: common/util has been removed; use common/base")
        if 'common/config/' in t or 'common/log/' in t or 'common/error/' in t or 'common/frame/' in t:
            violations.append(
                f"{p}: legacy common folders removed; use common/base, common/abi, common/media, common/public")
        if 'core/rtc/hw/android/' in t or 'core/rtc/hw/rockchip_mpp/' in t:
            violations.append(
                f"{p}: legacy core platform paths removed; use core/platform/android or core/platform/rockchip")

    if violations:
        print("Structure check failed:")
        for v in violations:
            print(f" - {v}")
        return 1

    print("Structure check passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

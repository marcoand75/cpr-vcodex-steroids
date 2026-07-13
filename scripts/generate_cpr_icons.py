#!/usr/bin/env python3
"""Generate CPR-vCodex icon C headers from KOReader SVGs.

Uses the cached KOReader icons produced by fetch_koreader_icons.py and the
existing convert_icon.py pipeline to emit 1-bit monochrome C arrays into
src/components/icons/.

For UIIcon values that have no suitable KOReader source, the script leaves
the existing .h file untouched (if present) so the build does not break.

Usage:
    python scripts/generate_cpr_icons.py
        [--koreader-dir build/koreader_icons]
        [--icons-dir src/components/icons]
        [--threshold 128]
        [--force-existing]
"""

import argparse
import os
import shutil
import subprocess
import sys

# ---------------------------------------------------------------------------
# Mapping: UIIcon semantic name -> (koreader_svg_relative_path, c_header_name, c_array_name_32, c_array_name_24)
#
# For icons with a custom array name (e.g. SleepModeIcon32 instead of SleepIcon),
# the third/fourth elements override the default naming.
# ---------------------------------------------------------------------------
ICON_MAPPING = [
    # UIIcon              KOReader SVG (mdlight or src)                  C header    32-array        24-array
    ("Folder",            ("src/appbar.cabinet.files.svg",),              "folder",    "FolderIcon",    "Folder24Icon"),
    ("Text",              ("src/appbar.page.text.svg",),                  "text",      "TextIcon",      "Text24Icon"),
    ("Image",             None,                                           "image",     "ImageIcon",     "Image24Icon"),       # no KOReader match
    ("Book",              ("mdlight/book.opened.svg",),                   "book",      "BookIcon",      "Book24Icon"),
    ("File",              ("src/appbar.cabinet.files.svg",),              "file",      "FileIcon",      "File24Icon"),        # reuse cabinet for file too
    ("Recent",            ("mdlight/bookmark.svg",),                      "recent",    "RecentIcon",    "Recent24Icon"),
    ("Settings",          ("mdlight/appbar.settings.svg",),               "settings2", "Settings2Icon", "Settings224Icon"),   # UIIcon::Settings -> Settings2Icon
    ("Apps",              ("mdlight/appbar.menu.svg",),                   "settings",  "SettingsIcon",  "Settings24Icon"),    # UIIcon::Apps -> SettingsIcon
    ("Transfer",          ("mdlight/plus.svg",),                          "transfer",  "TransferIcon",  "Transfer24Icon"),
    ("Library",           None,                                           "library",   "LibraryIcon",   "Library24Icon"),     # no KOReader match
    ("Trophy",            ("mdlight/check.svg",),                         "trophy",    "TrophyIcon",    "Trophy24Icon"),
    ("Wifi",              ("mdlight/wifi.svg",),                          "wifi",      "WifiIcon",      "Wifi24Icon"),
    ("Hotspot",           None,                                           "hotspot",   "HotspotIcon",   "Hotspot24Icon"),     # no KOReader match
    ("Heart",             None,                                           "heart",     "HeartIcon",     "Heart24Icon"),       # no KOReader match
    ("ScreenSaver",       None,                                           "screensaver","ScreenSaverIcon","ScreenSaver24Icon"),# no KOReader match
    ("Bookshelf",         ("mdlight/home.svg",),                          "bookshelf", "BookshelfIcon", "Bookshelf24Icon"),
    ("SleepMode",         None,                                           "sleep",     "SleepModeIcon", "SleepMode24Icon"),   # no KOReader match
    ("CleanMonitor",      None,                                           "cleanmonitor","CleanMonitorIcon","CleanMonitor24Icon"),# no KOReader match
    ("Heatmap",           None,                                           "heatmap",   "HeatmapReadingIcon","HeatmapReading24Icon"), # no KOReader match
    ("FlashcardQuiz",     None,                                           "flashcardquiz","FlashcardQuizIcon","FlashcardQuiz24Icon"), # no KOReader match
    ("ReadingProfile",    None,                                           "readingprofile","ReadingProfileIcon","ReadingProfile24Icon"), # no KOReader match
    ("LostDevice",        None,                                           "lostdevice","LostDeviceIcon","LostDevice24Icon"),   # no KOReader match
    ("OpdsBrowser",       ("src/appbar.magnify.browse.svg",),             "opdsbrowser","OPDSBrowserIcon","OPDSBrowser24Icon"), # no KOReader match
    ("Dictionary",        ("mdlight/info.svg",),                          "dictionary","DictionaryIcon","Dictionary24Icon"),
    ("GoalsMedal",        ("mdlight/star.full.svg",),                     "goalsmedal","GoalsMedalIcon","GoalsMedal24Icon"),
    ("ReadingStatsIcon",  None,                                           "readingstats","ReadingStatsIcon","ReadingStats24Icon"), # no KOReader match
    ("RecentBooks",       None,                                           "recentbooks","RecentBooksIcon","RecentBooks24Icon"),  # no KOReader match
    # Extra app/menu icons (not in UIIcon enum)
    ("Bookmark",          ("mdlight/bookmark.svg",),                      "bookmark",  "BookmarkIcon",  "Bookmark24Icon"),
    ("Search",            ("mdlight/appbar.search.svg",),                 "search",    "SearchIcon",    "Search24Icon"),
    ("Rotation",          ("mdlight/appbar.rotation.svg",),               "rotation",  "RotationIcon",  "Rotation24Icon"),
    ("Pageview",          ("mdlight/appbar.pageview.svg",),               "pageview",  "PageviewIcon",  "Pageview24Icon"),
    ("Camera",            None,                                           "CameraIcon",    "CameraIcon",    "Camera24Icon"),     # manual
    ("QrCode",            None,                                           "QrCodeIcon",    "QrCodeIcon",    "QrCode24Icon"),     # manual
    ("Progress",          None,                                           "ProgressIcon",  "ProgressIcon",  "Progress24Icon"),   # manual
    ("PowerOn",           None,                                           "PowerOnIcon",   "PowerOnIcon",   "PowerOn24Icon"),    # manual
    ("PowerOff",          None,                                           "PowerOffIcon",  "PowerOffIcon",  "PowerOff24Icon"),   # manual
]

# Icons that need a 24x24 variant (same semantic name, different size suffix).
# For KOReader-sourced icons we generate both 32 and 24. For kept icons we rely
# on the existing 24 variant if present, otherwise leave it alone.
ICONS_NEEDING_24 = {
    "Folder", "Text", "Image", "Book", "File", "Trophy", "Heart",
    "Recent", "Settings", "Apps", "Transfer", "Library", "Wifi", "Hotspot",
    "ScreenSaver", "Bookshelf", "SleepMode", "CleanMonitor", "Heatmap",
    "FlashcardQuiz", "ReadingProfile", "LostDevice", "OpdsBrowser",
    "Dictionary", "GoalsMedal", "ReadingStatsIcon", "RecentBooks",
    "Bookmark", "Search", "Rotation", "Pageview", "Camera", "QrCode",
    "Progress", "PowerOn", "PowerOff",
}


def resolve_svg(koreader_dir, candidates):
    for rel in candidates:
        path = os.path.join(koreader_dir, rel)
        if os.path.isfile(path):
            return path
    return None


def generate_icon(convert_script, svg_path, output_name, size, icons_dir, threshold, array_name=None):
    cmd = [
        sys.executable, convert_script,
        svg_path, output_name,
        str(size), str(size),
    ]
    if array_name:
        cmd.extend(["--name", array_name])
    env = os.environ.copy()
    # Point convert_icon.py at the project root by running from repo root
    result = subprocess.run(cmd, cwd=os.path.dirname(os.path.dirname(convert_script)), capture_output=True, text=True)
    if result.returncode != 0:
        print(f"  ERROR converting {svg_path} -> {output_name} ({size}x{size}): {result.stderr}", file=sys.stderr)
        return False
    print(f"  wrote {output_name}.h ({size}x{size})")
    return True


def main():
    parser = argparse.ArgumentParser(description="Generate CPR-vCodex icon headers from KOReader SVGs")
    parser.add_argument("--koreader-dir", default=os.path.join("build", "koreader_icons"), help="KOReader icon cache dir")
    parser.add_argument("--icons-dir", default=os.path.join("src", "components", "icons"), help="Output icons dir")
    parser.add_argument("--threshold", type=int, default=128, help="1-bit threshold (default 128)")
    parser.add_argument("--force-existing", action="store_true", help="Overwrite existing headers even if we have no KOReader source")
    parser.add_argument("--convert-icon", default=os.path.join("scripts", "convert_icon.py"), help="Path to convert_icon.py")
    args = parser.parse_args()

    convert_script = os.path.abspath(args.convert_icon)
    if not os.path.isfile(convert_script):
        print(f"ERROR: {convert_script} not found", file=sys.stderr)
        sys.exit(1)

    os.makedirs(args.icons_dir, exist_ok=True)

    mdlight_dir = os.path.join(args.koreader_dir, "mdlight")
    src_dir = os.path.join(args.koreader_dir, "src")

    replaced = 0
    kept = 0
    missing = 0

    for ui_name, candidates, header_name, array32, array24 in ICON_MAPPING:
        header_path = os.path.join(args.icons_dir, f"{header_name}.h")
        has_koreader_src = any(os.path.isfile(os.path.join(args.koreader_dir, p)) for p in (candidates or []))

        if not has_koreader_src:
            if os.path.exists(header_path):
                kept += 1
                print(f"KEEP  {header_name}.h (no KOReader source)")
            else:
                missing += 1
                print(f"MISS  {header_name}.h (no KOReader source and no existing file)", file=sys.stderr)
            continue

        # Generate 32x32
        svg = resolve_svg(args.koreader_dir, candidates)
        if not svg:
            print(f"WARNING: resolved source missing for {ui_name}", file=sys.stderr)
            continue

        ok32 = generate_icon(convert_script, svg, header_name, 32, args.icons_dir, args.threshold, array32)
        if not ok32:
            continue

        # Generate 24x24 for icons that need it
        if ui_name in ICONS_NEEDING_24:
            name24 = f"{header_name}24"
            generate_icon(convert_script, svg, name24, 24, args.icons_dir, args.threshold, array24)

        replaced += 1

    print(f"\nSummary: {replaced} replaced with KOReader sources, {kept} kept, {missing} missing")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Convert MP3 files from the source tree into device-ready WAV files for microSD.

This script is intended for the Nixie Clock audio workflow:

  Data to load/
    Sounds for the microSD card/
      _source_mp3/
        alarms/
          Artist/
            Song.mp3
        system/
          chime.mp3
        sfx/
          ble_on.mp3
      alarms/
        alarm_1__Artist__Song.wav
      system/
        system_1__chime.wav
      sfx/
        sfx_1__ble_on.wav

Workflow:
1. Place MP3 sources under _source_mp3 per category.
2. Run the script with a profile such as --profile alarm.
3. It re-encodes the files into WAV with device-compatible settings.
4. Final WAV files are placed into the root microSD folders used by the firmware.

Rules:
- Only *.mp3 files are processed.
- Existing *.wav files are reused when their settings still match.
- Output names keep a short device ID and a human-readable suffix.
- Final files are stored in the root working folders for the device.
"""

from __future__ import annotations

import argparse
import csv
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
MICROSD_ROOT = ROOT / "Sounds for the microSD card"
SOURCE_ROOT = MICROSD_ROOT / "_source_mp3"
CATEGORY_PREFIX = {
    "alarms": "alarm",
    "system": "system",
    "sfx": "sfx",
}
TARGET_DIRS = {
    "alarms": MICROSD_ROOT / "alarms",
    "system": MICROSD_ROOT / "system",
    "sfx": MICROSD_ROOT / "sfx",
}

DEFAULT_PROFILE = {
    "sample_rate": 22050,
    "channels": 2,
    "bit_depth": 16,
    "audio_codec": "pcm_s16le",
}

PROFILE_MAP = {
    "music": {"sample_rate": 22050, "channels": 2, "bit_depth": 16, "audio_codec": "pcm_s16le"},
    "alarm": {"sample_rate": 22050, "channels": 1, "bit_depth": 16, "audio_codec": "pcm_s16le"},
    "system": {"sample_rate": 16000, "channels": 1, "bit_depth": 16, "audio_codec": "pcm_s16le"},
    "sfx": {"sample_rate": 16000, "channels": 1, "bit_depth": 16, "audio_codec": "pcm_s16le"},
}


def ensure_ffmpeg() -> str | None:
    """Locate FFmpeg in PATH or in a locally bundled project folder."""
    ffmpeg_path = shutil.which("ffmpeg")
    if ffmpeg_path:
        return ffmpeg_path

    local_candidates = [
        ROOT / "ffmpeg-2026-08-30-git-818cecc6e1-essentials_build" / "bin" / "ffmpeg.exe",
        ROOT / "ffmpeg" / "bin" / "ffmpeg.exe",
        ROOT / "tools" / "ffmpeg" / "bin" / "ffmpeg.exe",
    ]
    for candidate in local_candidates:
        if candidate.exists():
            return str(candidate)

    print("[ERROR] ffmpeg not found in PATH or local project folder.")
    print("Please install FFmpeg or place ffmpeg.exe in one of these locations:")
    for candidate in local_candidates:
        print(f"  {candidate}")
    return None


def sanitize_token(token: str) -> str:
    """Convert a path token into a safe, filesystem-friendly label."""
    token = token.strip()
    token = token.replace("&", "and")
    token = token.replace("%", "percent")
    token = re.sub(r"[\\/:*?\"<>|]+", "_", token)
    token = re.sub(r"\s+", "_", token)
    token = re.sub(r"_+", "_", token)
    token = token.strip("._")
    return token or "unnamed"


def category_for(path: Path) -> str | None:
    """Return the audio category based on the folder layout under _source_mp3."""
    if not SOURCE_ROOT.exists():
        return None

    try:
        rel = path.relative_to(SOURCE_ROOT)
    except ValueError:
        return None

    if not rel.parts:
        return None

    first = rel.parts[0].lower()
    if first in CATEGORY_PREFIX:
        return first

    for category in CATEGORY_PREFIX:
        if category in [p.lower() for p in rel.parts]:
            return category

    return None


def build_human_name(category: str, source_path: Path) -> str:
    """Build the human-readable suffix used in the final WAV file name."""
    rel = source_path.relative_to(SOURCE_ROOT)
    parts = list(rel.parts)

    if parts and parts[0].lower() == category:
        parts = parts[1:]

    stem = source_path.stem
    if parts:
        tokens = []
        for p in parts[:-1]:
            if p.lower() == category:
                continue
            tokens.append(sanitize_token(p))
        tokens.append(sanitize_token(stem))
    else:
        tokens = [sanitize_token(stem)]

    clean_tokens = [t for t in tokens if t and t != "."]
    if not clean_tokens:
        clean_tokens = ["untitled"]

    return "__".join(clean_tokens)


def build_output_name(category: str, source_path: Path, index: int, keep_human_label: bool = False) -> str:
    """Return the final WAV file name for the target category and current source."""
    device_id = f"{CATEGORY_PREFIX[category]}_{index}"
    if not keep_human_label:
        return f"{device_id}.wav"

    human = build_human_name(category, source_path)
    return f"{device_id}__{human}.wav"


def parse_device_index_from_target(category: str, target: Path) -> int | None:
    """Extract the numeric index from a previously generated target file name."""
    prefix = f"{CATEGORY_PREFIX[category]}_"
    match = re.match(rf"^{re.escape(prefix)}(\d+)(?:__.*)?$", target.stem)
    if not match:
        return None
    return int(match.group(1))


def find_existing_index_for_source(category: str, source_path: Path, target_dir: Path) -> int | None:
    """Reuse the same device index if this source already has a target WAV."""
    target_human = build_human_name(category, source_path)
    for target in sorted(target_dir.glob("*.wav")):
        index = parse_device_index_from_target(category, target)
        if index is None:
            continue
        stem = target.stem
        suffix = stem.split("__", 1)[1] if "__" in stem else ""
        if suffix == target_human:
            return index
    return None


def next_available_index(category: str, target_dir: Path) -> int:
    """Return the next free index based on the highest existing target in this category."""
    highest = 0
    for target in target_dir.glob("*.wav"):
        index = parse_device_index_from_target(category, target)
        if index is not None and index > highest:
            highest = index
    return highest + 1


def ensure_target_dirs() -> None:
    for folder in TARGET_DIRS.values():
        folder.mkdir(parents=True, exist_ok=True)


def mp3_files() -> list[Path]:
    if not SOURCE_ROOT.exists():
        return []
    files = []
    for path in sorted(SOURCE_ROOT.rglob("*")):
        if path.is_file() and path.suffix.lower() == ".mp3":
            files.append(path)
    return files


def parse_args() -> argparse.Namespace:
    """Parse command-line arguments for the conversion profile and overrides."""
    parser = argparse.ArgumentParser(description="Convert MP3 library for the ESP32 SD-card audio folders.")
    parser.add_argument("--profile", choices=["music", "alarm", "system", "sfx"], default="alarm", help="Preset audio profile for conversion")
    parser.add_argument("--sample-rate", type=int, default=None, help="Override FFmpeg sample rate (Hz)")
    parser.add_argument("--channels", type=int, default=None, help="Override channel count (1 or 2)")
    parser.add_argument("--bit-depth", type=int, default=None, help="Override bit depth (usually 16)")
    parser.add_argument("--codec", type=str, default=None, help="Override FFmpeg codec (default: pcm_s16le)")
    return parser.parse_args()


def build_audio_settings(args: argparse.Namespace, category: str) -> dict:
    """Build the WAV settings for a given category and CLI profile."""
    base = PROFILE_MAP.get(args.profile, DEFAULT_PROFILE).copy()
    if category in PROFILE_MAP:
        base = PROFILE_MAP[category].copy()
    if args.sample_rate is not None:
        base["sample_rate"] = args.sample_rate
    if args.channels is not None:
        base["channels"] = args.channels
    if args.bit_depth is not None:
        base["bit_depth"] = args.bit_depth
    if args.codec is not None:
        base["audio_codec"] = args.codec
    return base


def read_wav_metadata(path: Path) -> dict | None:
    """Read basic WAV metadata to determine whether a target file matches the desired profile."""
    try:
        with path.open("rb") as f:
            header = f.read(44)
    except OSError:
        return None

    if len(header) < 44:
        return None
    if header[:4] != b"RIFF" or header[8:12] != b"WAVE":
        return None

    def rd16(offset: int) -> int:
        return int.from_bytes(header[offset:offset + 2], byteorder="little", signed=False)

    def rd32(offset: int) -> int:
        return int.from_bytes(header[offset:offset + 4], byteorder="little", signed=False)

    if header[20:22] != b"\x01\x00":
        return None

    channels = rd16(22)
    sample_rate = rd32(24)
    bits_per_sample = rd16(34)
    data_size = rd32(40)
    if channels not in (1, 2) or bits_per_sample != 16 or data_size == 0:
        return None

    return {
        "sample_rate": sample_rate,
        "channels": channels,
        "bit_depth": bits_per_sample,
        "audio_codec": "pcm_s16le",
    }


def convert_one(source: Path, target: Path, ffmpeg_bin: str, settings: dict) -> None:
    """Convert one source file to WAV with the requested output settings."""
    command = [
        ffmpeg_bin,
        "-y",
        "-i",
        str(source),
        "-ar",
        str(settings["sample_rate"]),
        "-ac",
        str(settings["channels"]),
        "-c:a",
        settings["audio_codec"],
        str(target),
    ]
    subprocess.run(command, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)


def main() -> int:
    """Entry point: scan sources, convert files, and produce the final microSD-ready tree."""
    args = parse_args()
    if not SOURCE_ROOT.exists():
        print(f"[ERROR] Source folder not found: {SOURCE_ROOT}")
        print(f"Expected path: {SOURCE_ROOT}")
        return 1

    ffmpeg_bin = ensure_ffmpeg()
    if not ffmpeg_bin:
        return 2

    print(f"[INFO] Using FFmpeg: {ffmpeg_bin}")
    print(f"[INFO] Profile: {args.profile}")
    ensure_target_dirs()

    rows = []
    processed = 0
    skipped = 0
    failed = 0

    seen_targets: set[str] = set()
    category_sources: dict[str, list[Path]] = {category: [] for category in CATEGORY_PREFIX}
    for src in mp3_files():
        category = category_for(src)
        if category is not None:
            category_sources.setdefault(category, []).append(src)

    for category, sources in category_sources.items():
        target_dir = TARGET_DIRS[category]
        existing_targets = list(target_dir.glob("*.wav")) if target_dir.exists() else []

        for src in sorted(sources):
            existing_index = find_existing_index_for_source(category, src, target_dir)
            if existing_index is None:
                existing_index = next_available_index(category, target_dir)
            output_name = build_output_name(category, src, existing_index, keep_human_label=True)
            target_path = target_dir / output_name

            settings = build_audio_settings(args, category)
            wav_meta = read_wav_metadata(target_path) if target_path.exists() else None
            if target_path.exists() and wav_meta is not None:
                matches = (
                    wav_meta.get("sample_rate") == settings["sample_rate"] and
                    wav_meta.get("channels") == settings["channels"] and
                    wav_meta.get("bit_depth") == settings["bit_depth"] and
                    wav_meta.get("audio_codec") == settings["audio_codec"]
                )
                if matches:
                    skipped += 1
                    rows.append({
                        "source": str(src.relative_to(ROOT)),
                        "target": str(target_path.relative_to(ROOT)),
                        "category": category,
                        "device_id": f"{CATEGORY_PREFIX[category]}_{existing_index}",
                        "human_label": build_human_name(category, src),
                        "status": "skip_same_settings",
                    })
                    print(f"[SKIP] {src} -> {target_path} (settings match)")
                    continue

                print(f"[INFO] {src} -> {target_path} has stale WAV settings, re-encoding")

            candidate = output_name
            counter = 1
            while candidate in seen_targets:
                stem = target_path.stem
                candidate = f"{stem}_{counter}.wav"
                counter += 1
            seen_targets.add(candidate)

            if candidate != output_name:
                target_path = target_dir / candidate

            print(f"[INFO] {category}: sample_rate={settings['sample_rate']} channels={settings['channels']} codec={settings['audio_codec']}")

            try:
                convert_one(src, target_path, ffmpeg_bin, settings)
                processed += 1
                rows.append({
                    "source": str(src.relative_to(ROOT)),
                    "target": str(target_path.relative_to(ROOT)),
                    "category": category,
                    "device_id": f"{CATEGORY_PREFIX[category]}_{existing_index}",
                    "human_label": build_human_name(category, src),
                    "status": "converted",
                })
                print(f"[OK] {src} -> {target_path}")
                existing_targets.append(target_path)
            except subprocess.CalledProcessError as exc:
                failed += 1
                rows.append({
                    "source": str(src.relative_to(ROOT)),
                    "target": str(target_path.relative_to(ROOT)),
                    "category": category,
                    "device_id": f"{CATEGORY_PREFIX[category]}_{existing_index}",
                    "human_label": build_human_name(category, src),
                    "status": "failed",
                    "error": exc.stderr.decode("utf-8", errors="ignore").strip(),
                })
                print(f"[FAIL] {src}")
                if exc.stderr:
                    print(exc.stderr.decode("utf-8", errors="ignore").strip())

    manifest_path = ROOT / "audio_manifest.csv"
    with manifest_path.open("w", encoding="utf-8", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=["source", "target", "category", "device_id", "human_label", "status", "error"])
        writer.writeheader()
        writer.writerows(rows)

    print("\nSummary:")
    print(f"  converted: {processed}")
    print(f"  skipped:   {skipped}")
    print(f"  failed:    {failed}")
    print(f"  manifest:  {manifest_path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("\nInterrupted by user")
        raise SystemExit(130)

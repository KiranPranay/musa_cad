#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Pranay Kiran
"""Pull an AutoCAD demonstration off YouTube as reference material: the transcript
(English when a translation exists, else the original) and still frames at chosen
seconds, so a command's exact prompts, gestures and on-screen result can be checked
against the real thing before it is implemented here.

    tools/autocad_ref.py VIDEO_ID_OR_URL [--frames 5,12.5,40] [--out DIR] [--height 480]

Needs `yt-dlp`, `youtube-transcript-api` and `opencv-python-headless` (pip). The video
is fetched once (<= --height, progressive MP4) into --out; frames land as PNGs named by
their second. Developer tooling only -- nothing here ships in the product.
"""
import argparse
import os
import re
import subprocess
import sys


def video_id(s: str) -> str:
    m = re.search(r"(?:v=|youtu\.be/|/shorts/)([A-Za-z0-9_-]{11})", s)
    return m.group(1) if m else s


def transcript(vid: str, out: str) -> str:
    from youtube_transcript_api import YouTubeTranscriptApi

    api = YouTubeTranscriptApi()
    listing = api.list(vid)
    first = None
    for t in listing:
        first = t
        break
    if first is None:
        return ""
    try:
        fetched = first.translate("en").fetch()
        lang = "en (translated from %s)" % first.language_code
    except Exception:  # noqa: BLE001 - not translatable: keep the original
        fetched = first.fetch()
        lang = first.language_code
    lines = ["[%7.1fs] %s" % (x.start, x.text) for x in fetched]
    path = os.path.join(out, "%s.transcript.txt" % vid)
    with open(path, "w", encoding="utf-8") as f:
        f.write("# language: %s\n" % lang)
        f.write("\n".join(lines))
    return path


def download(vid: str, out: str, height: int) -> str:
    target = os.path.join(out, "%s.mp4" % vid)
    if not os.path.exists(target):
        # Frames need no audio, and YouTube now serves video-only DASH streams (a plain
        # MP4 OpenCV reads directly), so ask for those first; no ffmpeg merge is needed.
        # yt-dlp needs a JavaScript runtime for YouTube (deno is enabled by default when
        # it is on PATH; `~/.deno/bin` is a user-local install).
        fmt = ("bv*[ext=mp4][height<=%d]/b[ext=mp4][height<=%d]/bv*[height<=%d]/b"
               % (height, height, height))
        env = dict(os.environ)
        env["PATH"] = os.path.expanduser("~/.deno/bin") + os.pathsep + env.get("PATH", "")
        subprocess.run(
            [sys.executable, "-m", "yt_dlp", "-q", "-f", fmt, "-o", target,
             "https://www.youtube.com/watch?v=%s" % vid],
            check=True, env=env,
        )
    return target


def frames(video: str, seconds: list, out: str, vid: str) -> list:
    import cv2

    cap = cv2.VideoCapture(video)
    paths = []
    for s in seconds:
        cap.set(cv2.CAP_PROP_POS_MSEC, s * 1000.0)
        ok, img = cap.read()
        if not ok:
            print("no frame at %.1fs" % s, file=sys.stderr)
            continue
        p = os.path.join(out, "%s_%06.1fs.png" % (vid, s))
        cv2.imwrite(p, img)
        paths.append(p)
    cap.release()
    return paths


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("video")
    ap.add_argument("--frames", default="", help="comma-separated seconds")
    ap.add_argument("--out", default=".")
    ap.add_argument("--height", type=int, default=480)
    ap.add_argument("--no-transcript", action="store_true")
    a = ap.parse_args()
    vid = video_id(a.video)
    os.makedirs(a.out, exist_ok=True)
    if not a.no_transcript:
        try:
            print("transcript:", transcript(vid, a.out))
        except Exception as e:  # noqa: BLE001
            print("transcript unavailable:", e, file=sys.stderr)
    if a.frames:
        secs = [float(x) for x in a.frames.split(",") if x.strip()]
        video = download(vid, a.out, a.height)
        for p in frames(video, secs, a.out, vid):
            print("frame:", p)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

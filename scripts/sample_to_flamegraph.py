#!/usr/bin/env python3

import argparse
import html
import re
from pathlib import Path


def parse_args():
    parser = argparse.ArgumentParser(description="Convert macOS sample output into a simple flamegraph-style SVG.")
    parser.add_argument("--input", required=True, help="Path to sample output text")
    parser.add_argument("--output", required=True, help="Path to output SVG")
    parser.add_argument("--title", default="Sample Flame Graph", help="Title shown in the SVG")
    parser.add_argument("--top", type=int, default=18, help="Number of top stack entries to plot")
    return parser.parse_args()


def extract_entries(text: str):
    marker = "Sort by top of stack, same collapsed (when >= 5):"
    if marker not in text:
      raise ValueError("sample output missing collapsed top-of-stack section")
    section = text.split(marker, 1)[1]
    entries = []
    for line in section.splitlines()[1:]:
        stripped = line.strip()
        if not stripped:
            break
        match = re.match(r"(.+?)\s{2,}(\d+)$", stripped)
        if not match:
            continue
        label = match.group(1).strip()
        count = int(match.group(2))
        label = label.replace("  (in lob_simulator)", "").replace("  (in libsystem_malloc.dylib)", "")
        label = label.replace("  (in libsystem_platform.dylib)", "").replace("  (in libsystem_kernel.dylib)", "")
        entries.append((label, count))
    return entries


def build_svg(entries, title):
    width = 1360
    bar_height = 28
    gap = 10
    top_pad = 90
    left_pad = 170
    right_pad = 32
    max_count = max(count for _, count in entries)
    height = top_pad + len(entries) * (bar_height + gap) + 40

    palette = ["#7aa2ff", "#1dd89b", "#ff647d", "#ffc15c", "#8fe3ff", "#ff9e64"]

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="#0b0f14"/>',
        f'<text x="36" y="42" fill="#edf2fa" font-family="Georgia, Times New Roman, serif" font-size="28">{html.escape(title)}</text>',
        '<text x="36" y="68" fill="#93a1b5" font-family="Courier New, monospace" font-size="14">macOS sample collapsed by top-of-stack count</text>',
    ]

    usable_width = width - left_pad - right_pad
    for index, (label, count) in enumerate(entries):
        y = top_pad + index * (bar_height + gap)
        bar_width = max(16, int((count / max_count) * usable_width))
        color = palette[index % len(palette)]
        parts.append(
            f'<text x="36" y="{y + 19}" fill="#cfd7e6" font-family="Courier New, monospace" font-size="13">{html.escape(label[:18])}</text>'
        )
        parts.append(
            f'<rect x="{left_pad}" y="{y}" width="{bar_width}" height="{bar_height}" rx="8" fill="{color}" fill-opacity="0.92"/>'
        )
        parts.append(
            f'<text x="{left_pad + 12}" y="{y + 19}" fill="#05070a" font-family="Courier New, monospace" font-size="12">{html.escape(label)} • {count}</text>'
        )

    parts.append("</svg>")
    return "\n".join(parts)


def main():
    args = parse_args()
    text = Path(args.input).read_text()
    entries = extract_entries(text)[: args.top]
    if not entries:
        raise ValueError("no stack entries found to render")
    svg = build_svg(entries, args.title)
    Path(args.output).write_text(svg)


if __name__ == "__main__":
    main()

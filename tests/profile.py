#!/usr/bin/python3
"""Profiles the load of every test image in every load mode, then aggregates the time of each profile section.

Usage: uv run tests/profile.py [paths...] [--enim EXE] [--modes m1,m2] [--repeat N] [--timeout S] [--csv FILE]
       [--report FILE]

paths: image files or directories (their .bmp .qoi .jpg .jpeg), default tests/images/out (tests/images/generate.py).
Each run is `enim --profile --exit-after-load <mode options> <image>`; enim prints its PLOG_PROFILE milestones
("[profile <ms> ms  tid <n>] <label>") to stderr. A section is the time between two consecutive milestones of the
main thread, named "<previous> -> <label>". Runs are sequential, so they don't skew each other's timings.

Modes (like tests/CMakeLists.txt):
  default        the mode and strategy the planner picks
  normal-cold    --force-normal-mode, empty pyramid cache: the pyramid is built
  normal-warm    --force-normal-mode right after normal-cold: the cached pyramid is reused
  gpu-convert    --force-fast-mode --force-gpu-convert   (BMP only)
  stream-upload  --force-fast-mode --force-stream-upload (BMP only)
Every run gets a temporary --cache-dir, emptied before each default and normal-cold run.
"""

import argparse
import csv
import os
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IMAGE_SUFFIXES = {".bmp", ".qoi", ".jpg", ".jpeg"}
PROFILE_LINE = re.compile(r"^\[profile\s+([\d.]+) ms\s+tid\s+(\d+)\] (.*)$")

# name -> (enim options, BMP only, empty the cache before the run)
MODES = {
    "default": ([], False, True),
    "normal-cold": (["--force-normal-mode"], False, True),
    "normal-warm": (["--force-normal-mode"], False, False),
    "gpu-convert": (["--force-fast-mode", "--force-gpu-convert"], True, True),
    "stream-upload": (["--force-fast-mode", "--force-stream-upload"], True, True),
}

# ANSI styles. Colors are on for a terminal (--color auto), unless NO_COLOR is set.
RESET, BOLD, DIM = "\033[0m", "\033[1m", "\033[2m"
RED, GREEN, YELLOW, BLUE, MAGENTA, CYAN = (f"\033[{n}m" for n in (31, 32, 33, 34, 35, 36))
MODE_COLORS = {"default": BLUE, "normal-cold": CYAN, "normal-warm": GREEN, "gpu-convert": MAGENTA,
               "stream-upload": YELLOW}
use_color = False


def style(text: str, *codes: str) -> str:
    return "".join(codes) + text + RESET if use_color and codes else text


def ms_style(ms: float) -> tuple[str, ...]:
    """Heat color of a duration: dim below 1 ms, then green, yellow, red, bold red from 1 s."""
    if ms < 1:
        return (DIM,)
    if ms < 50:
        return (GREEN,)
    if ms < 300:
        return (YELLOW,)
    if ms < 1000:
        return (RED,)
    return (BOLD, RED)


def setup_color(mode: str) -> None:
    global use_color
    if mode == "auto":
        use_color = sys.stdout.isatty() and "NO_COLOR" not in os.environ
    else:
        use_color = mode == "always"
    if use_color and sys.platform == "win32":
        os.system("")  # turns on ANSI escape processing in the Windows console


@dataclass
class Cell:
    """A table cell: widths come from the plain text, so colors don't break the alignment."""
    plain: str
    rendered: str

    def __add__(self, other: "Cell") -> "Cell":
        return Cell(self.plain + other.plain, self.rendered + other.rendered)


def cell(text: str, *codes: str) -> Cell:
    return Cell(text, style(text, *codes))


def ms_cell(ms: float, digits: int = 1) -> Cell:
    return cell(f"{ms:.{digits}f}", *ms_style(ms))


def section_cell(name: str) -> Cell:
    """"<previous> -> <label>": the arrow dimmed, the label (where the section ends) bold."""
    previous, label = name.split(" -> ", 1)
    return cell(previous) + cell(" -> ", DIM) + cell(label, BOLD)


def status_cell(status: str) -> Cell:
    if status == "ok":
        return cell("ok", GREEN)
    return cell(status, YELLOW) if status == "exit 1" else cell(status, BOLD, RED)


def format_row(cells: list[Cell], widths: list[int], left: int) -> str:
    """The first `left` columns are left aligned, the others right aligned; the last one isn't padded if left."""
    parts = []
    for i, (c, width) in enumerate(zip(cells, widths)):
        pad = " " * max(width - len(c.plain), 0)
        if i < left:
            parts.append(c.rendered + ("" if i == len(cells) - 1 else pad))
        else:
            parts.append(pad + c.rendered)
    return "  ".join(parts)


def print_header(header: list[str], widths: list[int], left: int) -> None:
    print(format_row([cell(h, BOLD) for h in header], widths, left))
    print(style("-" * (sum(widths) + 2 * (len(widths) - 1)), DIM))


def print_table(title: str, header: list[str], rows: list[list[Cell]], left: int = 1) -> None:
    widths = [max([len(header[i])] + [len(r[i].plain) for r in rows]) for i in range(len(header))]
    print(f"\n{style(title, BOLD, CYAN)}")
    print_header(header, widths, left)
    for row in rows:
        print(format_row(row, widths, left))


def markdown_table(title: str, header: list[str], rows: list[list[Cell]], left: int = 1) -> str:
    """A GitHub flavored Markdown table of the plain cell texts, under a "### title" heading."""
    def line(texts: list[str]) -> str:
        return "| " + " | ".join(t.replace("|", "\\|") for t in texts) + " |"

    lines = [f"### {title}", "", line(header), line([":---" if i < left else "---:" for i in range(len(header))])]
    lines += [line([c.plain for c in row]) for row in rows]
    return "\n".join(lines) + "\n"


@dataclass
class Run:
    image: str
    mode: str
    status: str  # "ok", "exit <code>", "timeout"
    total_ms: float = 0.0
    sections: list[tuple[str, float]] = field(default_factory=list)


def find_images(paths: list[Path]) -> list[Path]:
    images = []
    for path in paths:
        if path.is_dir():
            images += sorted(p for p in path.iterdir() if p.suffix.lower() in IMAGE_SUFFIXES)
        elif path.is_file():
            images.append(path)
        else:
            sys.exit(f"{path} doesn't exist (generate the test images with `uv run tests/images/generate.py`)")
    return images


def parse_profile(stderr: str) -> tuple[float, list[tuple[str, float]]]:
    """Total time and sections of the main thread (the thread of the first milestone, "start")."""
    events = []
    main_tid = None
    for line in stderr.splitlines():
        match = PROFILE_LINE.match(line.strip())
        if not match:
            continue
        ms, tid, label = float(match[1]), match[2], match[3].strip()
        main_tid = main_tid or tid
        if tid == main_tid:
            events.append((ms, label))
    sections = [(f"{a[1]} -> {b[1]}", b[0] - a[0]) for a, b in zip(events, events[1:])]
    total = next((ms for ms, label in events if label == "load finished"), events[-1][0] if events else 0.0)
    return total, sections


def empty_dir(path: Path) -> None:
    for child in path.iterdir():
        shutil.rmtree(child) if child.is_dir() else child.unlink()


def run_enim(enim: Path, image: Path, mode: str, cache: Path, timeout: float) -> Run:
    options, _, cold = MODES[mode]
    if cold:
        empty_dir(cache)
    cmd = [str(enim), "--profile", "--exit-after-load", "--cache-dir", str(cache), *options, str(image)]
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, errors="replace", timeout=timeout)
    except subprocess.TimeoutExpired as e:
        stderr = e.stderr.decode(errors="replace") if isinstance(e.stderr, bytes) else (e.stderr or "")
        total, sections = parse_profile(stderr)
        return Run(image.name, mode, "timeout", total, sections)
    total, sections = parse_profile(result.stderr)
    status = "ok" if result.returncode == 0 else f"exit {result.returncode}"
    return Run(image.name, mode, status, total, sections)


def stats_row(name: Cell, values: list[float]) -> list[Cell]:
    return [name, cell(str(len(values)), DIM), ms_cell(sum(values)), ms_cell(statistics.mean(values), 2),
            ms_cell(statistics.median(values), 2), ms_cell(min(values), 2), ms_cell(max(values), 2)]


RUN_HEADER = ["image", "mode", "total ms", "status", "sections (ms since the previous milestone)"]


def run_row(run: Run) -> list[Cell]:
    sections = cell("")
    for i, (name, ms) in enumerate(run.sections):
        if i:
            sections += cell(" · ", DIM)
        sections += cell(name.split(" -> ", 1)[1] + " ") + ms_cell(ms)
    return [cell(run.image), cell(run.mode, MODE_COLORS[run.mode]), ms_cell(run.total_ms), status_cell(run.status),
            sections]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("paths", nargs="*", type=Path, default=[ROOT / "tests" / "images" / "out"])
    default_exe = ROOT / "bin" / ("enim.exe" if sys.platform == "win32" else "enim")
    parser.add_argument("--enim", type=Path, default=default_exe, help=f"enim executable (default {default_exe})")
    parser.add_argument("--modes", default=",".join(MODES), help="comma separated modes (default: all)")
    parser.add_argument("--repeat", type=int, default=1, help="runs per image and mode (default 1)")
    parser.add_argument("--timeout", type=float, default=600, help="seconds per run (default 600)")
    parser.add_argument("--csv", type=Path, help="also write every section of every run to this CSV file")
    parser.add_argument("--report", type=Path,
                        help="also write the sections and per mode tables to this Markdown file")
    parser.add_argument("--color",choices=["auto", "always", "never"], default="auto",
                        help="colored output (default auto: when printing to a terminal and NO_COLOR isn't set)")
    args = parser.parse_args()
    setup_color(args.color)

    modes = [m.strip() for m in args.modes.split(",") if m.strip()]
    if unknown := [m for m in modes if m not in MODES]:
        sys.exit(f"unknown mode(s) {', '.join(unknown)}; known: {', '.join(MODES)}")
    if "normal-warm" in modes and "normal-cold" not in modes:
        modes.insert(modes.index("normal-warm"), "normal-cold")  # warm needs the pyramid the cold run builds
    if not args.enim.is_file():
        sys.exit(f"{args.enim} doesn't exist: build enim first (cmake --build bin) or pass --enim")
    images = find_images(args.paths)
    if not images:
        sys.exit("no images found")

    # rows are printed as the runs finish, so the widths are fixed up front
    run_widths = [max(len(RUN_HEADER[0]), *(len(i.name) for i in images)), max(len(RUN_HEADER[1]), *map(len, modes)),
                  len(RUN_HEADER[2]) + 1, len("timeout"), 0]
    print(style("Runs", BOLD, CYAN))
    print_header(RUN_HEADER, run_widths[:-1] + [len(RUN_HEADER[-1])], left=2)

    runs: list[Run] = []
    with tempfile.TemporaryDirectory(prefix="enim-profile-") as cache_dir:
        cache = Path(cache_dir)
        for image in images:
            image_modes = [m for m in modes if not MODES[m][1] or image.suffix.lower() == ".bmp"]
            for _ in range(args.repeat):
                for mode in image_modes:
                    run = run_enim(args.enim, image, mode, cache, args.timeout)
                    runs.append(run)
                    print(format_row(run_row(run), run_widths, left=2), flush=True)

    by_section: dict[str, list[float]] = defaultdict(list)
    by_mode: dict[str, list[float]] = defaultdict(list)
    for run in runs:
        for name, ms in run.sections:
            by_section[name].append(ms)
        if run.sections:
            by_mode[run.mode].append(run.total_ms)

    columns = ["runs", "total ms", "mean", "median", "min", "max"]
    section_rows = sorted(by_section.items(), key=lambda item: -sum(item[1]))
    tables = [
        ("Sections, all runs (ms)", ["section", *columns],
         [stats_row(section_cell(n), v) for n, v in section_rows]),
        ("Load time to 'load finished', per mode (ms)", ["mode", *columns],
         [stats_row(cell(m, MODE_COLORS[m]), by_mode[m]) for m in modes if by_mode[m]]),
    ]
    for table in tables:
        print_table(*table)
    if args.report:
        args.report.write_text("\n".join(markdown_table(*table) for table in tables), encoding="utf-8")

    # exit 1 is ErrorLogged (src/errors.hpp): the broken test images log an error by design. Anything else (a crash,
    # a window/GL failure, a timeout) fails the profile run.
    logged = [r for r in runs if r.status == "exit 1"]
    failed = [r for r in runs if r.status not in ("ok", "exit 1")]
    if logged:
        print(style(f"\n{len(logged)} run(s) logged an error (expected for the broken test images):", YELLOW))
        for run in logged:
            print(f"  {run.image} {style(run.mode, MODE_COLORS[run.mode])}")
    if failed:
        print(style(f"\n{len(failed)} run(s) failed:", BOLD, RED))
        for run in failed:
            print(f"  {run.image} {style(run.mode, MODE_COLORS[run.mode])}: {style(run.status, RED)}")

    if args.csv:
        with args.csv.open("w", newline="") as f:
            writer = csv.writer(f)
            writer.writerow(["image", "mode", "status", "section", "ms"])
            for run in runs:
                for name, ms in run.sections:
                    writer.writerow([run.image, run.mode, run.status, name, f"{ms:.3f}"])
        print(f"\nSections written to {args.csv}")

    if failed:
        sys.exit(1)


if __name__ == "__main__":
    main()

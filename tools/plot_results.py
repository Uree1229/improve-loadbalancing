#!/usr/bin/env python3
"""Render README figures from the public experiment summaries.

Run from any directory: python tools/plot_results.py
Requires matplotlib. Korean labels require Malgun Gothic, Noto Sans CJK KR,
Noto Sans KR, NanumGothic, or AppleGothic. SVGs embed glyph paths.
This reads published aggregates; it does not run a kernel benchmark.
"""

from __future__ import annotations

import json
from pathlib import Path
import re

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import font_manager
from matplotlib.patches import Patch
from matplotlib.ticker import MaxNLocator


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "docs" / "images"
INK = "#172B49"
MUTED = "#536579"
GRID = "#DEE5EC"
BG = "#F7F9FC"
COLORS = {
    "base": "#8795A6",
    "v18": "#087F8C",
    "v19": "#4777B8",
    "v22": "#D17C25",
    "v23": "#7959B4",
    "v24": "#C56B3D",
}
HATCHES = {"base": "", "v18": "", "v19": "//", "v22": "..", "v23": "..", "v24": "xx"}


def configure() -> None:
    installed = {font.name for font in font_manager.fontManager.ttflist}
    preferred = ("Malgun Gothic", "Noto Sans CJK KR", "Noto Sans KR", "NanumGothic", "AppleGothic")
    font = next((name for name in preferred if name in installed), None)
    if font is None:
        raise SystemExit("Install a Korean font: Malgun Gothic, Noto Sans CJK KR, Noto Sans KR, NanumGothic, or AppleGothic")
    plt.rcParams.update({
        "font.family": font, "font.size": 12, "text.color": INK,
        "axes.labelcolor": MUTED, "xtick.color": MUTED, "ytick.color": MUTED,
        "axes.unicode_minus": False, "svg.fonttype": "path",
        "svg.hashsalt": "loadbalancing-readme-v1", "figure.facecolor": BG,
        "savefig.facecolor": BG, "axes.facecolor": "white",
    })
    OUTPUT.mkdir(parents=True, exist_ok=True)


def read_json(path: str) -> dict:
    return json.loads((ROOT / path).read_text(encoding="utf-8"))


def frame(title: str, subtitle: str, *, rows: int = 1, columns: int = 3):
    height = 7.2 if rows == 1 else 10.1
    fig, axes = plt.subplots(rows, columns, figsize=(15.4, height), squeeze=False)
    fig.subplots_adjust(left=.065, right=.975, top=.72 if rows == 1 else .79,
                        bottom=.265 if rows == 1 else .205, wspace=.29, hspace=.55)
    fig.text(.055, .94, title, fontsize=23, weight="bold", va="top")
    fig.text(.055, .86 if rows == 1 else .885, subtitle, fontsize=12, color=MUTED, va="top")
    return fig, list(axes.flat)


def format_axis(ax, title: str, unit: str, maximum: float) -> None:
    ax.set_title(title, loc="left", fontsize=15, weight="bold", pad=16)
    ax.set_ylabel(unit, fontsize=11, labelpad=9)
    ax.set_ylim(0, maximum * 1.22)
    ax.yaxis.set_major_locator(MaxNLocator(nbins=5))
    ax.grid(axis="y", color=GRID, linewidth=.8)
    ax.set_axisbelow(True)
    ax.tick_params(axis="both", length=0, labelsize=11)
    ax.tick_params(axis="x", pad=10)
    for side in ("left", "right", "top"):
        ax.spines[side].set_visible(False)
    ax.spines["bottom"].set_color(GRID)


def bars(ax, values: list[float], labels: list[str], versions: list[str], title: str, unit: str, decimals: int = 3) -> None:
    format_axis(ax, title, unit, max(values))
    rectangles = ax.bar(range(len(values)), values, width=.64,
                        color=[COLORS[v] for v in versions], edgecolor="white", linewidth=.8)
    for rectangle, version in zip(rectangles, versions):
        rectangle.set_hatch(HATCHES[version])
    ax.bar_label(rectangles, labels=[f"{x:.{decimals}f}" for x in values], padding=7, fontsize=11, color=INK)
    ax.set_xticks(range(len(labels)), labels)


def footer(fig, lines: list[str]) -> None:
    # Reserve a dedicated footer rather than squeezing caveats into the axes.
    step = .036 if fig.get_figheight() < 8 else .027
    top = .155 if fig.get_figheight() < 8 else .135
    for i, line in enumerate(lines):
        fig.text(.055, top - i * step, line, fontsize=10, color=MUTED, va="top")


def save(fig, name: str) -> None:
    for suffix in ("png", "svg"):
        path = OUTPUT / f"{name}.{suffix}"
        metadata = {"Software": "tools/plot_results.py"} if suffix == "png" else {"Date": None, "Creator": "tools/plot_results.py"}
        fig.savefig(path, dpi=160, metadata=metadata)
        if suffix == "svg":
            # Normalize renderer whitespace for stable, clean Git diffs on Windows.
            svg = path.read_text(encoding="utf-8")
            path.write_text("\n".join(line.rstrip() for line in svg.splitlines()) + "\n",
                            encoding="utf-8", newline="\n")
        print(path.relative_to(ROOT))
    plt.close(fig)


def kernel_comparison() -> None:
    source = "experiments/fixed-host-kernel-ab-20261108/results.json"
    data = read_json(source)
    boots = data["boots"]
    assert [b["phase"] for b in boots] == ["a1", "b1", "a2", "b2", "a3"]
    assert data["formal_trials"] == 180
    labels = ["원본 1", "v18\n수정 1", "원본 2", "v18\n수정 2", "원본 3"]
    versions = ["base", "v18", "base", "v18", "base"]
    fig, axes = frame("커널 비교  |  원본 ↔ v18", "v23 Windows 호스트 정책 고정 · 새 부팅 5회 · 정식 실행 180회 · 모든 지표는 낮을수록 좋음")
    specs = [
        ("recovery-200", "median", "200ms 조건 복구 지연\n중앙값", "ms"),
        ("recovery-800", "maximum", "835ms 조건 복구 지연\n관측 최댓값", "ms"),
        ("pinned-256", "median_host_vm_cpu_s", "pinned-256 호스트 VM\nCPU 시간 중앙값", "초"),
    ]
    for ax, (condition, key, title, unit) in zip(axes, specs):
        expected = 4 if condition == "pinned-256" else 8
        assert all(b["conditions"][condition]["samples"] == expected for b in boots)
        bars(ax, [b["conditions"][condition][key] for b in boots], labels, versions, title, unit)
    footer(fig, [
        "표시 순서는 실제 부팅 순서. 복구 조건: 부팅당 각 8회 / pinned-256: 부팅당 4회. 원본은 보관된 프로젝트 기준 커널.",
        "200ms는 조건 이름(실제 hold 136~257ms). 최댓값은 표본의 최대치이며 p99나 최악시간 보장이 아님. 단일 VM 결과.",
        f"출처: {source}  |  공개 집계값을 시각화했으며 오차막대·신뢰구간은 추정하지 않음.",
    ])
    save(fig, "kernel-comparison")


def host_policy_comparison() -> None:
    source = "versions/v24/RESULTS.json"
    data = read_json(source)
    versions = ["v18", "v19", "v23", "v24"]
    assert data["formal_trials"] == 144
    assert not data["improvement_over_v23_pass"]
    fig, axes = frame("호스트 설정 버전 비교  |  v18 · v19 · v23 · v24", "모든 조건의 게스트 커널은 v18 · 같은 새 부팅 1회에서 교차 비교 · 정식 실행 144회 · 낮을수록 좋음", rows=2, columns=2)
    specs = [
        ("recovery_200_median_ms", "200ms 조건 복구 지연 중앙값", "ms"),
        ("recovery_835_median_ms", "835ms 조건 복구 지연 중앙값", "ms"),
        ("recovery_835_maximum_ms", "835ms 조건 복구 지연 관측 최댓값", "ms"),
        ("pinned_256_host_cpu_median_s", "pinned-256 호스트 VM CPU 시간 중앙값", "초"),
    ]
    for ax, (key, title, unit) in zip(axes, specs):
        bars(ax, [data[key][v] for v in versions], versions, versions, title, unit)
    footer(fig, [
        "v24는 v23 대비 추가 개선 기준에 미달. v24의 재부팅 재현은 진행하지 않음. CPU 주파수·에너지 사용량은 미측정.",
        "관측 최댓값은 표본의 최대치이며 p99나 최악시간 보장이 아님. 다른 부팅의 결과와 합산하지 않음.",
        f"출처: {source}  |  공개 집계값을 시각화했으며 오차막대·신뢰구간은 추정하지 않음.",
    ])
    save(fig, "host-policy-comparison")


def read_v22() -> dict:
    text = (ROOT / "versions/v22/RESULTS.md").read_text(encoding="utf-8")
    output = {"first_boot": {}, "reboot_reproduction": {}}
    names = {"첫 부팅": "first_boot", "재부팅": "reboot_reproduction"}
    seen = 0
    for line in text.splitlines():
        cells = [cell.strip() for cell in line.split("|")][1:-1]
        if len(cells) != 5 or cells[0] not in names:
            continue
        if "200ms" in cells[1]:
            key = "recovery_200_median_ms"
        elif "835ms" in cells[1]:
            key = "recovery_835_maximum_ms"
        elif "pinned-256" in cells[1]:
            key = "pinned_256_host_cpu_median_s"
        else:
            raise ValueError(f"Unexpected v22 metric: {cells[1]}")
        values = [float(re.fullmatch(r"([0-9]+\.[0-9]+)\s+(?:ms|s)", cell).group(1)) for cell in cells[2:]]
        output[names[cells[0]]][key] = dict(zip(["v18", "v19", "v22"], values))
        seen += 1
    assert seen == 6, "v22 result table changed; review the parser"
    return output


def reproduction(candidate: str) -> None:
    source = "versions/v22/RESULTS.md" if candidate == "v22" else "versions/v23/RESULTS.json"
    data = read_v22() if candidate == "v22" else read_json(source)
    versions = ["v18", "v19", candidate]
    fig, axes = frame(f"{candidate} 호스트 설정  |  첫 부팅과 재부팅", "모든 조건의 게스트 커널은 v18 · 부팅별 고유 대조군과 교차 비교 · 108회 × 2부팅 = 216회 · 낮을수록 좋음")
    fig.set_size_inches(15.4, 7.7)
    fig.subplots_adjust(top=.65)
    legend = [Patch(facecolor=COLORS[v], edgecolor="white", hatch=HATCHES[v], label=f"{v} 설정") for v in versions]
    fig.legend(handles=legend, loc="upper left", bbox_to_anchor=(.055, .795), ncol=3, frameon=False, fontsize=11, handlelength=1.6)
    specs = [
        ("recovery_200_median_ms", "200ms 조건 복구 지연\n중앙값", "ms"),
        ("recovery_835_maximum_ms", "835ms 조건 복구 지연\n관측 최댓값", "ms"),
        ("pinned_256_host_cpu_median_s", "pinned-256 호스트 VM\nCPU 시간 중앙값", "초"),
    ]
    boot_names = ["first_boot", "reboot_reproduction"]
    for ax, (key, title, unit) in zip(axes, specs):
        maximum = max(data[boot][key][version] for boot in boot_names for version in versions)
        format_axis(ax, title, unit, maximum)
        for i, version in enumerate(versions):
            values = [data[boot][key][version] for boot in boot_names]
            rects = ax.bar([i * .26 - .26, 1.3 + i * .26 - .26], values, width=.24,
                           color=COLORS[version], edgecolor="white", hatch=HATCHES[version], linewidth=.7)
            digits = 2 if candidate == "v22" else 3
            ax.bar_label(rects, labels=[f"{x:.{digits}f}" for x in values], padding=6, fontsize=9, color=INK)
        ax.set_xticks([0, 1.3], ["첫 부팅", "재부팅"])
        ax.set_xlim(-.55, 1.85)
    verdict = (
        "v22: 재부팅 pinned CPU 짝비교 변화 +20.24%로 +5% 이내 기준 미달. 막대는 집계 중앙값이며 짝비교 비율과 다름."
        if candidate == "v22" else
        "v23: 두 부팅에서 정한 기준 통과. 중단된 29회 실행은 제외. 각 부팅의 대조군과 비교하며 부팅 간 값을 합치지 않음."
    )
    sample_note = (
        "복구 조건: 부팅·설정당 각 8회 / pinned-256: 각 4회. 관측 최댓값은 표본 최대치이며 p99나 최악시간 보장이 아님."
        if candidate == "v22" else
        "관측 최댓값은 표본의 최대치이며 p99나 최악시간 보장이 아님. 단일 VM 결과이며 범용 성능 향상을 뜻하지 않음."
    )
    footer(fig, [
        verdict,
        sample_note,
        f"출처: {source}  |  공개 집계값 그대로 표시. 오차막대·신뢰구간은 추정하지 않음.",
    ])
    save(fig, f"{candidate}-reproduction")


if __name__ == "__main__":
    configure()
    kernel_comparison()
    host_policy_comparison()
    reproduction("v22")
    reproduction("v23")

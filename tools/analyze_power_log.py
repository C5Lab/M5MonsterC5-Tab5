#!/usr/bin/env python3
"""Summarize POWER_CSV telemetry copied from a Tab5 serial log."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import re
from statistics import fmean
from typing import Iterable, Sequence


MARKER = "POWER_CSV,"
ANSI_ESCAPE = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")


@dataclass(frozen=True)
class PowerSample:
    time_ms: int
    profile: str
    screen: str
    cpu_max_mhz: int
    voltage_v: float
    current_ma: float
    power_mw: float
    average_60s_power_mw: float
    energy_mwh: float


@dataclass(frozen=True)
class PowerSummary:
    samples: int
    duration_s: float
    average_voltage_v: float
    average_current_ma: float
    average_power_mw: float
    p95_power_mw: float
    energy_wh: float


def parse_power_log(lines: Iterable[str]) -> list[PowerSample]:
    samples: list[PowerSample] = []
    for line in lines:
        line = ANSI_ESCAPE.sub("", line)
        marker_at = line.find(MARKER)
        if marker_at < 0:
            continue
        fields = line[marker_at + len(MARKER) :].strip().split(",")
        if len(fields) != 9:
            continue
        try:
            samples.append(
                PowerSample(
                    time_ms=int(fields[0]),
                    profile=fields[1],
                    screen=fields[2],
                    cpu_max_mhz=int(fields[3]),
                    voltage_v=float(fields[4]),
                    current_ma=float(fields[5]),
                    power_mw=float(fields[6]),
                    average_60s_power_mw=float(fields[7]),
                    energy_mwh=float(fields[8]),
                )
            )
        except ValueError:
            continue
    return sorted(samples, key=lambda sample: sample.time_ms)


def summarize(samples: Sequence[PowerSample]) -> PowerSummary:
    if not samples:
        raise ValueError("no POWER_CSV samples found")

    duration_ms = max(0, samples[-1].time_ms - samples[0].time_ms)
    energy_wh = 0.0
    for previous, current in zip(samples, samples[1:]):
        elapsed_ms = current.time_ms - previous.time_ms
        if elapsed_ms <= 0:
            continue
        interval_power_mw = (previous.power_mw + current.power_mw) * 0.5
        energy_wh += interval_power_mw * elapsed_ms / 3_600_000_000.0

    ordered_power = sorted(sample.power_mw for sample in samples)
    p95_index = max(0, (95 * len(ordered_power) + 99) // 100 - 1)
    return PowerSummary(
        samples=len(samples),
        duration_s=duration_ms / 1000.0,
        average_voltage_v=fmean(sample.voltage_v for sample in samples),
        average_current_ma=fmean(sample.current_ma for sample in samples),
        average_power_mw=fmean(sample.power_mw for sample in samples),
        p95_power_mw=ordered_power[p95_index],
        energy_wh=energy_wh,
    )


def format_summary(result: PowerSummary) -> str:
    return "\n".join(
        [
            f"samples: {result.samples}",
            f"duration: {result.duration_s:.1f} s",
            f"average voltage: {result.average_voltage_v:.3f} V",
            f"average battery current: {result.average_current_ma:.1f} mA",
            f"average battery power: {result.average_power_mw:.1f} mW",
            f"p95 battery power: {result.p95_power_mw:.1f} mW",
            f"energy in capture: {result.energy_wh:.6f} Wh",
        ]
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="serial log containing POWER_CSV records")
    args = parser.parse_args()

    samples = parse_power_log(args.log.read_text(encoding="utf-8", errors="replace").splitlines())
    try:
        result = summarize(samples)
    except ValueError as error:
        parser.error(str(error))
    print(format_summary(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

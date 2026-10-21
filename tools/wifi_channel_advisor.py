"""Independent test reference for the 20 MHz AP-overlap heuristic.

Not a radio measurement, regulatory database or production C execution harness.
See docs/wifi-analyzer.md for scoring and coverage limitations.
"""
CHANNELS_5 = [36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112,
              116, 120, 124, 128, 132, 136, 140, 144, 149, 153, 157, 161,
              165, 169, 173, 177]


def advise(snapshot, band, extended=False, exclude=""):
    out = dict(state="no_snapshot", observed=0, unknown=0, choices=[])
    if not snapshot or not snapshot["valid"]:
        return out
    if band not in (1, 2):
        out["state"] = "invalid_band"
        return out
    if snapshot["truncated"]:
        out["state"] = "truncated"
        return out
    required = range(1, 14) if band == 1 else CHANNELS_5
    if snapshot["band"] not in (0, band) or not set(required) <= set(snapshot["channels"]):
        out["state"] = "incomplete_scope"
        return out
    candidates = [1, 6, 11] if band == 1 else CHANNELS_5[:25 if extended else 4]
    aps = [a for a in snapshot["aps"] if a["band"] == band and
           not (exclude and a["bssid"] == exclude)]
    out["state"] = "ok"
    out["observed"] = len(aps)
    for channel in candidates:
        center = 2407 + channel * 5 if band == 1 else 5000 + channel * 5
        choice = dict(channel=channel, score=0, overlapping=0, strongest=-128,
                      dfs=band == 2 and 52 <= channel <= 144)
        for a in aps:
            weight = (max(-100, min(-20, a["rssi"])) + 101) ** 2
            known = a["width"] in (20, 40, 80, 160, 8080) and a["center"] and (
                a["width"] != 8080 or a["center2"])
            if not known:
                choice["score"] += weight * 20
                if channel == candidates[0]:
                    out["unknown"] += 1
                continue
            half = 40 if a["width"] == 8080 else a["width"] // 2
            centers = [a["center"]] + ([a["center2"]] if a["width"] == 8080 else [])
            overlap = min(20, sum(max(0, min(center + 10, c + half) -
                                     max(center - 10, c - half)) for c in centers))
            choice["score"] += weight * overlap
            if overlap:
                choice["overlapping"] += 1
                choice["strongest"] = max(choice["strongest"], a["rssi"])
        out["choices"].append(choice)
    out["choices"].sort(key=lambda x: (x["score"], x["channel"]))
    return out

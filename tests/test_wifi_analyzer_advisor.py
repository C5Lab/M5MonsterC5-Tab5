"""Compiler-free reference tests; these do not execute the production C model."""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from wifi_channel_advisor import advise, CHANNELS_5


def snapshot(aps=(), channels=None, **extra):
    return dict(valid=True, truncated=False, band=0, channels=list(range(1, 15)) + CHANNELS_5
                if channels is None else channels, aps=list(aps), **extra)


def ap(channel, rssi=-60, width=20, center=None, bssid="a", band=1, center2=0):
    return dict(channel=channel, rssi=rssi, width=width,
                center=center if center is not None else (2407 + channel * 5 if band == 1 else 5000 + channel * 5),
                center2=center2, bssid=bssid, band=band)


class ChannelAdvisorTests(unittest.TestCase):
    def test_no_snapshot_or_wrong_band(self):
        self.assertEqual("no_snapshot", advise(None, 1)["state"])
        self.assertEqual("invalid_band", advise(snapshot(), 0)["state"])
        s = snapshot(); s["band"] = 1
        self.assertEqual("incomplete_scope", advise(s, 2)["state"])

    def test_truncated_or_partial_scans_do_not_recommend(self):
        s = snapshot(); s["truncated"] = True
        self.assertEqual("truncated", advise(s, 1)["state"])
        self.assertEqual("incomplete_scope", advise(snapshot(channels=[1, 6, 11]), 1)["state"])
        self.assertEqual("incomplete_scope", advise(snapshot(channels=[36, 40, 44, 48]), 2)["state"])

    def test_24_candidates_and_empty_tie(self):
        r = advise(snapshot(), 1)
        self.assertEqual([1, 6, 11], [x["channel"] for x in r["choices"]])
        self.assertEqual(0, r["observed"])
        self.assertTrue(all(x["score"] == 0 for x in r["choices"]))

    def test_strong_neighbor_and_adjacent_overlap(self):
        r = advise(snapshot([ap(1, -40), ap(6, -85)]), 1)
        self.assertEqual([11, 6, 1], [x["channel"] for x in r["choices"]])
        r = advise(snapshot([ap(3)]), 1)
        choices = {x["channel"]: x for x in r["choices"]}
        self.assertGreater(choices[1]["score"], choices[6]["score"])
        self.assertGreater(choices[6]["score"], choices[11]["score"])

    def test_wide_ap_penalizes_all_covered_5ghz_channels(self):
        r = advise(snapshot([ap(36, width=80, center=5210, band=2)]), 2)
        self.assertEqual([36, 40, 44, 48], [x["channel"] for x in r["choices"]])
        self.assertTrue(all(x["overlapping"] == 1 for x in r["choices"]))
        self.assertEqual(1, len({x["score"] for x in r["choices"]}))

    def test_disjoint_segments_leave_gap_unpenalized(self):
        r = advise(snapshot([ap(36, width=8080, center=5210, center2=5530, band=2)]), 2, extended=True)
        d = {x["channel"]: x for x in r["choices"]}
        self.assertEqual(0, d[64]["score"])
        self.assertGreater(d[36]["score"], 0)
        self.assertGreater(d[100]["score"], 0)

    def test_160mhz_ap_covers_both_80mhz_halves(self):
        r = advise(snapshot([ap(64, width=160, center=5250, band=2)]), 2, extended=True)
        d = {x["channel"]: x for x in r["choices"]}
        for channel in (36, 40, 44, 48, 52, 56, 60, 64):
            self.assertEqual(33620, d[channel]["score"])
        self.assertEqual(0, d[100]["score"])

    def test_maximum_snapshot_score_fits_uint32(self):
        r = advise(snapshot([ap(1, 20, bssid=str(i)) for i in range(128)]), 1)
        worst = r["choices"][-1]
        self.assertEqual(16796160, worst["score"])
        self.assertEqual(128, worst["overlapping"])

    def test_unknown_geometry_never_becomes_clear_spectrum(self):
        r = advise(snapshot([ap(64, width=0, center=0, band=2)]), 2)
        self.assertEqual(1, r["unknown"])
        self.assertTrue(all(x["score"] > 0 and x["overlapping"] == 0 for x in r["choices"]))
        self.assertEqual(1, len({x["score"] for x in r["choices"]}))

    def test_own_bssid_exclusion_is_explicit_and_exact(self):
        s = snapshot([ap(1, bssid="own"), ap(6, bssid="neighbor")])
        r = advise(s, 1, exclude="own")
        self.assertEqual(1, r["observed"])
        self.assertEqual(1, r["choices"][0]["channel"])
        self.assertEqual(2, advise(s, 1, exclude="missing")["observed"])

    def test_bands_are_independent_and_dfs_marked(self):
        r = advise(snapshot([ap(1, -30)]), 2, extended=True)
        self.assertEqual(0, r["observed"])
        self.assertEqual(25, len(r["choices"]))
        for x in r["choices"]:
            self.assertEqual(52 <= x["channel"] <= 144, x["dfs"])

    def test_scores_are_bounded_and_monotonic(self):
        scores = [advise(snapshot([ap(1, rssi)]), 1)["choices"][-1]["score"]
                  for rssi in [-127, -100, -90, -60, -20, 20]]
        self.assertEqual(sorted(scores), scores)
        self.assertEqual(scores[0], scores[1])
        self.assertEqual(scores[-1], scores[-2])


if __name__ == "__main__":
    unittest.main()

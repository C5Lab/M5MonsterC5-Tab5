import unittest

try:
    from tools.analyze_power_log import parse_power_log, summarize
except ModuleNotFoundError:
    parse_power_log = None
    summarize = None


class PowerLogAnalyzerTests(unittest.TestCase):
    def test_parses_uart_prefix_and_computes_energy(self):
        self.assertIsNotNone(parse_power_log, "tools/analyze_power_log.py is missing")
        samples = parse_power_log(
            [
                "I (100) power: POWER_CSV,0,interactive,on,360,8.000,1000.0,8000.0,8000.0,0.000\n",
                "I (1100) power: POWER_CSV,1000,interactive,on,360,8.000,1000.0,8000.0,8000.0,2.222\n",
                "I (2100) power: POWER_CSV,2000,interactive,on,360,8.000,1000.0,8000.0,8000.0,4.444\n",
            ]
        )

        self.assertEqual(len(samples), 3)
        self.assertEqual(samples[0].profile, "interactive")
        self.assertEqual(samples[0].cpu_max_mhz, 360)

        result = summarize(samples)
        self.assertEqual(result.samples, 3)
        self.assertAlmostEqual(result.duration_s, 2.0)
        self.assertAlmostEqual(result.average_current_ma, 1000.0)
        self.assertAlmostEqual(result.average_power_mw, 8000.0)
        self.assertAlmostEqual(result.energy_wh, 8.0 * 2.0 / 3600.0)

    def test_ignores_noise_and_keeps_charging_samples(self):
        self.assertIsNotNone(parse_power_log, "tools/analyze_power_log.py is missing")
        samples = parse_power_log(
            [
                "ordinary firmware log\n",
                "POWER_CSV_HEADER,time_ms,profile,screen,cpu_max_mhz,voltage_v,current_ma,power_mw,avg60_power_mw,energy_mwh\n",
                "\x1b[33mW (10) tag: POWER_CSV,5000,screen_off,off,180,8.200,-250.0,0.0,100.0,12.500\x1b[0m\n",
                "broken POWER_CSV,row\n",
            ]
        )

        self.assertEqual(len(samples), 1)
        self.assertEqual(samples[0].screen, "off")
        self.assertAlmostEqual(samples[0].current_ma, -250.0)
        self.assertAlmostEqual(samples[0].power_mw, 0.0)


if __name__ == "__main__":
    unittest.main()

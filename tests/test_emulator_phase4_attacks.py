"""Collect Phase 4.5 browser acceptance; requires the rebuilt static artifact."""
import unittest
from test_emulator_attacks_deauth_arp import AttacksDeauthArpBrowser
from test_emulator_attacks_karma_beacon import KarmaBeaconBrowser
from test_emulator_attacks_rogue_evil_mitm import NativeAttackBrowser
from test_emulator_observer_activity import ObserverActivityBrowser

if __name__=='__main__':unittest.main()

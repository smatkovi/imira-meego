# Zweiter Eingriff: Suchlaeufe ohne Zusatz-IEs.
#
# Der wl1271-Treiber im Harmattan-Kernel meldet `max scan IE len = 0` und
# `max scan SSIDs = 1` (nachgemessen). wpa_supplicant haengt an jede P2P-Suche
# WPS- und P2P-IEs (148 Byte) und bei p2p_probe zusaetzlich eine Ratenmaske,
# die es im 2.6.32 noch nicht gab -- der Treiber antwortet mit EINVAL und es
# findet gar kein Scan statt. Ohne die IEs erkennt uns kein Dongle als
# P2P-Geraet, aber wir finden *seine* Gruppe im Beacon, und darum geht es.
#
# Wie der erste Eingriff nur aktiv, wenn WPA_P2P_AS_STATION gesetzt ist.
import sys
p = "/tmp/wpa-harmattan/wpa_supplicant-2.11/src/drivers/driver_nl80211_scan.c"
s = open(p).read()

old = """	struct wpa_driver_nl80211_data *drv = bss->drv;
	struct nl_msg *msg;
	size_t i;
	u32 scan_flags = 0;

	msg = nl80211_cmd_msg(bss, 0, cmd);"""
new = """	struct wpa_driver_nl80211_data *drv = bss->drv;
	struct nl_msg *msg;
	size_t i;
	u32 scan_flags = 0;
	/* Harmattan: der Treiber nimmt keine Zusatz-IEs und nur eine SSID. */
	int bare = getenv("WPA_P2P_AS_STATION") != NULL;

	msg = nl80211_cmd_msg(bss, 0, cmd);"""
if old not in s: sys.exit("scan_common head nicht gefunden")
s = s.replace(old, new, 1)

old = """		for (i = 0; i < params->num_ssids; i++) {"""
new = """		for (i = 0; i < params->num_ssids && !(bare && i >= 1); i++) {"""
if old not in s: sys.exit("ssid-Schleife nicht gefunden")
s = s.replace(old, new, 1)

old = """		if (!params->non_coloc_6ghz) {
			wpa_printf(MSG_DEBUG,
				   "nl80211: Scan co-located APs on 6 GHz");
			scan_flags |= NL80211_SCAN_FLAG_COLOCATED_6GHZ;
		}"""
new = """		if (!params->non_coloc_6ghz && !bare) {
			wpa_printf(MSG_DEBUG,
				   "nl80211: Scan co-located APs on 6 GHz");
			scan_flags |= NL80211_SCAN_FLAG_COLOCATED_6GHZ;
		}"""
if old not in s: sys.exit("6-GHz-Schalter nicht gefunden")
s = s.replace(old, new, 1)

old = """	if (params->extra_ies) {
		wpa_hexdump(MSG_MSGDUMP, "nl80211: Scan extra IEs",
			    params->extra_ies, params->extra_ies_len);
		if (nla_put(msg, NL80211_ATTR_IE, params->extra_ies_len,
			    params->extra_ies))
			goto fail;
	}"""
new = """	if (params->extra_ies && bare) {
		wpa_printf(MSG_DEBUG, "nl80211: %u Byte Zusatz-IEs weggelassen "
			   "(Treiber nimmt keine)", (unsigned) params->extra_ies_len);
	} else if (params->extra_ies) {
		wpa_hexdump(MSG_MSGDUMP, "nl80211: Scan extra IEs",
			    params->extra_ies, params->extra_ies_len);
		if (nla_put(msg, NL80211_ATTR_IE, params->extra_ies_len,
			    params->extra_ies))
			goto fail;
	}"""
if old not in s: sys.exit("extra_ies nicht gefunden")
s = s.replace(old, new, 1)

old = """	if (scan_flags &&
	    nla_put_u32(msg, NL80211_ATTR_SCAN_FLAGS, scan_flags))"""
new = """	if (scan_flags && !bare &&
	    nla_put_u32(msg, NL80211_ATTR_SCAN_FLAGS, scan_flags))"""
if old not in s: sys.exit("scan_flags nicht gefunden")
s = s.replace(old, new, 1)

old = """	if (params->p2p_probe) {
		struct nlattr *rates;

		wpa_printf(MSG_DEBUG, "nl80211: P2P probe - mask SuppRates");"""
new = """	if (params->p2p_probe && getenv("WPA_P2P_AS_STATION")) {
		wpa_printf(MSG_DEBUG, "nl80211: P2P probe - Ratenmaske "
			   "weggelassen (Treiber von 2011)");
	} else if (params->p2p_probe) {
		struct nlattr *rates;

		wpa_printf(MSG_DEBUG, "nl80211: P2P probe - mask SuppRates");"""
if old not in s: sys.exit("p2p_probe nicht gefunden")
s = s.replace(old, new, 1)

open(p, "w").write(s)
print("Suchlauf-Eingriff angewandt")

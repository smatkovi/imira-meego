# Teach wpa_supplicant to do P2P on a driver that has no P2P interface types.
# The wl12xx of the stock Harmattan kernel (2.6.32) offers only ADHOC, STATION
# and MONITOR, but it does remain_on_channel and off-channel mgmt_tx -- that is
# everything a P2P *client* needs (measured on the N950, 09.10.2026). The phone
# can never be the group owner, so the sink has to open the group.
# Both changes are inert unless WPA_P2P_AS_STATION is set in the environment.
import re, sys, os
base = "/tmp/wpa-harmattan/wpa_supplicant-2.11/src/drivers/"

# --- 1. claim P2P capability when only the iftypes are missing --------------
p = base + "driver_nl80211_capa.c"
s = open(p).read()
old = """	if (info->p2p_go_supported && info->p2p_client_supported)
		drv->capa.flags |= WPA_DRIVER_FLAGS_P2P_CAPABLE;"""
new = """	if (info->p2p_go_supported && info->p2p_client_supported)
		drv->capa.flags |= WPA_DRIVER_FLAGS_P2P_CAPABLE;
	else if (os_getenv("WPA_P2P_AS_STATION")) {
		/* Harmattan: no P2P iftypes, but remain-on-channel and
		 * off-channel mgmt_tx are there -- enough to be a P2P client.
		 * The group interface becomes a plain STATION (see
		 * nl80211_p2p_as_station() in driver_nl80211.c). */
		wpa_printf(MSG_INFO, "nl80211: P2P forced on without P2P "
			   "iftypes (client only, group iface = STATION)");
		drv->capa.flags |= WPA_DRIVER_FLAGS_P2P_CAPABLE;
	}"""
if old not in s: sys.exit("capa site not found")
s = s.replace(old, new, 1)
open(p, "w").write(s)

# --- 2. map the P2P interface types onto STATION ----------------------------
p = base + "driver_nl80211.c"
s = open(p).read()
helper = """
/* Harmattan: translate the P2P interface types to STATION, see
 * driver_nl80211_capa.c. Only active with WPA_P2P_AS_STATION set. */
static enum nl80211_iftype nl80211_p2p_as_station(enum nl80211_iftype iftype)
{
	if (!os_getenv("WPA_P2P_AS_STATION"))
		return iftype;
	if (iftype == NL80211_IFTYPE_P2P_CLIENT ||
	    iftype == NL80211_IFTYPE_P2P_DEVICE)
		return NL80211_IFTYPE_STATION;
	return iftype;
}


static int nl80211_create_iface_once(struct wpa_driver_nl80211_data *drv,"""
old = "\nstatic int nl80211_create_iface_once(struct wpa_driver_nl80211_data *drv,"
if old not in s: sys.exit("create_iface site not found")
s = s.replace(old, helper, 1)

old = """	struct nl_msg *msg;
	int ifidx;
	int ret = -ENOBUFS;

	wpa_printf(MSG_DEBUG, "nl80211: Create interface iftype %d (%s)",
		   iftype, nl80211_iftype_str(iftype));"""
new = """	struct nl_msg *msg;
	int ifidx;
	int ret = -ENOBUFS;

	iftype = nl80211_p2p_as_station(iftype);
	wpa_printf(MSG_DEBUG, "nl80211: Create interface iftype %d (%s)",
		   iftype, nl80211_iftype_str(iftype));"""
if old not in s: sys.exit("create_iface body not found")
s = s.replace(old, new, 1)

# set_mode: the one place SET_INTERFACE carries an iftype
old = """	wpa_printf(MSG_DEBUG, "nl80211: Set mode ifindex %d iftype %d (%s)",
		   ifindex, mode, nl80211_iftype_str(mode));"""
new = """	mode = nl80211_p2p_as_station(mode);
	wpa_printf(MSG_DEBUG, "nl80211: Set mode ifindex %d iftype %d (%s)",
		   ifindex, mode, nl80211_iftype_str(mode));"""
if old not in s: sys.exit("set_mode site not found")
s = s.replace(old, new, 1)
open(p, "w").write(s)
print("patched")

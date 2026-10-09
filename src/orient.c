/* orient.c -- die Drehung der Oberflaeche beim Compositor erfragen.
 *
 * mcompositor meldet sie als Contextkit-Eigenschaft auf dem **Sitzungs**bus:
 *
 *   Dienst  org.maemo.mcompositor.context
 *   Pfad    /Screen/CurrentWindow/OrientationAngle   (nicht unter
 *           /org/maemo/contextkit/ -- dieser Anbieter haengt seine Objekte
 *           direkt in die Wurzel, nachgesehen per Introspektion)
 *   Methode org.maemo.contextkit.Property.Get -> Feld aus einer Variante,
 *           dann ein uint64 Zeitstempel
 *
 * Gegenprobe am Geraet: Oberflaeche hochkant -> 270, und von Hand gebraucht
 * wurde dafuer eine Drehung von 90 Grad; also Drehung = (360 - Winkel) % 360.
 *
 * Der Lagesensor (Screen.TopEdge beim SensorService) waere die andere Quelle,
 * sagt aber nur, wie das Geraet liegt -- nicht, was die Oberflaeche daraus
 * macht (Anwendungen duerfen sich festlegen, und die Tastatur zwingt quer).
 */
#include "orient.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dbus/dbus.h>

static DBusConnection *conn;

/* sudo raeumt die Umgebung auf; Harmattan legt die Adresse des Sitzungsbusses
 * zusaetzlich in /tmp/session_bus_address.user ab. */
static char *address_from_file(void)
{
    FILE *f = fopen("/tmp/session_bus_address.user", "r");
    if (!f) return NULL;
    static char line[512];
    char *addr = NULL;
    while (fgets(line, sizeof line, f)) {
        char *p = strstr(line, "DBUS_SESSION_BUS_ADDRESS=");
        if (!p) continue;
        p += strlen("DBUS_SESSION_BUS_ADDRESS=");
        if (*p == '\'' || *p == '"') p++;
        char *e = p;
        while (*e && *e != '\'' && *e != '"' && *e != '\n' && *e != ';') e++;
        *e = 0;
        addr = p;
        break;
    }
    fclose(f);
    return addr;
}

int orient_open(void)
{
    DBusError err;
    dbus_error_init(&err);
    const char *addr = getenv("DBUS_SESSION_BUS_ADDRESS");
    if (!addr || !*addr) addr = address_from_file();
    if (addr && *addr) {
        conn = dbus_connection_open(addr, &err);
        if (conn && !dbus_bus_register(conn, &err)) {
            dbus_connection_unref(conn);
            conn = NULL;
        }
    } else {
        conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    }
    if (!conn) {
        fprintf(stderr, "Lage: kein Sitzungsbus (%s)\n",
                dbus_error_is_set(&err) ? err.message : "keine Adresse");
        dbus_error_free(&err);
        return -1;
    }
    dbus_connection_set_exit_on_disconnect(conn, FALSE);
    dbus_error_free(&err);
    return 0;
}

int orient_angle(void)
{
    if (!conn) return -1;
    DBusMessage *m = dbus_message_new_method_call(
        "org.maemo.mcompositor.context",
        "/Screen/CurrentWindow/OrientationAngle",
        "org.maemo.contextkit.Property", "Get");
    if (!m) return -1;
    DBusError err;
    dbus_error_init(&err);
    DBusMessage *r = dbus_connection_send_with_reply_and_block(conn, m, 400, &err);
    dbus_message_unref(m);
    if (!r) { dbus_error_free(&err); return -1; }

    int angle = -1;
    DBusMessageIter it, arr, var;
    if (dbus_message_iter_init(r, &it) &&
        dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_ARRAY) {
        dbus_message_iter_recurse(&it, &arr);
        if (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_VARIANT) {
            dbus_message_iter_recurse(&arr, &var);
            int t = dbus_message_iter_get_arg_type(&var);
            if (t == DBUS_TYPE_UINT32) {
                dbus_uint32_t v;
                dbus_message_iter_get_basic(&var, &v);
                angle = (int)v;
            } else if (t == DBUS_TYPE_INT32) {
                dbus_int32_t v;
                dbus_message_iter_get_basic(&var, &v);
                angle = (int)v;
            }
        }
    }
    dbus_message_unref(r);
    dbus_error_free(&err);
    if (angle != 0 && angle != 90 && angle != 180 && angle != 270) return -1;
    return angle;
}

void orient_close(void)
{
    if (conn) { dbus_connection_unref(conn); conn = NULL; }
}

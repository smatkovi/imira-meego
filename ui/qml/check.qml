// Prüfstück: baut jede Seite einmal und sagt, was dabei schiefgeht.
// Läuft mit IMIRA_QML=/opt/imira/qml/check.qml /opt/imira/bin/imira
import QtQuick 1.1
import com.nokia.meego 1.0

Rectangle {
    width: 854; height: 480; color: "black"
    Component.onCompleted: {
        var seiten = ["MainPage.qml", "SettingsPage.qml", "LogPage.qml",
                      "AboutPage.qml"]
        var schlecht = 0
        for (var i = 0; i < seiten.length; i++) {
            var c = Qt.createComponent(Qt.resolvedUrl(seiten[i]))
            if (c.status === Component.Error) {
                console.log("FEHLER " + seiten[i] + ": " + c.errorString())
                schlecht++
            } else {
                var o = c.createObject(null)
                if (!o) { console.log("FEHLER " + seiten[i] + ": kein Objekt"); schlecht++ }
                else { console.log("ok " + seiten[i]); o.destroy() }
            }
        }
        console.log(schlecht === 0 ? "ALLE SEITEN OK" : (schlecht + " SEITEN KAPUTT"))
        Qt.quit()
    }
}

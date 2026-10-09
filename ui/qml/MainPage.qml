import QtQuick 1.1
import com.nokia.meego 1.0

Page {
    id: page
    Component.onCompleted: caster.scan()

    tools: ToolBarLayout {
        ToolIcon {
            iconId: "toolbar-refresh"
            enabled: !caster.scanning
            onClicked: caster.scan()
        }
        ToolIcon {
            iconId: "toolbar-view-menu"
            onClicked: menu.open()
        }
    }

    Menu {
        id: menu
        MenuLayout {
            MenuItem {
                text: qsTr("Einstellungen")
                onClicked: pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
            }
            MenuItem {
                text: qsTr("Protokoll")
                onClicked: pageStack.push(Qt.resolvedUrl("LogPage.qml"))
            }
            MenuItem {
                text: qsTr("Über Imira")
                onClicked: pageStack.push(Qt.resolvedUrl("AboutPage.qml"))
            }
        }
    }

    Column {
        id: head
        anchors { top: parent.top; left: parent.left; right: parent.right; margins: 16 }
        spacing: 6
        Label {
            text: caster.running ? caster.detail : qsTr("Empfänger")
            font.pixelSize: 32
            color: "white"
        }
        Label {
            font.pixelSize: 18
            color: caster.state === "Fehler" ? "#ff6b6b" : "#808080"
            text: {
                if (caster.state === "überträgt") return qsTr("Bild läuft")
                if (caster.state === "verbunden") return qsTr("Empfänger meldet sich")
                if (caster.state === "wartet") return qsTr("warte auf den Empfänger")
                if (caster.state === "Fehler") return caster.detail
                if (caster.scanning) return qsTr("suche …")
                if (caster.receivers.length === 0)
                    return qsTr("keiner gefunden (%1 gewöhnliche WLANs übergangen)")
                           .arg(caster.others)
                return qsTr("%1 gefunden").arg(caster.receivers.length)
            }
        }
    }

    BusyIndicator {
        anchors.centerIn: parent
        running: caster.scanning && caster.receivers.length === 0
        visible: running
        platformStyle: BusyIndicatorStyle { size: "large" }
    }

    ListView {
        id: list
        anchors { top: head.bottom; left: parent.left; right: parent.right
                  bottom: footer.top; topMargin: 16; bottomMargin: 8 }
        clip: true
        model: caster.receivers
        delegate: Item {
            width: list.width
            height: 72
            Row {
                anchors { fill: parent; leftMargin: 16; rightMargin: 16 }
                spacing: 16
                Image {
                    anchors.verticalCenter: parent.verticalCenter
                    source: "image://theme/icon-m-common-video"
                }
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 120
                    Label {
                        text: modelData.name
                        font.pixelSize: 26
                        color: "white"
                        elide: Text.ElideRight
                        width: parent.width
                    }
                    Label {
                        font.pixelSize: 16
                        color: "#9fd29f"
                        text: qsTr("Miracast-Gruppe · %1 dBm").arg(modelData.signal)
                    }
                }
            }
            MouseArea {
                anchors.fill: parent
                onClicked: caster.castTo(modelData.name)
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width; height: 1; color: "#303030"
            }
        }
    }

    Column {
        id: footer
        anchors { bottom: parent.bottom; left: parent.left; right: parent.right
                  margins: 16 }
        spacing: 8
        Label {
            width: parent.width
            wrapMode: Text.WordWrap
            font.pixelSize: 16
            color: "#707070"
            visible: !caster.running
            text: qsTr("Hier stehen nur Miracast-Empfänger (Gruppen, deren "
                     + "Kennung mit DIRECT- beginnt). Ist deiner nicht dabei, "
                     + "muss er sich erst zeigen — manche tun das nur, solange "
                     + "ihr Startbild auf dem Fernseher steht.")
        }
        Button {
            width: parent.width
            visible: caster.running
            text: qsTr("Beenden")
            onClicked: caster.stop()
        }
    }
}

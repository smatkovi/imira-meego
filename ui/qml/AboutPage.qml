import QtQuick 1.1
import com.nokia.meego 1.0

Page {
    tools: ToolBarLayout {
        ToolIcon { iconId: "toolbar-back"; onClicked: pageStack.pop() }
    }

    Flickable {
        anchors.fill: parent
        anchors.margins: 16
        contentHeight: col.height
        Column {
            id: col
            width: parent.width
            spacing: 12
            Label { text: "Imira"; font.pixelSize: 40 }
            Label {
                width: parent.width; wrapMode: Text.WordWrap; font.pixelSize: 20
                color: "#b0b0b0"
                text: qsTr("Bildschirm des N9/N950 auf einen Miracast-Empfänger "
                         + "übertragen: Bild aus dem Framebuffer, H.264 auf dem "
                         + "DSP, Ton als LPCM, alles als MPEG-TS über RTP.")
            }
            Label {
                width: parent.width; wrapMode: Text.WordWrap; font.pixelSize: 18
                color: "#808080"
                text: qsTr("Nach dem Vorbild von Imira für Sailfish OS "
                         + "(github.com/JimKnopfIoT/harbour-imira, GPL-3). Der "
                         + "MPEG-TS-Packer und der RTP-Sender stammen von dort, "
                         + "beide ursprünglich aus aethercast. Der Rest ist für "
                         + "Harmattan neu gebaut: Lipstick-Recorder, droidmedia "
                         + "und systemd gibt es hier nicht.")
            }
            Label {
                width: parent.width; wrapMode: Text.WordWrap; font.pixelSize: 18
                color: "#808080"
                text: qsTr("Wi-Fi Direct kann dieses Gerät nicht von sich aus: "
                         + "der Treiber trägt keine P2P-Angaben in seine "
                         + "Suchanfragen. Der Empfänger muss deshalb ein Netz "
                         + "aufspannen, mit dem sich das Telefon verbinden kann.")
            }
            Label { text: "GPL-3.0-or-later"; font.pixelSize: 18; color: "#606060" }
        }
    }
}

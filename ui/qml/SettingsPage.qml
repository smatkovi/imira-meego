import QtQuick 1.1
import com.nokia.meego 1.0

Page {
    tools: ToolBarLayout {
        ToolIcon { iconId: "toolbar-back"; onClicked: pageStack.pop() }
    }

    Flickable {
        anchors.fill: parent
        contentHeight: col.height + 32
        Column {
            id: col
            width: parent.width - 32
            x: 16
            spacing: 16

            Label { text: qsTr("Einstellungen"); font.pixelSize: 36 }

            Label { text: qsTr("Bildgröße"); font.pixelSize: 22; color: "#b0b0b0" }
            ButtonColumn {
                width: parent.width
                Repeater {
                    model: caster.sizeNames
                    Button {
                        text: modelData
                        checkable: true
                        checked: index === caster.sizeIndex
                        onClicked: caster.sizeIndex = index
                    }
                }
            }

            Label {
                text: qsTr("Bilder je Sekunde: %1").arg(caster.fps)
                font.pixelSize: 22; color: "#b0b0b0"
            }
            Slider {
                width: parent.width
                minimumValue: 5; maximumValue: 30; stepSize: 5
                value: caster.fps
                onValueChanged: caster.fps = value
            }

            Row {
                spacing: 16
                Switch { checked: caster.audio; onCheckedChanged: caster.audio = checked }
                Label { text: qsTr("Ton mitschicken"); anchors.verticalCenter: parent.verticalCenter }
            }
            Row {
                spacing: 16
                Switch { checked: caster.autoRotate; onCheckedChanged: caster.autoRotate = checked }
                Label { text: qsTr("Lage automatisch"); anchors.verticalCenter: parent.verticalCenter }
            }

            Label {
                width: parent.width
                wrapMode: Text.WordWrap
                font.pixelSize: 18; color: "#808080"
                text: qsTr("Der Ton geht als unkomprimiertes LPCM mit und braucht "
                         + "dauerhaft 1,5 Mbit/s. Wird das Bild zäh, hilft es, "
                         + "ihn abzuschalten.")
            }
        }
    }
}

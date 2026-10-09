import QtQuick 1.1
import com.nokia.meego 1.0

Page {
    tools: ToolBarLayout {
        ToolIcon { iconId: "toolbar-back"; onClicked: pageStack.pop() }
        ToolIcon { iconId: "toolbar-delete"; onClicked: caster.clearLog() }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.margins: 12
        contentHeight: text.height
        clip: true
        Text {
            id: text
            width: parent.width
            wrapMode: Text.Wrap
            font.pixelSize: 16
            font.family: "Nokia Pure Text Mono"
            color: "#d0d0d0"
            text: caster.log
            onTextChanged: flick.contentY = Math.max(0, text.height - flick.height)
        }
    }
}

// Fensterrahmen: com.nokia.meego, wie jede Harmattan-App.
import QtQuick 1.1
import com.nokia.meego 1.0

PageStackWindow {
    id: app
    initialPage: MainPage { }
    showStatusBar: true

    platformStyle: PageStackWindowStyle {
        // Dunkel, weil das Bild beim Übertragen die Hauptsache ist.
        background: "image://theme/meegotouch-video-background"
    }

    ToolBarLayout {
        id: commonTools
        visible: false
    }
}

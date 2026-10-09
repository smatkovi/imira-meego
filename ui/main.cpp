// Imira für MeeGo Harmattan -- Oberfläche.
//
// Qt 4.7 hat weder QQuickView noch Silica: ein QDeclarativeView zeigt die
// Seiten aus qml/, die mit com.nokia.meego geschrieben sind. Die Technik
// dahinter (Handschlag, castd) steuert Caster.
#include <QApplication>
#include <QDeclarativeContext>
#include <QDeclarativeEngine>
#include <QDeclarativeView>
#include <QDir>
#include <QFileInfo>
#include <QTextCodec>
#include <QtDebug>

#include "caster.h"

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTextCodec::setCodecForTr(QTextCodec::codecForName("UTF-8"));
    QTextCodec::setCodecForCStrings(QTextCodec::codecForName("UTF-8"));
    app.setApplicationName("imira");
    app.setOrganizationName("imira");

    // Neben dem Programm liegt qml/ -- im Paket /opt/imira/qml, im Baum
    // daneben.
    QString base = QFileInfo(QCoreApplication::applicationFilePath()).path();
    // IMIRA_QML laedt eine andere Datei -- fuer die Pruefung aller Seiten,
    // ohne die App selbst anzufassen.
    QString qml = qgetenv("IMIRA_QML");
    if (!qml.isEmpty() && QFile::exists(qml)) {
        // nichts weiter zu tun
    } else qml = base + "/../qml/main.qml";
    if (!QFile::exists(qml)) qml = base + "/qml/main.qml";
    if (!QFile::exists(qml)) qml = "/opt/imira/qml/main.qml";

    Caster caster;
    QDeclarativeView view;
    view.engine()->rootContext()->setContextProperty("caster", &caster);
    view.setResizeMode(QDeclarativeView::SizeRootObjectToView);
    view.setSource(QUrl::fromLocalFile(qml));
    if (view.status() == QDeclarativeView::Error) {
        foreach (const QDeclarativeError &e, view.errors()) qWarning() << e;
        return 1;
    }
    // Ohne das bleibt Qt.quit() wirkungslos ("no receivers connected").
    QObject::connect(view.engine(), SIGNAL(quit()), &app, SLOT(quit()));
    view.showFullScreen();
    return app.exec();
}

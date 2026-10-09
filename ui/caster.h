// Caster -- die Bedienseite der Übertragung.
//
// Die Oberfläche fasst weder castd noch den Handschlag direkt an: sie startet
// /opt/imira/wfd-proto.py, liest dessen Protokoll mit und leitet daraus den
// Zustand ab. Das Skript startet castd selbst, sobald die Senke PLAY sagt.
#ifndef IMIRA_CASTER_H_
#define IMIRA_CASTER_H_

#include <QDBusMessage>
#include <QObject>
#include <QVariantList>
#include <QProcess>
#include <QSettings>
#include <QString>
#include <QStringList>

class Caster : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString detail READ detail NOTIFY stateChanged)
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(QString log READ log NOTIFY logChanged)
    Q_PROPERTY(int sizeIndex READ sizeIndex WRITE setSizeIndex NOTIFY settingsChanged)
    Q_PROPERTY(int fps READ fps WRITE setFps NOTIFY settingsChanged)
    Q_PROPERTY(bool audio READ audio WRITE setAudio NOTIFY settingsChanged)
    Q_PROPERTY(bool autoRotate READ autoRotate WRITE setAutoRotate NOTIFY settingsChanged)
    Q_PROPERTY(QStringList sizeNames READ sizeNames CONSTANT)
    Q_PROPERTY(QVariantList receivers READ receivers NOTIFY receiversChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY receiversChanged)
    Q_PROPERTY(int others READ others NOTIFY receiversChanged)

public:
    explicit Caster(QObject *parent = 0);
    ~Caster();

    QString state() const { return state_; }
    QString detail() const { return detail_; }
    bool running() const { return proc_ && proc_->state() != QProcess::NotRunning; }
    QString log() const { return log_.join("\n"); }

    int sizeIndex() const { return sizeIndex_; }
    void setSizeIndex(int i);
    int fps() const { return fps_; }
    void setFps(int f);
    bool audio() const { return audio_; }
    void setAudio(bool a);
    bool autoRotate() const { return autoRotate_; }
    void setAutoRotate(bool a);
    QStringList sizeNames() const;

    QVariantList receivers() const { return receivers_; }
    bool scanning() const { return scanning_; }
    int others() const { return others_; }

public slots:
    void scan();
    void castTo(const QString &name);
    void start();
    void stop();
    void clearLog();

signals:
    void receiversChanged();
    void stateChanged();
    void logChanged();
    void settingsChanged();

private slots:
    void scanResult(const QDBusMessage &msg);
    void scanDone();
    void readOutput();
    void finished(int code, QProcess::ExitStatus status);

private:
    void note(const QString &line);
    void setState(const QString &s, const QString &d);

    QProcess *proc_;
    QVariantList receivers_;
    bool scanning_;
    int others_;              /* gewoehnliche WLANs, nur gezaehlt */
    QSettings settings_;
    QStringList log_;
    QString state_;
    QString detail_;
    QString target_;
    int sizeIndex_;
    int fps_;
    bool audio_;
    bool autoRotate_;
};

#endif

#include "caster.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDateTime>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QRegExp>
#include <QStringList>
#include <QtDebug>

namespace {

// Die Größen, die ein Empfänger laut WFD können muss bzw. üblich sind.
// 640x480p60 ist CEA-Bit 0 und Pflicht für jede Senke; 848x480 ist die
// Auflösung des Bildschirms selbst (VESA), spart also das Skalieren.
struct Size { const char *name; int w, h; const char *cea, *vesa; };
const Size kSizes[] = {
    { "640 x 480 (jede Senke)", 640, 480, "00000001", "00000000" },
    { "848 x 480 (Bildschirm)", 848, 480, "00000000", "00000800" },
    { "720 x 480",              720, 480, "00000002", "00000000" },
};
const int kSizeCount = sizeof(kSizes) / sizeof(kSizes[0]);

const char *kIcd = "com.nokia.icd2";
const char *kIcdPath = "/com/nokia/icd2";
const char *kIcdIface = "com.nokia.icd2";

const char *kProto = "/opt/imira/wfd-proto.py";
const char *kPython = "/usr/bin/python3.11";

} // namespace

Caster::Caster(QObject *parent)
    : QObject(parent), proc_(0), settings_("imira", "imira"),
      state_("aus"), detail_("bereit"),
      sizeIndex_(settings_.value("sizeIndex", 0).toInt()),
      fps_(settings_.value("fps", 20).toInt()),
      audio_(settings_.value("audio", true).toBool()),
      autoRotate_(settings_.value("autoRotate", true).toBool()),
      scanning_(false), others_(0)
{
    if (sizeIndex_ < 0 || sizeIndex_ >= kSizeCount) sizeIndex_ = 0;
    // icd2 schickt die Suchergebnisse nur an den, der gefragt hat -- und nur,
    // solange der verbunden bleibt. Deshalb sucht die App selbst, statt ein
    // Hilfsprogramm zu rufen.
    QDBusConnection::systemBus().connect(QString(), kIcdPath, kIcdIface,
                                         "scan_result_sig", this,
                                         SLOT(scanResult(QDBusMessage)));
}

Caster::~Caster() { stop(); }

QStringList Caster::sizeNames() const
{
    QStringList l;
    for (int i = 0; i < kSizeCount; ++i) l << QString::fromUtf8(kSizes[i].name);
    return l;
}

void Caster::setSizeIndex(int i)
{
    if (i < 0 || i >= kSizeCount || i == sizeIndex_) return;
    sizeIndex_ = i;
    settings_.setValue("sizeIndex", i);
    emit settingsChanged();
}

void Caster::setFps(int f)
{
    if (f < 5 || f > 30 || f == fps_) return;
    fps_ = f;
    settings_.setValue("fps", f);
    emit settingsChanged();
}

void Caster::setAudio(bool a)
{
    if (a == audio_) return;
    audio_ = a;
    settings_.setValue("audio", a);
    emit settingsChanged();
}

void Caster::setAutoRotate(bool a)
{
    if (a == autoRotate_) return;
    autoRotate_ = a;
    settings_.setValue("autoRotate", a);
    emit settingsChanged();
}

void Caster::note(const QString &line)
{
    qDebug() << "imira:" << line;   // steht auch in ui.log, fuer die Ferne
    log_ << QDateTime::currentDateTime().toString("HH:mm:ss") + "  " + line;
    while (log_.size() > 400) log_.removeFirst();
    emit logChanged();
}

void Caster::setState(const QString &s, const QString &d)
{
    if (state_ == s && detail_ == d) return;
    state_ = s;
    detail_ = d;
    emit stateChanged();
}

void Caster::scan()
{
    if (scanning_) return;
    receivers_.clear();
    others_ = 0;
    scanning_ = true;
    emit receiversChanged();
    note(QString::fromUtf8("suche Empfänger …"));
    QDBusInterface icd(kIcd, kIcdPath, kIcdIface, QDBusConnection::systemBus());
    QDBusReply<QStringList> r = icd.call("scan_req", (uint)0);
    if (!r.isValid()) {
        note(QString::fromUtf8("Suche geht nicht: %1").arg(r.error().message()));
        scanning_ = false;
        emit receiversChanged();
        return;
    }
    // icd2 sagt nicht, wann es fertig ist; nach zehn Sekunden ist Schluss.
    QTimer::singleShot(10000, this, SLOT(scanDone()));
}

void Caster::scanDone()
{
    if (!scanning_) return;
    scanning_ = false;
    note(QString::fromUtf8("Suche fertig: %1 Empfänger, %2 gewöhnliche WLANs "
                           "übergangen").arg(receivers_.size()).arg(others_));
    emit receiversChanged();
}

void Caster::scanResult(const QDBusMessage &msg)
{
    const QList<QVariant> a = msg.arguments();
    if (qgetenv("IMIRA_SCAN_DEBUG") == "1") {
        QStringList d;
        for (int i = 0; i < a.size(); ++i)
            d << QString("%1:%2=%3").arg(i)
                 .arg(QString::fromLatin1(a.at(i).typeName()))
                 .arg(a.at(i).toString().left(24));
        note("roh " + d.join(" "));
    }
    // Reihenfolge laut icd2: status, Zeit, Diensttyp, Dienstmerkmale,
    // Dienstkennung, Netztyp, Netzname, Netzkennung, Netzmerkmale,
    // Signalstärke, Stationskennung, dB.
    // Reihenfolge am Geraet nachgemessen (15 Werte): 7 = Netztyp,
    // 8 = Name, 10 = Kennung als Bytes, 12 = Guete, 13 = Stationskennung,
    // 14 = Feldstaerke in dBm. Die Beschreibung der icd2-Schnittstelle
    // zaehlt anders -- hier gilt, was ankommt.
    QString name, station;
    int signal = 0;
    if (a.size() >= 15) {
        name = a.at(8).toString();
        station = a.at(13).toString();
        signal = a.at(14).toInt();
    }
    if (name.isEmpty()) return;
    // Dasselbe Netz kommt von mehreren Stationen; die staerkste gilt.
    for (int i = 0; i < receivers_.size(); ++i) {
        QVariantMap m = receivers_.at(i).toMap();
        if (m.value("name").toString() != name) continue;
        if (signal > m.value("signal").toInt()) {
            m["signal"] = signal;
            m["station"] = station;
            receivers_[i] = m;
            emit receiversChanged();
        }
        return;
    }

    // Nur Miracast-Gruppen. Die Wi-Fi-Direct-Festlegung schreibt vor, dass
    // die Kennung einer Gruppe mit "DIRECT-" beginnt -- das ist das einzige
    // Merkmal, das ein Suchlauf über icd2 hergibt (P2P-Angaben trägt dieser
    // Treiber nicht). Gewöhnliche WLANs werden nur gezählt, nicht gezeigt.
    if (!name.startsWith("DIRECT-")) { others_++; return; }
    QVariantMap m;
    m["name"] = name;
    m["station"] = station;
    m["signal"] = signal;
    m["direct"] = true;
    receivers_.append(m);
    emit receiversChanged();
}

void Caster::castTo(const QString &name)
{
    // Der Empfänger ist gewählt. Bin ich schon in seinem Netz, geht es
    // sofort los; sonst muss icd2 erst dorthin verbinden.
    target_ = name;
    note(QString::fromUtf8("Empfänger gewählt: %1").arg(name));
    QDBusInterface icd(kIcd, kIcdPath, kIcdIface, QDBusConnection::systemBus());
    QDBusMessage r = icd.call("addrinfo_req");
    bool here = false;
    foreach (const QVariant &v, r.arguments())
        if (v.toString() == name) here = true;
    if (!here) {
        // Beitreten kann ich erst, wenn ein echter Dongle sagt, wie er es
        // haben will (offenes Netz, Passwort oder WPS auf Zuruf). Bis dahin
        // ehrlich bleiben statt zu raten.
        note(QString::fromUtf8("noch nicht in diesem Netz — der Beitritt "
                               "braucht einen Dongle zum Entwickeln"));
        setState("Fehler", QString::fromUtf8("Beitritt zu %1 fehlt noch").arg(name));
    }
    start();
}

void Caster::start()
{
    if (running()) return;
    if (!QFile::exists(kProto)) {
        setState("Fehler", QString::fromUtf8("%1 fehlt").arg(kProto));
        return;
    }
    const Size &sz = kSizes[sizeIndex_];

    proc_ = new QProcess(this);
    proc_->setProcessChannelMode(QProcess::MergedChannels);
    connect(proc_, SIGNAL(readyRead()), this, SLOT(readOutput()));
    connect(proc_, SIGNAL(finished(int, QProcess::ExitStatus)),
            this, SLOT(finished(int, QProcess::ExitStatus)));

    QStringList env = QProcess::systemEnvironment();
    env << QString("IMIRA_SIZE=%1x%2").arg(sz.w).arg(sz.h)
        << QString("IMIRA_FPS=%1").arg(fps_)
        << QString("IMIRA_ROT=%1").arg(autoRotate_ ? "auto" : "0")
        << QString("IMIRA_CEA=%1").arg(sz.cea)
        << QString("IMIRA_VESA=%1").arg(sz.vesa)
        << QString("IMIRA_AUDIO=%1").arg(audio_ ? "LPCM 00000002 00"
                                                : "LPCM 00000002 00")
        << QString("IMIRA_RTSP_WAIT=120");
    proc_->setEnvironment(env);

    note(QString::fromUtf8("starte: %1 %2 (%3x%4, %5/s, Ton %6, Lage %7)")
         .arg(kPython).arg(kProto).arg(sz.w).arg(sz.h).arg(fps_)
         .arg(audio_ ? "an" : "aus").arg(autoRotate_ ? "automatisch" : "fest"));
    proc_->start(kPython, QStringList() << kProto);
    setState("wartet", QString::fromUtf8("warte auf den Empfänger"));
    emit stateChanged();
}

void Caster::stop()
{
    if (!proc_) return;
    if (proc_->state() != QProcess::NotRunning) {
        // SIGTERM: das Skript meldet der Senke ein sauberes Sitzungsende und
        // raeumt castd mit weg.
        proc_->terminate();
        if (!proc_->waitForFinished(3000)) proc_->kill();
    }
    proc_->deleteLater();
    proc_ = 0;
    setState("aus", "bereit");
}

void Caster::clearLog()
{
    log_.clear();
    emit logChanged();
}

void Caster::readOutput()
{
    while (proc_ && proc_->canReadLine()) {
        QString line = QString::fromUtf8(proc_->readLine()).trimmed();
        if (line.isEmpty()) continue;
        note(line);
        if (line.contains("sink connected from"))
            setState("verbunden", QString::fromUtf8("Empfänger meldet sich"));
        else if (line.contains("starting RTP stream"))
            setState(QString::fromUtf8("überträgt"),
                     QString::fromUtf8("Bild läuft"));
        else if (line.contains("waiting for the sink"))
            setState("wartet", QString::fromUtf8("warte auf den Empfänger"));
        else if (line.startsWith("!!"))
            setState("Fehler", line.mid(2).trimmed());
    }
}

void Caster::finished(int code, QProcess::ExitStatus)
{
    note(QString::fromUtf8("Übertragung beendet (Rückgabe %1)").arg(code));
    if (proc_) { proc_->deleteLater(); proc_ = 0; }
    setState("aus", "bereit");
}

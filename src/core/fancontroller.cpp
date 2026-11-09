#include "fancontroller.h"

#include "smc.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>
#include <QSettings>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <sstream>
#include <QSet>
#include <algorithm>
#include <vector>

#include <cmath>
#include <functional>

namespace {

QString socketPath() {
    const QByteArray env = qgetenv("FANCTL_SOCKET");   // keep it alive!
    if (!env.isEmpty()) return QString::fromLocal8Bit(env);
    return QString::fromStdString(defaultSocketPath());
}

QString shellQuote(const QString &s) {
    QString out = "'";
    for (QChar c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    return out + "'";
}

QString appleScriptQuote(QString s) {
    s.replace('\\', "\\\\");
    s.replace('"', "\\\"");
    return s;
}

// 把 SMC 的 4 字符 key 归类并翻译成人能看懂的名字
struct SensorMeta {
    QString category;    // 中文类别名
    QString categoryEn;  // 英文类别名
    QString name;
    int rank = 99;
};

SensorMeta metaFor(const QString &key) {
    SensorMeta m;
    auto set = [&](const char *zh, const char *en, int rank) {
        m.category = QString::fromUtf8(zh);
        m.categoryEn = QString::fromUtf8(en);
        m.name = m.category;
        m.rank = rank;
    };
    // 注意大小写：Tp01 是性能核，TPUx 是电源管理；TG0D 这类大写也要认
    const QChar c1 = key.size() > 1 ? key.at(1) : QChar();
    auto is = [&](QChar a, QChar b) { return c1 == a || c1 == b; };
    if (key.startsWith("TCM")) set("CPU 封装", "CPU Package", 0);
    else if (c1 == 'p') set("CPU 性能核", "CPU Performance Core", 1);          // 小写 p：性能核
    else if (is('e', 'E')) set("CPU 能效核", "CPU Efficiency Core", 2);
    else if (is('g', 'G')) set("GPU 集群", "GPU Cluster", 3);
    else if (is('m', 'M')) set("内存", "Memory", 4);
    else if (is('b', 'B')) set("电池", "Battery", 5);
    else if (is('v', 'V')) set("显示面板", "Display", 6);
    else if (c1 == 'P' || key.startsWith("TP")) set("电源管理", "Power Management", 7);   // 大写 P：电源
    else if (is('a', 'A')) set("环境", "Ambient", 8);
    else if (c1 == 'h') set("散热片", "Heatsink", 9);
    else if (c1 == 's') set("触控板", "Trackpad", 10);
    else if (key.startsWith("TC")) set("CPU 传感器", "CPU Sensor", 11);
    else set("传感器", "Sensor", 20);
    return m;
}

QString bundleResources() {
    // .../FanCtl.app/Contents/MacOS/FanCtl -> .../FanCtl.app/Contents/Resources
    QDir macos(QCoreApplication::applicationDirPath());
    macos.cdUp();
    return macos.filePath("Resources");
}

void runAsAdmin(const QString &shellCommand, QObject *ctx,
                std::function<void(int, const QString &)> done) {
    const QString script =
        QString("do shell script \"%1\" with administrator privileges")
            .arg(appleScriptQuote(shellCommand));
    auto *proc = new QProcess(ctx);
    QObject::connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), ctx,
                     [proc, done](int code, QProcess::ExitStatus status) {
                         QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
                         if (err.isEmpty())
                             err = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
                         done(status == QProcess::NormalExit ? code : -1, err);
                         proc->deleteLater();
                     });
    proc->start("/usr/bin/osascript", {"-e", script});
}

}  // namespace

FanController::FanController(QObject *parent) : QObject(parent) {
    timer_ = new QTimer(this);
    timer_->setInterval(1200);
    connect(timer_, &QTimer::timeout, this, &FanController::refresh);

    verifyTimer_ = new QTimer(this);
    verifyTimer_->setInterval(500);
    connect(verifyTimer_, &QTimer::timeout, this, &FanController::verifyTick);

    {
        QSettings settings;
        english_ = settings.value("english", false).toBool();
    }
    loadPresets();
    refresh();
    timer_->start();
}

void FanController::setBusy(bool v) {
    if (busy_ == v) return;
    busy_ = v;
    emit changed();
}

void FanController::refresh() {
    if (verifying_) return;

    installed_ = QFile::exists("/Library/LaunchDaemons/com.fanctl.daemon.plist") &&
                 QFile::exists("/usr/local/libexec/fanctl/fanctld");

    std::string reply, error;
    if (!ipcRequest(socketPath().toStdString(), "STATUS", reply, error)) {
        const bool wasConnected = connected_;
        connected_ = false;
        fans_.clear();
        sensors_.clear();
        statusText_ = installed_ ? L("后台组件没有运行", "Helper isn't running")
                                       : L("尚未启用风扇控制", "Fan control not enabled");
        if (wasConnected) emit changed();
        else emit changed();
        return;
    }

    Status st = parseStatus(reply);
    if (!st.ok) {
        connected_ = false;
        statusText_ = QString::fromStdString(st.error);
        emit changed();
        return;
    }
    connected_ = true;
    outdated_ = !st.hasProto;
    applyStatus(st);
    emit changed();
}

void FanController::applyStatus(const Status &st) {
    mode_ = QString::fromStdString(st.mode);

    fans_.clear();
    for (const auto &f : st.fans) {
        QVariantMap m;
        m["index"] = f.index;
        m["actual"] = int(std::lround(f.actual));
        m["min"] = int(std::lround(f.min));
        m["max"] = int(std::lround(f.max));
        m["target"] = int(std::lround(f.target));
        m["forced"] = f.mode != 0;
        fans_ << m;
    }

    // 按「类别优先级 + key」固定排序，温度变化不会改变顺序
    struct Row {
        QString key;
        QString label;
        QString category;
        QString categoryEn;
        int rank = 99;
        double value = 0;
    };
    std::vector<Row> rows;
    rows.reserve(st.sensors.size());
    for (const auto &s : st.sensors) {
        const QString key = QString::fromStdString(s.first);
        const SensorMeta m = metaFor(key);
        rows.push_back({key, m.name, m.category, m.categoryEn, m.rank, s.second});
    }
    std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) {
        if (a.rank != b.rank) return a.rank < b.rank;
        return a.key < b.key;
    });

    // 同一类别内部编号；只有一个成员时不显示编号
    QHash<QString, int> totals;
    for (const auto &r : rows) totals[r.category]++;
    QHash<QString, int> seen;
    for (auto &r : rows) {
        const int n = ++seen[r.category];
        const QString base = english_ ? r.categoryEn : r.category;
        r.label = totals[r.category] > 1 ? QString("%1 %2").arg(base).arg(n) : base;
    }

    sensors_.clear();
    highlights_.clear();
    QSet<QString> usedCategory;
    for (const auto &r : rows) {
        QVariantMap m;
        m["key"] = r.key;
        m["label"] = r.label;
        m["display"] = QString("%1 · %2").arg(r.label, r.key);
        m["value"] = r.value;
        m["category"] = r.category;
        sensors_ << m;

        // 每个类别取第一个作为「卡片」，顺序固定，不随温度跳动
        if (highlights_.size() < 12 && !usedCategory.contains(r.category)) {
            usedCategory.insert(r.category);
            highlights_ << m;
        }
    }

    if (st.hasCurve) {
        curveSensor_ = QString::fromStdString(st.curveSensor);
        QVariantList pts;
        for (const auto &p : st.curve) {
            QVariantList pair;
            pair << p.first << p.second;
            pts.append(QVariant(pair));   // 注意：不能写 pts << pair，会被拍平
        }
        if (!pts.isEmpty() && pts != curvePoints_) {
            curvePoints_ = pts;
            emit curvePointsChanged();
        }
    }

    statusText_ = fans_.isEmpty()
                      ? L("这台机器没有可控制的风扇", "No controllable fans on this Mac")
                      : (english_ ? QString("%1 fan(s)").arg(fans_.size())
                                  : QString("%1 台风扇").arg(fans_.size()));
}

// ---------------------------------------------------------------- 控制

void FanController::setAutomatic() {
    std::string reply, error;
    ipcRequest(socketPath().toStdString(), "AUTO", reply, error);
    refresh();
}

void FanController::setManual(int rpm) {
    std::string reply, error;
    ipcRequest(socketPath().toStdString(), "MANUAL " + std::to_string(rpm), reply, error);
    refresh();
}

void FanController::setFan(int index, int rpm) {
    std::string reply, error;
    ipcRequest(socketPath().toStdString(),
               "MANUALONE " + std::to_string(index) + " " + std::to_string(rpm), reply, error);
    refresh();
}

QVariantList FanController::defaultCurve() const {
    int lo = 1200, hi = 5000;
    if (!fans_.isEmpty()) {
        const QVariantMap f = fans_.first().toMap();
        if (f.value("min").toInt() > 0) lo = f.value("min").toInt();
        if (f.value("max").toInt() > lo) hi = f.value("max").toInt();
    }
    const int span = hi - lo;
    QVariantList pts;
    auto add = [&pts](int t, int r) {
        QVariantList pair;
        pair << t << r;
        pts.append(QVariant(pair));
    };
    add(40, lo);
    add(55, lo + span / 4);
    add(70, lo + span * 6 / 10);
    add(85, lo + span * 9 / 10);
    return pts;
}

void FanController::enterCurveMode() {
    if (outdated_) {
        emit toast(L("后台组件版本过旧，请运行 install.command 更新后再用曲线",
                     "Helper is outdated — run install.command first"));
        return;
    }
    QString sensor = curveSensor_;
    if (sensor.isEmpty() && !highlights_.isEmpty())
        sensor = highlights_.first().toMap().value("key").toString();
    setCurve(sensor, curvePoints_.size() >= 2 ? curvePoints_ : defaultCurve());
}

void FanController::setCurve(const QString &sensor, const QVariantList &points) {
    if (outdated_) {
        emit toast(L("后台组件版本过旧，请运行 install.command 更新后再用曲线",
                     "Helper is outdated — run install.command first"));
        return;
    }
    QVariantList use = points;
    if (use.size() < 2) use = defaultCurve();
    if (use.size() < 2) return;

    curveSensor_ = sensor;
    curvePoints_ = use;
    emit curvePointsChanged();

    std::ostringstream cmd;
    cmd << "CURVE " << sensor.toStdString() << ' ' << use.size();
    for (const auto &p : use) {
        const QVariantList pair = p.toList();
        if (pair.size() < 2) continue;
        cmd << ' ' << pair[0].toDouble() << ' ' << pair[1].toDouble();
    }

    std::string reply, error;
    ipcRequest(socketPath().toStdString(), cmd.str(), reply, error);
    refresh();
}

// ---------------------------------------------------------------- 预设

void FanController::loadPresets() {
    QSettings settings;
    const QByteArray raw = settings.value("curvePresets").toByteArray();
    if (!raw.isEmpty()) {
        const QJsonDocument doc = QJsonDocument::fromJson(raw);
        if (doc.isArray()) presets_ = doc.array().toVariantList();
    }
    if (!presets_.isEmpty()) return;

    auto preset = [](const QString &name, const QVariantList &pts) {
        QVariantMap m;
        m["name"] = name;
        m["sensor"] = QString();
        m["points"] = pts;
        return m;
    };
    presets_ << preset("安静", QVariantList{QVariantList{40, 1200}, QVariantList{55, 1500},
                                            QVariantList{75, 2600}, QVariantList{88, 3600}});
    presets_ << preset("均衡", QVariantList{QVariantList{40, 1300}, QVariantList{55, 2000},
                                            QVariantList{70, 3200}, QVariantList{85, 4600}});
    presets_ << preset("性能", QVariantList{QVariantList{35, 1800}, QVariantList{50, 2800},
                                            QVariantList{65, 4200}, QVariantList{80, 5800}});
    storePresets();
}

void FanController::storePresets() {
    QSettings settings;
    settings.setValue("curvePresets",
                      QJsonDocument(QJsonArray::fromVariantList(presets_)).toJson(QJsonDocument::Compact));
}

void FanController::savePreset(const QString &name, const QString &sensor,
                               const QVariantList &points) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || points.size() < 2) return;

    QVariantMap entry;
    entry["name"] = trimmed;
    entry["sensor"] = sensor;
    entry["points"] = points;

    bool replaced = false;
    for (int i = 0; i < presets_.size(); ++i) {
        if (presets_[i].toMap().value("name").toString() == trimmed) {
            presets_[i] = entry;
            replaced = true;
            break;
        }
    }
    if (!replaced) presets_ << entry;
    storePresets();
    emit changed();
    emit toast(english_ ? QString("Saved preset \"%1\"").arg(trimmed)
                            : QString("已保存预设「%1」").arg(trimmed));
}

void FanController::deletePreset(const QString &name) {
    for (int i = 0; i < presets_.size(); ++i) {
        if (presets_[i].toMap().value("name").toString() == name) {
            presets_.removeAt(i);
            storePresets();
            emit changed();
            emit toast(english_ ? QString("Deleted preset \"%1\"").arg(name)
                            : QString("已删除预设「%1」").arg(name));
            return;
        }
    }
}

void FanController::applyPreset(const QString &name) {
    for (const auto &item : presets_) {
        const QVariantMap m = item.toMap();
        if (m.value("name").toString() != name) continue;
        QString sensor = m.value("sensor").toString();
        if (sensor.isEmpty()) sensor = curveSensor_;
        const QVariantList pts = m.value("points").toList();
        if (pts.size() >= 2) setCurve(sensor, pts);
        return;
    }
}

void FanController::setEnglish(bool en) {
    if (english_ == en) return;
    english_ = en;
    QSettings().setValue("english", en);
    emit languageChanged();
    refresh();
}

void FanController::setPaused(bool paused) {
    if (paused) timer_->stop();
    else if (!timer_->isActive()) timer_->start();
}

void FanController::quitCleanly() {
    std::string reply, error;
    ipcRequest(socketPath().toStdString(), "AUTO", reply, error);
}

// ---------------------------------------------------------------- 启用

void FanController::enableControl() {
    if (installed_ && connected_) {
        setAutomatic();
        return;
    }
    if (installed_) startDaemon();
    else installHelper();
}

void FanController::installHelper() {
    const QString res = bundleResources();
    const QString src = res + "/fanctld";
    if (!QFile::exists(src)) {
        emit toast(L("安装包缺少 fanctld，请重新构建", "Bundle is missing fanctld"));
        return;
    }
    setBusy(true);
    emit toast(L("正在请求管理员授权…", "Waiting for administrator approval…"));

    const QString cmd =
        "mkdir -p /usr/local/libexec/fanctl; "
        "/usr/bin/install -m 755 " + shellQuote(src) +
        " /usr/local/libexec/fanctl/fanctld; "
        "/usr/bin/install -m 644 " + shellQuote(res + "/com.fanctl.daemon.plist") +
        " /Library/LaunchDaemons/com.fanctl.daemon.plist; "
        "/bin/launchctl bootout system/com.fanctl.daemon 2>/dev/null; "
        "/bin/sleep 1; "
        "/bin/launchctl bootstrap system /Library/LaunchDaemons/com.fanctl.daemon.plist";

    runAsAdmin(cmd, this, [this](int code, const QString &err) {
        setBusy(false);
        if (code != 0) {
            emit toast((english_ ? "Failed: " : "启用失败：") + err);
            refresh();
            return;
        }
        QTimer::singleShot(600, this, [this] {
            refresh();
            emit toast(connected_ ? L("风扇控制已启用", "Fan control enabled")
                                      : L("组件已安装，但还没连上", "Helper installed but not responding"));
        });
    });
}

void FanController::startDaemon() {
    setBusy(true);
    runAsAdmin("/bin/launchctl bootstrap system "
               "/Library/LaunchDaemons/com.fanctl.daemon.plist",
               this, [this](int code, const QString &err) {
                   setBusy(false);
                   if (code != 0 && !err.isEmpty()) emit toast(err);
                   QTimer::singleShot(800, this, &FanController::refresh);
               });
}

// ---------------------------------------------------------------- 诊断

void FanController::diagnose() {
    setBusy(true);
    SMC smc(false);
    QStringList lines;
    bool feasible = false;
    QString verdict = L("不可用", "Unavailable");

    if (!smc.isOpen()) {
        lines << L("无法打开 AppleSMC：", "Cannot open AppleSMC: ") +
                     QString::fromStdString(smc.lastError());
    } else {
        auto keys = smc.allKeys();
        lines << L("SMC 服务        ：已连接（AppleSMC）",
                   "SMC service     : connected (AppleSMC)");
        lines << L("枚举到的 Key 数 ：", "Keys enumerated : ") + QString::number(keys.size());

        auto fanCount = smc.readValue("FNum");
        lines << (fanCount
                      ? L("风扇数量 FNum   ：", "Fan count FNum  : ") + QString::number(int(*fanCount))
                      : L("风扇数量 FNum   ：缺失", "Fan count FNum  : missing"));

        static const char *kFanKeys[] = {"FNum", "F0Ac", "F0Mn", "F0Mx", "F0Tg",
                                         "F0Md", "F1Ac", "F1Mn", "F1Mx", "F1Tg",
                                         "F1Md", "F1Sf"};
        QStringList present;
        for (const char *k : kFanKeys) {
            if (auto in = smc.info(k))
                present << QString("%1[%2]").arg(k, QString::fromStdString(in->type));
        }
        lines << L("风扇相关 Key    ：", "Fan keys        : ") +
                     (present.isEmpty() ? L("（无）", "none") : present.join("  "));

        if (fanCount && int(*fanCount) > 0) {
            for (const auto &f : smc.fans()) {
                lines << (english_
                              ? QString("Fan %1          : %2 rpm (min %3, max %4)")
                                    .arg(f.index)
                                    .arg(int(std::lround(f.actual)))
                                    .arg(int(std::lround(f.min)))
                                    .arg(int(std::lround(f.max)))
                              : QString("风扇 %1          ：%2 rpm（最低 %3，最高 %4）")
                                    .arg(f.index)
                                    .arg(int(std::lround(f.actual)))
                                    .arg(int(std::lround(f.min)))
                                    .arg(int(std::lround(f.max))));
            }
        }

        lines << L("数值字节序      ：", "Value byte order: ") +
                     (smc.littleEndian() ? L("主机序（小端）", "host (little-endian)")
                                         : L("网络序（大端）", "network (big-endian)"));

        auto sensors = smc.sensors();
        int live = 0;
        for (const auto &t : sensors)
            if (std::abs(t.value) > 0.01) ++live;
        lines << (english_ ? QString("Temperature     : %1 sensors (%2 reporting)")
                                 .arg(sensors.size())
                                 .arg(live)
                           : QString("温度传感器      ：%1 个（%2 个有读数）")
                                 .arg(sensors.size())
                                 .arg(live));
        for (int i = 0; i < sensors.size() && i < 4; ++i)
            lines << QString("                  %1  %2°C")
                         .arg(QString::fromStdString(sensors[i].key))
                         .arg(sensors[i].value, 0, 'f', 1);

        lines << "";
        if (!fanCount || int(*fanCount) == 0) {
            verdict = L("不可行", "Not possible");
            lines << L("结论：SMC 没有暴露风扇控制 Key，无法控制风扇。",
                       "Result: the SMC exposes no fan control keys.");
        } else if (!smc.info("F0Tg")) {
            verdict = L("部分可行", "Partial");
            lines << L("结论：检测到风扇，但缺少 F0Tg，无法设定转速。",
                       "Result: fans found, but F0Tg is missing.");
        } else {
            verdict = L("可行", "Possible");
            feasible = true;
            lines << L("结论：检测到风扇与全部控制 Key，可以点「验证」实测。",
                       "Result: fans and control keys present — run the verification.");
        }
    }

    verdict_ = verdict;
    diagnosis_ = lines.join('\n');
    setBusy(false);
    emit changed();

    if (feasible) emit toast(L("诊断完成：", "Diagnosis: ") + verdict +
                   L("，可以点「验证」", " — you can run the verification now"));
}

// ---------------------------------------------------------------- 验证

void FanController::verify() {
    std::string reply, error;
    if (!ipcRequest(socketPath().toStdString(), "STATUS", reply, error)) {
        emit toast(L("后台组件没有运行", "Helper isn't running"));
        return;
    }
    Status st = parseStatus(reply);
    if (!st.ok || st.fans.empty()) {
        emit toast(L("没有可验证的风扇", "No fans to verify"));
        return;
    }

    const auto &f = st.fans.front();
    double target = f.actual + 300;
    if (target > f.max - 50) target = f.actual - 300;
    if (target < f.min + 50) target = (f.min + f.max) / 2;

    verifyBefore_ = f.actual;
    verifyBest_ = 0;
    verifyTicks_ = 0;
    verifying_ = true;
    verifyResult_ = "正在写入目标转速…";
    emit changed();

    std::string cmd = "MANUAL";
    for (const auto &fan : st.fans) {
        double t = fan.actual + 300;
        if (t > fan.max - 50) t = fan.actual - 300;
        if (t < fan.min + 50) t = (fan.min + fan.max) / 2;
        cmd += " " + std::to_string(int(t));
    }
    ipcRequest(socketPath().toStdString(), cmd, reply, error);
    verifyTimer_->start();
}

void FanController::verifyTick() {
    ++verifyTicks_;
    std::string reply, error;
    if (!ipcRequest(socketPath().toStdString(), "STATUS", reply, error)) {
        verifyTimer_->stop();
        verifying_ = false;
        verifyResult_ = "验证中断：与后台组件失去连接";
        emit changed();
        return;
    }
    Status st = parseStatus(reply);
    if (!st.ok || st.fans.empty()) {
        verifyTimer_->stop();
        verifying_ = false;
        verifyResult_ = "验证中断：读不到状态";
        emit changed();
        return;
    }
    double now = st.fans.front().actual;
    double delta = now - verifyBefore_;
    if (std::abs(delta) > std::abs(verifyBest_)) verifyBest_ = delta;

    verifyResult_ = QString("期望 %1 rpm，当前 %2 rpm（变化 %3）")
                        .arg(int(std::lround(st.fans.front().target)))
                        .arg(int(std::lround(now)))
                        .arg(delta >= 0 ? "+" + QString::number(int(delta))
                                        : QString::number(int(delta)));
    emit changed();

    if (std::abs(verifyBest_) >= 60 || verifyTicks_ >= 20) {
        verifyTimer_->stop();
        verifying_ = false;
        const bool pass = std::abs(verifyBest_) >= 60;
        verifyResult_ = pass
                            ? QString("验证通过：风扇响应正常（最大变化 %1 rpm）")
                                  .arg(int(std::lround(verifyBest_)))
                            : QString("验证未通过：风扇没有响应");
        ipcRequest(socketPath().toStdString(), "AUTO", reply, error);
        emit changed();
    }
}

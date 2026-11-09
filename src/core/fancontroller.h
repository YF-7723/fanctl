// FanController — QML 后端。把「SMC / 守护进程 / 诊断 / 验证」全部包在 C++ 里，
// QML 只负责画界面，不再需要知道守护进程的存在。
#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QStringList>

#include "ipc.h"

class QTimer;

class FanController : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(bool installed READ installed NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool outdated READ outdated NOTIFY changed)
    Q_PROPERTY(bool english READ english WRITE setEnglish NOTIFY languageChanged)
    Q_PROPERTY(QString mode READ mode NOTIFY changed)
    Q_PROPERTY(QVariantList fans READ fans NOTIFY changed)
    Q_PROPERTY(QVariantList sensors READ sensors NOTIFY changed)
    Q_PROPERTY(QVariantList highlights READ highlights NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(QString verdict READ verdict NOTIFY changed)
    Q_PROPERTY(QString diagnosis READ diagnosis NOTIFY changed)
    Q_PROPERTY(QString verifyResult READ verifyResult NOTIFY changed)
    Q_PROPERTY(bool verifying READ verifying NOTIFY changed)
    Q_PROPERTY(QString curveSensor READ curveSensor NOTIFY changed)
    Q_PROPERTY(QVariantList curvePoints READ curvePoints NOTIFY curvePointsChanged)
    Q_PROPERTY(QVariantList presets READ presets NOTIFY changed)

public:
    explicit FanController(QObject *parent = nullptr);

    bool connected() const { return connected_; }
    bool installed() const { return installed_; }
    bool busy() const { return busy_; }
    bool outdated() const { return outdated_; }
    bool english() const { return english_; }
    Q_INVOKABLE void setEnglish(bool en);
    QString mode() const { return mode_; }
    QVariantList fans() const { return fans_; }
    QVariantList sensors() const { return sensors_; }
    QVariantList highlights() const { return highlights_; }
    QString statusText() const { return statusText_; }
    QString verdict() const { return verdict_; }
    QString diagnosis() const { return diagnosis_; }
    QString verifyResult() const { return verifyResult_; }
    bool verifying() const { return verifying_; }
    QString curveSensor() const { return curveSensor_; }
    QVariantList curvePoints() const { return curvePoints_; }
    QVariantList presets() const { return presets_; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setAutomatic();
    Q_INVOKABLE void setManual(int rpm);
    Q_INVOKABLE void setFan(int index, int rpm);
    Q_INVOKABLE void setCurve(const QString &sensor, const QVariantList &points);
    Q_INVOKABLE void enterCurveMode();
    Q_INVOKABLE void savePreset(const QString &name, const QString &sensor,
                                const QVariantList &points);
    Q_INVOKABLE void deletePreset(const QString &name);
    Q_INVOKABLE void applyPreset(const QString &name);
    Q_INVOKABLE void diagnose();
    Q_INVOKABLE void verify();
    Q_INVOKABLE void enableControl();   // 首次运行：安装/启动后台组件
    Q_INVOKABLE void quitCleanly();
    Q_INVOKABLE void setPaused(bool paused);     // 退出前把风扇交还系统

signals:
    void changed();
    void languageChanged();
    void curvePointsChanged();
    void toast(const QString &message);

private:
    void applyStatus(const Status &st);
    void installHelper();
    void startDaemon();
    void setBusy(bool v);
    void verifyTick();

    bool connected_ = false;
    bool installed_ = false;
    bool busy_ = false;
    bool verifying_ = false;
    bool outdated_ = false;
    bool english_ = false;
    QString L(const char *zh, const char *en) const { return english_ ? QString::fromUtf8(en) : QString::fromUtf8(zh); }

    QString mode_ = "auto";
    QVariantList fans_;
    QVariantList sensors_;
    QVariantList highlights_;
    QString statusText_;
    QString verdict_;
    QString diagnosis_;
    QString verifyResult_;

    QString curveSensor_ = "Tp01";
    QVariantList curvePoints_;
    QVariantList defaultCurve() const;
    QVariantList presets_;
    void loadPresets();
    void storePresets();

    QTimer *timer_ = nullptr;
    QTimer *verifyTimer_ = nullptr;
    int verifyTicks_ = 0;
    double verifyBefore_ = 0;
    double verifyBest_ = 0;
};

#ifndef CALIBRATIONRUNNER_H
#define CALIBRATIONRUNNER_H

#include <QObject>
#include <QString>

class QProcess;
class QTimer;

class CalibrationRunner : public QObject
{
    Q_OBJECT

public:
    explicit CalibrationRunner(const QString &command, QObject *parent = nullptr);

public slots:
    void start();
    void stop();

signals:
    void outputReady(const QString &text);
    void finished(const QString &message);

private:
    void queueOutput(const QString &text);
    void flushOutput();
    void complete(const QString &message);

    QString m_command;
    QProcess *m_process = nullptr;
    QTimer *m_flushTimer = nullptr;
    QString m_pendingOutput;
    bool m_completed = false;
};

#endif // CALIBRATIONRUNNER_H

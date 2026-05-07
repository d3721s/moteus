#include "calibrationrunner.h"

#include <QProcess>
#include <QTimer>

namespace
{
constexpr int OutputFlushMs = 100;
}

CalibrationRunner::CalibrationRunner(const QString &command, QObject *parent)
    : QObject(parent)
    , m_command(command)
{
}

void CalibrationRunner::start()
{
    if (m_process) {
        return;
    }

    m_process = new QProcess(this);
    m_flushTimer = new QTimer(this);
    m_flushTimer->setInterval(OutputFlushMs);

    connect(m_flushTimer, &QTimer::timeout, this, &CalibrationRunner::flushOutput);
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        queueOutput(QString::fromUtf8(m_process->readAllStandardOutput()));
    });
    connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
        queueOutput(QString::fromUtf8(m_process->readAllStandardError()));
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        queueOutput(QStringLiteral("\n进程错误：%1\n").arg(m_process->errorString()));
        if (error == QProcess::FailedToStart) {
            complete(QStringLiteral("\n进程启动失败\n"));
        }
    });
    connect(m_process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
                const QString message = exitStatus == QProcess::NormalExit
                    ? QStringLiteral("\n进程结束，退出码：%1\n").arg(exitCode)
                    : QStringLiteral("\n进程异常结束\n");
                complete(message);
            });

    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_process->start(QStringLiteral("bash"), {QStringLiteral("-lc"), m_command});
}

void CalibrationRunner::stop()
{
    if (!m_process || m_process->state() == QProcess::NotRunning) {
        return;
    }

    m_process->terminate();
    if (!m_process->waitForFinished(3000)) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
    complete(QStringLiteral("\n进程已强制中止\n"));
}

void CalibrationRunner::queueOutput(const QString &text)
{
    if (text.isEmpty()) {
        return;
    }

    m_pendingOutput += text;
    if (m_flushTimer && !m_flushTimer->isActive()) {
        m_flushTimer->start();
    }
}

void CalibrationRunner::flushOutput()
{
    if (m_pendingOutput.isEmpty()) {
        if (m_flushTimer) {
            m_flushTimer->stop();
        }
        return;
    }

    QString text;
    text.swap(m_pendingOutput);
    emit outputReady(text);
}

void CalibrationRunner::complete(const QString &message)
{
    if (m_completed) {
        return;
    }

    m_completed = true;
    if (m_flushTimer) {
        m_flushTimer->stop();
    }
    if (m_process) {
        queueOutput(QString::fromUtf8(m_process->readAllStandardOutput()));
        queueOutput(QString::fromUtf8(m_process->readAllStandardError()));
    }
    flushOutput();
    emit finished(message);
}

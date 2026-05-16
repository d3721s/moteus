#include "uniformmotionwindow.h"

#include <QDoubleSpinBox>
#include <QDoubleValidator>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace
{
constexpr int MaxUniformityPoints = 1400;
constexpr double MinDtSec = 0.0005;
constexpr double MinMeanSpeedRpm = 0.1;

QLabel *metricLabel(const QString &text = QStringLiteral("-"))
{
    auto *label = new QLabel(text);
    label->setMinimumWidth(110);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}
}

class UniformityChartWidget : public QWidget
{
public:
    explicit UniformityChartWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumHeight(260);
    }

    void setData(const QVector<UniformMotionWindow::SpeedSample> &samples, double thresholdPercent)
    {
        m_samples = samples;
        m_thresholdPercent = thresholdPercent;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QWidget::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRectF full = rect().adjusted(6, 6, -6, -6);
        painter.fillRect(full, QColor(18, 22, 26));
        painter.setPen(QPen(QColor(68, 78, 88), 1));
        painter.drawRect(full);

        const QRectF plot = full.adjusted(62, 14, -14, -38);
        painter.setPen(QPen(QColor(45, 54, 62), 1));
        for (int i = 0; i <= 4; ++i) {
            const qreal y = plot.top() + plot.height() * i / 4.0;
            painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        }
        for (int i = 0; i <= 8; ++i) {
            const qreal x = plot.left() + plot.width() * i / 8.0;
            painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        }

        if (m_samples.isEmpty()) {
            painter.setPen(QColor(170, 176, 188));
            painter.drawText(plot, Qt::AlignCenter, QStringLiteral("等待串口编码器连续采样数据..."));
            return;
        }

        double maxAbs = std::max(1.0, m_thresholdPercent);
        for (const auto &sample : m_samples) {
            maxAbs = std::max(maxAbs, std::abs(sample.deviationPercent));
        }
        maxAbs = std::min(std::max(maxAbs * 1.25, m_thresholdPercent * 1.6), 100.0);

        auto mapPoint = [&](int index, double deviation) -> QPointF {
            const double xDenom = std::max(1.0, double(m_samples.size() - 1));
            const qreal x = plot.left() + double(index) / double(xDenom) * plot.width();
            const qreal y = plot.center().y() - deviation / maxAbs * (plot.height() / 2.0);
            return QPointF(x, y);
        };

        const QPointF bandTopLeft = mapPoint(0, m_thresholdPercent);
        const QPointF bandBottomRight = mapPoint(m_samples.size() - 1, -m_thresholdPercent);
        const qreal bandTop = std::min(bandTopLeft.y(), bandBottomRight.y());
        const qreal bandBottom = std::max(bandTopLeft.y(), bandBottomRight.y());
        QRectF band(QPointF(plot.left(), bandTop), QPointF(plot.right(), bandBottom));
        painter.fillRect(band, QColor(29, 76, 55, 130));

        painter.setPen(QPen(QColor(120, 210, 150), 1, Qt::DashLine));
        painter.drawLine(mapPoint(0, m_thresholdPercent), mapPoint(m_samples.size() - 1, m_thresholdPercent));
        painter.drawLine(mapPoint(0, -m_thresholdPercent), mapPoint(m_samples.size() - 1, -m_thresholdPercent));
        painter.setPen(QPen(QColor(110, 118, 128), 1));
        painter.drawLine(mapPoint(0, 0.0), mapPoint(m_samples.size() - 1, 0.0));

        QPainterPath path;
        path.moveTo(mapPoint(0, m_samples.first().deviationPercent));
        for (int i = 1; i < m_samples.size(); ++i) {
            path.lineTo(mapPoint(i, m_samples.at(i).deviationPercent));
        }
        painter.setPen(QPen(QColor(84, 204, 255), 2));
        painter.drawPath(path);

        painter.setPen(QColor(176, 184, 194));
        painter.drawText(QRectF(full.left() + 10, full.bottom() - 24, 260, 18),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("X: sample, Y: speed deviation (%)"));
        painter.drawText(QRectF(full.right() - 260, full.bottom() - 24, 250, 18),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QStringLiteral("green band: +/- %1%").arg(m_thresholdPercent, 0, 'f', 2));

        painter.setPen(QColor(176, 184, 194));
        painter.drawText(QRectF(full.left() + 8, plot.top() - 8, 48, 16),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QStringLiteral("+%1").arg(maxAbs, 0, 'f', 1));
        painter.drawText(QRectF(full.left() + 8, plot.center().y() - 8, 48, 16),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QStringLiteral("0"));
        painter.drawText(QRectF(full.left() + 8, plot.bottom() - 8, 48, 16),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QStringLiteral("-%1").arg(maxAbs, 0, 'f', 1));
    }

private:
    QVector<UniformMotionWindow::SpeedSample> m_samples;
    double m_thresholdPercent = 2.0;
};

UniformMotionWindow::UniformMotionWindow(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("串口编码器匀速检测"));
    setWindowFlags(windowFlags()
                   | Qt::WindowMinimizeButtonHint
                   | Qt::WindowMaximizeButtonHint
                   | Qt::WindowCloseButtonHint);
    resize(960, 560);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    auto *controls = new QWidget(this);
    auto *grid = new QGridLayout(controls);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(6);

    m_windowSpin = new QSpinBox(controls);
    m_windowSpin->setRange(5, 300);
    m_windowSpin->setValue(40);
    m_windowSpin->setSuffix(QStringLiteral(" 点"));

    m_thresholdSpin = new QDoubleSpinBox(controls);
    m_thresholdSpin->setRange(0.05, 50.0);
    m_thresholdSpin->setDecimals(2);
    m_thresholdSpin->setSingleStep(0.25);
    m_thresholdSpin->setValue(2.0);
    m_thresholdSpin->setSuffix(QStringLiteral(" %"));

    m_initialSpeedEdit = new QLineEdit(controls);
    m_initialSpeedEdit->setPlaceholderText(QStringLiteral("自动"));
    m_initialSpeedEdit->setValidator(new QDoubleValidator(-1000000.0, 1000000.0, 3, m_initialSpeedEdit));
    m_initialSpeedEdit->setToolTip(QStringLiteral("非 0 时按 (瞬时转速 - 速度初值) / |速度初值| 计算速度变化率；留空或 0 时自动使用窗口均值。"));

    m_clearButton = new QPushButton(QStringLiteral("清空曲线"), controls);
    m_sampleCountLabel = metricLabel();
    m_speedLabel = metricLabel();
    m_meanSpeedLabel = metricLabel();
    m_deviationLabel = metricLabel();
    m_stateLabel = metricLabel(QStringLiteral("等待采样"));

    grid->addWidget(new QLabel(QStringLiteral("判定窗口"), controls), 0, 0);
    grid->addWidget(m_windowSpin, 0, 1);
    grid->addWidget(new QLabel(QStringLiteral("波动阈值"), controls), 0, 2);
    grid->addWidget(m_thresholdSpin, 0, 3);
    grid->addWidget(new QLabel(QStringLiteral("速度初值(rpm)"), controls), 0, 4);
    grid->addWidget(m_initialSpeedEdit, 0, 5);
    grid->addWidget(m_clearButton, 0, 6);
    grid->addWidget(new QLabel(QStringLiteral("样本"), controls), 1, 0);
    grid->addWidget(m_sampleCountLabel, 1, 1);
    grid->addWidget(new QLabel(QStringLiteral("瞬时转速"), controls), 1, 2);
    grid->addWidget(m_speedLabel, 1, 3);
    grid->addWidget(new QLabel(QStringLiteral("平均转速"), controls), 1, 4);
    grid->addWidget(m_meanSpeedLabel, 1, 5);
    grid->addWidget(new QLabel(QStringLiteral("最大波动"), controls), 1, 6);
    grid->addWidget(m_deviationLabel, 1, 7);
    grid->addWidget(new QLabel(QStringLiteral("判定"), controls), 1, 8);
    grid->addWidget(m_stateLabel, 1, 9);
    grid->setColumnStretch(9, 1);
    root->addWidget(controls, 0);

    m_chart = new UniformityChartWidget(this);
    root->addWidget(m_chart, 1);

    connect(m_clearButton, &QPushButton::clicked, this, &UniformMotionWindow::clearSamples);
    connect(m_windowSpin, &QSpinBox::valueChanged, this, [this](int) { updateStats(); });
    connect(m_thresholdSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { updateStats(); });
    connect(m_initialSpeedEdit, &QLineEdit::textChanged, this, [this](const QString &) { updateStats(); });

    updateStats();
}

void UniformMotionWindow::addEncoderSample(double timestampSec,
                                           double positionTurns,
                                           double,
                                           bool continuousPosition)
{
    if (!m_hasLastPosition) {
        m_hasLastPosition = true;
        m_lastWasContinuous = continuousPosition;
        m_lastTimestampSec = timestampSec;
        m_lastPositionTurns = positionTurns;
        m_unwrappedTurns = positionTurns;
        updateStats();
        return;
    }

    const double dt = timestampSec - m_lastTimestampSec;
    if (dt < MinDtSec) {
        return;
    }

    double deltaTurns = positionTurns - m_lastPositionTurns;
    if (!continuousPosition || !m_lastWasContinuous) {
        if (deltaTurns > 0.5) {
            deltaTurns -= 1.0;
        } else if (deltaTurns < -0.5) {
            deltaTurns += 1.0;
        }
    }

    m_unwrappedTurns += deltaTurns;
    const double speedRpm = deltaTurns / dt * 60.0;

    SpeedSample sample;
    sample.timestampSec = timestampSec;
    sample.speedRpm = speedRpm;
    m_samples.append(sample);
    if (m_samples.size() > MaxUniformityPoints) {
        m_samples.remove(0, m_samples.size() - MaxUniformityPoints);
    }

    m_lastWasContinuous = continuousPosition;
    m_lastTimestampSec = timestampSec;
    m_lastPositionTurns = positionTurns;
    updateStats();
}

void UniformMotionWindow::clearSamples()
{
    m_samples.clear();
    m_hasLastPosition = false;
    m_lastWasContinuous = false;
    m_lastTimestampSec = 0.0;
    m_lastPositionTurns = 0.0;
    m_unwrappedTurns = 0.0;
    updateStats();
}

void UniformMotionWindow::updateStats()
{
    const int count = m_samples.size();
    const int window = m_windowSpin ? m_windowSpin->value() : 40;
    const int start = std::max(0, count - window);
    const int windowCount = count - start;

    double mean = 0.0;
    for (int i = start; i < count; ++i) {
        mean += m_samples.at(i).speedRpm;
    }
    if (windowCount > 0) {
        mean /= double(windowCount);
    }

    bool initialSpeedOk = false;
    const double initialSpeedRpm = m_initialSpeedEdit ? m_initialSpeedEdit->text().trimmed().toDouble(&initialSpeedOk) : 0.0;
    const bool useInitialSpeed = initialSpeedOk && std::abs(initialSpeedRpm) >= MinMeanSpeedRpm;

    double maxDeviation = 0.0;
    if (useInitialSpeed || std::abs(mean) >= MinMeanSpeedRpm) {
        for (int i = 0; i < count; ++i) {
            double referenceSpeed = initialSpeedRpm;
            if (!useInitialSpeed) {
                referenceSpeed = mean;
                const int localStart = std::max(0, i - window + 1);
                const int localCount = i - localStart + 1;
                if (localCount > 0) {
                    referenceSpeed = 0.0;
                    for (int j = localStart; j <= i; ++j) {
                        referenceSpeed += m_samples.at(j).speedRpm;
                    }
                    referenceSpeed /= double(localCount);
                }
            }

            double deviation = 0.0;
            if (std::abs(referenceSpeed) >= MinMeanSpeedRpm) {
                deviation = (m_samples.at(i).speedRpm - referenceSpeed) / std::abs(referenceSpeed) * 100.0;
            }
            m_samples[i].deviationPercent = deviation;
            if (i >= start) {
                maxDeviation = std::max(maxDeviation, std::abs(deviation));
            }
        }
    } else {
        for (SpeedSample &sample : m_samples) {
            sample.deviationPercent = 0.0;
        }
    }

    const double threshold = m_thresholdSpin ? m_thresholdSpin->value() : 2.0;
    if (m_chart) {
        m_chart->setData(m_samples, threshold);
    }

    if (m_sampleCountLabel) {
        m_sampleCountLabel->setText(QString::number(count));
    }
    if (m_speedLabel) {
        m_speedLabel->setText(count > 0 ? QStringLiteral("%1 rpm").arg(m_samples.last().speedRpm, 0, 'f', 3)
                                        : QStringLiteral("-"));
    }
    if (m_meanSpeedLabel) {
        m_meanSpeedLabel->setText(windowCount > 0 ? QStringLiteral("%1 rpm").arg(mean, 0, 'f', 3)
                                                  : QStringLiteral("-"));
    }
    if (m_deviationLabel) {
        m_deviationLabel->setText(windowCount > 0 ? QStringLiteral("%1 %").arg(maxDeviation, 0, 'f', 3)
                                                  : QStringLiteral("-"));
    }
    if (m_stateLabel) {
        if (count < 5) {
            m_stateLabel->setText(QStringLiteral("样本不足"));
            m_stateLabel->setStyleSheet(QStringLiteral("color: #475467; font-weight: 700;"));
        } else if (std::abs(mean) < MinMeanSpeedRpm) {
            m_stateLabel->setText(QStringLiteral("速度过低"));
            m_stateLabel->setStyleSheet(QStringLiteral("color: #B54708; font-weight: 800;"));
        } else if (maxDeviation <= threshold) {
            m_stateLabel->setText(QStringLiteral("匀速"));
            m_stateLabel->setStyleSheet(QStringLiteral("color: #05603A; font-weight: 800;"));
        } else {
            m_stateLabel->setText(QStringLiteral("不匀速"));
            m_stateLabel->setStyleSheet(QStringLiteral("color: #B42318; font-weight: 800;"));
        }
    }
}

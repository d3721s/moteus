#ifndef UNIFORMMOTIONWINDOW_H
#define UNIFORMMOTIONWINDOW_H

#include <QDialog>
#include <QVector>

class QLabel;
class QPushButton;
class QSpinBox;
class QDoubleSpinBox;

class UniformityChartWidget;

class UniformMotionWindow : public QDialog
{
    Q_OBJECT

public:
    struct SpeedSample
    {
        double timestampSec = 0.0;
        double speedRpm = 0.0;
        double deviationPercent = 0.0;
    };

    explicit UniformMotionWindow(QWidget *parent = nullptr);

public slots:
    void addEncoderSample(double timestampSec, double positionTurns, double angleDeg, bool continuousPosition);
    void clearSamples();

private:
    void updateStats();

    UniformityChartWidget *m_chart = nullptr;
    QLabel *m_sampleCountLabel = nullptr;
    QLabel *m_speedLabel = nullptr;
    QLabel *m_meanSpeedLabel = nullptr;
    QLabel *m_deviationLabel = nullptr;
    QLabel *m_stateLabel = nullptr;
    QSpinBox *m_windowSpin = nullptr;
    QDoubleSpinBox *m_thresholdSpin = nullptr;
    QPushButton *m_clearButton = nullptr;

    QVector<SpeedSample> m_samples;
    bool m_hasLastPosition = false;
    bool m_lastWasContinuous = false;
    double m_lastTimestampSec = 0.0;
    double m_lastPositionTurns = 0.0;
    double m_unwrappedTurns = 0.0;
};

#endif // UNIFORMMOTIONWINDOW_H

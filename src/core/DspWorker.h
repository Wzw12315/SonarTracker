#pragma once
#include <QThread>
#include <QString>
#include <atomic>
#include "DataTypes.h"
#include "detect_line_spectrum_from_lofar_change.h"

class DspWorker : public QThread {
    Q_OBJECT
public:
    explicit DspWorker(QObject *parent = nullptr);
    ~DspWorker();

    void setDirectory(const QString& dirPath);
    void setConfig(const DspConfig& config);
    // 【新增】：接收先验真值
        void setGroundTruths(const std::vector<TargetTruth>& truths) { m_groundTruths = truths; }
    void stop();
    void pause();
    void resume();
    bool isPaused() const { return m_isPaused; }

signals:
    void frameProcessed(const FrameResult& result);
    void logReady(const QString& log);
    void reportReady(const QString& report);
    void offlineResultsReady(const QList<OfflineTargetResult>& results);
    void processingFinished();
    void batchFinished(int batchIndex, int startFrame, int endFrame, const std::vector<BatchTargetFeature>& features);

    // 【新增】：抛出结构化仪表盘数据
    void evaluationResultReady(const SystemEvaluationResult& result);
protected:
    void run() override;

private:
    QString m_directory;
    DspConfig m_config;
    std::atomic<bool> m_isRunning;
    std::atomic<bool> m_isPaused;
    // 【新增】：存储先验真值
        std::vector<TargetTruth> m_groundTruths;

};

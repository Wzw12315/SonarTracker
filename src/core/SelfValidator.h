#ifndef SELFVALIDATOR_H
#define SELFVALIDATOR_H

#include <QObject>
#include <QString>
#include <vector>
#include "DataTypes.h"

class SelfValidator : public QObject {
    Q_OBJECT
public:
    explicit SelfValidator(QObject *parent = nullptr);

    void loadTruthData(const QString& filePath);
    void setTruthData(const std::vector<TargetTruth>& manualData); // 新增：接收手动输入的数据
    double calculateTheoreticalAngle(int targetId, double timeSeconds);
    // 【新增】：暴露已加载的真值表，供 DspWorker 作为打分基准
        const std::vector<TargetTruth>& getTruthData() const { return m_truthData; }

public slots:
    void onBatchFinished(int batchIndex, int startFrame, int endFrame, const std::vector<BatchTargetFeature>& dspFeatures);

signals:
    void validationLogReady(const QString& logStr);
    void batchAccuracyComputed(int batchIndex, double accuracy);

private:
    std::vector<TargetTruth> m_truthData;
    void initDefaultTruthData();
    // 【新增】：声明特征匹配评估函数
        double evaluateMatch(const BatchTargetFeature& feature, const TargetTruth& truth);

};

#endif // SELFVALIDATOR_H

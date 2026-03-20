#include "SelfValidator.h"
#include <cmath>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

SelfValidator::SelfValidator(QObject *parent) : QObject(parent) {
    initDefaultTruthData();
}

void SelfValidator::initDefaultTruthData() {
    m_truthData = {
        {1, "Target 1", 60.0, 20000.0, 4.0, 45.0, {125.0}, 2.1},
        {2, "Target 2", 80.0, 20000.0, 5.0, 80.0, {112.0}, 2.8}
    };
}

void SelfValidator::loadTruthData(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QByteArray fileData = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(fileData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !jsonDoc.isObject()) return;

    QJsonObject rootObj = jsonDoc.object();
    if (rootObj.contains("targets") && rootObj["targets"].isArray()) {
        QJsonArray targetArray = rootObj["targets"].toArray();
        m_truthData.clear();
        for (int i = 0; i < targetArray.size(); ++i) {
            QJsonObject tObj = targetArray[i].toObject();
            TargetTruth truth;
            truth.id = tObj["id"].toInt();
            truth.name = tObj["name"].toString();
            truth.initialAngle = tObj["initialAngle"].toDouble();
            truth.initialDistance = tObj["initialDistance"].toDouble();
            truth.speed = tObj["speed"].toDouble();
            truth.course = tObj["course"].toDouble();
            truth.trueDemonFreq = tObj["trueDemonFreq"].toDouble();
            QJsonArray lofarArr = tObj["trueLofarFreqs"].toArray();
            for (int j = 0; j < lofarArr.size(); ++j) {
                truth.trueLofarFreqs.push_back(lofarArr[j].toDouble());
            }
            m_truthData.push_back(truth);
        }
    }
}

void SelfValidator::setTruthData(const std::vector<TargetTruth>& manualData) {
    m_truthData = manualData;
}

double SelfValidator::calculateTheoreticalAngle(int targetId, double timeSeconds) {
    auto it = std::find_if(m_truthData.begin(), m_truthData.end(), [targetId](const TargetTruth& t) { return t.id == targetId; });
    if (it == m_truthData.end()) return -1.0;

    double X0 = it->initialDistance * std::cos(it->initialAngle * M_PI / 180.0);
    double Y0 = it->initialDistance * std::sin(it->initialAngle * M_PI / 180.0);

    double X = X0 + it->speed * std::cos(it->course * M_PI / 180.0) * timeSeconds;
    double Y = Y0 + it->speed * std::sin(it->course * M_PI / 180.0) * timeSeconds;

    double angleRad = std::atan2(Y, X);
    double angleDeg = angleRad * 180.0 / M_PI;
    if (angleDeg < 0) angleDeg += 360.0;

    return angleDeg;
}

void SelfValidator::onBatchFinished(int batchIndex, int startFrame, int endFrame, const std::vector<BatchTargetFeature>& dspFeatures) {
    QString logOutput = "";
    double timeSeconds = endFrame * 3.0;

    logOutput += "======================================================\n";
    logOutput += QString("第 %1 批数据 (帧 %2 - %3) 综合判别报告\n").arg(batchIndex).arg(startFrame).arg(endFrame);
    logOutput += "======================================================\n";

    int correctCount = 0;
    // 定义方位容差波门（度），解算方位在此范围内即认为跟踪成功
    const double AZIMUTH_GATE = 6.0;

    for (const auto& feature : dspFeatures) {
        int tId = feature.formalId;
        if (tId > m_truthData.size() || tId <= 0) continue;

        const TargetTruth& truth = m_truthData[tId - 1];
        double realAngle = calculateTheoreticalAngle(tId, timeSeconds);

        // 计算方位误差，处理 360 度跨越问题
        double angleError = std::abs(feature.calAngle - realAngle);
        if (angleError > 180.0) angleError = 360.0 - angleError;

        // 【核心逻辑】：只要方位锁定在波门内，即算作锁定目标
        bool isLocked = (angleError <= AZIMUTH_GATE);
        if (isLocked) correctCount++;

        QString judgeStr = isLocked ? "✅ 方位锁定" : "❌ 丢失目标";

        QString calFreqStr = "";
        for(double f : feature.calLofar) calFreqStr += QString::number(f, 'f', 1) + " ";
        QString trueFreqStr = "";
        for(double f : truth.trueLofarFreqs) trueFreqStr += QString::number(f, 'f', 1) + " ";

        logOutput += QString("▶ 目标 %1：%2\n").arg(tId).arg(truth.name);
        logOutput += QString("  计算频率: [ %1] Hz  |  真实频率: [ %2] Hz\n").arg(calFreqStr).arg(trueFreqStr);
        logOutput += QString("  计算轴频: %1 Hz  |  真实轴频: %2 Hz\n").arg(feature.calDemon, 0, 'f', 1).arg(truth.trueDemonFreq, 0, 'f', 1);
        logOutput += QString("  计算方位: %1°  |  真实方位: %2° (误差: %3°)\n").arg(feature.calAngle, 0, 'f', 1).arg(realAngle, 0, 'f', 1).arg(angleError, 0, 'f', 2);
        logOutput += QString("  综合状态: %1\n").arg(judgeStr);
        logOutput += "----------------------------------------------------\n";
    }

    double accuracy = dspFeatures.empty() ? 0.0 : (correctCount * 100.0 / dspFeatures.size());
    logOutput += QString("【本批次综合指标】稳定跟踪率: %1%\n").arg(accuracy, 0, 'f', 2);

    emit batchAccuracyComputed(batchIndex, accuracy);
    emit validationLogReady(logOutput);
}

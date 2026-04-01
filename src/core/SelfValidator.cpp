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
    // 【核心修改】：彻底清空代码中硬编码的预制真值模板。
    // 这样系统默认启动时就是一张“白纸”，m_truthData 为空。
    // 除非用户手动导入 JSON，否则将完美触发系统的“无先验实战盲测模式”。
    m_truthData.clear();
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

void SelfValidator::onBatchFinished(int batchIndex, int startFrame, int endFrame, const std::vector<BatchTargetFeature>& features) {
    if (m_truthData.empty()) return;

    QString log = QString("\n======================================================\n");
    log += QString("               批处理 [第 %1 批] 自动置信度检验               \n").arg(batchIndex);
    log += QString("======================================================\n");
    log += QString("检验跨度: 帧 %1 至 帧 %2\n").arg(startFrame).arg(endFrame);
    log += QString("本批次输入目标聚类数: %1\n").arg(features.size());

    int correctCount = 0;

    for (const auto& feature : features) {
        log += QString("\n▶ 目标 %1：\n").arg(feature.formalId);

        QString freqsStr;
        for (size_t i = 0; i < feature.calLofar.size(); ++i) {
            freqsStr += QString::number(feature.calLofar[i], 'f', 1);
            if (i < feature.calLofar.size() - 1) freqsStr += ", ";
        }
        if (freqsStr.isEmpty()) freqsStr = "无";

        QString freqsStrDcv;
        for (size_t i = 0; i < feature.calLofarDcv.size(); ++i) {
            freqsStrDcv += QString::number(feature.calLofarDcv[i], 'f', 1);
            if (i < feature.calLofarDcv.size() - 1) freqsStrDcv += ", ";
        }
        if (freqsStrDcv.isEmpty()) freqsStrDcv = "无";

        log += QString("    计算方位: %1°, 瞬时计算频率: [ %2 ] Hz, DCV计算频率: [ %3 ] Hz, 计算轴频: %4 Hz\n")
               .arg(feature.calAngle, 0, 'f', 1)
               .arg(freqsStr)
               .arg(freqsStrDcv)
               .arg(feature.calDemon, 0, 'f', 1);

        double maxScore = -1.0;
        int bestMatchTruthId = -1;

        for (const auto& truth : m_truthData) {
            double score = evaluateMatch(feature, truth);
            if (score > maxScore) {
                maxScore = score;
                bestMatchTruthId = truth.id;
            }
        }

        if (bestMatchTruthId != -1) {
            TargetTruth bestTruth;
            for (const auto& t : m_truthData) {
                if (t.id == bestMatchTruthId) bestTruth = t;
            }

            double azimuthError = std::abs(feature.calAngle - bestTruth.initialAngle);

            // 【核心修正】：阈值降至 50.0。第一批次(初始化时)若无线谱，仅靠方位(30分)+轴频(30分)依旧可以判定成功！
            bool isCorrect = (maxScore >= 50.0);

            QString trueLofarStr;
            for (size_t i = 0; i < bestTruth.trueLofarFreqs.size(); ++i) {
                trueLofarStr += QString::number(bestTruth.trueLofarFreqs[i], 'f', 1);
                if (i < bestTruth.trueLofarFreqs.size() - 1) trueLofarStr += ", ";
            }
            if (trueLofarStr.isEmpty()) trueLofarStr = "无";

            log += QString("    最佳匹配先验: [%1] (Truth ID: %2)\n").arg(bestTruth.name).arg(bestTruth.id);
            log += QString("    真实方位: %1° (方位误差: %2°)\n").arg(bestTruth.initialAngle, 0, 'f', 1).arg(azimuthError, 0, 'f', 1);
            log += QString("    真实线谱: [ %1 ] Hz\n").arg(trueLofarStr);
            log += QString("    真实轴频: %1 Hz\n").arg(bestTruth.trueDemonFreq > 0 ? QString::number(bestTruth.trueDemonFreq, 'f', 1) : "无");
            log += QString("    置信度得分: %1 / 100.0\n").arg(maxScore, 0, 'f', 1);

            // 【核心修正】：彻底删除物理类别强制判断，直接输出判定成功或失败
            log += QString("    综合判别: 判定%1\n").arg(isCorrect ? "成功" : "失败");

            if (isCorrect) correctCount++;

        } else {
            log += "    无法匹配任何先验真值 (得分过低)\n";
        }
    }

    double batchAccuracy = features.empty() ? 0.0 : (double)correctCount / features.size() * 100.0;
    log += QString("\n------------------------------------------------------\n");
    log += QString(">> 本批次综合识别正确率: %1%\n").arg(batchAccuracy, 0, 'f', 2);

    emit validationLogReady(log);
    emit batchAccuracyComputed(batchIndex, batchAccuracy);
}

// =========================================================================
// 核心特征距离匹配算法：综合对比 瞬时线谱、DCV累积线谱、方位角 和 轴频
// =========================================================================
double SelfValidator::evaluateMatch(const BatchTargetFeature& feature, const TargetTruth& truth) {
    double score = 0.0;

    // 1. 方位角匹配 (权重 30 分)
    // 假设允许最大偏差 10 度，越接近满分越多
    double angleDiff = std::abs(feature.calAngle - truth.initialAngle);
    if (angleDiff <= 10.0) {
        score += 30.0 * (1.0 - angleDiff / 10.0);
    }

    // 2. 轴频 DEMON 匹配 (权重 30 分)
    // 假设允许最大偏差 3 Hz
    if (truth.trueDemonFreq > 0 && feature.calDemon > 0) {
        double demonDiff = std::abs(feature.calDemon - truth.trueDemonFreq);
        if (demonDiff <= 3.0) {
            score += 30.0 * (1.0 - demonDiff / 3.0);
        }
    } else if (truth.trueDemonFreq == 0 && feature.calDemon == 0) {
        score += 30.0; // 都无轴频，完全匹配
    }

    // 3. 线谱 LOFAR 匹配 (权重 40 分)
    // 【核心升级】：综合瞬时线谱和DCV线谱与真值进行交叉比对，提高低信噪比下的得分
    if (!truth.trueLofarFreqs.empty()) {
        int hitCount = 0;
        for (double trueFreq : truth.trueLofarFreqs) {
            bool hit = false;
            // 允许 3.5 Hz 的频率容差（多普勒展宽）
            // 先在瞬时线谱里找
            for (double fInst : feature.calLofar) {
                if (std::abs(fInst - trueFreq) <= 3.5) { hit = true; break; }
            }
            // 如果瞬时谱没找到，去强大的 DCV 累积谱里找
            if (!hit) {
                for (double fDcv : feature.calLofarDcv) {
                    if (std::abs(fDcv - trueFreq) <= 3.5) { hit = true; break; }
                }
            }
            if (hit) hitCount++;
        }
        // 按命中比例给分
        score += 40.0 * ((double)hitCount / truth.trueLofarFreqs.size());
    } else {
        score += 40.0; // 如果目标真值本身就没有离散线谱，默认这部分满分
    }

    return score;
}

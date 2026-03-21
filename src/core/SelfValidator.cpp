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

void SelfValidator::onBatchFinished(int batchIndex, int startFrame, int endFrame, const std::vector<BatchTargetFeature>& features) {
    // 【修正】：使用 m_truthData.empty() 替代未声明的 m_hasTruthData
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

        // ========================================================
        // 格式化提取出新增的 DCV 累积计算频点
        // ========================================================
        QString freqsStrDcv;
        for (size_t i = 0; i < feature.calLofarDcv.size(); ++i) {
            freqsStrDcv += QString::number(feature.calLofarDcv[i], 'f', 1);
            if (i < feature.calLofarDcv.size() - 1) freqsStrDcv += ", ";
        }
        if (freqsStrDcv.isEmpty()) freqsStrDcv = "无";

        log += QString("    输入计算方位: %1°, 瞬时计算频率: [ %2 ] Hz, DCV计算频率: [ %3 ] Hz, 计算轴频: %4 Hz\n")
               .arg(feature.calAngle, 0, 'f', 1)
               .arg(freqsStr)
               .arg(freqsStrDcv)
               .arg(feature.calDemon, 0, 'f', 1);
        // ========================================================

        double maxScore = -1.0;
        int bestMatchTruthId = -1;

        // 【修正】：将 m_truthList 替换为头文件中正确声明的 m_truthData
        for (const auto& truth : m_truthData) {
            // 注意：如果你的 evaluateMatch 函数还没有适配传入 feature.calLofarDcv，
            // 内部可能依然只使用了瞬时的 calLofar，这是正常的，当前只要跑通即可。
            double score = evaluateMatch(feature, truth);
            if (score > maxScore) {
                maxScore = score;
                bestMatchTruthId = truth.id;
            }
        }

        if (bestMatchTruthId != -1) {
            TargetTruth bestTruth;
            // 【修正】：将 m_truthList 替换为 m_truthData
            for (const auto& t : m_truthData) {
                if (t.id == bestMatchTruthId) bestTruth = t;
            }

            log += QString("    最佳匹配先验: [%1] (Truth ID: %2)\n").arg(bestTruth.name).arg(bestTruth.id);
            log += QString("    置信度得分: %1 / 100.0\n").arg(maxScore, 0, 'f', 1);

            QString estClass = (maxScore >= 60.0) ? (bestTruth.initialDistance > 20.0 ? "水下潜艇" : "水面舰船") : "未知杂波";
            QString trueClass = (bestTruth.initialDistance > 20.0) ? "水下潜艇" : "水面舰船";
            bool isCorrect = (estClass == trueClass && maxScore >= 60.0);

            log += QString("    真实深度: %1 m -> 物理类别基准: %2\n").arg(bestTruth.initialDistance).arg(trueClass);
            log += QString("    综合判别: [%1] -> 判别%2\n").arg(estClass).arg(isCorrect ? "正确" : "错误");

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

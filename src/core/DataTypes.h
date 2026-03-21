#pragma once
#include <QVector>
#include <QList>
#include <QString>
#include <QMetaType>
#include <vector>

// 全局信号处理配置参数
struct DspConfig {
    double fs = 5000.0;
    int M = 512;
    double d = 1.2;
    double c = 1500.0;
    double r_scan = 20000.0;
    double timeStep = 3.0;

    int dcvRlIter = 20;
    double lofarMin = 100.0;
    double lofarMax = 300.0;
    double demonMin = 350.0;
    double demonMax = 2000.0;
    int nfftR = 15000;
    int nfftWin = 30000;

    double azDetBgMult = 5.0;
    double azDetSidelobeRatio = 0.02;
    int azDetPeakMinDist = 2;

    int lofarBgMedWindow = 150;
    double lofarSnrThreshMult = 2.5;
    int lofarPeakMinDist = 30;

    // [新增] 累积DCV提取参数
    int dcvLofarBgMedWindow = 100;
    double dcvLofarSnrThreshMult = 1.2;
    int dcvLofarPeakMinDist = 15;

    int firOrder = 64;
    double firCutoff = 0.1;

    double tpswG = 45.0;
    double tpswE = 2.0;
    double tpswC = 1.15;
    int dpL = 5;
    double dpAlpha = 1.5;
    double dpBeta = 1.0;
    double dpGamma = 0.1;

    int batchSize = 40;
};
Q_DECLARE_METATYPE(DspConfig)

struct TargetTruth {
    int id;
    QString name;
    double initialAngle;      // 起始方位(度)
    double initialDistance;   // 起始距离(m)
    double speed;             // 航速(m/s)
    double course;            // 航向(度)
    std::vector<double> trueLofarFreqs; // 真实线谱群
    double trueDemonFreq;     // 真实轴频
};
struct BatchTargetFeature {
    int formalId;
    double calAngle;
    std::vector<double> calLofar;
    std::vector<double> calLofarDcv; // [新增] 批处理聚合后的 DCV 累积线谱
    double calDemon;
};

struct TargetTrack {
    int id;
    int internal_id;
    bool isConfirmed;
    int totalHits;
    int age;

    bool isActive;
    int missedCount;
    double currentAngle;
    double currentAngleCbf;
    int currentLoc;

    QVector<double> lofarSpectrum;
    QVector<double> demonSpectrum;
    QVector<double> lineSpectrumAmp;

    QVector<double> lofarFullLinear;
    QVector<double> cbfLofarFullLinear;

    std::vector<double> lineSpectra;
    double shaftFreq;

    // [新增] 累积DCV线谱专属存储
    std::vector<double> lineSpectraDcv;
    QVector<double> lineSpectrumAmpDcv;
    QVector<double> accumulatedDcvSpectrum;
};
Q_DECLARE_METATYPE(TargetTrack)

struct FrameResult {
    int frameIndex;
    double timestamp;
    QVector<double> thetaAxis;
    QVector<double> cbfData;
    QVector<double> dcvData;
    QVector<double> detectedAngles;
    QString logMessage;
    QList<TargetTrack> tracks;
};
Q_DECLARE_METATYPE(FrameResult)

struct OfflineTargetResult {
    int targetId;
    double startAngle;
    int timeFrames;
    int freqBins;
    double minTime;
    double maxTime;
    double displayFreqMin;
    double displayFreqMax;
    QVector<double> rawLofarDb;
    QVector<double> tpswLofarDb;
    QVector<double> dpCounter;
};
Q_DECLARE_METATYPE(QList<OfflineTargetResult>)

// =========================================================
// 【新增】：用于前端渲染 Dashboard 驾驶舱的结构化评估结果
// =========================================================
struct TargetEvaluation {
    int targetId;
    QString lineSpectraStr;
    double accuracy; // 百分比正确率 (0~100)
    double shaftFreq;

    // [新增] 累积DCV专属评估结果
    QString lineSpectraStrDcv;
    double accuracyDcv = 0.0;
};
Q_DECLARE_METATYPE(TargetEvaluation)

struct SystemEvaluationResult {
    double totalTimeSec;
    double realtimeTimeSec;
    double batchTimeSec;
    int confirmedTargetCount;
    QList<TargetEvaluation> targetEvals;
};
Q_DECLARE_METATYPE(SystemEvaluationResult)

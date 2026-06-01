#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "predict.h"
#include <QProcess>
#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QSpinBox>
#include <filesystem>
#include <QMessageBox>
#include "nlohmann/json.hpp"
#include <fstream>
#include <QFileDialog>
#include <iostream>

namespace fs = std::filesystem;

mainwindow::mainwindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::mainwindow)
{
    ui->setupUi(this);
    this->setWindowTitle("ATAR Calculator");

    ui->NumAtarRes->setSingleStep(0.05);
    ui->NumAtarRes->setRange(30.00, 99.95);

    loadAtarTable();

    fs::path target_fileA = "vcaa_summary.json";
    fs::path target_fileB = "scaling_conglomerate.json";

    if (fs::exists(target_fileB) && fs::exists(target_fileA)) {
        ui->AddClassBtn->setEnabled(true);
        populateSubjects();
    }
    else {
        ui->AddClassBtn->setEnabled(false);
    }
}

mainwindow::~mainwindow()
{
    delete ui;
}

void mainwindow::populateSubjects() {
    std::ifstream fileConglomerate("scaling_conglomerate.json");
    std::ifstream fileVcaa("vcaa_summary.json");

    if (!fileConglomerate.is_open() || !fileVcaa.is_open()) {
        return;
    }

    json conglomerate;
    json vcaaData;

    try {
        fileConglomerate >> conglomerate;
        fileVcaa >> vcaaData;
    } catch (const json::parse_error& e) {
        return;
    }

    ui->AddX->clear();

    for (const auto& [code, data] : conglomerate.items()) {
        if (data.contains("subject") && data["subject"].is_string()) {
            std::string name = data["subject"];
            bool matchFound = false;

            if (vcaaData.contains(code)) {
                matchFound = true;
            }
            else {
                for (const auto& [vcaaKey, vcaaValue] : vcaaData.items()) {
                    if (vcaaValue.contains("vcaa_subject") && vcaaValue["vcaa_subject"] == name) {
                        matchFound = true;
                        break;
                    }
                }
            }

            if (matchFound) {
                QString qtName = QString::fromStdString(name);
                QString qtCode = QString::fromStdString(code);
                ui->AddX->addItem(qtName, qtCode);
            }
        }
    }
}

double mainwindow::calculateSingleRowRaw(const ActiveRow& row) {
    double weightedAggregatePercent = 0.0;
    for (size_t i = 0; i < row.spinBoxes.size(); ++i) {
        weightedAggregatePercent += row.spinBoxes[i]->value() * row.weights[i];
    }
    return weightedAggregatePercent / 2.0;
}

void mainwindow::globalRecalculateAggregate() {
    if (m_blockSignals) return;
    m_blockSignals = true;

    struct SubjectScore {
        std::string code;
        double scaled;
        double raw;
        size_t rowIndex;
    };

    std::vector<SubjectScore> allScores;

    for (size_t idx = 0; idx < m_activeRows.size(); ++idx) {
        auto& row = m_activeRows[idx];
        double rawStudyScore = calculateSingleRowRaw(row);
        double scaledScore = 0.0;

        if (row.hasPrediction && row.benchmarks.size() == 7) {
            if (rawStudyScore <= 20.0) {
                double slope = (row.benchmarks[1] - row.benchmarks[0]) / 5.0;
                scaledScore = std::max(0.0, row.benchmarks[0] + slope * (rawStudyScore - 20.0));
            } else if (rawStudyScore >= 50.0) {
                scaledScore = row.benchmarks[6];
            } else {
                double baseRaw = 20.0;
                for (size_t i = 0; i < 6; ++i) {
                    double nextRaw = baseRaw + 5.0;
                    if (rawStudyScore >= baseRaw && rawStudyScore <= nextRaw) {
                        double fraction = (rawStudyScore - baseRaw) / 5.0;
                        scaledScore = row.benchmarks[i] + fraction * (row.benchmarks[i+1] - row.benchmarks[i]);
                        break;
                    }
                    baseRaw = nextRaw;
                }
            }

            allScores.push_back({row.subjectCode, scaledScore, rawStudyScore, idx});
        } else {
            row.scoreDisplayLabel->setText(QString("Raw:    %1\nScaled: N/A").arg(rawStudyScore, 0, 'f', 1));
        }
    }

    double totalAggregate = 0.0;

    if (!allScores.empty()) {
        auto isEnglishSubject = [](const std::string& code) {
            return (code.rfind("EN", 0) == 0 || code.rfind("EL", 0) == 0 || code.rfind("EG", 0) == 0);
        };

        std::vector<SubjectScore> englishSubjects;
        std::vector<SubjectScore> remainingSubjects;

        for (const auto& s : allScores) {
            if (isEnglishSubject(s.code)) {
                englishSubjects.push_back(s);
            } else {
                remainingSubjects.push_back(s);
            }
        }

        std::sort(englishSubjects.begin(), englishSubjects.end(), [](const SubjectScore& a, const SubjectScore& b){
            return a.scaled > b.scaled;
        });

        SubjectScore primaryEnglish;
        bool hasEnglish = false;

        if (!englishSubjects.empty()) {
            primaryEnglish = englishSubjects[0];
            hasEnglish = true;
            for (size_t i = 1; i < englishSubjects.size(); ++i) {
                remainingSubjects.push_back(englishSubjects[i]);
            }
        }

        std::sort(remainingSubjects.begin(), remainingSubjects.end(), [&](const SubjectScore& a, const SubjectScore& b){
            if (std::abs(a.scaled - b.scaled) > 0.01) {
                return a.scaled > b.scaled;
            }
            return isEnglishSubject(a.code) && !isEnglishSubject(b.code);
        });

        std::vector<SubjectScore> top4;
        std::vector<SubjectScore> bottomIncrements;

        if (hasEnglish) {
            top4.push_back(primaryEnglish);
        }

        size_t remIdx = 0;
        while (top4.size() < 4 && remIdx < remainingSubjects.size()) {
            top4.push_back(remainingSubjects[remIdx]);
            remIdx++;
        }

        while (remIdx < remainingSubjects.size()) {
            bottomIncrements.push_back(remainingSubjects[remIdx]);
            remIdx++;
        }

        for (const auto& s : top4) {
            totalAggregate += s.scaled;
            m_activeRows[s.rowIndex].scoreDisplayLabel->setText(
                QString("Raw:    %1\nScaled: %2")
                    .arg(s.raw, 0, 'f', 1)
                    .arg(s.scaled, 0, 'f', 1)
                );
        }

        std::sort(bottomIncrements.begin(), bottomIncrements.end(), [](const SubjectScore& a, const SubjectScore& b){
            return a.scaled > b.scaled;
        });

        for (size_t i = 0; i < bottomIncrements.size(); ++i) {
            const auto& s = bottomIncrements[i];
            if (i < 2) {
                double contribution = s.scaled * 0.10;
                totalAggregate += contribution;

                m_activeRows[s.rowIndex].scoreDisplayLabel->setText(
                    QString("Raw:    %1\nScaled: %2\n(10%: +%3)")
                        .arg(s.raw, 0, 'f', 1)
                        .arg(s.scaled, 0, 'f', 1)
                        .arg(contribution, 0, 'f', 1)
                    );
            } else {
                m_activeRows[s.rowIndex].scoreDisplayLabel->setText(
                    QString("Raw:    %1\nScaled: %2\n(Excluded)")
                        .arg(s.raw, 0, 'f', 1)
                        .arg(s.scaled, 0, 'f', 1)
                    );
            }
        }
    }

    double matchingAtar = getAtarFromAggregate(totalAggregate);
    ui->NumAtarRes->setValue(matchingAtar);

    m_blockSignals = false;
}

void mainwindow::on_redownloadDataBtn_clicked()
{
    QString exeDir = QCoreApplication::applicationDirPath();
    QString scriptPath = QDir(exeDir).filePath("tools/master.py");

    QProcess *pythonProcess = new QProcess(this);

#ifdef Q_OS_WIN
    QString program = "python";
#else
    QString program = "python3";
#endif

    QStringList arguments;
    arguments << scriptPath;

    connect(pythonProcess, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this, [this, pythonProcess](int exitCode, QProcess::ExitStatus status) {

                if (exitCode == 0 && status == QProcess::NormalExit) {
                    this->populateSubjects();
                    ui->AddClassBtn->setEnabled(true);
                } else {
                    qDebug() << "Python pipeline failed. Error output:" << pythonProcess->readAllStandardError();
                }
                pythonProcess->deleteLater();
            });

    pythonProcess->start(program, arguments);
}

void mainwindow::on_AddClassBtn_clicked()
{
    QString selectedCode = ui->AddX->currentData().toString();
    QString selectedName = ui->AddX->currentText();

    if (selectedCode.isEmpty() || selectedName.isEmpty()) {
        QMessageBox::warning(this, "Selection Error", "Please select a valid subject first.");
        return;
    }

    if (!addSubjectRow(selectedCode.toStdString(), selectedName.toStdString())) {
        QMessageBox::warning(this, "Data Error", "Selected subject does not exist in VCAA summary registry.");
    }
}

bool mainwindow::addSubjectRow(const std::string& codeStr, const std::string& nameStr, const std::vector<int>& savedScores)
{
    std::ifstream file("vcaa_summary.json");
    if (!file.is_open()) return false;

    json vcaaData;
    try { file >> vcaaData; } catch (...) { return false; }

    json subjectInfo;
    bool found = false;

    if (vcaaData.contains(codeStr)) {
        subjectInfo = vcaaData[codeStr];
        found = true;
    } else {
        for (const auto& [key, value] : vcaaData.items()) {
            if ((value.contains("vcaa_subject") && value["vcaa_subject"] == nameStr) ||
                (value.contains("vcaa_code") && value["vcaa_code"] == codeStr)) {
                subjectInfo = value;
                found = true;
                break;
            }
        }
    }

    if (!found) return false;

    std::ifstream fileConglomerate("scaling_conglomerate.json");
    json conglomerate;
    Predictor::ScalingPrediction prediction;
    bool hasPrediction = false;

    if (fileConglomerate.is_open() && (fileConglomerate >> conglomerate)) {
        Predictor predictor(conglomerate);

        if (predictor.predict_subject(codeStr, 2026, prediction)) {
            hasPrediction = true;
        } else {
            std::string lowerCode = codeStr;
            std::string lowerName = nameStr;
            std::transform(lowerCode.begin(), lowerCode.end(), lowerCode.begin(), ::tolower);
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

            for (const auto& [conglomKey, conglomValue] : conglomerate.items()) {
                std::string lowerKey = conglomKey;
                std::transform(lowerKey.begin(), lowerKey.end(), lowerKey.begin(), ::tolower);

                bool keyMatch = (lowerCode == lowerKey);

                if (!keyMatch && conglomValue.contains("subject") && conglomValue["subject"].is_string()) {
                    std::string subjectName = conglomValue["subject"];
                    std::transform(subjectName.begin(), subjectName.end(), subjectName.begin(), ::tolower);

                    if (lowerName == subjectName || lowerName.find(subjectName) != std::string::npos) {
                        keyMatch = true;
                    }
                }

                if (keyMatch) {
                    bool isTargetSpecialist = (lowerName.find("special") != std::string::npos || lowerCode == "ns");
                    bool isTargetGeneral = (lowerName.find("general") != std::string::npos || lowerCode == "nf");

                    std::string conglomSubName = conglomValue.value("subject", "");
                    std::transform(conglomSubName.begin(), conglomSubName.end(), conglomSubName.begin(), ::tolower);

                    bool isKeySpecialist = (lowerKey == "ns" || conglomSubName.find("special") != std::string::npos);
                    bool isKeyGeneral = (lowerKey == "nf" || conglomSubName.find("general") != std::string::npos);

                    if (isTargetSpecialist && isKeyGeneral) continue;
                    if (isTargetGeneral && isKeySpecialist) continue;

                    if (predictor.predict_subject(conglomKey, 2026, prediction)) {
                        hasPrediction = true;
                        break;
                    }
                }
            }
        }
    }

    QHBoxLayout *rowLayout = new QHBoxLayout();
    std::vector<QWidget*> rowWidgets;
    std::vector<QLayout*> rowLayouts;

    std::string subjectName = subjectInfo.value("vcaa_subject", "Unknown Subject");
    QLabel *classLabel = new QLabel(QString::fromStdString(subjectName), this);
    classLabel->setStyleSheet("font-weight: bold;");
    classLabel->setMinimumWidth(160);
    classLabel->setWordWrap(true);
    rowLayout->addWidget(classLabel);
    rowWidgets.push_back(classLabel);

    struct AssessmentInput {
        QSpinBox* spinBox;
        double weight;
    };
    std::vector<AssessmentInput> dynamicInputs;

    if (subjectInfo.contains("assessments") && subjectInfo["assessments"].is_array()) {
        size_t spinBoxIndex = 0;
        for (const auto& assessment : subjectInfo["assessments"]) {
            std::string typeStr = assessment.value("type", "Assessment Block");
            int contribution = assessment.value("contribution_percent", 0);

            QVBoxLayout *gaLayout = new QVBoxLayout();
            rowLayouts.push_back(gaLayout);

            QString labelText = QString::fromStdString(typeStr) + QString(" (%1%)").arg(contribution);
            QLabel *gaLabel = new QLabel(labelText, this);
            gaLabel->setWordWrap(true);
            gaLabel->setMaximumWidth(150);
            rowWidgets.push_back(gaLabel);

            QSpinBox *gaScore = new QSpinBox(this);
            gaScore->setRange(0, 100);

            if (spinBoxIndex < savedScores.size()) {
                gaScore->setValue(savedScores[spinBoxIndex]);
            }
            spinBoxIndex++;

            rowWidgets.push_back(gaScore);

            gaLayout->addWidget(gaLabel);
            gaLayout->addWidget(gaScore);
            rowLayout->addLayout(gaLayout);

            dynamicInputs.push_back({gaScore, contribution / 100.0});
        }
    }

    QVBoxLayout *scoreLayout = new QVBoxLayout();
    rowLayouts.push_back(scoreLayout);

    QLabel *scoreTitleLabel = new QLabel("Study Score", this);
    scoreTitleLabel->setStyleSheet("font-weight: bold; color: #eb4034;");
    rowWidgets.push_back(scoreTitleLabel);

    QLabel *scoreDisplayLabel = new QLabel("Raw: --\nScaled: --", this);
    scoreDisplayLabel->setMinimumWidth(100);
    scoreDisplayLabel->setStyleSheet("font-family: monospace; font-size: 12px;");
    rowWidgets.push_back(scoreDisplayLabel);

    scoreLayout->addWidget(scoreTitleLabel);
    scoreLayout->addWidget(scoreDisplayLabel);
    rowLayout->addLayout(scoreLayout);

    QPushButton *deleteBtn = new QPushButton(this);
    deleteBtn->setToolTip("Remove Subject");
    deleteBtn->setFixedWidth(35);
    deleteBtn->setCursor(Qt::PointingHandCursor);
    deleteBtn->setIcon(this->style()->standardIcon(QStyle::SP_TrashIcon));

    rowLayout->addWidget(deleteBtn);
    rowWidgets.push_back(deleteBtn);

    QFrame *lineSeparator = new QFrame(this);
    lineSeparator->setFrameShape(QFrame::HLine);
    lineSeparator->setFrameShadow(QFrame::Sunken);
    lineSeparator->setStyleSheet("color: #dcdcdc; margin: 6px 0;");
    rowWidgets.push_back(lineSeparator);

    std::vector<double> benchmarks = hasPrediction ? prediction.scaled_benchmarks : std::vector<double>();

    ActiveRow newRowState;
    newRowState.subjectCode = codeStr;
    newRowState.hasPrediction = hasPrediction;
    newRowState.benchmarks = benchmarks;
    newRowState.scoreDisplayLabel = scoreDisplayLabel;

    for (const auto& input : dynamicInputs) {
        newRowState.spinBoxes.push_back(input.spinBox);
        newRowState.weights.push_back(input.weight);
    }

    m_activeRows.push_back(newRowState);

    for (const auto& input : dynamicInputs) {
        connect(input.spinBox, qOverload<int>(&QSpinBox::valueChanged), this, [this]() {
            globalRecalculateAggregate();
        });
    }

    connect(deleteBtn, &QPushButton::clicked, this, [this, rowLayout, rowWidgets, rowLayouts, codeStr]() {
        m_activeRows.erase(
            std::remove_if(m_activeRows.begin(), m_activeRows.end(), [&](const ActiveRow& r) {
                return r.subjectCode == codeStr;
            }), m_activeRows.end()
            );

        for (QWidget* widget : rowWidgets) widget->deleteLater();
        for (QLayout* layout : rowLayouts) layout->deleteLater();
        rowLayout->deleteLater();

        QTimer::singleShot(50, this, [this]() { globalRecalculateAggregate(); });
    });

    globalRecalculateAggregate();

    ui->SubjectLayout->addLayout(rowLayout);
    ui->SubjectLayout->addWidget(lineSeparator);

    return true;
}

void mainwindow::on_NumAtarRes_valueChanged(double arg1)
{
    if (m_blockSignals || m_activeRows.empty()) return;
    m_blockSignals = true;

    auto isEnglishSubject = [](const std::string& code) {
        return (code.rfind("EN", 0) == 0 || code.rfind("EL", 0) == 0 || code.rfind("EG", 0) == 0);
    };

    std::vector<size_t> englishIndices;
    std::vector<size_t> remainingIndices;

    for (size_t i = 0; i < m_activeRows.size(); ++i) {
        if (isEnglishSubject(m_activeRows[i].subjectCode)) {
            englishIndices.push_back(i);
        } else {
            remainingIndices.push_back(i);
        }
    }

    auto getSubjectStrength = [this](size_t idx) {
        const auto& row = m_activeRows[idx];
        return (row.hasPrediction && row.benchmarks.size() == 7) ? row.benchmarks[2] : 30.0;
    };

    auto strengthComparator = [&](size_t a, size_t b) { return getSubjectStrength(a) > getSubjectStrength(b); };
    std::sort(englishIndices.begin(), englishIndices.end(), strengthComparator);
    std::sort(remainingIndices.begin(), remainingIndices.end(), strengthComparator);

    std::vector<size_t> top4Indices;
    std::vector<size_t> incrementIndices;

    if (!englishIndices.empty()) {
        top4Indices.push_back(englishIndices[0]);
        for (size_t i = 1; i < englishIndices.size(); ++i) {
            remainingIndices.push_back(englishIndices[i]);
        }
        std::sort(remainingIndices.begin(), remainingIndices.end(), strengthComparator);
    }

    size_t remIdx = 0;
    while (top4Indices.size() < 4 && remIdx < remainingIndices.size()) {
        top4Indices.push_back(remainingIndices[remIdx++]);
    }
    while (incrementIndices.size() < 2 && remIdx < remainingIndices.size()) {
        incrementIndices.push_back(remainingIndices[remIdx++]);
    }

    double maxPossibleAggregate = 0.0;
    for (size_t idx : top4Indices) {
        maxPossibleAggregate += (m_activeRows[idx].hasPrediction && !m_activeRows[idx].benchmarks.empty()) ? m_activeRows[idx].benchmarks.back() : 50.0;
    }
    for (size_t idx : incrementIndices) {
        maxPossibleAggregate += ((m_activeRows[idx].hasPrediction && !m_activeRows[idx].benchmarks.empty()) ? m_activeRows[idx].benchmarks.back() : 50.0) * 0.10;
    }

    double targetAggregate = getAggregateFromAtar(arg1);
    if (targetAggregate > maxPossibleAggregate) {
        targetAggregate = maxPossibleAggregate;
        ui->NumAtarRes->setValue(getAtarFromAggregate(maxPossibleAggregate));
    }

    struct RowTargetMeta {
        size_t rowIndex;
        double weight;
        double currentRaw;
        double currentScaled;
    };

    auto scaledToRaw = [](const auto& row, double scaled) {
        if (!row.hasPrediction || row.benchmarks.size() != 7) return scaled;
        if (scaled <= row.benchmarks[0]) {
            return (row.benchmarks[0] > 0.0) ? (scaled / row.benchmarks[0]) * 20.0 : 0.0;
        }
        if (scaled >= row.benchmarks[6]) return 50.0;

        double baseRaw = 20.0;
        for (size_t i = 0; i < 6; ++i) {
            if (scaled >= row.benchmarks[i] && scaled <= row.benchmarks[i+1]) {
                double rangeScaled = row.benchmarks[i+1] - row.benchmarks[i];
                double fraction = (rangeScaled > 0.0) ? (scaled - row.benchmarks[i]) / rangeScaled : 0.0;
                return baseRaw + fraction * 5.0;
            }
            baseRaw += 5.0;
        }
        return 50.0;
    };

    auto applyToSpinBoxes = [](auto& row, double targetRaw) {
        double targetWeightedAggregate = targetRaw * 2.0;
        double curWeighted = 0.0;
        double zeroWeights = 0.0;
        std::vector<size_t> zIdx, nzIdx;

        for (size_t i = 0; i < row.spinBoxes.size(); ++i) {
            if (row.spinBoxes[i]->value() == 0) {
                zeroWeights += row.weights[i];
                zIdx.push_back(i);
            } else {
                curWeighted += row.spinBoxes[i]->value() * row.weights[i];
                nzIdx.push_back(i);
            }
        }

        if (zIdx.empty()) {
            double factor = (curWeighted > 0.01) ? (targetWeightedAggregate / curWeighted) : 1.0;
            for (auto i : nzIdx) {
                row.spinBoxes[i]->setValue(std::clamp(static_cast<int>(std::round(row.spinBoxes[i]->value() * factor)), 0, 100));
            }
        } else {
            double def = targetWeightedAggregate - curWeighted;
            double val = (zeroWeights > 0.0 && def > 0.0) ? (def / zeroWeights) : 0.0;
            for (auto i : zIdx) {
                row.spinBoxes[i]->setValue(std::clamp(static_cast<int>(std::round(val)), 0, 100));
            }
        }
    };

    std::vector<RowTargetMeta> targetRows;
    auto gatherMeta = [&](size_t idx, double weight) {
        auto& row = m_activeRows[idx];
        double curRaw = calculateSingleRowRaw(row);
        double curScaled = curRaw;

        if (row.hasPrediction && row.benchmarks.size() == 7) {
            if (curRaw <= 20.0) {
                double slope = (row.benchmarks[1] - row.benchmarks[0]) / 5.0;
                curScaled = std::max(0.0, row.benchmarks[0] + slope * (curRaw - 20.0));
            } else if (curRaw >= 50.0) {
                curScaled = row.benchmarks[6];
            } else {
                double baseRaw = 20.0;
                for (size_t i = 0; i < 6; ++i) {
                    double nextRaw = baseRaw + 5.0;
                    if (curRaw >= baseRaw && curRaw <= nextRaw) {
                        double fraction = (curRaw - baseRaw) / 5.0;
                        curScaled = row.benchmarks[i] + fraction * (row.benchmarks[i+1] - row.benchmarks[i]);
                        break;
                    }
                    baseRaw = nextRaw;
                }
            }
        }
        targetRows.push_back({idx, weight, curRaw, curScaled});
    };

    for (size_t idx : top4Indices) gatherMeta(idx, 1.0);
    for (size_t idx : incrementIndices) gatherMeta(idx, 0.10);

    double remainingAggregateToDistribute = targetAggregate;
    double totalActiveWeightUnits = 0.0;
    std::vector<RowTargetMeta*> dynamicRows;

    for (auto& r : targetRows) {
        if (r.currentScaled * r.weight >= (targetAggregate / targetRows.size())) {
            remainingAggregateToDistribute -= (r.currentScaled * r.weight);
        } else {
            totalActiveWeightUnits += r.weight;
            dynamicRows.push_back(&r);
        }
    }

    if (dynamicRows.empty() && !targetRows.empty()) {
        double totalAllWeights = 0.0;
        for (auto& r : targetRows) totalAllWeights += r.weight;
        double uniformScaledTarget = (totalAllWeights > 0.0) ? (targetAggregate / totalAllWeights) : 0.0;

        for (auto& r : targetRows) {
            auto& row = m_activeRows[r.rowIndex];
            double targetRaw = scaledToRaw(row, uniformScaledTarget);
            applyToSpinBoxes(row, targetRaw);

            if (r.weight == 1.0) {
                row.scoreDisplayLabel->setText(QString("Raw:    %1\nScaled: %2").arg(targetRaw, 0, 'f', 1).arg(uniformScaledTarget, 0, 'f', 1));
            } else {
                row.scoreDisplayLabel->setText(QString("Raw:    %1\nScaled: %2\n(10%: +%3)").arg(targetRaw, 0, 'f', 1).arg(uniformScaledTarget, 0, 'f', 1).arg(uniformScaledTarget * 0.1, 0, 'f', 1));
            }
        }
    }
    else {
        for (auto* rPtr : dynamicRows) {
            double assignedScaled = (totalActiveWeightUnits > 0.0) ? (remainingAggregateToDistribute / totalActiveWeightUnits) : 0.0;
            auto& row = m_activeRows[rPtr->rowIndex];
            double targetRaw = scaledToRaw(row, assignedScaled);
            applyToSpinBoxes(row, targetRaw);

            if (rPtr->weight == 1.0) {
                row.scoreDisplayLabel->setText(QString("Raw:    %1\nScaled: %2").arg(targetRaw, 0, 'f', 1).arg(assignedScaled, 0, 'f', 1));
            } else {
                row.scoreDisplayLabel->setText(QString("Raw:    %1\nScaled: %2\n(10%: +%3)").arg(targetRaw, 0, 'f', 1).arg(assignedScaled, 0, 'f', 1).arg(assignedScaled * 0.1, 0, 'f', 1));
            }
        }

        for (auto& r : targetRows) {
            bool isDynamic = false;
            for (auto* d : dynamicRows) { if (d->rowIndex == r.rowIndex) isDynamic = true; }
            if (!isDynamic) {
                auto& row = m_activeRows[r.rowIndex];
                if (r.weight == 1.0) {
                    row.scoreDisplayLabel->setText(QString("Raw:    %1\nScaled: %2").arg(r.currentRaw, 0, 'f', 1).arg(r.currentScaled, 0, 'f', 1));
                } else {
                    row.scoreDisplayLabel->setText(QString("Raw:    %1\nScaled: %2\n(10%: +%3)").arg(r.currentRaw, 0, 'f', 1).arg(r.currentScaled, 0, 'f', 1).arg(r.currentScaled * 0.1, 0, 'f', 1));
                }
            }
        }
    }

    for (size_t i = remIdx; i < remainingIndices.size(); ++i) {
        auto& row = m_activeRows[remainingIndices[i]];
        double rawVal = calculateSingleRowRaw(row);
        row.scoreDisplayLabel->setText(QString("Raw:    %1\nScaled: --\n(Excluded)").arg(rawVal, 0, 'f', 1));
    }
    m_blockSignals = false;
    globalRecalculateAggregate();
}

void mainwindow::loadAtarTable() {
    m_aggregateToAtarMap.clear();
    std::ifstream file("atar_from_aggregate.json");
    if (!file.is_open()) return;

    nlohmann::json j;
    try {
        file >> j;
        for (auto& [aggStr, atarVal] : j.items()) {
            m_aggregateToAtarMap[std::stod(aggStr)] = atarVal.get<double>();
        }
    } catch (...) {}
}

double mainwindow::getAtarFromAggregate(double aggregate) {
    if (m_aggregateToAtarMap.empty()) return 0.0;

    if (aggregate >= m_aggregateToAtarMap.begin()->first) return 99.95;
    if (aggregate <= m_aggregateToAtarMap.rbegin()->first) return 30.00;

    auto it = m_aggregateToAtarMap.lower_bound(aggregate);
    if (it == m_aggregateToAtarMap.end()) return m_aggregateToAtarMap.rbegin()->second;
    if (it == m_aggregateToAtarMap.begin()) return m_aggregateToAtarMap.begin()->second;

    auto prevIt = std::prev(it);
    double x1 = it->first, y1 = it->second;
    double x2 = prevIt->first, y2 = prevIt->second;

    return y1 + ((aggregate - x1) / (x2 - x1)) * (y2 - y1);
}

double mainwindow::getAggregateFromAtar(double targetAtar) {
    if (m_aggregateToAtarMap.empty()) return 0.0;

    if (targetAtar >= 99.95) return m_aggregateToAtarMap.begin()->first;

    for (auto it = m_aggregateToAtarMap.begin(); it != std::prev(m_aggregateToAtarMap.end()); ++it) {
        auto nextIt = std::next(it);
        double agg1 = it->first, atar1 = it->second;
        double agg2 = nextIt->first, atar2 = nextIt->second;

        if (targetAtar <= atar1 && targetAtar >= atar2) {
            double fraction = (targetAtar - atar2) / (atar1 - atar2);
            return agg2 + fraction * (agg1 - agg2);
        }
    }
    return 0.0;
}

void mainwindow::on_saveBtn_clicked()
{
    if (m_activeRows.empty()) {
        QMessageBox::information(this, "Save Profile", "There are no subjects to save.");
        return;
    }

    QString fileName = QFileDialog::getSaveFileName(this, "Save Subject Profile", "", "JSON Files (*.json)");
    if (fileName.isEmpty()) return;

    nlohmann::json saveJson = nlohmann::json::array();

    for (const auto& row : m_activeRows) {
        nlohmann::json rowJson;

        rowJson["subjectCode"] = row.subjectCode;

        nlohmann::json scoresArray = nlohmann::json::array();
        for (QSpinBox* sb : row.spinBoxes) {
            scoresArray.push_back(sb->value());
        }
        rowJson["scores"] = scoresArray;
        saveJson.push_back(rowJson);
    }

    std::ofstream file(fileName.toStdString());
    if (file.is_open()) {
        file << saveJson.dump(4);
        QMessageBox::information(this, "Success", "Profile saved successfully!");
    } else {
        QMessageBox::critical(this, "Save Error", "Could not open file for writing.");
    }
}

void clearLayout(QLayout* layout) {
    if (!layout) return;

    QLayoutItem* item;
    while ((item = layout->takeAt(0)) != nullptr) {
        if (QWidget* widget = item->widget()) {
            widget->setParent(nullptr);
            widget->deleteLater();
        } else if (QLayout* childLayout = item->layout()) {
            clearLayout(childLayout);
        }
        delete item;
    }
}

void mainwindow::on_pushBtn_clicked()
{
    QString fileName = QFileDialog::getOpenFileName(this, "Load Subject Profile", "", "JSON Files (*.json)");
    if (fileName.isEmpty()) return;

    std::ifstream file(fileName.toStdString());
    if (!file.is_open()) {
        QMessageBox::critical(this, "Load Error", "Could not open file.");
        return;
    }

    nlohmann::json loadJson;
    try {
        file >> loadJson;
    } catch (...) {
        QMessageBox::critical(this, "Parser Error", "File is corrupted or not a valid profile JSON.");
        return;
    }

    if (!loadJson.is_array()) {
        QMessageBox::critical(this, "Format Error", "Invalid structure inside save profile.");
        return;
    }

    m_blockSignals = true;

    clearLayout(ui->SubjectLayout);
    m_activeRows.clear();

    std::ifstream registryFile("vcaa_summary.json");
    nlohmann::json vcaaRegistry;
    if (registryFile.is_open()) {
        try {
            registryFile >> vcaaRegistry;
        } catch(...) {}
    }

    for (const auto& rowJson : loadJson) {
        if (!rowJson.contains("subjectCode")) {
            continue;
        }

        std::string codeStr = rowJson["subjectCode"].get<std::string>();

        if (codeStr == "EF") {
            codeStr = "EN01";
        }

        std::string resolvedName = "";
        if (vcaaRegistry.contains(codeStr)) {
            resolvedName = vcaaRegistry[codeStr].value("vcaa_subject", "");
        } else {
            for (const auto& [key, value] : vcaaRegistry.items()) {
                if (value.value("vcaa_code", "") == codeStr) {
                    resolvedName = value.value("vcaa_subject", "");
                    break;
                }
            }
        }

        if (resolvedName.empty()) {
            resolvedName = "Loaded Subject";
        }

        addSubjectRow(codeStr, resolvedName, rowJson.value("scores", std::vector<int>()));
    }

    m_blockSignals = false;
    globalRecalculateAggregate();
}
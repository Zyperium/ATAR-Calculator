#pragma once
#include <QMainWindow>
#include <vector>
#include <QSpinBox>
#include <QLabel>
#include <map>
#include "nlohmann/json.hpp"

QT_BEGIN_NAMESPACE
namespace Ui { class mainwindow; }
QT_END_NAMESPACE

class mainwindow : public QMainWindow
{
    Q_OBJECT

public:
    mainwindow(QWidget *parent = nullptr);
    ~mainwindow();

private slots:
    void on_AddClassBtn_clicked();
    void on_redownloadDataBtn_clicked();
    void on_NumAtarRes_valueChanged(double arg1);

    void on_pushBtn_clicked();

    void on_saveBtn_clicked();

private:
    Ui::mainwindow *ui;
    void populateSubjects();
    bool addSubjectRow(const std::string& codeStr, const std::string& nameStr, const std::vector<int>& savedScores = {});

    struct ActiveRow {
        std::string subjectCode;
        std::vector<QSpinBox*> spinBoxes;
        std::vector<double> weights;
        QLabel* scoreDisplayLabel;
        std::vector<double> benchmarks;
        bool hasPrediction;
    };

    std::vector<ActiveRow> m_activeRows;
    std::map<double, double, std::greater<double>> m_aggregateToAtarMap;

    void loadAtarTable();
    void globalRecalculateAggregate();
    double calculateSingleRowRaw(const ActiveRow& row);
    double getAtarFromAggregate(double aggregate);
    double getAggregateFromAtar(double targetAtar);
    bool m_blockSignals = false; // Prevents recursive calculation loops
};

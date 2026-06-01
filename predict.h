#ifndef PREDICT_H
#define PREDICT_H

#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include "nlohmann/json.hpp"

using json = nlohmann::json;

class Predictor {
public:
    struct ScalingPrediction {
        std::string subject_name;
        double mean;
        double std_dev;
        std::vector<double> scaled_benchmarks;
    };

    explicit Predictor(const json& master_db) : db(master_db) {}

    bool predict_subject(const std::string& subject_code, int target_year, ScalingPrediction& out_result) const {
        if (!db.contains(subject_code)) {
            return false;
        }

        const auto& subject_data = db[subject_code];
        out_result.subject_name = subject_data.value("subject", "Unknown Subject");

        std::vector<int> years;
        std::vector<double> means;
        std::vector<double> std_devs;
        std::vector<std::vector<double>> benchmarks_history(7);

        for (const auto& el : subject_data.items()) {
            std::string key = el.key();
            if (key == "subject") continue;

            years.push_back(std::stoi(key));
            means.push_back(el.value()["mean"].get<double>());
            std_devs.push_back(el.value()["std_dev"].get<double>());

            auto bench = el.value()["scaled_benchmarks"].get<std::vector<double>>();
            for (size_t i = 0; i < 7; ++i) {
                benchmarks_history[i].push_back(bench[i]);
            }
        }

        if (years.empty()) return false;

        out_result.mean = calculate_regression(years, means, target_year);
        out_result.std_dev = calculate_regression(years, std_devs, target_year);

        out_result.scaled_benchmarks.clear();
        for (size_t i = 0; i < 7; ++i) {
            double pred_bench = calculate_regression(years, benchmarks_history[i], target_year);

            pred_bench = std::max(0.0, std::min(55.0, pred_bench));
            out_result.scaled_benchmarks.push_back(pred_bench);
        }

        return true;
    }

private:
    json db;

    double calculate_regression(const std::vector<int>& x_vec, const std::vector<double>& y_vec, int target_x) const {
        double sum_w = 0.0, sum_w_x = 0.0, sum_w_y = 0.0, sum_w_x2 = 0.0, sum_w_xy = 0.0;

        for (size_t i = 0; i < x_vec.size(); ++i) {
            double weight = static_cast<double>(i + 1);
            double x = x_vec[i];
            double y = y_vec[i];

            sum_w += weight;
            sum_w_x += weight * x;
            sum_w_y += weight * y;
            sum_w_x2 += weight * x * x;
            sum_w_xy += weight * x * y;
        }

        double denominator = (sum_w * sum_w_x2 - sum_w_x * sum_w_x);
        if (std::abs(denominator) < 1e-6) return y_vec.back();

        double m = (sum_w * sum_w_xy - sum_w_x * sum_w_y) / denominator;
        double b = (sum_w_y - m * sum_w_x) / sum_w;

        return m * target_x + b;
    }
};

#endif // PREDICT_H

#ifdef _WIN32
#define _WIN32_WINNT 0x0A00
#endif

#include "httplib.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>
#include <mutex>
#include <unordered_map>

// Thread-safe session & rate-limit store
std::mutex auth_mutex;
std::unordered_map<std::string, std::string> active_sessions; // token -> email
std::unordered_map<std::string, int> ip_request_counts;

// Generate a 32-character cryptographic hex session token
std::string generate_session_token() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;
    std::ostringstream oss;
    oss << std::hex << dis(gen) << dis(gen);
    return oss.str();
}

// Apply strict HTTP security headers to every response
void set_security_headers(httplib::Response& res) {
    res.set_header("X-Content-Type-Options", "nosniff");
    res.set_header("X-Frame-Options", "DENY");
    res.set_header("Referrer-Policy", "strict-origin-when-cross-origin");
    res.set_header("Permissions-Policy", "geolocation=(), microphone=(), camera=()");
}

struct ColumnStats {
    std::string name;
    size_t count = 0;
    double mean = 0.0;
    double std_dev = 0.0;
    double min_val = 0.0;
    double median = 0.0;
    double max_val = 0.0;
    std::vector<double> values;
};

// Escape strings safely for JSON
std::string escape_json(const std::string& s) {
    std::ostringstream o;
    for (char c : s) {
        if (c == '"') o << "\\\"";
        else if (c == '\\') o << "\\\\";
        else if (c == '\n') o << "\\n";
        else if (c == '\r') continue;
        else o << c;
    }
    return o.str();
}

// Built-in Synthetic E-Commerce Dataset Generator (Solves having no SQL data)
std::string generate_sample_csv(int rows = 500) {
    std::mt19937 gen(42);
    std::uniform_real_distribution<> ad_dist(100.0, 5000.0);
    std::uniform_real_distribution<> disc_dist(0.0, 0.30);
    std::normal_distribution<> noise(0.0, 150.0);

    std::ostringstream csv;
    csv << "ad_spend,website_visits,discount_rate,conversion_rate,daily_revenue\n";
    csv << std::fixed << std::setprecision(2);

    for (int i = 0; i < rows; ++i) {
        double ad_spend = ad_dist(gen);
        double visits = ad_spend * 2.4 + std::abs(noise(gen)) * 3.0;
        double discount = disc_dist(gen);
        double conv_rate = 0.02 + (discount * 0.08) + (noise(gen) / 20000.0);
        conv_rate = std::max(0.005, std::min(0.15, conv_rate));
        double revenue = (visits * conv_rate * 85.0) * (1.0 - discount) + noise(gen);
        revenue = std::max(50.0, revenue);

        csv << ad_spend << "," << std::round(visits) << ","
            << discount << "," << conv_rate << "," << revenue << "\n";
    }
    return csv.str();
}

// Pandas-style .describe() engine + JSON serializer
std::string analyze_csv_to_json(const std::string& csv_text) {
    std::istringstream stream(csv_text);
    std::string line;

    if (!std::getline(stream, line)) {
        return R"({"error":"Empty CSV data"})";
    }

    // Parse headers
    std::vector<std::string> headers;
    std::stringstream header_ss(line);
    std::string col;
    while (std::getline(header_ss, col, ',')) {
        col.erase(std::remove(col.begin(), col.end(), '\r'), col.end());
        headers.push_back(col);
    }

    size_t num_cols = headers.size();
    std::vector<std::vector<double>> numeric_cols(num_cols);
    std::vector<std::vector<std::string>> preview_rows;
    size_t total_rows = 0;

    // Parse rows
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        std::stringstream row_ss(line);
        std::string cell;
        std::vector<std::string> row_cells;
        size_t col_idx = 0;

        while (std::getline(row_ss, cell, ',') && col_idx < num_cols) {
            cell.erase(std::remove(cell.begin(), cell.end(), '\r'), cell.end());
            row_cells.push_back(cell);
            try {
                size_t pos = 0;
                double val = std::stod(cell, &pos);
                if (pos == cell.size()) {
                    numeric_cols[col_idx].push_back(val);
                }
            } catch (...) {
                // Non-numeric cell skipped for numeric stats
            }
            col_idx++;
        }
        if (preview_rows.size() < 8) {
            preview_rows.push_back(row_cells);
        }
        total_rows++;
    }

    // Compute statistics
    std::vector<ColumnStats> stats_list;
    for (size_t i = 0; i < num_cols; ++i) {
        auto& vals = numeric_cols[i];
        if (vals.empty()) continue;

        ColumnStats st;
        st.name = headers[i];
        st.count = vals.size();
        st.values = vals;

        double sum = std::accumulate(vals.begin(), vals.end(), 0.0);
        st.mean = sum / st.count;

        double sq_sum = 0.0;
        for (double v : vals) sq_sum += (v - st.mean) * (v - st.mean);
        st.std_dev = std::sqrt(sq_sum / st.count);

        std::vector<double> sorted = vals;
        std::sort(sorted.begin(), sorted.end());
        st.min_val = sorted.front();
        st.max_val = sorted.back();
        st.median = (st.count % 2 == 0)
            ? (sorted[st.count / 2 - 1] + sorted[st.count / 2]) / 2.0
            : sorted[st.count / 2];

        stats_list.push_back(st);
    }

    // Build JSON response
    std::ostringstream json;
    json << std::fixed << std::setprecision(4);
    json << "{\"total_rows\":" << total_rows << ",\"headers\":[";
    for (size_t i = 0; i < headers.size(); ++i) {
        json << "\"" << escape_json(headers[i]) << "\"" << (i + 1 < headers.size() ? "," : "");
    }
    json << "],\"stats\":[";
    for (size_t i = 0; i < stats_list.size(); ++i) {
        const auto& s = stats_list[i];
        json << "{\"name\":\"" << escape_json(s.name) << "\","
             << "\"count\":" << s.count << ","
             << "\"mean\":" << s.mean << ","
             << "\"std\":" << s.std_dev << ","
             << "\"min\":" << s.min_val << ","
             << "\"median\":" << s.median << ","
             << "\"max\":" << s.max_val << ","
             << "\"sample\":[";
        size_t limit = std::min<size_t>(100, s.values.size());
        for (size_t j = 0; j < limit; ++j) {
            json << s.values[j] << (j + 1 < limit ? "," : "");
        }
        json << "]}" << (i + 1 < stats_list.size() ? "," : "");
    }
    json << "],\"preview\":[";
    for (size_t r = 0; r < preview_rows.size(); ++r) {
        json << "[";
        for (size_t c = 0; c < preview_rows[r].size(); ++c) {
            json << "\"" << escape_json(preview_rows[r][c]) << "\""
                 << (c + 1 < preview_rows[r].size() ? "," : "");
        }
        json << "]" << (r + 1 < preview_rows.size() ? "," : "");
    }
    json << "]}";
    return json.str();
}

int main() {
    httplib::Server svr;

    // Prevent memory exhaustion and keep the service responsive.
    svr.set_payload_max_length(10 * 1024 * 1024);
    svr.set_read_timeout(5, 0);
    svr.set_write_timeout(5, 0);

    // Global middleware for security headers and rate limiting.
    svr.set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res) {
        set_security_headers(res);

        std::lock_guard<std::mutex> lock(auth_mutex);
        if (++ip_request_counts[req.remote_addr] > 500) {
            res.status = 429;
            res.set_content(R"({"error":"Rate limit exceeded"})", "application/json");
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    // Serve the frontend from the project root so index.html is available.
    svr.set_mount_point("/", ".");

    // Login endpoint: email|password -> session token
    svr.Post("/api/login", [](const httplib::Request& req, httplib::Response& res) {
        auto sep = req.body.find('|');
        if (sep == std::string::npos || req.body.size() > 256) {
            res.status = 400;
            res.set_content(R"({"error":"Invalid credentials format"})", "application/json");
            return;
        }

        std::string email = req.body.substr(0, sep);
        std::string pass = req.body.substr(sep + 1);

        if (pass.length() < 6) {
            res.status = 401;
            res.set_content(R"({"error":"Unauthorized"})", "application/json");
            return;
        }

        std::string token = generate_session_token();
        {
            std::lock_guard<std::mutex> lock(auth_mutex);
            active_sessions[token] = email;
        }

        res.set_content("{\"token\":\"" + token + "\",\"email\":\"" + escape_json(email) + "\"}", "application/json");
    });

    // Protected CSV analysis endpoint.
    svr.Post("/api/analyze", [](const httplib::Request& req, httplib::Response& res) {
        std::string auth_header = req.get_header_value("Authorization");
        std::string token = (auth_header.rfind("Bearer ", 0) == 0) ? auth_header.substr(7) : "";

        bool valid = false;
        {
            std::lock_guard<std::mutex> lock(auth_mutex);
            valid = !token.empty() && active_sessions.count(token) > 0;
        }

        if (!valid) {
            res.status = 401;
            res.set_content(R"({"error":"Unauthorized: Valid session token required"})", "application/json");
            return;
        }

        res.set_content(analyze_csv_to_json(req.body), "application/json");
    });

    // Unauthenticated sample dataset endpoint.
    svr.Get("/api/sample", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(analyze_csv_to_json(generate_sample_csv(1000)), "application/json");
    });

    int port = 8080;
    if (const char* env_p = std::getenv("PORT")) {
        port = std::stoi(env_p);
    }

    std::cout << "Hardened PulseML C++ Engine running on port " << port << "\n";
    svr.listen("0.0.0.0", port);
    return 0;
}

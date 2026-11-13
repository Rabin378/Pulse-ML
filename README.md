# PulseML Studio — Native C++17 Data Science & Analytics Engine

**PulseML** is a full-stack, zero-GIL data science and statistical profiling web application built on a compiled **ISO C++17** backend paired with an interactive hardware-inspired web dashboard.

Instead of relying on slow interpreted scripts or external SQL databases, PulseML ingests raw CSV datasets directly into contiguous C++ memory vectors, computes Pandas-style `.describe()` statistics in microseconds, and streams JSON tensors to the browser for real-time visualization.

---

## Key Features

- **Bare-Metal C++17 Compute Engine:** Multi-threaded HTTP REST server (`cpp-httplib`) engineered for sub-millisecond data profiling without Python's Global Interpreter Lock (GIL).
- **Zero-Copy CSV Ingestion:** Drag-and-drop any `.csv` spreadsheet to automatically detect numeric features and compute `count`, `mean`, `std_dev`, `min`, `median`, and `max`.
- **Built-In Synthetic Telemetry Generator:** Uses Mersenne Twister (`std::mt19937`) distributions to generate realistic e-commerce ad spend, traffic, conversion rates, and daily revenue datasets on demand.
- **Interactive Visualization Studio:** Multi-mode Chart.js canvas supporting **Line Trends**, **Bar Series**, and **10-Bin Frequency Histograms** with automated skewness and volatility (CV) diagnostics.
- **Hardened Security & Cloud Ready:** Includes payload size caps, request timeouts, security headers, non-root Docker execution, and one-click deployment to **Google Cloud Run**.

---

## Project Architecture

```text
pulse-ml/
├── Dockerfile              # Multi-core GCC 13 container build for Google Cloud Run
├── include/
│   └── httplib.h           # Single-header C++ HTTP/HTTPS server library
├── public/
│   ├── index.html          # Interactive dashboard, Chart.js studio & SEO metadata
│   ├── robots.txt          # Search engine crawler directives
│   └── sitemap.xml         # Google Search Console XML sitemap
└── src/
    └── main.cpp            # C++17 CSV parser, statistical engine & REST API

# Real-Time-Thermal-Gradient-Simulation-using-QNX-RTOS

A high-performance, real-time **2D heat diffusion simulator** built on **QNX Neutrino RTOS**, demonstrating advanced IPC (Inter-Process Communication), multi-threaded computation, and thermal visualization.

---

## 📌 Overview

This project simulates heat diffusion across a 128×128 grid using the finite-difference heat equation. It runs as a **client-server architecture** on QNX Neutrino, where:

- The **Server** computes thermal diffusion using a 4-thread worker pool with barrier synchronization
- The **Client** sends grid data, receives results, and renders ASCII visualizations and exports data
- Communication uses **QNX native IPC** (`MsgSendv` / `MsgReplyv`) with optional **async pulse notifications**

> Built as a systems programming project to explore QNX RTOS primitives: message passing, named channels, pthreads, barriers, and real-time scheduling.

---

## 🏗️ Architecture

```
┌─────────────────────────────────────────────────────┐
│                    CLIENT (client.c)                │
│                                                     │
│  Initialize Grid → Send via MsgSendv → Visualize   │
│  (128×128 float) ←── Receive results ──────────     │
│                                                     │
│  • ASCII heatmap rendering                          │
│  • CSV export for Python/MATLAB                     │
│  • Auto-generated MATLAB scripts                    │
└────────────────────┬────────────────────────────────┘
                     │ QNX IPC (name_open / MsgSendv)
                     ▼
┌─────────────────────────────────────────────────────┐
│                    SERVER (server.c)                │
│                                                     │
│  name_attach("thermal_sim_service_v2")              │
│                                                     │
│  ┌──────────────────────────────────────────────┐   │
│  │           Thread Pool (4 Workers)            │   │
│  │                                              │   │
│  │  Thread 0 │ Thread 1 │ Thread 2 │ Thread 3   │   │
│  │  Rows 0-31│Rows 32-63│Rows 64-95│Rows 96-127 │   │
│  │                                              │   │
│  │    pthread_barrier_t (synchronization)       │   │
│  └──────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────┘
```

---

## ✨ Features

- **QNX Native IPC** — Uses `name_attach`, `MsgSendv`, `MsgReplyv`, and `MsgRead` for zero-copy grid transfer
- **Multi-threaded Computation** — 4 POSIX worker threads partition the grid row-wise; barrier sync ensures correctness
- **Async Pulse Notifications** — Optional `SIGEV_PULSE` events notify client upon simulation completion
- **Real-time Statistics** — Per-step min/max/avg temperature tracking with mutex-protected global stats
- **ASCII Visualization** — Color-coded terminal heatmap (ANSI escape codes) with full 128×128 grid display
- **Data Export** — CSV export for every N steps + auto-generated MATLAB `.m` visualization scripts
- **Web Viewer** — `thermal_viewer.html` for interactive 2D heatmap, 3D surface, and contour plots (via Plotly.js)

---

## 🔬 Heat Diffusion Model

The simulation uses the **explicit finite-difference method** for the 2D heat equation:

```
T_new[i][j] = T_old[i][j] + α × (T[i-1][j] + T[i+1][j] + T[i][j-1] + T[i][j+1] - 4 × T[i][j])
```

Where `α` (conductivity) controls the rate of diffusion. Boundary cells are held fixed.

**Initial Condition:** A single 100°C heat source is placed at the center of the grid (simulating a CPU die), and heat diffuses outward over N iterations.

---

## 📁 Project Structure

```
qnx-thermal-simulator/
│
├── src/
│   ├── server.c           # QNX server: IPC handler + thread pool + simulation engine
│   ├── client.c           # QNX client: grid init, IPC send/recv, visualization, export
│   └── thermal_ipc.h      # Shared header: grid constants, pulse codes, message struct
│
├── tools/
│   └── thermal_viewer.html  # Browser-based interactive visualization (Plotly.js)
│
├── Makefile               # Build configuration for QNX (qcc)
├── .gitignore
└── README.md
```

---

## 🛠️ Build & Run

### Prerequisites

- **QNX Software Development Platform (SDP) 7.x or 8.x**
- QNX `qcc` compiler with Neutrino target
- POSIX threading support (`libpthread`)

### Build

```bash
# Clone the repo
git clone https://github.com/YOUR_USERNAME/qnx-thermal-simulator.git
cd qnx-thermal-simulator

# Build both server and client
make all
```

Or manually:

```bash
# Compile server
qcc -o thermal_server src/server.c -lpthread -lm

# Compile client
qcc -o thermal_client src/client.c -lm
```

### Run

Open two terminals on your QNX target:

**Terminal 1 — Start the server:**
```bash
./thermal_server
```

**Terminal 2 — Run the client:**
```bash
# Default: 10 steps, conductivity = 0.20
./thermal_client

# Custom: 50 steps, conductivity = 0.15
./thermal_client 50 0.15
```

### Arguments

| Argument      | Default | Description                             |
|---------------|---------|------------------------------------------|
| `argv[1]`     | `10`    | Number of simulation steps               |
| `argv[2]`     | `0.20`  | Thermal conductivity (α), range 0.0–0.25 |

> ⚠️ Keep conductivity ≤ 0.25 for numerical stability of the explicit method.

---

## 📊 Output & Visualization

### Terminal Output (per step)
```
✓ STEP 5 COMPLETE (2.34 ms)

--- HEATSINK CENTER (7x7 Slice) ---
  .     .     .     .     .     .     .
  .    12.3  18.4  20.1  18.4  12.3   .
  .    18.4  34.5  45.2  34.5  18.4   .
  .    20.1  45.2  87.6  45.2  20.1   .
  ...

╔════════════════════════════════════╗
║       STEP  5 STATISTICS          ║
╠════════════════════════════════════╣
║ Max Temp:       87.65°C           ║
║ Min Temp:        0.00°C           ║
║ Avg Temp:        0.21°C           ║
║ Hot Cells:         48             ║
╚════════════════════════════════════╝
```

### Exported Files
Every 10 steps (and at the final step), the client writes to `/tmp/`:

| File | Description |
|------|-------------|
| `thermal_step_NNN.csv` | Raw X,Y,Temperature data for all 16,384 cells |
| `visualize_step_NNN.m` | Auto-generated MATLAB script (heatmap, surface, contour, histogram) |

### Web Viewer
Load any exported CSV into `tools/thermal_viewer.html` in your browser:

- **2D Heatmap** — Jet colorscale temperature map
- **3D Surface Plot** — Interactive rotating surface
- **Contour Plot** — Isothermal lines
- **Live statistics** — Max, min, avg, std deviation

---

## 🔧 Key QNX Concepts Demonstrated

| Concept | Implementation |
|---------|----------------|
| Named channels | `name_attach()` / `name_open()` |
| Synchronous IPC | `MsgSendv()` / `MsgReplyv()` |
| Async IPC | `SIGEV_PULSE` + `MsgDeliverEvent()` |
| Scatter-gather I/O | `iov_t` / `SETIOV()` / `MsgRead()` |
| Thread pool | `pthread_create()` × 4 workers |
| Barrier sync | `pthread_barrier_t` (workers + main) |
| Mutex protection | `pthread_mutex_t` for global stats |
| Real-time timing | `clock_gettime(CLOCK_MONOTONIC)` |
| Connection timeout | Retry loop with `name_open()` |

---

## 📈 Performance

Measured on QNX Neutrino target with a 128×128 grid:

| Metric | Value |
|--------|-------|
| Grid size | 128×128 = 16,384 cells (64 KB) |
| Threads | 4 workers (32 rows each) |
| Typical iteration time | ~1–5 ms |
| IPC transfer size | 65,536 bytes per round trip |

---

## 🤝 Contributing

Contributions and suggestions are welcome! Feel free to open an issue or pull request for:
- GPU/SIMD acceleration of the diffusion kernel
- Real-time WebSocket streaming to the browser viewer
- Support for non-uniform initial conditions (multiple heat sources)
- Python visualization script export (alongside MATLAB)

---

## 📄 License

This project is licensed under the **MIT License** — see [LICENSE](LICENSE) for details.

---

## 👤 Author

**Your Name**
- GitHub: [@MahidharT](https://github.com/MahidharT)
- Built as part of QNX RTOS systems programming coursework / personal project

---

*Built with ❤️ on QNX Neutrino RTOS*

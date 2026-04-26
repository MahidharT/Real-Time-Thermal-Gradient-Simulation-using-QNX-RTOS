#ifndef THERMAL_IPC_H
#define THERMAL_IPC_H

#include <stdint.h>
#include <sys/siginfo.h>

#define ATTACH_POINT "thermal_sim_service_v2"
#define GRID_DIM 128
#define GRID_SIZE (GRID_DIM * GRID_DIM * sizeof(float))  // 65536 bytes
#define TIMEOUT_SEC 10

// Pulse codes for async communication
#define TIMER_PULSE_CODE (_PULSE_CODE_MINAVAIL + 1)
#define SIM_COMPLETE_PULSE (_PULSE_CODE_MINAVAIL + 2)
#define SHUTDOWN_PULSE (_PULSE_CODE_MINAVAIL + 3)



// Add these visualization functions declarations
void print_temperature_grid(float *grid, int display_width);
void print_heatmap_ascii(float *grid);
void export_to_csv(float *grid, const char *filename, int step);


typedef struct {
    uint32_t msg_type;
    float conductivity;
    int iterations;
    struct sigevent event;  // For asynchronous notification
} thermal_header_t;

#endif // THERMAL_IPC_H


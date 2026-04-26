#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <sys/neutrino.h>
#include <sys/dispatch.h>
#include <time.h>
#include "thermal_ipc.h"

// Global grids
float global_grid[GRID_DIM * GRID_DIM];
float next_grid[GRID_DIM * GRID_DIM];

// Thread pool management
#define NUM_THREADS 4
pthread_t worker_threads[NUM_THREADS];
pthread_barrier_t iteration_barrier;
pthread_mutex_t stats_lock = PTHREAD_MUTEX_INITIALIZER;

// Global statistics
float global_max_temp = 0.0f;
float global_min_temp = 0.0f;
int simulation_active = 0;

// Thread-specific data
typedef struct {
    int thread_id;
    int start_row;
    int end_row;
    float conductivity;
    volatile int* active_flag;
} thread_data_t;

thread_data_t thread_args[NUM_THREADS];

// Error checking macro
#define CHECK_QNX_CALL(call, msg) \
    do { \
        if ((call) == -1) { \
            perror(msg); \
            return EXIT_FAILURE; \
        } \
    } while(0)

// Worker thread function with barrier synchronization
void* worker_thread(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;
    float local_max, local_min;

    printf("SERVER: Worker thread %d started (rows %d-%d)\n",
           data->thread_id, data->start_row, data->end_row);

    while (1) {
        // Wait at barrier for work to be available
        pthread_barrier_wait(&iteration_barrier);

        // Check if we should exit
        if (*(data->active_flag) == 0) {
            break;
        }

        // Initialize local statistics
        local_max = -1000.0f;
        local_min = 1000.0f;

        // Calculate heat diffusion for assigned rows
        for (int i = data->start_row; i < data->end_row; i++) {
            if (i <= 0 || i >= GRID_DIM - 1) continue;

            for (int j = 1; j < GRID_DIM - 1; j++) {
                int idx = i * GRID_DIM + j;
                float center = global_grid[idx];
                float neighbors = global_grid[idx-1] + global_grid[idx+1] +
                                  global_grid[idx-GRID_DIM] + global_grid[idx+GRID_DIM];

                // Heat Equation: T_new = T_old + alpha * (Sum_Neighbors - 4 * T_old)
                next_grid[idx] = center + data->conductivity * (neighbors - 4.0f * center);

                // Track local min/max
                if (next_grid[idx] > local_max) local_max = next_grid[idx];
                if (next_grid[idx] < local_min) local_min = next_grid[idx];
            }
        }

        // Update global statistics with mutex protection
        pthread_mutex_lock(&stats_lock);
        if (local_max > global_max_temp) global_max_temp = local_max;
        if (local_min < global_min_temp) global_min_temp = local_min;
        pthread_mutex_unlock(&stats_lock);

        // Wait for all threads to complete
        pthread_barrier_wait(&iteration_barrier);
    }

    printf("SERVER: Worker thread %d exiting\n", data->thread_id);
    return NULL;
}

// Initialize thread pool
int init_thread_pool() {
    int rows_per_thread = GRID_DIM / NUM_THREADS;

    // Initialize barrier (NUM_THREADS workers + 1 main thread)
    if (pthread_barrier_init(&iteration_barrier, NULL, NUM_THREADS + 1) != 0) {
        perror("pthread_barrier_init failed");
        return -1;
    }

    simulation_active = 1;

    // Create worker threads
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_args[i].thread_id = i;
        thread_args[i].start_row = i * rows_per_thread;
        thread_args[i].end_row = (i + 1) * rows_per_thread;
        thread_args[i].conductivity = 0.0f;  // Will be updated per iteration
        thread_args[i].active_flag = &simulation_active;

        if (pthread_create(&worker_threads[i], NULL, worker_thread, &thread_args[i]) != 0) {
            perror("pthread_create failed");
            return -1;
        }
    }

    printf("SERVER: Thread pool initialized with %d workers\n", NUM_THREADS);
    return 0;
}

// Cleanup thread pool
void cleanup_thread_pool() {
    simulation_active = 0;

    // Release all threads from barrier
    pthread_barrier_wait(&iteration_barrier);

    // Wait for all threads to exit
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(worker_threads[i], NULL);
    }

    pthread_barrier_destroy(&iteration_barrier);
    printf("SERVER: Thread pool destroyed\n");
}

// Run one simulation iteration using thread pool
void run_simulation_iteration(float conductivity) {
    // Reset statistics
    pthread_mutex_lock(&stats_lock);
    global_max_temp = -1000.0f;
    global_min_temp = 1000.0f;
    pthread_mutex_unlock(&stats_lock);

    // Update conductivity for all threads
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_args[i].conductivity = conductivity;
    }

    // Release threads to start work
    pthread_barrier_wait(&iteration_barrier);

    // Wait for all threads to complete
    pthread_barrier_wait(&iteration_barrier);

    // Copy results to global grid
    memcpy(global_grid, next_grid, GRID_SIZE);
}

int main() {
    name_attach_t *attach;
    int rcvid;
    thermal_header_t hdr;
    iov_t riov[1];
    struct _pulse pulse;

    printf("========================================\n");
    printf("  THERMAL GRADIENT SIMULATOR SERVER\n");
    printf("========================================\n");
    printf("Grid: %dx%d (%.1f KB)\n", GRID_DIM, GRID_DIM, GRID_SIZE/1024.0f);
    printf("Threads: %d\n", NUM_THREADS);
    printf("Attach Point: %s\n", ATTACH_POINT);
    printf("========================================\n\n");

    // Attach to namespace
    attach = name_attach(NULL, ATTACH_POINT, 0);
    if (attach == NULL) {
        perror("name_attach failed");
        return EXIT_FAILURE;
    }
    printf("SERVER: Successfully attached to namespace\n");

    // Initialize thread pool
    if (init_thread_pool() != 0) {
        name_detach(attach, 0);
        return EXIT_FAILURE;
    }

    printf("SERVER: Ready to accept client connections\n\n");

    // Main message loop
    while (1) {
        rcvid = MsgReceive(attach->chid, &hdr, sizeof(hdr), NULL);

        if (rcvid == 0) {
            // Pulse received (could be from timer in future)
            memcpy(&pulse, &hdr, sizeof(pulse));

            if (pulse.code == SHUTDOWN_PULSE) {
                printf("SERVER: Shutdown pulse received\n");
                break;
            }
            continue;
        }

        if (rcvid < 0) {
            fprintf(stderr, "SERVER: MsgReceive error: %s\n", strerror(errno));
            continue;
        }

        // Read the grid data from client
        int bytes_read = MsgRead(rcvid, global_grid, GRID_SIZE, sizeof(hdr));
        if (bytes_read != (int)GRID_SIZE) {
            fprintf(stderr, "SERVER: MsgRead failed, expected %lu bytes, got %d\n",
                    (unsigned long)GRID_SIZE, bytes_read);
            MsgError(rcvid, EIO);
            continue;
        }

        printf("SERVER: Received simulation request (conductivity=%.2f)\n",
               hdr.conductivity);

        // Run the simulation
        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);

        run_simulation_iteration(hdr.conductivity);

        clock_gettime(CLOCK_MONOTONIC, &end);
        double elapsed_ms = (end.tv_sec - start.tv_sec) * 1000.0 +
                           (end.tv_nsec - start.tv_nsec) / 1000000.0;

        printf("SERVER: Simulation complete in %.2f ms (Max: %.1f°C, Min: %.1f°C)\n",
               elapsed_ms, global_max_temp, global_min_temp);

        // Send response back to client using IOV
        SETIOV(&riov[0], global_grid, GRID_SIZE);
        if (MsgReplyv(rcvid, 0, riov, 1) == -1) {
            fprintf(stderr, "SERVER: MsgReplyv failed: %s\n", strerror(errno));
        }

        // If client requested async notification, send it
        if (hdr.event.sigev_notify != SIGEV_NONE) {
            if (MsgDeliverEvent(rcvid, &hdr.event) == -1) {
                fprintf(stderr, "SERVER: MsgDeliverEvent failed: %s\n", strerror(errno));
            } else {
                printf("SERVER: Async notification sent to client\n");
            }
        }
    }

    // Cleanup
    printf("\nSERVER: Shutting down...\n");
    cleanup_thread_pool();
    name_detach(attach, 0);
    printf("SERVER: Shutdown complete\n");

    return EXIT_SUCCESS;
}  // ← THIS CLOSING BRACE WAS MISSING!


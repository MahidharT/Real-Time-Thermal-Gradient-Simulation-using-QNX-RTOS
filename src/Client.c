#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/neutrino.h>
#include <sys/dispatch.h>
#include "thermal_ipc.h"

// ============================================================================
// VISUALIZATION AND HELPER FUNCTIONS
// ============================================================================

// Helper function to print the 7x7 center of the grid
void print_heatsink_center(float *grid) {
    printf("\n--- HEATSINK CENTER (7x7 Slice) ---\n");
    for (int y = 61; y <= 67; y++) {
        for (int x = 61; x <= 67; x++) {
            float val = grid[y * GRID_DIM + x];
            if (val > 1.0f)
                printf("%5.1f ", val);
            else
                printf("  .   ");
        }
        printf("\n");
    }
    printf("------------------------------------\n");
}

// Print full statistics
void print_statistics(float *grid, int step) {
    float max_temp = -1000.0f;
    float min_temp = 1000.0f;
    float avg_temp = 0.0f;
    int hot_cells = 0;

    for (int i = 0; i < GRID_DIM * GRID_DIM; i++) {
        if (grid[i] > max_temp) max_temp = grid[i];
        if (grid[i] < min_temp) min_temp = grid[i];
        avg_temp += grid[i];
        if (grid[i] > 10.0f) hot_cells++;
    }
    avg_temp /= (GRID_DIM * GRID_DIM);

    printf("\n╔════════════════════════════════════╗\n");
    printf("║       STEP %2d STATISTICS         ║\n", step);
    printf("╠════════════════════════════════════╣\n");
    printf("║ Max Temp:      %6.2f°C          ║\n", max_temp);
    printf("║ Min Temp:      %6.2f°C          ║\n", min_temp);
    printf("║ Avg Temp:      %6.2f°C          ║\n", avg_temp);
    printf("║ Hot Cells:     %6d             ║\n", hot_cells);
    printf("╚════════════════════════════════════╝\n");
}

// Print full temperature grid with color-coded ASCII
void print_temperature_grid(float *grid, int display_width) {
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║              TEMPERATURE GRID VISUALIZATION                    ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n");

    int step = GRID_DIM / display_width;
    if (step < 1) step = 1;

    // Print column numbers
    printf("    ");
    for (int j = 0; j < GRID_DIM; j += step) {
        printf("%3d", j);
    }
    printf("\n");

    for (int i = 0; i < GRID_DIM; i += step) {
        printf("%3d ", i);
        for (int j = 0; j < GRID_DIM; j += step) {
            float temp = grid[i * GRID_DIM + j];

            // Color-coded ASCII characters based on temperature
            char symbol;
            if (temp < 20.0f)       symbol = ' ';   // Very cold
            else if (temp < 30.0f)  symbol = '.';   // Cold
            else if (temp < 40.0f)  symbol = '-';   // Cool
            else if (temp < 50.0f)  symbol = '=';   // Warm
            else if (temp < 60.0f)  symbol = '+';   // Hot
            else if (temp < 70.0f)  symbol = '*';   // Very hot
            else if (temp < 80.0f)  symbol = 'o';   // Critical
            else if (temp < 90.0f)  symbol = 'O';   // Danger
            else if (temp < 100.0f) symbol = '#';   // Extreme
            else                    symbol = '@';   // Maximum

            printf("%3c", symbol);
        }
        printf("\n");
    }

    printf("\nLegend:\n");
    printf("  ' ' <20°C  | '.' 20-30°C | '-' 30-40°C | '=' 40-50°C\n");
    printf("  '+' 50-60°C | '*' 60-70°C | 'o' 70-80°C | 'O' 80-90°C\n");
    printf("  '#' 90-100°C | '@' >100°C\n");
    printf("════════════════════════════════════════════════════════════════\n");
}

// Print detailed heatmap with temperature values
void print_heatmap_ascii(float *grid) {
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║                   DETAILED HEATMAP (32x32)                     ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n");

    int display_size = 32;
    int step = GRID_DIM / display_size;

    printf("    ");
    for (int j = 0; j < display_size; j++) {
        printf("%2d ", j * step);
    }
    printf("\n");

    for (int i = 0; i < display_size; i++) {
        printf("%3d ", i * step);
        for (int j = 0; j < display_size; j++) {
            int idx = (i * step) * GRID_DIM + (j * step);
            float temp = grid[idx];

            // ANSI color codes for terminal
            const char *color;
            if (temp < 30.0f)       color = "\033[34m";  // Blue (cold)
            else if (temp < 50.0f)  color = "\033[36m";  // Cyan (cool)
            else if (temp < 70.0f)  color = "\033[32m";  // Green (warm)
            else if (temp < 85.0f)  color = "\033[33m";  // Yellow (hot)
            else if (temp < 95.0f)  color = "\033[31m";  // Red (very hot)
            else                    color = "\033[35m";  // Magenta (critical)

            char symbol;
            if (temp < 40.0f)       symbol = '.';
            else if (temp < 60.0f)  symbol = '+';
            else if (temp < 80.0f)  symbol = '*';
            else if (temp < 95.0f)  symbol = '#';
            else                    symbol = '@';

            printf("%s%c%c%c\033[0m", color, symbol, symbol, symbol);
        }
        printf("\n");
    }
    printf("════════════════════════════════════════════════════════════════\n");
}

// Export grid to CSV for MATLAB/Python analysis
void export_to_csv(float *grid, const char *filename, int step) {
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        fprintf(stderr, "Failed to create CSV file: %s\n", filename);
        return;
    }

    // Write header
    fprintf(fp, "# Thermal Simulation Data - Step %d\n", step);
    fprintf(fp, "# Grid Size: %dx%d\n", GRID_DIM, GRID_DIM);
    fprintf(fp, "# Format: X, Y, Temperature (°C)\n");
    fprintf(fp, "X,Y,Temperature\n");

    // Write data
    for (int i = 0; i < GRID_DIM; i++) {
        for (int j = 0; j < GRID_DIM; j++) {
            fprintf(fp, "%d,%d,%.6f\n", j, i, grid[i * GRID_DIM + j]);
        }
    }

    fclose(fp);
    printf("CLIENT: Exported temperature data to %s\n", filename);
}

// Generate MATLAB visualization script
void export_matlab_script(const char *csv_filename, const char *script_filename, int step) {
    FILE *fp = fopen(script_filename, "w");
    if (!fp) {
        fprintf(stderr, "Failed to create MATLAB script: %s\n", script_filename);
        return;
    }

    fprintf(fp, "%% MATLAB Visualization Script for Thermal Simulation\n");
    fprintf(fp, "%% Generated automatically - Step %d\n\n", step);

    fprintf(fp, "clear all; close all;\n\n");

    fprintf(fp, "%% Load data\n");
    fprintf(fp, "data = csvread('%s', 4, 0);\n", csv_filename);
    fprintf(fp, "grid_size = %d;\n\n", GRID_DIM);

    fprintf(fp, "%% Reshape into 2D grid\n");
    fprintf(fp, "X = reshape(data(:,1), grid_size, grid_size);\n");
    fprintf(fp, "Y = reshape(data(:,2), grid_size, grid_size);\n");
    fprintf(fp, "T = reshape(data(:,3), grid_size, grid_size);\n\n");

    fprintf(fp, "%% Create figure with multiple plots\n");
    fprintf(fp, "figure('Position', [100 100 1400 900]);\n\n");

    fprintf(fp, "%% 2D Heatmap\n");
    fprintf(fp, "subplot(2,2,1);\n");
    fprintf(fp, "imagesc(T);\n");
    fprintf(fp, "colormap(jet);\n");
    fprintf(fp, "colorbar;\n");
    fprintf(fp, "title('Temperature Distribution (°C) - Step %d');\n", step);
    fprintf(fp, "xlabel('X Position');\n");
    fprintf(fp, "ylabel('Y Position');\n");
    fprintf(fp, "axis equal tight;\n\n");

    fprintf(fp, "%% 3D Surface Plot\n");
    fprintf(fp, "subplot(2,2,2);\n");
    fprintf(fp, "surf(X, Y, T);\n");
    fprintf(fp, "colormap(jet);\n");
    fprintf(fp, "colorbar;\n");
    fprintf(fp, "title('3D Temperature Surface');\n");
    fprintf(fp, "xlabel('X Position');\n");
    fprintf(fp, "ylabel('Y Position');\n");
    fprintf(fp, "zlabel('Temperature (°C)');\n");
    fprintf(fp, "shading interp;\n");
    fprintf(fp, "view(45, 30);\n\n");

    fprintf(fp, "%% Contour Plot\n");
    fprintf(fp, "subplot(2,2,3);\n");
    fprintf(fp, "contourf(T, 20);\n");
    fprintf(fp, "colormap(jet);\n");
    fprintf(fp, "colorbar;\n");
    fprintf(fp, "title('Temperature Contours');\n");
    fprintf(fp, "xlabel('X Position');\n");
    fprintf(fp, "ylabel('Y Position');\n");
    fprintf(fp, "axis equal tight;\n\n");

    fprintf(fp, "%% Temperature Distribution Histogram\n");
    fprintf(fp, "subplot(2,2,4);\n");
    fprintf(fp, "histogram(T(:), 50, 'FaceColor', [0.3 0.6 0.9]);\n");
    fprintf(fp, "title('Temperature Distribution');\n");
    fprintf(fp, "xlabel('Temperature (°C)');\n");
    fprintf(fp, "ylabel('Frequency');\n");
    fprintf(fp, "grid on;\n\n");

    fprintf(fp, "%% Print statistics\n");
    fprintf(fp, "fprintf('\\n=== Thermal Statistics ===\\n');\n");
    fprintf(fp, "fprintf('Max Temperature: %%.2f °C\\n', max(T(:)));\n");
    fprintf(fp, "fprintf('Min Temperature: %%.2f °C\\n', min(T(:)));\n");
    fprintf(fp, "fprintf('Avg Temperature: %%.2f °C\\n', mean(T(:)));\n");
    fprintf(fp, "fprintf('Std Deviation:   %%.2f °C\\n', std(T(:)));\n");

    fclose(fp);
    printf("CLIENT: Generated MATLAB script: %s\n", script_filename);
}

// Connect with timeout and retry logic
int connect_with_timeout(const char* attach_point, int timeout_sec) {
    int fd;
    struct timespec start, now;
    int retry_count = 0;

    clock_gettime(CLOCK_MONOTONIC, &start);

    printf("CLIENT: Connecting to server '%s'...\n", attach_point);

    while (1) {
        fd = name_open(attach_point, 0);

        if (fd != -1) {
            printf("CLIENT: Connected successfully after %d attempt(s)\n", retry_count + 1);
            return fd;
        }

        // Check timeout
        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed = (now.tv_sec - start.tv_sec);

        if (elapsed >= timeout_sec) {
            fprintf(stderr, "CLIENT: Connection timeout after %.0f seconds\n", elapsed);
            fprintf(stderr, "CLIENT: Server '%s' unreachable\n", attach_point);
            return -1;
        }

        retry_count++;
        if (retry_count % 5 == 0) {
            printf("CLIENT: Still trying to connect... (%.0f/%d seconds)\n",
                   elapsed, timeout_sec);
        }

        sleep(1);
    }
}

// ============================================================================
// MAIN FUNCTION
// ============================================================================

int main(int argc, char *argv[]) {
    int fd, chid, coid;
    float my_grid[GRID_DIM * GRID_DIM];
    thermal_header_t hdr;
    iov_t siov[2], riov[1];
    //struct _pulse pulse;

    // Parse command line arguments
    int num_steps = 10;
    float conductivity = 0.20f;

    if (argc > 1) num_steps = atoi(argv[1]);
    if (argc > 2) conductivity = atof(argv[2]);

    printf("========================================\n");
    printf("  THERMAL GRADIENT SIMULATOR CLIENT\n");
    printf("========================================\n");
    printf("Grid: %dx%d (%.1f KB)\n", GRID_DIM, GRID_DIM, GRID_SIZE/1024.0f);
    printf("Steps: %d\n", num_steps);
    printf("Conductivity: %.2f\n", conductivity);
    printf("========================================\n\n");

    // Connect to server with timeout
    fd = connect_with_timeout(ATTACH_POINT, TIMEOUT_SEC);
    if (fd == -1) {
        return EXIT_FAILURE;
    }

    // Create channel for async notifications (optional feature)
    chid = ChannelCreate(0);
    if (chid == -1) {
        perror("ChannelCreate failed");
        name_close(fd);
        return EXIT_FAILURE;
    }

    coid = ConnectAttach(0, 0, chid, _NTO_SIDE_CHANNEL, 0);
    if (coid == -1) {
        perror("ConnectAttach failed");
        ChannelDestroy(chid);
        name_close(fd);
        return EXIT_FAILURE;
    }

    // Initialize grid - set CPU die at center to 100°C
    memset(my_grid, 0, sizeof(my_grid));
    my_grid[(GRID_DIM/2) * GRID_DIM + (GRID_DIM/2)] = 100.0f;

    printf("CLIENT: Initial heat source placed at center (100.0°C)\n");
    print_heatsink_center(my_grid);

    // Setup message header with async notification
    hdr.msg_type = 1;
    hdr.conductivity = conductivity;
    hdr.iterations = 1;
    hdr.event.sigev_notify = SIGEV_PULSE;
    hdr.event.sigev_coid = coid;
    struct sched_param param;
    SchedGet(0, 0, &param);
    hdr.event.sigev_priority = param.sched_priority;
    hdr.event.sigev_code = SIM_COMPLETE_PULSE;

    // Setup IOV for sending
    SETIOV(&siov[0], &hdr, sizeof(hdr));
    SETIOV(&siov[1], my_grid, GRID_SIZE);
    SETIOV(&riov[0], my_grid, GRID_SIZE);

    printf("\nCLIENT: Starting %d-step thermal simulation...\n", num_steps);
    printf("========================================\n");

    struct timespec sim_start, sim_end;
    clock_gettime(CLOCK_MONOTONIC, &sim_start);

    // Run simulation steps
    for (int step = 1; step <= num_steps; step++) {
        struct timespec step_start, step_end;
        clock_gettime(CLOCK_MONOTONIC, &step_start);

        // Send simulation request
        if (MsgSendv(fd, siov, 2, riov, 1) == -1) {
            fprintf(stderr, "CLIENT: MsgSendv failed at step %d: %s\n",
                    step, strerror(errno));
            break;
        }

        clock_gettime(CLOCK_MONOTONIC, &step_end);
        double step_time = (step_end.tv_sec - step_start.tv_sec) * 1000.0 +
                          (step_end.tv_nsec - step_start.tv_nsec) / 1000000.0;

        printf("\n✓ STEP %d COMPLETE (%.2f ms)\n", step, step_time);

        // Show visualizations every step
        print_heatsink_center(my_grid);
        print_statistics(my_grid, step);

        // Show ASCII heatmap every 5 steps
        if (step % 5 == 0 || step == num_steps) {
            print_heatmap_ascii(my_grid);
        }

        // Export data every 10 steps or at the end
        if (step % 10 == 0 || step == num_steps) {
            char csv_file[256];
            char matlab_file[256];
            snprintf(csv_file, sizeof(csv_file), "/tmp/thermal_step_%03d.csv", step);
            snprintf(matlab_file, sizeof(matlab_file), "/tmp/visualize_step_%03d.m", step);

            export_to_csv(my_grid, csv_file, step);
            export_matlab_script(csv_file, matlab_file, step);
        }

        // Optional: wait for async pulse (demonstrates async capability)
        // Uncomment to see async notifications
        /*
        int pulse_rcvid = MsgReceive(chid, &pulse, sizeof(pulse), NULL);
        if (pulse_rcvid == 0 && pulse.code == SIM_COMPLETE_PULSE) {
            printf("CLIENT: Async notification received\n");
        }
        */

        // Delay between steps for visualization
        if (step < num_steps) {
            usleep(300000);  // 300ms
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &sim_end);
    double total_time = (sim_end.tv_sec - sim_start.tv_sec) * 1000.0 +
                       (sim_end.tv_nsec - sim_start.tv_nsec) / 1000000.0;

    printf("\n========================================\n");
    printf("SIMULATION COMPLETE\n");
    printf("Total Time: %.2f ms (%.2f ms/step avg)\n",
           total_time, total_time / num_steps);
    printf("========================================\n");

    // Final full grid display
    printf("\n\nFINAL TEMPERATURE DISTRIBUTION:\n");
    print_temperature_grid(my_grid, 32);

    printf("\nExported files to /tmp/:\n");
    printf("  - thermal_step_*.csv (for Python/MATLAB)\n");
    printf("  - visualize_step_*.m (MATLAB scripts)\n");

    // Cleanup
    ConnectDetach(coid);
    ChannelDestroy(chid);
    name_close(fd);

    printf("\nCLIENT: Disconnected from server\n");

    return EXIT_SUCCESS;
}


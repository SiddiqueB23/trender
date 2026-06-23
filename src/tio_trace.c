#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <math.h>

#include "tio.h"

#define TIO_TRACE_NAME_MAX 128

// Sentinel stored in tio_trace_display.text to mark a task's right edge; never
// appears in a real event name. Translated to the UTF-8 bytes for U+258C
// (LEFT HALF BLOCK, "\xe2\x96\x8c") at output time.
#define TIO_TRACE_EDGE_MARKER '\x01'

typedef struct {
    char* group_name;
    char** event_names;
    uint32_t* event_colors;
    double* starts;
    double* ends;
    int* lane;
    int* draw_order; /* task indices sorted by start time, ascending */
    int num_tasks;
    int max_overlapping_tasks;
} tio_trace_group_t;

typedef struct {
    int num_groups;
    tio_trace_group_t* group;
} tio_trace_t;

typedef struct {
    char     name[TIO_TRACE_NAME_MAX];
    uint32_t color;
    double   start;
    double   end;
    int      ending_found;
} trace_task_t;

// Finds the index of a task with the given name, or -1 if not present.
static int find_task(trace_task_t* tasks, int count, const char* name) {
    for (int i = 0; i < count; i++) {
        if (strcmp(tasks[i].name, name) == 0) return i;
    }
    return -1;
}

// Assigns each task a lane (y row) such that no two tasks sharing a lane overlap
// in time, using greedy interval coloring: process tasks in start-time order, and
// place each one in the lowest-numbered lane whose last task already ended. The
// number of lanes used equals the maximum number of tasks overlapping at any
// instant, so this also gives max_overlapping_tasks. Returns the lane count.
//
// order_out receives the task indices sorted by start time ascending (caller-
// allocated, size n). The display draws tasks in this order rather than file
// order so that touching tasks (one ending exactly where the next begins, which
// the column-rounded display can collapse into the same cell) resolve the
// shared cell consistently in favor of the chronologically later task, instead
// of whichever happened to appear first in the log file.
static int assign_lanes(double* starts, double* ends, int* lane_out, int* order_out, int n) {
    if (n == 0) return 0;

    int* order = order_out;
    for (int i = 0; i < n; i++) order[i] = i;

    // Simple insertion sort of task indices by start time.
    for (int i = 1; i < n; i++) {
        int idx = order[i];
        int j = i - 1;
        while (j >= 0 && starts[order[j]] > starts[idx]) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = idx;
    }

    double* lane_end = malloc((size_t)n * sizeof(double));
    int num_lanes = 0;

    for (int i = 0; i < n; i++) {
        int task = order[i];
        int chosen = -1;
        for (int l = 0; l < num_lanes; l++) {
            if (lane_end[l] <= starts[task]) {
                chosen = l;
                break;
            }
        }
        if (chosen < 0) {
            chosen = num_lanes++;
        }
        lane_end[chosen] = ends[task];
        lane_out[task] = chosen;
    }

    free(lane_end);
    return num_lanes;
}

typedef struct {
    char          group_name[TIO_TRACE_NAME_MAX];
    trace_task_t* tasks;
    int           count;
    int           capacity;
} group_build_t;

// Finds the index of a group with the given name, or -1 if not present.
static int find_group(group_build_t* groups, int count, const char* name) {
    for (int i = 0; i < count; i++) {
        if (strcmp(groups[i].group_name, name) == 0) return i;
    }
    return -1;
}

int tio_trace_read_log(const char* filename, tio_trace_t* out) {
    FILE* f = fopen(filename, "r");
    if (f == NULL) {
        fprintf(stderr, "error: could not open trace log '%s'\n", filename);
        return -1;
    }

    group_build_t* groups = NULL;
    int group_count = 0;
    int group_capacity = 0;

    char     group_name[TIO_TRACE_NAME_MAX];
    char     name[TIO_TRACE_NAME_MAX];
    double   t;
    uint32_t color;

    // Parse one event per line: group_name,name,seconds,0xAARRGGBB
    while (fscanf(f, "%127[^,],%127[^,],%lf,%x\n", group_name, name, &t, &color) == 4) {
        int gidx = find_group(groups, group_count, group_name);
        if (gidx < 0) {
            if (group_count == group_capacity) {
                group_capacity = group_capacity ? group_capacity * 2 : 8;
                groups = realloc(groups, (size_t)group_capacity * sizeof(group_build_t));
            }
            gidx = group_count++;
            snprintf(groups[gidx].group_name, sizeof groups[gidx].group_name, "%s", group_name);
            groups[gidx].tasks = NULL;
            groups[gidx].count = 0;
            groups[gidx].capacity = 0;
        }
        group_build_t* group = &groups[gidx];

        int idx = find_task(group->tasks, group->count, name);

        if (idx < 0) {
            // First occurrence: this is the task's start.
            if (group->count == group->capacity) {
                group->capacity = group->capacity ? group->capacity * 2 : 16;
                group->tasks = realloc(group->tasks, (size_t)group->capacity * sizeof(trace_task_t));
            }
            trace_task_t* task = &group->tasks[group->count++];
            snprintf(task->name, sizeof task->name, "%s", name);
            task->color = color;
            task->start = t;
            task->end = t;
            task->ending_found = 0;
        } else if (!group->tasks[idx].ending_found) {
            // Second occurrence: this is the task's end.
            group->tasks[idx].end = t;
            group->tasks[idx].ending_found = 1;
        } else {
            // Third (or later) occurrence: unexpected.
            fprintf(stderr, "warning: more than 2 events for \"%s\" in group \"%s\" (extra event ignored)\n",
                    name, group_name);
        }
    }

    fclose(f);

    // Warn about tasks that never got a matching end event.
    for (int g = 0; g < group_count; g++) {
        for (int i = 0; i < groups[g].count; i++) {
            if (!groups[g].tasks[i].ending_found) {
                fprintf(stderr, "warning: no end event for \"%s\" in group \"%s\" (start logged at %.6f)\n",
                        groups[g].tasks[i].name, groups[g].group_name, groups[g].tasks[i].start);
            }
        }
    }

    // Build the final tio_trace_t: one tio_trace_group_t per group, each with its
    // own lane assignment independent of every other group.
    out->num_groups = group_count;
    out->group = malloc((size_t)group_count * sizeof(tio_trace_group_t));

    for (int g = 0; g < group_count; g++) {
        trace_task_t* tasks = groups[g].tasks;
        int count = groups[g].count;
        tio_trace_group_t* dst = &out->group[g];

        dst->group_name   = strdup(groups[g].group_name);
        dst->num_tasks     = count;
        dst->event_names   = malloc((size_t)count * sizeof(char*));
        dst->event_colors  = malloc((size_t)count * sizeof(uint32_t));
        dst->starts         = malloc((size_t)count * sizeof(double));
        dst->ends           = malloc((size_t)count * sizeof(double));

        for (int i = 0; i < count; i++) {
            dst->event_names[i]  = strdup(tasks[i].name);
            dst->event_colors[i] = tasks[i].color;
            dst->starts[i]       = tasks[i].start;
            dst->ends[i]         = tasks[i].end;
        }

        dst->lane = malloc((size_t)count * sizeof(int));
        dst->draw_order = malloc((size_t)count * sizeof(int));
        dst->max_overlapping_tasks = assign_lanes(dst->starts, dst->ends, dst->lane, dst->draw_order, count);

        free(tasks);
    }

    free(groups);
    return 0;
}

void tio_trace_free(tio_trace_t* trace) {
    for (int g = 0; g < trace->num_groups; g++) {
        tio_trace_group_t* group = &trace->group[g];
        for (int i = 0; i < group->num_tasks; i++) {
            free(group->event_names[i]);
        }
        free(group->group_name);
        free(group->event_names);
        free(group->event_colors);
        free(group->starts);
        free(group->ends);
        free(group->lane);
        free(group->draw_order);
    }
    free(trace->group);
}

typedef struct {
    double start;
    double end;
    double time_per_char;
} tio_trace_display_params;

typedef struct {
    int width; /* set to terminal wdith */
    int height; /* set to sum of each group's header + lane rows */
    int* group_row_offset; /* per group: row of its first lane (header is row-1) */
    /* width x height arrays */
    uint32_t* color; /* bg color corresponding to task color */
    char* text; /* populated by spaces except where tasks are */
} tio_trace_display;

void tio_trace_display_init(tio_trace_display* display, tio_ctx_t* tio, tio_trace_t trace) {
    int rows = 0, cols = 0;
    tio_get_window_size(tio, &rows, &cols);

    display->width = cols > 0 ? cols : 80;

    display->group_row_offset = malloc((size_t)trace.num_groups * sizeof(int));
    int height = 0;
    for (int g = 0; g < trace.num_groups; g++) {
        if (trace.group[g].max_overlapping_tasks <= 0) {
            display->group_row_offset[g] = -1; // group has no tasks: not drawn
            continue;
        }
        height += 1; // header row
        display->group_row_offset[g] = height;
        height += trace.group[g].max_overlapping_tasks;
    }
    display->height = height > 0 ? height : 1;

    size_t cells = (size_t)display->width * (size_t)display->height;
    display->color = malloc(cells * sizeof(uint32_t));
    display->text = malloc(cells * sizeof(char));
}

void tio_trace_display_refresh(tio_trace_display* display, tio_trace_display_params* params, tio_trace_t trace) {
    size_t cells = (size_t)display->width * (size_t)display->height;
    memset(display->color, 0, cells * sizeof(uint32_t));
    memset(display->text, ' ', cells * sizeof(char));

    if (params->time_per_char <= 0.0) return;

    for (int g = 0; g < trace.num_groups; g++) {
        int group_row = display->group_row_offset[g];
        if (group_row < 0) continue; // group has no tasks

        // Header row: the group's name, plain text, truncated to fit.
        const char* group_name = trace.group[g].group_name;
        int group_name_len = (int)strlen(group_name);
        int chars_to_copy = group_name_len < display->width ? group_name_len : display->width;
        memcpy(&display->text[(group_row - 1) * display->width], group_name, (size_t)chars_to_copy);

        tio_trace_group_t group = trace.group[g];
        for (int oi = 0; oi < group.num_tasks; oi++) {
            int i = group.draw_order[oi];
            int row = group_row + group.lane[i];
            if (row < 0 || row >= display->height) continue;

            // Fill is half-open [start, end): a column is the task's if some instant in
            // [start, end) falls inside it. start_col uses floor (the start instant always
            // belongs to its column); raw_end_col is the last column still inside the
            // interval, i.e. ceil(end) - 1. This means a task whose end lands exactly on a
            // column boundary does NOT claim that boundary column, so a task starting
            // exactly there (the common touching-task case) never has to share a cell.
            int start_col = (int)floor((group.starts[i] - params->start) / params->time_per_char);
            double end_rel = (group.ends[i] - params->start) / params->time_per_char;
            int raw_end_col = (int)floor(end_rel) - 1;
            if (raw_end_col < 0 || start_col >= display->width) continue; // entirely out of view
            if (raw_end_col < start_col) continue; // shorter than one column's time span: nothing to draw

            // The task's true end is visible (not cut off by the view's right edge) only
            // if raw_end_col still lands within the display.
            int genuine_edge = raw_end_col < display->width;

            if (start_col < 0) start_col = 0;
            int end_col = genuine_edge ? raw_end_col : display->width - 1;

            for (int c = start_col; c <= end_col; c++) {
                display->color[row * display->width + c] = group.event_colors[i];
            }

            // Lay the event name left-aligned within its visible span, truncated to fit.
            // The rightmost column is reserved for the edge marker when the task's true
            // end is on screen.
            const char* name = group.event_names[i];
            int name_len = (int)strlen(name);
            int span = end_col - start_col + 1;
            int name_span = genuine_edge ? span - 1 : span;
            int task_chars_to_copy = name_len < name_span ? name_len : name_span;
            for (int c = 0; c < task_chars_to_copy; c++) {
                display->text[row * display->width + start_col + c] = name[c];
            }

            if (genuine_edge) {
                display->text[row * display->width + end_col] = TIO_TRACE_EDGE_MARKER;
            }
        }
    }
}

// Picks black or white foreground text for the given RGB background, using
// perceived-brightness (ITU-R BT.601) luminance so text stays readable against
// both light and dark task colors.
static uint32_t contrasting_fg(int r, int g, int b) {
    int luminance = (299 * r + 587 * g + 114 * b) / 1000;
    return luminance > 128 ? 0x000000 : 0xFFFFFF;
}

void tio_trace_display_output(tio_trace_display* display, tio_ctx_t* tio) {
    // Worst case per cell: SGR bg + fg color change (~40 bytes) + 1 char. Plus a
    // reset and CRLF per row, plus a leading "move cursor home" sequence.
    size_t cap = (size_t)display->width * (size_t)display->height * 44 + (size_t)display->height * 8 + 16;
    char* buf = malloc(cap);
    char* p = buf;

    memcpy(p, "\x1b[H", 3); p += 3;

    uint32_t prev_color = 0xFFFFFFFF; // sentinel that never matches a real color
    for (int row = 0; row < display->height; row++) {
        for (int col = 0; col < display->width; col++) {
            char ch = display->text[row * display->width + col];
            uint32_t color = display->color[row * display->width + col];

            if (ch == TIO_TRACE_EDGE_MARKER) {
                // Default background, task color as foreground for the glyph itself.
                int r = (int)((color >> 16) & 0xFF);
                int g = (int)((color >> 8) & 0xFF);
                int b = (int)(color & 0xFF);
                p += sprintf(p, "\x1b[49m\x1b[38;2;%d;%d;%dm", r, g, b);
                memcpy(p, "\xe2\x96\x8c", 3); p += 3; // U+258C LEFT HALF BLOCK
                prev_color = 0xFFFFFFFF; // force the next cell to re-emit its own SGR state
                continue;
            }

            if (color != prev_color) {
                int r = (int)((color >> 16) & 0xFF);
                int g = (int)((color >> 8) & 0xFF);
                int b = (int)(color & 0xFF);
                uint32_t fg = contrasting_fg(r, g, b);
                p += sprintf(p, "\x1b[48;2;%d;%d;%dm\x1b[38;2;%d;%d;%dm",
                             r, g, b,
                             (int)((fg >> 16) & 0xFF), (int)((fg >> 8) & 0xFF), (int)(fg & 0xFF));
                prev_color = color;
            }
            *p++ = ch;
        }
        memcpy(p, "\x1b[0m\r\n", 6); p += 6;
        prev_color = 0xFFFFFFFF;
    }

    tio_write(tio, buf, (size_t)(p - buf));
    free(buf);
}

void tio_trace_display_free(tio_trace_display* display) {
    free(display->group_row_offset);
    free(display->color);
    free(display->text);
}

// Clamps the view so it never extends past [data_start, data_end] and never
// shows more than the full data range at once.
static void clamp_view(tio_trace_display_params* params, int width, double data_start, double data_end) {
    double max_visible = data_end - data_start;
    if (max_visible <= 0.0) max_visible = 1.0;

    double visible = params->time_per_char * width;
    if (visible > max_visible) {
        visible = max_visible;
        params->time_per_char = visible / width;
    }

    if (params->start < data_start) params->start = data_start;
    if (params->start + visible > data_end) params->start = data_end - visible;
    if (params->start < data_start) params->start = data_start;

    params->end = params->start + visible;
}

// Zooms by `factor` (<1 zooms in, >1 zooms out), keeping the center of the
// current view fixed, then re-clamps to the data range.
static void zoom_view(tio_trace_display_params* params, int width, double data_start, double data_end, double factor) {
    double visible = params->time_per_char * width;
    double center = params->start + visible * 0.5;

    visible *= factor;

    // Don't allow zooming in past 1/1000th of the full data range.
    double min_visible = (data_end - data_start) / (1000.0 * width);
    if (min_visible <= 0.0) min_visible = 1e-9;
    if (visible < min_visible) visible = min_visible;

    params->start = center - visible * 0.5;
    params->time_per_char = visible / width;
    clamp_view(params, width, data_start, data_end);
}

// Pans by 10% of the currently visible time range, then re-clamps.
static void pan_view(tio_trace_display_params* params, int width, double data_start, double data_end, double direction) {
    double visible = params->time_per_char * width;
    params->start += direction * visible * 0.1;
    clamp_view(params, width, data_start, data_end);
}

static void trace_sleep_ms(int ms) {
#if defined(_WIN32) || defined(_WIN64)
    Sleep((DWORD)ms);
#else
    struct timespec req = { .tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000L };
    nanosleep(&req, NULL);
#endif
}

tio_ctx_t tio_ctx;

void cleanup(void) {
    tio_destroy(&tio_ctx);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <trace-log-file>\n", argv[0]);
        return 1;
    }

    tio_trace_t trace = {0};
    if (tio_trace_read_log(argv[1], &trace) != 0) {
        return 1;
    }

    int any_tasks = 0;
    for (int g = 0; g < trace.num_groups; g++) {
        if (trace.group[g].num_tasks > 0) any_tasks = 1;
    }
    if (!any_tasks) {
        fprintf(stderr, "no complete tasks found in '%s'\n", argv[1]);
        tio_trace_free(&trace);
        return 1;
    }

    double data_start = 0.0, data_end = 0.0;
    int seen_any = 0;
    for (int g = 0; g < trace.num_groups; g++) {
        tio_trace_group_t* group = &trace.group[g];
        for (int i = 0; i < group->num_tasks; i++) {
            if (!seen_any) {
                data_start = group->starts[i];
                data_end = group->ends[i];
                seen_any = 1;
                continue;
            }
            if (group->starts[i] < data_start) data_start = group->starts[i];
            if (group->ends[i] > data_end) data_end = group->ends[i];
        }
    }

    tio_init(&tio_ctx);
    atexit(cleanup);

    tio_trace_display display;
    tio_trace_display_init(&display, &tio_ctx, trace);

    tio_trace_display_params params;
    params.start = data_start;
    params.end = data_end;
    params.time_per_char = (data_end - data_start) / display.width;
    if (params.time_per_char <= 0.0) params.time_per_char = 1.0;

    printf("\x1b[2J"); // Clear screen
    printf("\x1b[?25l"); // Hide cursor
    fflush(stdout);

    int quit_requested = 0;
    while (!quit_requested) {
        int queue_bytes = tio_get_event_queue_byte_size(&tio_ctx);
        int bytes_processed = 0;
        while (bytes_processed < queue_bytes) {
            tio_input_event event = TIO_INPUT_EVENT_INITIALIZER;
            bytes_processed += tio_pop_event_queue(&tio_ctx, &event);
            if (event.type != TIO_INPUT_EVENT_TYPE_KEY) continue;

            switch (event.code) {
            case 'q':
            case CTRL_Q:
                quit_requested = 1;
                break;
            case 'w':
                zoom_view(&params, display.width, data_start, data_end, 0.9);
                break;
            case 's':
                zoom_view(&params, display.width, data_start, data_end, 1.1);
                break;
            case 'a':
                pan_view(&params, display.width, data_start, data_end, -1.0);
                break;
            case 'd':
                pan_view(&params, display.width, data_start, data_end, 1.0);
                break;
            }
        }

        tio_trace_display_refresh(&display, &params, trace);
        tio_trace_display_output(&display, &tio_ctx);

        if (!quit_requested) trace_sleep_ms(16);
    }

    printf("\x1b[%d;1H\r\n", display.height + 1);
    printf("\x1b[?25h"); // Show cursor
    fflush(stdout);

    tio_trace_display_free(&display);
    tio_trace_free(&trace);
    return 0;
}

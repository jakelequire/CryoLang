/* startup_helpers.c - C startup hooks for the
 * linux_startup_runs_c_initializers_once project.
 *
 * The object holds one .preinit_array entry, one .init_array constructor and
 * one .fini_array destructor. A Cryo program pulls it out of the archive by
 * calling the two counters, and so links all three hooks with it:
 *
 *   - each initializer counts its runs; the program prints both counts, and
 *     glibc's startup runs each exactly once;
 *   - the destructor writes a line straight to fd 1 when the process exits,
 *     which happens only if the exit runs the executable's .fini_array.
 */
#include <unistd.h>

static int preinit_runs = 0;
static int init_runs = 0;

static void count_preinit_run(void) { preinit_runs++; }

__attribute__((section(".preinit_array"), used))
static void (*preinit_entry)(void) = count_preinit_run;

__attribute__((constructor))
static void count_init_run(void) { init_runs++; }

__attribute__((destructor))
static void announce_exit(void) {
    static const char line[] = "the C destructor ran at exit\n";
    ssize_t written = write(1, line, sizeof line - 1);
    (void)written;
}

int startup_preinit_runs(void) { return preinit_runs; }

int startup_init_runs(void) { return init_runs; }

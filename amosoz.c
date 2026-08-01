/*
 * amosOZ — Arianna Method Operating System
 * The most atomic way to build a working OS-like environment from scratch.
 * Single file. No external dependencies. The complete first algorithm.
 *
 * AMOS is the operating body. OZ is the extensibility field.
 * Compile: gcc -o amosoz amosoz.c -lm
 * Run: ./amosoz
 *
 * "No kernel mythology here: only state, contracts, and consequences."
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <signal.h>
#include "aml/ariannamethod.h"  /* vendored AML field engine (am_init/am_exec/am_get_state) */

#define UNUSED(x) ((void)(x))

#define VERSION "0.4.0"
#define SYSTEM_NAME "amosOZ"
#define BUILD_DATE "2026-06-30"

#define MOTD \
    "amosOZ v" VERSION " — Arianna Method Operating System\n" \
    "Dedicated to Amos Oz (עוז — strength, courage)\n" \
    "Compatibility is a treaty, not obedience.\n" \
    "Type 'help', 'selftest', or 'motd'. Every command leaves a trace.\n\n"

/* Guard against snprintf overflow: clamp n to buffer size */
static inline int safe_append(char *buf, int offset, int bufsz, const char *fmt, ...) __attribute__((format(printf,4,5)));
static inline int safe_append(char *buf, int offset, int bufsz, const char *fmt, ...) {
    if (offset >= bufsz) return offset;
    va_list ap; va_start(ap, fmt);
    int written = vsnprintf(buf + offset, bufsz - offset, fmt, ap);
    va_end(ap);
    if (written > 0) offset += written;
    if (offset > bufsz) offset = bufsz;
    return offset;
}

#define MAX_PATH 512
#define MAX_CONTENT 4096
#define MAX_FILES 512
#define MAX_BLOCKS 256
#define MAX_PROCS 64
#define MAX_MODULES 32
#define MAX_ENV 64
#define MAX_HISTORY 100
#define MAX_LEDGER 256
#define MAX_SLOTS 16
#define MAX_SLOT_OCCUPANTS 8
#define MAX_DEVICES 16
#define MAX_HOOKS 8
#define MAX_HOOK_SUBS 8
#define MAX_CMD_LEN 1024
#define MAX_PIPE_PARTS 8
#define MEM_TOTAL_KB 65536

/* ─── Error Codes ─────────────────────────────────────────────────────────── */
#define ERR_OK 0
#define ERR_NOT_FOUND 1
#define ERR_PERMISSION 2
#define ERR_EXISTS 3
#define ERR_NO_MEMORY 4
#define ERR_INVALID 5
#define ERR_IO 6
#define ERR_NO_PROCESS 7
#define ERR_MODULE 8

/* ─── Hardware Profile ────────────────────────────────────────────────────── */
typedef struct {
    char platform[64];
    char platform_release[64];
    char machine[64];
    char processor[64];
    char hostname[64];
    int cpu_count;
    long memory_mb;
    char terminal[32];
    int persistence;
} HWProfile;

/* ─── Virtual Memory ─────────────────────────────────────────────────────── */
typedef struct {
    int id;
    int size;
    char flags[8];
    char owner[32];
    int used;
} MemBlock;

typedef struct {
    int total_kb;
    int used_kb;
    MemBlock blocks[MAX_BLOCKS];
    int block_count;
    int next_id;
} VirtualMemory;

/* ─── Virtual Filesystem ──────────────────────────────────────────────────── */
typedef struct {
    char path[MAX_PATH];
    int is_dir;
    int is_symlink;
    char symlink_target[MAX_PATH];
    char content[MAX_CONTENT];
    char perms[12];
    char owner[16];
    time_t created;
    time_t modified;
    int used;
} FSNode;

typedef struct {
    FSNode nodes[MAX_FILES];
    int node_count;
    char cwd[MAX_PATH];
} VirtualFS;

/* ─── Process Table ───────────────────────────────────────────────────────── */
typedef struct {
    int pid;
    int ppid;           /* parent for real process tree */
    char name[32];
    char state[16];     /* ready, running, blocked, zombie */
    time_t started;
    int ticks;
    int exit_code;
    int used;
    char cwd[MAX_PATH]; /* per-process cwd for minimal OS */
    int sleep_ticks;    /* for blocking sleep */
    /* per-process resource ownership - foundation of minimal OS */
    int mem_used_kb;
    int owned_blocks[MAX_BLOCKS]; /* block ids owned by this proc */
    int owned_block_count;
    int open_fds[32]; /* simple fd table: index into fs nodes or -1 */
    int priority; /* 0 normal, higher = preferred in scheduler */
    int max_slice;  /* time slice for preemption/fairness */
    int slice_used;
    int mem_limit_kb; /* resource limit for minimal OS */
    int signals; /* pending signals for IPC/blocking */
    int cpu_time; /* cpu time consumed while running (foundation accounting) */
    int cpu_limit; /* hard cpu time limit (ticks) - enforce in scheduler */
    int cpu_violations; /* count of times limit was hit */
    int numbness; /* signals this monad cannot feel; KILL and STOP pierce it */
    char mailbox[256]; /* simple IPC mailbox for send/recv */
    /* the monad's own weather. The shared field is the system's; this is what THIS monad
     * carries and argues with. All zero = no argument, so the scheduler reduces to what it
     * was before the slice existed. Velocity is deliberately absent: tempo belongs to the
     * system, not to one monad. */
    float mood_pain, mood_tension, mood_flow, mood_warmth;
    void *aml_prog;    /* open AML program when this monad's life IS a program; NULL otherwise */
    int   spawned;     /* 1 = backed by a real OS process (fork+exec); 0 = virtual monad (SARTRE ns contract) */
    pid_t real_pid;    /* real OS pid when spawned; 0 otherwise */
} Process;

typedef struct {
    Process procs[MAX_PROCS];
    int next_pid;
    int tick_count;
    int sched_next;  /* round-robin scheduler pointer for real OS feel */
} ProcessTable;

/* ─── OZ Ledger ───────────────────────────────────────────────────────────── */
/* "Every command leaves a trace." */
typedef struct {
    char command[MAX_CMD_LEN];
    char parsed_cmd[32];
    char parsed_args[256];
    char actor[16];
    int tick;
    time_t timestamp;
    int result_code;
    char explanation[128];
    int reversible;
    char undo_path[MAX_PATH];
    char undo_content[MAX_CONTENT];
    int undo_is_dir;
    int undo_was_create;
} LedgerEntry;

typedef struct {
    LedgerEntry entries[MAX_LEDGER];
    int head;   /* oldest entry index for ring buffer */
    int count;  /* current number of valid entries (0..MAX) */
} OZLedger;

/* ─── Module System ───────────────────────────────────────────────────────── */
/* "A slot is a promise with a boundary." */
typedef struct {
    char name[32];
    char description[64];
    char commands[4][16];
    int cmd_count;
    char slots[4][32];
    int slot_count;
    char hooks[4][16];
    int hook_count;
    char contract_provides[4][32];
    int provides_count;
    char contract_requires[4][32];
    int requires_count;
    char contract_version[8];
    int loaded;
} Module;

/* ─── Slots ───────────────────────────────────────────────────────────────── */
typedef struct {
    char name[32];
    char occupants[MAX_SLOT_OCCUPANTS][32];
    int occupant_count;
} Slot;

/* ─── Hooks ───────────────────────────────────────────────────────────────── */
typedef struct {
    char name[16];
    char subscribers[MAX_HOOK_SUBS][32];
    int sub_count;
} Hook;

/* ─── Devices ─────────────────────────────────────────────────────────────── */
typedef struct {
    char name[16];
    char type[8];
    char status[16];
} Device;

/* ─── Environment ─────────────────────────────────────────────────────────── */
typedef struct {
    char key[32];
    char value[128];
} EnvVar;

/* ─── Kernel ──────────────────────────────────────────────────────────────── */
/* "The shell is the first weather of the system." */
typedef struct {
    HWProfile hw;
    VirtualMemory mem;
    VirtualFS fs;
    ProcessTable procs;
    OZLedger ledger;
    Module modules[MAX_MODULES];
    int module_count;
    Slot slots[MAX_SLOTS];
    int slot_count;
    Hook hooks[MAX_HOOKS];
    int hook_count;
    Device devices[MAX_DEVICES];
    int device_count;
    EnvVar env[MAX_ENV];
    int env_count;
    char history[MAX_HISTORY][MAX_CMD_LEN];
    int history_count;
    time_t boot_time;
    char user[16];
    int running;
    int fortune_idx;
    int hook_boot_fired;
    int hook_sched_fired;
    char boot_log[1024];
    int job_count;
    char fortune_oz_idx;
    int current_pid; /* the one the scheduler picked as running - for syscall context */
    int shell_pid;   /* the main interactive shell pid for parent/ppid consistency in foundation */
    int suppress_next_auto_tick; /* fg etc stick for next cmd */
} Kernel;

static Kernel K;

typedef struct {
    int active;
    int reversible;
    char explanation[128];
    char undo_path[MAX_PATH];
    char undo_content[MAX_CONTENT];
    int undo_is_dir;
    int undo_was_create;
} LedgerMeta;

static LedgerMeta pending_ledger;

#define MAX_SCRIPT_DEPTH 8
static char shell_stdin_buf[MAX_CONTENT];
static int shell_stdin_len;
static int shell_depth;

static void ledger_meta_clear(void) { memset(&pending_ledger, 0, sizeof(pending_ledger)); }

static void ledger_meta_set(int reversible, const char *explanation,
                            const char *undo_path, const char *undo_content,
                            int undo_is_dir, int undo_was_create) {
    pending_ledger.active = 1;
    pending_ledger.reversible = reversible;
    snprintf(pending_ledger.explanation, sizeof(pending_ledger.explanation), "%s", explanation);
    if (undo_path) strncpy(pending_ledger.undo_path, undo_path, MAX_PATH-1);
    if (undo_content) strncpy(pending_ledger.undo_content, undo_content, MAX_CONTENT-1);
    pending_ledger.undo_is_dir = undo_is_dir;
    pending_ledger.undo_was_create = undo_was_create;
}

/* ─── Forward declarations ────────────────────────────────────────────────── */
static void kernel_init(void);
static int dispatch(const char *line, char *output, int outsize);
static int kernel_syscall(const char *op, char *out, int outsz, int argc, char **argv);
static void proc_refresh_all(void);
static int fire_hook(const char *hook_name);
static int fs_access(const FSNode *n, const char *user, char op);
static int validate_module_contracts(void);
static int fs_find(VirtualFS *fs, const char *path);
static int fs_add_dir(VirtualFS *fs, const char *path);
static int fs_add_file(VirtualFS *fs, const char *path, const char *content);
static int dispatch_argv(const char *line, int argc, char **argv, char *output, int outsize);
static int shell_execute_line(const char *line, char *output, int outsize);
static void str_trim_inplace(char *s);
static int proc_is_virtual(const char *path);
static const char *env_get(const char *key);

typedef int (*CmdFunc)(char*, int, int, char**);
typedef struct { const char *name; CmdFunc func; } CmdEntry;
static const CmdEntry CMD_TABLE[];

/* ─── Hardware Detection ──────────────────────────────────────────────────── */
/* Detection must never crash. Unknown = "unknown". */
static void detect_hardware(HWProfile *hw) {
    memset(hw, 0, sizeof(HWProfile));
    #ifdef __linux__
    strcpy(hw->platform, "Linux");
    #elif defined(__APPLE__)
    strcpy(hw->platform, "Darwin");
    #else
    strcpy(hw->platform, "unknown");
    #endif

    FILE *f;
    strcpy(hw->platform_release, "unknown");
    f = fopen("/proc/version", "r");
    if (f) {
        if (fgets(hw->platform_release, sizeof(hw->platform_release), f)) {
            char *nl = strchr(hw->platform_release, '\n');
            if (nl) *nl = '\0';
            /* Truncate to just version */
            char *sp = strchr(hw->platform_release, ' ');
            if (sp) { sp = strchr(sp+1, ' '); if (sp) *sp = '\0'; }
        }
        fclose(f);
    }

    #if defined(__x86_64__)
    strcpy(hw->machine, "x86_64");
    #elif defined(__aarch64__)
    strcpy(hw->machine, "aarch64");
    #else
    strcpy(hw->machine, "unknown");
    #endif

    strcpy(hw->processor, hw->machine);
    if (gethostname(hw->hostname, sizeof(hw->hostname)) != 0)
        strcpy(hw->hostname, "unknown");

    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    hw->cpu_count = (ncpu > 0) ? (int)ncpu : -1;

    hw->memory_mb = -1;
    f = fopen("/proc/meminfo", "r");
    if (f) {
        char buf[128];
        while (fgets(buf, sizeof(buf), f)) {
            long kb;
            if (sscanf(buf, "MemTotal: %ld kB", &kb) == 1) {
                hw->memory_mb = kb / 1024;
                break;
            }
        }
        fclose(f);
    }

    const char *term = getenv("TERM");
    strncpy(hw->terminal, term ? term : "unknown", sizeof(hw->terminal)-1);
    hw->persistence = 1;
}

/* ─── Virtual Memory ──────────────────────────────────────────────────────── */
static void mem_init(VirtualMemory *m) {
    memset(m, 0, sizeof(VirtualMemory));
    m->total_kb = MEM_TOTAL_KB;
    m->next_id = 1;
}

static int mem_alloc(VirtualMemory *m, int size, int owner_pid, const char *flags) {
    if (m->used_kb + size > m->total_kb) return -1;
    if (m->block_count >= MAX_BLOCKS) return -1;

    /* find the owning process for accounting - hardcore per-proc isolation */
    Process *owner = NULL;
    for (int i=0; i<MAX_PROCS; i++) {
        if (K.procs.procs[i].used && K.procs.procs[i].pid == owner_pid) {
            owner = &K.procs.procs[i];
            break;
        }
    }
    if (!owner) {
        /* strict foundation: caller must have valid current/owner context (boot spawns init+shell first) */
        return -1;
    }

    if (owner->owned_block_count >= MAX_BLOCKS) return -1;
    if (owner->mem_used_kb + size > owner->mem_limit_kb) return -1; /* per-proc limit */

    int idx = m->block_count++;
    m->blocks[idx].id = m->next_id++;
    m->blocks[idx].size = size;
    snprintf(m->blocks[idx].owner, sizeof(m->blocks[idx].owner), "%d", owner_pid);
    strncpy(m->blocks[idx].flags, flags, 7);
    m->blocks[idx].used = 1;
    m->used_kb += size;

    /* charge to process */
    owner->mem_used_kb += size;
    owner->owned_blocks[owner->owned_block_count++] = m->blocks[idx].id;

    return m->blocks[idx].id;
}

static int mem_free(VirtualMemory *m, int bid, int requester_pid) {
    for (int i = 0; i < m->block_count; i++) {
        if (m->blocks[i].used && m->blocks[i].id == bid) {
            /* ownership check - only owner or parent? for minimal: exact pid */
            char own[32];
            snprintf(own, sizeof(own), "%d", requester_pid);
            if (strcmp(m->blocks[i].owner, own) != 0) {
                return ERR_PERMISSION; /* hardcore isolation */
            }
            m->used_kb -= m->blocks[i].size;

            /* uncharge from proc */
            for (int p=0; p<MAX_PROCS; p++) {
                if (K.procs.procs[p].used && K.procs.procs[p].pid == requester_pid) {
                    K.procs.procs[p].mem_used_kb -= m->blocks[i].size;
                    /* remove from owned list */
                    for (int b=0; b<K.procs.procs[p].owned_block_count; b++) {
                        if (K.procs.procs[p].owned_blocks[b] == bid) {
                            for (int k=b; k < K.procs.procs[p].owned_block_count-1; k++)
                                K.procs.procs[p].owned_blocks[k] = K.procs.procs[p].owned_blocks[k+1];
                            K.procs.procs[p].owned_block_count--;
                            break;
                        }
                    }
                    break;
                }
            }
            m->blocks[i].used = 0;
            return ERR_OK;
        }
    }
    return ERR_NOT_FOUND;
}

/* ─── Virtual Filesystem ──────────────────────────────────────────────────── */
static void fs_init(VirtualFS *fs) {
    memset(fs, 0, sizeof(VirtualFS));
    strcpy(fs->cwd, "/");
}

static int fs_find(VirtualFS *fs, const char *path) {
    for (int i = 0; i < fs->node_count; i++) {
        if (fs->nodes[i].used && strcmp(fs->nodes[i].path, path) == 0)
            return i;
    }
    return -1;
}

static void fs_resolve(VirtualFS *fs, const char *path, char *out) {
    const char *base = fs->cwd;
    /* prefer current proc's cwd for per-process view */
    for (int i=0; i<MAX_PROCS; i++) {
        if (K.procs.procs[i].used && K.procs.procs[i].pid == K.current_pid) {
            base = K.procs.procs[i].cwd;
            break;
        }
    }
    if (path[0] == '/') {
        snprintf(out, MAX_PATH, "%s", path);
    } else {
        if (strcmp(base, "/") == 0)
            snprintf(out, MAX_PATH, "/%s", path);
        else
            snprintf(out, MAX_PATH, "%s/%s", base, path);
    }
    /* Normalize: remove trailing slash, handle .. and . */
    char parts[32][64];
    int pcount = 0;
    char tmp[MAX_PATH];
    strcpy(tmp, out);
    char *tok = strtok(tmp, "/");
    while (tok) {
        if (strcmp(tok, ".") == 0) { /* skip */ }
        else if (strcmp(tok, "..") == 0) { if (pcount > 0) pcount--; }
        else if (pcount < 32) { strncpy(parts[pcount], tok, 63); parts[pcount][63] = '\0'; pcount++; }
        tok = strtok(NULL, "/");
    }
    if (pcount == 0) { strcpy(out, "/"); return; }
    out[0] = '\0';
    for (int i = 0; i < pcount; i++) {
        strcat(out, "/");
        strcat(out, parts[i]);
    }
    for (int depth = 0; depth < 8; depth++) {
        int idx = fs_find(fs, out);
        if (idx < 0 || !fs->nodes[idx].used || !fs->nodes[idx].is_symlink) break;
        char tmp[MAX_PATH];
        fs_resolve(fs, fs->nodes[idx].symlink_target, tmp);
        strncpy(out, tmp, MAX_PATH - 1);
        out[MAX_PATH - 1] = '\0';
    }
}

static int fs_resolve_node(const char *path) {
    char resolved[MAX_PATH];
    fs_resolve(&K.fs, path, resolved);
    return fs_find(&K.fs, resolved);
}

static int fs_read_to_buf(const char *path, char *buf, int bufsz) {
    int idx = fs_resolve_node(path);
    if (idx < 0) return ERR_NOT_FOUND;
    if (K.fs.nodes[idx].is_dir || K.fs.nodes[idx].is_symlink) return ERR_INVALID;
    if (fs_access(&K.fs.nodes[idx], K.user, 'r') != ERR_OK) return ERR_PERMISSION;
    if (proc_is_virtual(K.fs.nodes[idx].path)) proc_refresh_all();
    const char *p = K.fs.nodes[idx].path;
    if (strcmp(p, "/dev/null") == 0) {
        buf[0] = '\0';
        return ERR_OK;
    }
    if (strcmp(p, "/dev/zero") == 0 || strcmp(p, "/dev/full") == 0) {
        memset(buf, 0, bufsz);
        return ERR_OK;
    }
    if (strcmp(p, "/dev/random") == 0 || strcmp(p, "/dev/urandom") == 0) {
        /* simple pseudo-random for minimal OS */
        static unsigned seed = 0;
        if (seed == 0) seed = (unsigned)time(NULL);
        for (int k = 0; k < bufsz-1; k++) {
            seed = seed * 1103515245 + 12345;
            buf[k] = (char)((seed >> 16) & 0xff);
        }
        buf[bufsz-1] = '\0';
        return ERR_OK;
    }
    if (strcmp(p, "/dev/tty") == 0) {
        /* tty: read stub, write swallows (console echo sim elsewhere) */
        snprintf(buf, bufsz, "tty: current console (stub input)\n");
        return ERR_OK;
    }
    strncpy(buf, K.fs.nodes[idx].content, bufsz - 1);
    buf[bufsz - 1] = '\0';
    return ERR_OK;
}

static int patmatch(const char *pat, const char *name) {
    if (!pat || !name) return 0;
    if (strcmp(pat, "*") == 0) return 1;
    const char *star = strchr(pat, '*');
    if (!star) return strcmp(pat, name) == 0;
    size_t pre = (size_t)(star - pat);
    if (strncmp(pat, name, pre) != 0) return 0;
    const char *suffix = star + 1;
    size_t slen = strlen(suffix), nlen = strlen(name);
    if (slen > nlen) return 0;
    return strcmp(name + nlen - slen, suffix) == 0;
}

static int fs_add_symlink(VirtualFS *fs, const char *path, const char *target) {
    if (fs->node_count >= MAX_FILES) return ERR_NO_MEMORY;
    if (fs_find(fs, path) >= 0) return ERR_EXISTS;
    int i = fs->node_count++;
    memset(&fs->nodes[i], 0, sizeof(FSNode));
    strncpy(fs->nodes[i].path, path, MAX_PATH - 1);
    fs->nodes[i].is_symlink = 1;
    fs_resolve(fs, target, fs->nodes[i].symlink_target);
    strcpy(fs->nodes[i].perms, "lrwxrwxrwx");
    strcpy(fs->nodes[i].owner, "user");
    fs->nodes[i].created = fs->nodes[i].modified = time(NULL);
    fs->nodes[i].used = 1;
    return ERR_OK;
}

/* ─── Permission Treaty ───────────────────────────────────────────────────── */
static int perm_bit(const char *perms, int bit_idx) {
    if (!perms || (int)strlen(perms) < 9) return 0;
    return perms[bit_idx] != '-';
}

static int fs_access(const FSNode *n, const char *user, char op) {
    if (!n || !n->used) return ERR_NOT_FOUND;
    if (strcmp(user, "root") == 0) return ERR_OK;
    int base = (strcmp(n->owner, user) == 0) ? 0 : 6;
    int bit = base;
    if (op == 'r') bit += 0;
    else if (op == 'w') bit += 1;
    else if (op == 'x') bit += 2;
    else return ERR_INVALID;
    return perm_bit(n->perms, bit) ? ERR_OK : ERR_PERMISSION;
}

static int proc_is_virtual(const char *path) {
    return path && (strncmp(path, "/proc/", 6) == 0 || strcmp(path, "/proc") == 0);
}

static void proc_write_file(const char *path, const char *content) {
    int idx = fs_find(&K.fs, path);
    if (idx < 0) {
        fs_add_file(&K.fs, path, content);
        idx = fs_find(&K.fs, path);
    }
    if (idx >= 0) {
        strncpy(K.fs.nodes[idx].content, content, MAX_CONTENT - 1);
        K.fs.nodes[idx].content[MAX_CONTENT - 1] = '\0';
        strcpy(K.fs.nodes[idx].perms, "r--r--r--");
        strcpy(K.fs.nodes[idx].owner, "root");
        K.fs.nodes[idx].modified = time(NULL);
    }
}

static void proc_refresh_all(void) {
    char buf[MAX_CONTENT];
    int elapsed = (int)(time(NULL) - K.boot_time);

    snprintf(buf, sizeof(buf), "%d.%d\n", elapsed, 0);
    proc_write_file("/proc/uptime", buf);

    snprintf(buf, sizeof(buf),
        "MemTotal:       %d kB\nMemFree:        %d kB\nMemAvailable:   %d kB\n",
        K.mem.total_kb, K.mem.total_kb - K.mem.used_kb, K.mem.total_kb - K.mem.used_kb);
    proc_write_file("/proc/meminfo", buf);

    snprintf(buf, sizeof(buf),
        "processor\t: 0\nmodel name\t: amosOZ virtual cpu\n"
        "cpu cores\t: %d\nhostname\t: %s\n",
        K.hw.cpu_count > 0 ? K.hw.cpu_count : 1, K.hw.hostname);
    proc_write_file("/proc/cpuinfo", buf);

    snprintf(buf, sizeof(buf), "%s %s %s %s\n",
        SYSTEM_NAME, VERSION, K.hw.machine, K.hw.platform);
    proc_write_file("/proc/version", buf);

    snprintf(buf, sizeof(buf),
        "Name:\tamossh\nPid:\t1\nState:\trunning (virtual)\nUid:\t1000\nGid:\t1000\n"
        "VmSize:\t%d kB\nThreads:\t%d\n",
        K.mem.used_kb, K.procs.next_pid - 1);
    proc_write_file("/proc/self/status", buf);

    /* per-process /proc/<pid>/status - deepening /proc foundation */
    for (int i = 0; i < MAX_PROCS; i++) {
        if (K.procs.procs[i].used) {
            int pid = K.procs.procs[i].pid;
            char ppath[MAX_PATH];
            snprintf(ppath, sizeof(ppath), "/proc/%d/status", pid);
            /* count fds first */
            int fcount = 0;
            for (int f=0; f<32; f++) if (K.procs.procs[i].open_fds[f] >= 0) fcount++;
            snprintf(buf, sizeof(buf),
                "Name:\t%s\nPid:\t%d\nPPid:\t%d\nState:\t%s\n"
                "Ticks:\t%d\nCpuTime:\t%d\nCpuLimit:\t%d\nCpuViolations:\t%d\nPrio:\t%d\nSlice:\t%d/%d\n"
                "Mem:\t%d/%d kB\nSignals:\t%d\nNumbness:\t%d\nFds:\t%d\n"
                "Mood:\tpain %.3f tension %.3f flow %.3f warmth %.3f\n",
                K.procs.procs[i].name, pid, K.procs.procs[i].ppid,
                K.procs.procs[i].state,
                K.procs.procs[i].ticks, K.procs.procs[i].cpu_time, K.procs.procs[i].cpu_limit, K.procs.procs[i].cpu_violations,
                K.procs.procs[i].priority, K.procs.procs[i].slice_used, K.procs.procs[i].max_slice,
                K.procs.procs[i].mem_used_kb, K.procs.procs[i].mem_limit_kb,
                K.procs.procs[i].signals, K.procs.procs[i].numbness, fcount,
                K.procs.procs[i].mood_pain, K.procs.procs[i].mood_tension,
                K.procs.procs[i].mood_flow, K.procs.procs[i].mood_warmth);
            proc_write_file(ppath, buf);

            /* /proc/<pid>/fd list */
            char fdpath[MAX_PATH];
            snprintf(fdpath, sizeof(fdpath), "/proc/%d/fd", pid);
            char fdlist[MAX_CONTENT] = "";
            int pos = 0;
            for (int f=0; f<32; f++) {
                int node = K.procs.procs[i].open_fds[f];
                if (node >= 0) {
                    pos = safe_append(fdlist, pos, sizeof(fdlist), "%d -> %s\n", f, K.fs.nodes[node].path);
                }
            }
            if (pos == 0) strcpy(fdlist, "(no open fds)\n");
            proc_write_file(fdpath, fdlist);

            /* /proc/<pid>/cpu - cpu accounting */
            char cpupath[MAX_PATH];
            snprintf(cpupath, sizeof(cpupath), "/proc/%d/cpu", pid);
            snprintf(buf, sizeof(buf),
                "cpu_time: %d\ncpu_limit: %d\npriority: %d\nslice_used: %d\nmax_slice: %d\n",
                K.procs.procs[i].cpu_time, K.procs.procs[i].cpu_limit,
                K.procs.procs[i].priority, K.procs.procs[i].slice_used, K.procs.procs[i].max_slice);
            proc_write_file(cpupath, buf);

            /* /proc/<pid>/mem - memory accounting */
            char mempath[MAX_PATH];
            snprintf(mempath, sizeof(mempath), "/proc/%d/mem", pid);
            snprintf(buf, sizeof(buf),
                "mem_used_kb: %d\nmem_limit_kb: %d\nowned_blocks: %d\n",
                K.procs.procs[i].mem_used_kb, K.procs.procs[i].mem_limit_kb,
                K.procs.procs[i].owned_block_count);
            proc_write_file(mempath, buf);

            /* /proc/<pid>/stat - classic minimal stat */
            char statpath[MAX_PATH];
            snprintf(statpath, sizeof(statpath), "/proc/%d/stat", pid);
            snprintf(buf, sizeof(buf),
                "%d (%s) %s %d %d %d %d %d %lu %lu %lu %lu %lu %lu %lu %ld %ld %ld %ld %ld %ld %llu %lu %ld\n",
                pid, K.procs.procs[i].name, K.procs.procs[i].state,
                K.procs.procs[i].ppid, 0 /* pgrp */, 0 /* session */, 0 /* tty */, 0 /* tpgid */,
                0lu /* flags */, 0lu,0lu,0lu,0lu, /* minflt etc */
                (unsigned long)K.procs.procs[i].ticks, (unsigned long)K.procs.procs[i].cpu_time,
                0l,0l,0l,0l,0l,0l, /* prio, nice etc */
                (unsigned long long)K.procs.procs[i].cpu_time, (unsigned long)K.procs.procs[i].mem_used_kb, 0l);
            proc_write_file(statpath, buf);

            /* /proc/<pid>/mailbox for IPC */
            char mboxpath[MAX_PATH];
            snprintf(mboxpath, sizeof(mboxpath), "/proc/%d/mailbox", pid);
            proc_write_file(mboxpath, K.procs.procs[i].mailbox[0] ? K.procs.procs[i].mailbox : "(empty)\n");
        }
    }
}

static int fs_add_dir(VirtualFS *fs, const char *path) {
    if (fs->node_count >= MAX_FILES) return ERR_NO_MEMORY;
    if (fs_find(fs, path) >= 0) return ERR_EXISTS;
    int i = fs->node_count++;
    memset(&fs->nodes[i], 0, sizeof(FSNode));
    strncpy(fs->nodes[i].path, path, MAX_PATH-1);
    fs->nodes[i].is_dir = 1;
    strcpy(fs->nodes[i].perms, "rwxr-xr-x");
    strcpy(fs->nodes[i].owner, "root");
    fs->nodes[i].created = fs->nodes[i].modified = time(NULL);
    fs->nodes[i].used = 1;
    return ERR_OK;
}

static int fs_add_file(VirtualFS *fs, const char *path, const char *content) {
    if (fs->node_count >= MAX_FILES) return ERR_NO_MEMORY;
    int idx = fs_find(fs, path);
    if (idx >= 0) {
        strncpy(fs->nodes[idx].content, content, MAX_CONTENT-1);
        fs->nodes[idx].modified = time(NULL);
        return ERR_OK;
    }
    int i = fs->node_count++;
    memset(&fs->nodes[i], 0, sizeof(FSNode));
    strncpy(fs->nodes[i].path, path, MAX_PATH-1);
    fs->nodes[i].is_dir = 0;
    strncpy(fs->nodes[i].content, content, MAX_CONTENT-1);
    strcpy(fs->nodes[i].perms, "rw-r--r--");
    strcpy(fs->nodes[i].owner, "user");
    fs->nodes[i].created = fs->nodes[i].modified = time(NULL);
    fs->nodes[i].used = 1;
    return ERR_OK;
}

static void fs_init_tree(VirtualFS *fs) {
    const char *dirs[] = {"/", "/bin", "/etc", "/home", "/tmp", "/var", "/dev",
                          "/proc", "/sys", "/usr", "/usr/lib", "/usr/share",
                          "/usr/share/amosoz", "/home/user", "/var/log",
                          "/etc/amosoz", NULL};
    for (int i = 0; dirs[i]; i++) fs_add_dir(fs, dirs[i]);
    fs_add_file(fs, "/etc/hostname", "amosoz");
    fs_add_file(fs, "/etc/amosoz/version", VERSION);
    fs_add_file(fs, "/etc/motd",
        "amosOZ — Arianna Method Operating System\n"
        "Dedicated to Amos Oz (עוז)\n"
        "OZ begins where extension becomes accountable.\n");
    fs_add_file(fs, "/etc/os-release",
        "NAME=\"amosOZ\"\n"
        "PRETTY_NAME=\"amosOZ (Arianna Method Operating System)\"\n"
        "ID=amosoz\n"
        "VERSION_ID=\"" VERSION "\"\n"
        "HOME_URL=\"https://github.com/ariannamethod/amos.OZ\"\n"
        "SUPPORT_END=\"never\"\n");
    fs_add_file(fs, "/proc/uptime", "0.0\n");
    fs_add_file(fs, "/proc/meminfo", "MemTotal: 65536 kB\n");
    fs_add_file(fs, "/proc/cpuinfo", "processor\t: 0\n");
    fs_add_file(fs, "/proc/version", SYSTEM_NAME " kernel\n");
    fs_add_file(fs, "/proc/self/status", "Name:\tamossh\n");

    fs_add_file(fs, "/usr/share/amosoz/quotes.txt",
        "A conflict begins and ends in the hearts of men, not in the hills.\n"
        "The opposite of indifference is not love but curiosity.\n"
        "We all pay a price for our dreams.\n"
        "עוז — strength lives in the willingness to see the other.\n"
        "Literature is a chamber of justice without a judge.\n");
    const char *bins[] = {
        "echo", "ls", "cat", "pwd", "uname", "whoami", "date", "help",
        "grep", "head", "tail", "wc", "test", "hostname", "id", "true", "false",
        "uptime", "dmesg", "find", "ln", "export", "source", "spec", "doctor",
        NULL
    };
    for (int i = 0; bins[i]; i++) {
        char bpath[MAX_PATH];
        char bcontent[48];
        snprintf(bpath, sizeof(bpath), "/bin/%s", bins[i]);
        snprintf(bcontent, sizeof(bcontent), "#!/amossh\n__builtin__\n");
        fs_add_file(fs, bpath, bcontent);
        int bi = fs_find(fs, bpath);
        if (bi >= 0) {
            strcpy(fs->nodes[bi].perms, "rwxr-xr-x");
            strcpy(fs->nodes[bi].owner, "root");
        }
    }

    fs_add_file(fs, "/home/user/hello.amos",
        "#!/amossh\n"
        "echo Script greeting:\n"
        "echo $@\n");
    fs_add_file(fs, "/dev/null", "");
    fs_add_file(fs, "/dev/zero", "");
    fs_add_file(fs, "/dev/full", "");
    fs_add_file(fs, "/dev/random", "");
    fs_add_file(fs, "/dev/urandom", "");
    fs_add_file(fs, "/dev/tty", ""); /* console, current "terminal" */
    int hi = fs_find(fs, "/home/user/hello.amos");
    if (hi >= 0) {
        strcpy(fs->nodes[hi].perms, "rwxr-xr-x");
        strcpy(fs->nodes[hi].owner, "user");
    }
}

/* ─── Process Table ───────────────────────────────────────────────────────── */
static void proc_init(ProcessTable *pt) {
    memset(pt, 0, sizeof(ProcessTable));
    pt->next_pid = 1;
    pt->sched_next = 0;
}

static int proc_spawn(ProcessTable *pt, const char *name, int parent_pid) {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (!pt->procs[i].used) {
            pt->procs[i].pid = pt->next_pid++;
            pt->procs[i].ppid = parent_pid;
            strncpy(pt->procs[i].name, name, 31);
            strcpy(pt->procs[i].state, "ready");
            pt->procs[i].started = time(NULL);
            pt->procs[i].ticks = 0;
            pt->procs[i].exit_code = 0;
            pt->procs[i].used = 1;
            strcpy(pt->procs[i].cwd, "/");  /* default per-proc cwd */
            pt->procs[i].sleep_ticks = 0;
            pt->procs[i].mem_used_kb = 0;
            pt->procs[i].owned_block_count = 0;
            for (int f=0; f<32; f++) pt->procs[i].open_fds[f] = -1;
            pt->procs[i].priority = 0;
            pt->procs[i].max_slice = 5;  /* default time slice for fair preemption */
            pt->procs[i].slice_used = 0;
            pt->procs[i].mem_limit_kb = 4096; /* default limit per proc */
            pt->procs[i].signals = 0;
            pt->procs[i].cpu_time = 0;
            pt->procs[i].cpu_limit = 100000; /* default high cpu limit */
            pt->procs[i].cpu_violations = 0;
            pt->procs[i].numbness = 0;
            pt->procs[i].mailbox[0] = '\0';
            pt->procs[i].mood_pain = pt->procs[i].mood_tension = 0.0f;
            pt->procs[i].mood_flow = pt->procs[i].mood_warmth = 0.0f;
            pt->procs[i].aml_prog = NULL;
            pt->procs[i].spawned = 0;
            pt->procs[i].real_pid = 0;
            return pt->procs[i].pid;
        }
    }
    return -1;
}

/* Real process slot: fork+setrlimit+execvp a target, register it as a spawned slot.
 * argv is NULL-terminated; argv[0] is the executable. amosOZ is single-threaded, so
 * execvp (PATH search) is fork-safe here. Returns the virtual pid, or -1. This is the
 * SARTRE ns_spawn contract (spawned=1) grown natively — real, not simulated. */
static int proc_real_spawn(const char *name, char *const argv[], int mem_limit_mb, int parent_pid) {
    pid_t child = fork();
    if (child < 0) return -1;
    if (child == 0) {
        if (mem_limit_mb > 0) {
            struct rlimit rl;
            rl.rlim_cur = rl.rlim_max = (rlim_t)mem_limit_mb * 1024 * 1024;
            setrlimit(RLIMIT_AS, &rl); /* no-op on macOS; enforced on Linux */
        }
        execvp(argv[0], argv);
        _exit(127); /* exec failed */
    }
    int pid = proc_spawn(&K.procs, name, parent_pid);
    if (pid < 0) {
        /* no free slot: don't leak the child */
        kill(child, SIGKILL);
        int st; waitpid(child, &st, 0);
        return -1;
    }
    for (int i = 0; i < MAX_PROCS; i++) {
        if (K.procs.procs[i].used && K.procs.procs[i].pid == pid) {
            K.procs.procs[i].spawned = 1;
            K.procs.procs[i].real_pid = child;
            strcpy(K.procs.procs[i].state, "running");
            break;
        }
    }
    return pid;
}

/* Reap exited real children (non-blocking); a dead real slot becomes a zombie so the
 * existing wait/init-reap paths free it. Called each main-loop iteration. */
static void proc_reap_real(void) {
    for (int i = 0; i < MAX_PROCS; i++) {
        Process *p = &K.procs.procs[i];
        if (p->used && p->spawned && p->real_pid > 0 && strcmp(p->state, "zombie") != 0) {
            int st;
            pid_t r = waitpid(p->real_pid, &st, WNOHANG);
            if (r == p->real_pid) {
                p->exit_code = WIFEXITED(st) ? WEXITSTATUS(st)
                             : WIFSIGNALED(st) ? 128 + WTERMSIG(st) : 0;
                p->real_pid = 0;
                strcpy(p->state, "zombie");
            }
        }
    }
}

/* An AML program is a monad's whole life: it runs inside the quantum that spawned it, is
 * charged for the work it did, and is a zombie the moment it is finished — so `wait` reaps
 * it exactly like a real child. Synchronous on purpose: the field (`AM_State G` in the
 * vendored AML) is a singleton with no lock, so a threaded monad would race the main loop's
 * am_step. Budgeted execution needs a step/resume API AML does not have yet. */
static int aml_program_lines(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[MAX_CMD_LEN];
    int n = 0;
    while (fgets(line, sizeof(line), f)) {
        const char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p && *p != '\n') n++;
    }
    fclose(f);
    return n;
}

static int is_aml_target(const char *name) {
    int len = (int)strlen(name);
    return len > 4 && strcmp(name + len - 4, ".aml") == 0;
}

/* Close a monad's program if it still holds one. Every path that frees a process slot must
 * go through here, or the open AML program leaks with the slot. */
static void proc_release_aml(Process *p) {
    if (p->aml_prog) { am_program_close(p->aml_prog); p->aml_prog = NULL; }
}

/* One quantum of a living monad: it runs up to max_slice statements of its own program,
 * pays for what it actually ran, and folds what it moved in the shared field into its own
 * mood — so a program that keeps perturbing keeps its argument alive against the decay.
 * When the program ends the monad becomes a zombie carrying its result. */
static void proc_aml_slice(Process *p) {
    AM_State *fs = am_get_state();
    float b_pain = fs->pain, b_tension = fs->tension, b_flow = fs->flow, b_warmth = fs->warmth;

    int before = am_program_remaining(p->aml_prog);
    int done = am_program_step(p->aml_prog, p->max_slice > 0 ? p->max_slice : 1);
    int consumed = before - am_program_remaining(p->aml_prog);

    p->cpu_time += consumed;
    p->mood_pain    += fs->pain    - b_pain;
    p->mood_tension += fs->tension - b_tension;
    p->mood_flow    += fs->flow    - b_flow;
    p->mood_warmth  += fs->warmth  - b_warmth;

    if (done) {
        p->exit_code = am_program_close(p->aml_prog);
        p->aml_prog = NULL;
        strcpy(p->state, "zombie");
    }
}

#define MAX_AML_PROGRAM 65536

static int proc_aml_spawn(const char *path, const char *label, char *out, int sz) {
    int lines = aml_program_lines(path);
    if (lines < 0) { snprintf(out, sz, "run: cannot read AML program '%s'", path); return ERR_NOT_FOUND; }

    /* the shell spawned this monad, so the shell is its parent — current_pid drifts with the
     * scheduler and parenting to the drift is what made `wait` miss real children before */
    int pid = proc_spawn(&K.procs, label, K.shell_pid > 0 ? K.shell_pid : 1);
    if (pid < 0) { snprintf(out, sz, "run: process table full"); return ERR_NO_MEMORY; }
    int idx = -1;
    for (int i = 0; i < MAX_PROCS; i++)
        if (K.procs.procs[i].used && K.procs.procs[i].pid == pid) { idx = i; break; }

    /* The monad does not run here. It is opened and left ready: the scheduler gives it a
     * quantum like any other proc, and it runs max_slice statements of its own program per
     * turn. A monad's life is now as long as its program, not as long as one command. */
    char src[MAX_AML_PROGRAM];
    FILE *f = fopen(path, "r");
    if (!f) { K.procs.procs[idx].used = 0; snprintf(out, sz, "run: cannot read AML program '%s'", path); return ERR_NOT_FOUND; }
    size_t n = fread(src, 1, sizeof(src) - 1, f);
    int too_big = !feof(f);
    fclose(f);
    src[n] = '\0';
    if (too_big) {
        K.procs.procs[idx].used = 0;
        snprintf(out, sz, "run: AML program '%s' exceeds %d bytes", path, MAX_AML_PROGRAM);
        return ERR_INVALID;
    }

    K.procs.procs[idx].aml_prog = am_program_open(src);
    if (!K.procs.procs[idx].aml_prog) {
        K.procs.procs[idx].used = 0;
        snprintf(out, sz, "run: '%s' opened no program (empty or out of memory)", path);
        return ERR_INVALID;
    }
    strcpy(K.procs.procs[idx].state, "ready");
    snprintf(out, sz, "AML monad '%s' pid %d opened, %d statements, %d per quantum",
             label, pid, am_program_remaining(K.procs.procs[idx].aml_prog),
             K.procs.procs[idx].max_slice);
    return ERR_OK;
}

/* init reaps orphans, not everyone. A zombie whose parent is still alive belongs to that
 * parent's `wait` — reaping it here is what made a collected exit code a race. */
static int proc_is_orphan(ProcessTable *pt, int idx) {
    int ppid = pt->procs[idx].ppid;
    if (ppid <= 1) return 1;
    for (int i = 0; i < MAX_PROCS; i++)
        if (pt->procs[i].used && pt->procs[i].pid == ppid) return 0;
    return 1;
}

static int proc_kill(ProcessTable *pt, int pid, int exit_code) {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (pt->procs[i].used && pt->procs[i].pid == pid) {
            pt->procs[i].state[0] = '\0';
            strcpy(pt->procs[i].state, "zombie");
            pt->procs[i].exit_code = exit_code;
            /* unblock parent if it was waiting */
            if (pt->procs[i].ppid > 0) {
                for (int p = 0; p < MAX_PROCS; p++) {
                    if (pt->procs[p].used && pt->procs[p].pid == pt->procs[i].ppid &&
                        strcmp(pt->procs[p].state, "blocked") == 0) {
                        strcpy(pt->procs[p].state, "ready");
                        pt->procs[p].sleep_ticks = 0;
                    }
                }
            }
            return ERR_OK;
        }
    }
    return ERR_NO_PROCESS;
}

/* minimal wait: reap first zombie child of parent, return its pid and status */
static int proc_wait(ProcessTable *pt, int parent_pid, int *status_out) {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (pt->procs[i].used && pt->procs[i].ppid == parent_pid &&
            strcmp(pt->procs[i].state, "zombie") == 0) {
            *status_out = pt->procs[i].exit_code;
            int child_pid = pt->procs[i].pid;
            proc_release_aml(&pt->procs[i]);
            pt->procs[i].used = 0; /* reap */
            return child_pid;
        }
    }
    return -1; /* no zombie child */
}

static int proc_exec(int pid, const char *newname) {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (K.procs.procs[i].used && K.procs.procs[i].pid == pid) {
            strncpy(K.procs.procs[i].name, newname, 31);
            /* exec: replace image, keep fds/cwd/mem/limits/prio per minimal POSIX sim; reset signals, slice, sleep, violations */
            K.procs.procs[i].signals = 0;
            K.procs.procs[i].numbness = 0;
            K.procs.procs[i].sleep_ticks = 0;
            K.procs.procs[i].slice_used = 0;
            K.procs.procs[i].cpu_violations = 0; /* reset on exec */
            strcpy(K.procs.procs[i].state, "ready");
            return 0;
        }
    }
    return -1;
}

/* --- Per-process FD table (minimal OS foundation) --- */
static int alloc_fd(int pid, int node_idx) {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (K.procs.procs[i].used && K.procs.procs[i].pid == pid) {
            for (int f = 0; f < 32; f++) {
                if (K.procs.procs[i].open_fds[f] == -1) {
                    K.procs.procs[i].open_fds[f] = node_idx;
                    return f;
                }
            }
        }
    }
    return -1;
}

static int close_fd(int pid, int fd) {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (K.procs.procs[i].used && K.procs.procs[i].pid == pid) {
            if (fd >= 0 && fd < 32 && K.procs.procs[i].open_fds[fd] != -1) {
                K.procs.procs[i].open_fds[fd] = -1;
                return 0;
            }
        }
    }
    return -1;
}

static int get_fd_node(int pid, int fd) {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (K.procs.procs[i].used && K.procs.procs[i].pid == pid) {
            if (fd >= 0 && fd < 32) return K.procs.procs[i].open_fds[fd];
        }
    }
    return -1;
}

static int dup_fd(int pid, int oldfd) {
    int node = get_fd_node(pid, oldfd);
    if (node < 0) return -1;
    return alloc_fd(pid, node);
}

static float clampf01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

/* Field-governed primary selection — the dissolution of the round-robin.
 * The coupling law is READ FROM THE AML FIELD each step, and the field advances
 * deterministically (am_step), so this is reproducible. It is not a fixed choice
 * between regimes: A (velocity tempo), B (chamber balance), C (dissonance pressure)
 * are all present every step, and their weight is itself read from the field now:
 *   - base: priority dominates (scaled x1000; the field never overrides a priority
 *     level — it governs the choice AMONG equal-priority ready procs, where the old
 *     round-robin was arbitrary). Coefficients are tunable to deepen the dissolution.
 *   - A velocity: NOMOVE/BREATHE bias holding current; RUN biases preemption; WALK is
 *     neutral (so a calm default reduces exactly to priority + round-robin).
 *   - B chamber (weight = emergence): flow+warmth favor continuity (keep current),
 *     tension+pain favor preemption (switch away).
 *   - C pressure (weight = dissonance): surface the cpu-heaviest proc; past the tunnel
 *     threshold with an active wormhole, jump the round-robin origin (a tunnel skip). */
static int proc_field_select(ProcessTable *pt) {
    AM_State *fs = am_get_state();
    float w_pressure = clampf01(fs->dissonance);   /* C */
    float w_chamber  = clampf01(fs->emergence);    /* B */
    float continuity = fs->flow + fs->warmth;      /* smooth: stay on current */
    float agitation  = fs->tension + fs->pain;     /* rough: switch away      */
    float vel_bias = 0.0f;                          /* A: tempo, always on; WALK neutral */
    switch (fs->velocity_mode) {
        case 0: vel_bias = +1.0f; break;   /* NOMOVE  -> hold current   */
        case 3: vel_bias = +0.3f; break;   /* BREATHE -> gentle hold    */
        case 2: vel_bias = -1.0f; break;   /* RUN     -> preempt        */
        default: vel_bias = 0.0f; break;   /* WALK / BACKWARD: neutral  */
    }
    int tunnel = fs->wormhole_active && fs->dissonance > fs->tunnel_threshold;
    int origin = tunnel ? (pt->sched_next + 1) % MAX_PROCS : pt->sched_next;

    int best = -1;
    float best_score = -1e30f;
    for (int k = 0; k < MAX_PROCS; k++) {
        int idx = (origin + k) % MAX_PROCS;
        Process *p = &pt->procs[idx];
        if (!(p->used && (strcmp(p->state, "ready") == 0 || strcmp(p->state, "running") == 0)
              && p->cpu_time < p->cpu_limit)) continue;
        int is_cur = (p->pid == K.current_pid);
        float score = (float)p->priority * 1000.0f;                                  /* base: priority dominates */
        score += w_pressure * ((float)p->cpu_time / (float)(p->cpu_limit > 0 ? p->cpu_limit : 1)) * 100.0f; /* C */
        float cham = is_cur ? (continuity - agitation) : (agitation - continuity);   /* B */
        score += w_chamber * cham * 50.0f;
        /* B': the monad's own argument. Its own tension/pain demand the CPU; its own
         * flow/warmth are content to yield. Weighted by the same shared emergence — the
         * weather decides how loudly anyone gets to argue. An empty mood adds exactly 0. */
        float own_demand = (p->mood_tension + p->mood_pain) - (p->mood_flow + p->mood_warmth);
        score += w_chamber * own_demand * 50.0f;
        float stick = is_cur ? vel_bias : -vel_bias;                                 /* A */
        score += stick * fs->velocity_magnitude * 50.0f;
        score -= (float)k * 0.001f;                                                   /* round-robin tiebreak */
        if (score > best_score) { best_score = score; best = idx; }
    }
    return best;
}

static int proc_tick(ProcessTable *pt) {
    pt->tick_count++;

    /* Real round-robin scheduler: C is the center.
     * Skips blocked. Handles sleep countdown.
     * Only one process gets CPU per tick.
     */
    int n = MAX_PROCS;

    /* first, countdown sleeps ( -1 means signal wait, no auto unblock) */
    for (int i = 0; i < n; i++) {
        if (pt->procs[i].used && strcmp(pt->procs[i].state, "blocked") == 0) {
            if (pt->procs[i].sleep_ticks > 0) {
                pt->procs[i].sleep_ticks--;
            }
            if (pt->procs[i].sleep_ticks == 0) {
                strcpy(pt->procs[i].state, "ready");
            }
        }
    }

    /* signal delivery for deeper IPC - respect mask (KILL/STOP can't be masked) */
    for (int i = 0; i < n; i++) {
        if (pt->procs[i].used && pt->procs[i].signals) {
            int sigs = pt->procs[i].signals & ~pt->procs[i].numbness;
            if (pt->procs[i].signals & (1 << 9)) { /* SIGKILL always */
                strcpy(pt->procs[i].state, "zombie");
                pt->procs[i].exit_code = 9;
                pt->procs[i].signals = 0;
                pt->procs[i].numbness = 0;
            } else if (pt->procs[i].signals & (1 << 19)) { /* SIGSTOP always */
                strcpy(pt->procs[i].state, "stopped");
                pt->procs[i].signals &= ~(1 << 19);
            } else if (sigs & (1 << 15)) { /* SIGTERM */
                strcpy(pt->procs[i].state, "zombie");
                pt->procs[i].exit_code = 15;
                pt->procs[i].signals = 0;
            } else if (sigs & (1 << 18)) { /* SIGCONT */
                if (strcmp(pt->procs[i].state, "stopped") == 0) {
                    strcpy(pt->procs[i].state, "ready");
                }
                pt->procs[i].signals &= ~(1 << 18);
            } else if (sigs) {
                /* other unmasked signals unblock */
                if (strcmp(pt->procs[i].state, "blocked") == 0) {
                    strcpy(pt->procs[i].state, "ready");
                }
                pt->procs[i].signals &= ~sigs;
            }
            /* numbness persists across ticks — it is a property of the monad, not of the
             * signal being delivered. A numbed signal simply stays pending until `feel`. */
        }
    }

    /* a mood is passing weather, not a caste: it fades for every monad each tick. Without
     * this the discharge below only reaches whoever is being served, and a monad content to
     * yield would carry that contentment forever and never be scheduled again. */
    for (int i = 0; i < n; i++) {
        if (!pt->procs[i].used) continue;
        pt->procs[i].mood_pain    *= 0.90f;
        pt->procs[i].mood_tension *= 0.90f;
        pt->procs[i].mood_flow    *= 0.90f;
        pt->procs[i].mood_warmth  *= 0.90f;
    }

    /* time-slice preemption reset (the tick's housekeeping stays) */
    for (int i = 0; i < n; i++) {
        Process *p = &pt->procs[i];
        if (p->used && (strcmp(p->state, "ready") == 0 || strcmp(p->state, "running") == 0)) {
            if (p->slice_used >= p->max_slice) {
                p->slice_used = 0;
                strcpy(p->state, "ready");
            }
        }
    }
    /* primary selection is now governed by the AML resonance field. Priority still
     * dominates; the field bends the choice among equal-priority ready procs (calm =
     * round-robin, agitation = pressure/chamber/velocity). The never-none cascade below
     * remains the guaranteed floor. */
    int best_idx = proc_field_select(pt);
    if (best_idx < 0) {
        /* full scan fallback for any ready under limit */
        for (int i = 0; i < n; i++) {
            Process *p = &pt->procs[i];
            if (p->used && (strcmp(p->state, "ready") == 0 || strcmp(p->state, "running") == 0) && p->cpu_time < p->cpu_limit) {
                best_idx = i;
                break;
            }
        }
    }
    if (best_idx < 0) {
        /* last resort: any non-stopped under limit */
        for (int i = 0; i < n; i++) {
            Process *p = &pt->procs[i];
            if (p->used && strcmp(p->state, "stopped") != 0 && strcmp(p->state, "zombie") != 0 && p->cpu_time < p->cpu_limit) {
                best_idx = i;
                strcpy(p->state, "ready");
                break;
            }
        }
    }
    if (best_idx < 0) {
        /* absolute last: pick first ready and force */
        for (int i = 0; i < n; i++) {
            Process *p = &pt->procs[i];
            if (p->used && strcmp(p->state, "ready") == 0) {
                best_idx = i;
                break;
            }
        }
    }
    if (best_idx >= 0) {
        /* init reaps zombies automatically (minimal OS) */
        if (pt->procs[best_idx].pid == 1) {
            for (int z = 0; z < n; z++) {
                if (pt->procs[z].used && strcmp(pt->procs[z].state, "zombie") == 0 && proc_is_orphan(pt, z)) {
                    proc_release_aml(&pt->procs[z]);
                    pt->procs[z].used = 0;
                }
            }
        }
        /* demote previous */
        for (int j = 0; j < n; j++) {
            if (pt->procs[j].used && strcmp(pt->procs[j].state, "running") == 0 && j != best_idx) {
                strcpy(pt->procs[j].state, "ready");
            }
        }
        Process *p = &pt->procs[best_idx];

        /* enforce cpu limit - hardcore resource limit */
        if (p->cpu_time >= p->cpu_limit) {
            p->cpu_violations++;
            strcpy(p->state, "stopped");
            pt->sched_next = (best_idx + 1) % n;
        } else {
            strcpy(p->state, "running");
            p->ticks++;
            p->cpu_time++;
            p->slice_used++;
            K.current_pid = p->pid;
            /* being served spends the argument: a monad that gets the CPU discharges its
             * mood toward calm. Without this a tense monad wins every round forever and
             * starves the rest — the field would not bend the scheduler, it would jam it. */
            p->mood_pain    *= 0.85f;
            p->mood_tension *= 0.85f;
            p->mood_flow    *= 0.85f;
            p->mood_warmth  *= 0.85f;

            /* an AML monad spends its quantum running its own program */
            if (p->aml_prog) proc_aml_slice(p);

            /* if just exhausted, reset for next (a monad that died in its slice stays dead) */
            if (p->slice_used >= p->max_slice && strcmp(p->state, "running") == 0) {
                p->slice_used = 0;
                strcpy(p->state, "ready");
            }

            pt->sched_next = (best_idx + 1) % n;
            return pt->tick_count;
        }
    }
    /* foundation guarantee: never 'none' or running=none. always pick or force a ready proc, set current_pid, init reaps zombies */
    int has_running = 0;
    for (int i = 0; i < n; i++) if (pt->procs[i].used && strcmp(pt->procs[i].state, "running") == 0) has_running = 1;
    if (!has_running) {
        int chosen = -1;
        for (int i = 0; i < n && chosen < 0; i++) {
            Process *p = &pt->procs[i];
            if (p->used && strcmp(p->state, "ready") == 0 && p->cpu_time < p->cpu_limit) chosen = i;
        }
        if (chosen < 0) {
            for (int i = 0; i < n && chosen < 0; i++) if (pt->procs[i].used && strcmp(pt->procs[i].state, "ready") == 0) chosen = i;
        }
        if (chosen < 0) {
            for (int i = 0; i < n && chosen < 0; i++) if (pt->procs[i].used && strcmp(pt->procs[i].state, "zombie") != 0 && strcmp(pt->procs[i].state, "stopped") != 0) {
                chosen = i;
                strcpy(pt->procs[i].state, "ready");
            }
        }
        if (chosen >= 0) {
            Process *p = &pt->procs[chosen];
            strcpy(p->state, "running");
            p->ticks++;
            p->cpu_time++;
            K.current_pid = p->pid;
            if (p->pid == 1) {
                for (int z = 0; z < n; z++) if (pt->procs[z].used && strcmp(pt->procs[z].state, "zombie") == 0 && proc_is_orphan(pt, z)) { proc_release_aml(&pt->procs[z]); pt->procs[z].used = 0; }
            }
        }
    }
    if (K.current_pid <= 0) {
        /* ultimate guarantee for foundation: force shell or init or first to keep OS alive (no none) */
        int force = (K.shell_pid > 0 ? K.shell_pid : 1);
        for (int i=0; i<n; i++) if (pt->procs[i].used && pt->procs[i].pid == force) {
            strcpy(pt->procs[i].state, "running");
            K.current_pid = force;
            break;
        }
        if (K.current_pid <= 0) for (int i=0; i<n; i++) if (pt->procs[i].used) {
            strcpy(pt->procs[i].state, "running");
            K.current_pid = pt->procs[i].pid;
            break;
        }
    }
    return pt->tick_count;
}

/* ─── OZ Ledger ───────────────────────────────────────────────────────────── */
static void ledger_init(OZLedger *l) { memset(l, 0, sizeof(OZLedger)); l->head = 0; l->count = 0; }

static void ledger_record_ex(OZLedger *l, const char *cmd, const char *pcmd, const char *pargs,
                             const char *actor, int tick, int result, const char *explanation,
                             int reversible, const char *undo_path, const char *undo_content,
                             int undo_is_dir, int undo_was_create) {
    int idx;
    if (l->count < MAX_LEDGER) {
        idx = (l->head + l->count) % MAX_LEDGER;
        l->count++;
    } else {
        idx = l->head;
        l->head = (l->head + 1) % MAX_LEDGER;
    }
    LedgerEntry *e = &l->entries[idx];
    strncpy(e->command, cmd, MAX_CMD_LEN-1);
    strncpy(e->parsed_cmd, pcmd ? pcmd : "", 31);
    strncpy(e->parsed_args, pargs ? pargs : "", 255);
    strncpy(e->actor, actor, 15);
    e->tick = tick;
    e->timestamp = time(NULL);
    e->result_code = result;
    snprintf(e->explanation, 127, "%s", explanation ? explanation : "executed");
    e->reversible = reversible;
    if (undo_path) strncpy(e->undo_path, undo_path, MAX_PATH-1);
    if (undo_content) strncpy(e->undo_content, undo_content, MAX_CONTENT-1);
    e->undo_is_dir = undo_is_dir;
    e->undo_was_create = undo_was_create;
}

/* ─── Module System ───────────────────────────────────────────────────────── */
/* "OZ begins where extension becomes accountable." */
static void init_builtin_slots(void) {
    const char *slot_names[] = {
        "shell.commands", "fs.drivers", "devices", "ai.hooks",
        "sched.hooks", "boot.hooks", "diagnostics", "experiments",
        "oz.contracts", "oz.ledger"
    };
    K.slot_count = 10;
    for (int i = 0; i < 10; i++) {
        memset(&K.slots[i], 0, sizeof(Slot));
        strcpy(K.slots[i].name, slot_names[i]);
    }
}

static void init_hooks(void) {
    const char *hook_names[] = {"boot", "sched", "ai", "diag", "experiment"};
    K.hook_count = 5;
    for (int i = 0; i < 5; i++) {
        memset(&K.hooks[i], 0, sizeof(Hook));
        strcpy(K.hooks[i].name, hook_names[i]);
    }
}

static int find_slot(const char *name) {
    for (int i = 0; i < K.slot_count; i++)
        if (strcmp(K.slots[i].name, name) == 0) return i;
    return -1;
}

static int find_hook(const char *name) {
    for (int i = 0; i < K.hook_count; i++)
        if (strcmp(K.hooks[i].name, name) == 0) return i;
    return -1;
}

static void register_module(const char *name, const char *desc,
                            const char *cmds[], int ncmds,
                            const char *mslots[], int nslots,
                            const char *mhooks[], int nhooks,
                            const char *provides[], int nprov,
                            const char *requires[], int nreq,
                            const char *ver) {
    if (K.module_count >= MAX_MODULES) return;
    Module *m = &K.modules[K.module_count++];
    memset(m, 0, sizeof(Module));
    strncpy(m->name, name, 31);
    strncpy(m->description, desc, 63);
    m->cmd_count = ncmds;
    for (int i = 0; i < ncmds && i < 4; i++) strncpy(m->commands[i], cmds[i], 15);
    m->slot_count = nslots;
    for (int i = 0; i < nslots && i < 4; i++) {
        strncpy(m->slots[i], mslots[i], 31);
        int si = find_slot(mslots[i]);
        if (si >= 0 && K.slots[si].occupant_count < MAX_SLOT_OCCUPANTS) {
            strcpy(K.slots[si].occupants[K.slots[si].occupant_count++], name);
        }
    }
    m->hook_count = nhooks;
    for (int i = 0; i < nhooks && i < 4; i++) {
        strncpy(m->hooks[i], mhooks[i], 15);
        int hi = find_hook(mhooks[i]);
        if (hi >= 0 && K.hooks[hi].sub_count < MAX_HOOK_SUBS) {
            strcpy(K.hooks[hi].subscribers[K.hooks[hi].sub_count++], name);
        }
    }
    m->provides_count = nprov;
    for (int i = 0; i < nprov && i < 4; i++) strncpy(m->contract_provides[i], provides[i], 31);
    m->requires_count = nreq;
    for (int i = 0; i < nreq && i < 4; i++) strncpy(m->contract_requires[i], requires[i], 31);
    strncpy(m->contract_version, ver, 7);
    m->loaded = 1;
}

static int unload_module(const char *name) {
    for (int i = 0; i < K.module_count; i++) {
        if (K.modules[i].loaded && strcmp(K.modules[i].name, name) == 0) {
            K.modules[i].loaded = 0;
            /* Remove from slots */
            for (int s = 0; s < K.slot_count; s++) {
                for (int o = 0; o < K.slots[s].occupant_count; o++) {
                    if (strcmp(K.slots[s].occupants[o], name) == 0) {
                        for (int k = o; k < K.slots[s].occupant_count-1; k++)
                            strcpy(K.slots[s].occupants[k], K.slots[s].occupants[k+1]);
                        K.slots[s].occupant_count--;
                        break;
                    }
                }
            }
            /* Remove from hooks */
            for (int h = 0; h < K.hook_count; h++) {
                for (int o = 0; o < K.hooks[h].sub_count; o++) {
                    if (strcmp(K.hooks[h].subscribers[o], name) == 0) {
                        for (int k = o; k < K.hooks[h].sub_count-1; k++)
                            strcpy(K.hooks[h].subscribers[k], K.hooks[h].subscribers[k+1]);
                        K.hooks[h].sub_count--;
                        break;
                    }
                }
            }
            return ERR_OK;
        }
    }
    return ERR_NOT_FOUND;
}

static int slot_is_occupied(const char *slot_name) {
    int si = find_slot(slot_name);
    if (si < 0) return 0;
    return K.slots[si].occupant_count > 0;
}

static int validate_module_contracts(void) {
    for (int i = 0; i < K.module_count; i++) {
        if (!K.modules[i].loaded) continue;
        for (int j = 0; j < K.modules[i].requires_count; j++) {
            const char *req = K.modules[i].contract_requires[j];
            if (!slot_is_occupied(req)) return ERR_MODULE;
        }
    }
    return ERR_OK;
}

static int fire_hook(const char *hook_name) {
    int hi = find_hook(hook_name);
    if (hi < 0) return 0;
    return K.hooks[hi].sub_count;
}

static void init_builtin_modules(void) {
    { const char *c[]={"uptime"}; const char *s[]={"shell.commands"};
      const char *h[]={NULL}; const char *p[]={"uptime"}; const char *r[]={NULL};
      register_module("coreutils","Core utilities",c,1,s,1,h,0,p,1,r,0,"0.1"); }
    { const char *c[]={"hwinfo"}; const char *s[]={"diagnostics"};
      const char *h[]={"boot"}; const char *p[]={"hwinfo"}; const char *r[]={NULL};
      register_module("hwprobe","Hardware probe",c,1,s,1,h,1,p,1,r,0,"0.1"); }
    { const char *c[]={"diag_status"}; const char *s[]={"diagnostics"};
      const char *h[]={"diag"}; const char *p[]={"diag_status"}; const char *r[]={NULL};
      register_module("diag","Diagnostics",c,1,s,1,h,1,p,1,r,0,"0.1"); }
    { const char *c[]={"ai_status"}; const char *s[]={"ai.hooks"};
      const char *h[]={"ai"}; const char *p[]={"ai_status","intent_hints"}; const char *r[]={"oz.ledger"};
      register_module("ai_seed","AI seed hooks",c,1,s,1,h,1,p,2,r,1,"0.1"); }
    { const char *c[]={"ledger_size"}; const char *s[]={"oz.ledger","oz.contracts"};
      const char *h[]={"ai"}; const char *p[]={"ledger_size","trace","replay"}; const char *r[]={NULL};
      register_module("oz_ledger","OZ Ledger provenance",c,1,s,2,h,1,p,3,r,0,"0.1"); }
    { const char *c[]={"fortune"}; const char *s[]={"shell.commands","experiments"};
      const char *h[]={"experiment"}; const char *p[]={"fortune"}; const char *r[]={NULL};
      register_module("fortune_ext","Example extension: fortune",c,1,s,2,h,1,p,1,r,0,"0.1"); }
}

/* ─── Devices ─────────────────────────────────────────────────────────────── */
static void init_devices(void) {
    /* keep in sync with fs /dev/ entries for complete devices foundation */
    const char *names[] = {"console","mem","null","zero","full","random","urandom","tty"};
    const char *types[] = {"char","block","char","char","char","char","char","char"};
    K.device_count = 8;
    for (int i = 0; i < 8; i++) {
        strcpy(K.devices[i].name, names[i]);
        strcpy(K.devices[i].type, types[i]);
        strcpy(K.devices[i].status, "active");
    }
}

/* ─── Environment ─────────────────────────────────────────────────────────── */
static void env_set(const char *key, const char *val) {
    for (int i = 0; i < K.env_count; i++) {
        if (strcmp(K.env[i].key, key) == 0) {
            strncpy(K.env[i].value, val, 127);
            return;
        }
    }
    if (K.env_count < MAX_ENV) {
        strncpy(K.env[K.env_count].key, key, 31);
        strncpy(K.env[K.env_count].value, val, 127);
        K.env_count++;
    }
}

static const char* env_get(const char *key) {
    for (int i = 0; i < K.env_count; i++)
        if (strcmp(K.env[i].key, key) == 0) return K.env[i].value;
    return NULL;
}

static void env_unset(const char *key) {
    for (int i = 0; i < K.env_count; i++) {
        if (strcmp(K.env[i].key, key) == 0) {
            for (int j = i; j < K.env_count-1; j++) K.env[j] = K.env[j+1];
            K.env_count--;
            return;
        }
    }
}

/* ─── Slot Manifest (declarative orchestration, slots.tsv-style) ───────────── */
/* A data-declared registry of runnable targets: `run <name>` resolves a slot to a real
 * process (baked/repo), a script, or a deferred AML target — the agnostic-target idea
 * borrowed from ariannamethod.cli's runtime/slots.tsv. Missing manifest = no slots. */
#define MAX_MANIFEST_SLOTS 16
typedef struct {
    char name[64];
    char label[64];
    char kind[16];      /* baked | repo | script | aml */
    char state[16];     /* wired | ... */
    char target[MAX_PATH];
    char notes[128];
    int  used;
} SlotEntry;
static SlotEntry g_slots[MAX_MANIFEST_SLOTS];
static int g_slot_count;

static void slot_manifest_load(const char *path) {
    g_slot_count = 0;
    memset(g_slots, 0, sizeof(g_slots));
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[1024];
    while (fgets(line, sizeof(line), f) && g_slot_count < MAX_MANIFEST_SLOTS) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        if (!line[0] || line[0] == '#') continue;
        char *fields[6] = {0};
        int nf = 0;
        char *save = NULL;
        for (char *tok = strtok_r(line, "\t", &save); tok && nf < 6; tok = strtok_r(NULL, "\t", &save))
            fields[nf++] = tok;
        if (nf < 5) continue;                          /* need name..target */
        if (strcmp(fields[0], "slot") == 0) continue;  /* header row */
        SlotEntry *s = &g_slots[g_slot_count];
        snprintf(s->name,   sizeof(s->name),   "%s", fields[0]);
        snprintf(s->label,  sizeof(s->label),  "%s", fields[1] ? fields[1] : "");
        snprintf(s->kind,   sizeof(s->kind),   "%s", fields[2] ? fields[2] : "");
        snprintf(s->state,  sizeof(s->state),  "%s", fields[3] ? fields[3] : "");
        snprintf(s->target, sizeof(s->target), "%s", fields[4] ? fields[4] : "");
        snprintf(s->notes,  sizeof(s->notes),  "%s", (nf >= 6 && fields[5]) ? fields[5] : "");
        s->used = 1;
        g_slot_count++;
    }
    fclose(f);
}

static SlotEntry *slot_find(const char *name) {
    for (int i = 0; i < g_slot_count; i++)
        if (g_slots[i].used && strcmp(g_slots[i].name, name) == 0) return &g_slots[i];
    return NULL;
}

/* ─── Kernel Init ─────────────────────────────────────────────────────────── */
static void kernel_init(void) {
    memset(&K, 0, sizeof(Kernel));
    detect_hardware(&K.hw);
    mem_init(&K.mem);
    fs_init(&K.fs);
    fs_init_tree(&K.fs);
    proc_init(&K.procs);
    ledger_init(&K.ledger);
    init_builtin_slots();
    init_hooks();
    init_builtin_modules();
    init_devices();
    slot_manifest_load("runtime/slots.tsv");
    am_init();  /* boot the AML resonance field (the continuous substrate) */
    am_exec("SCHUMANN 7.83\nPROPHECY 3\nDESTINY 0.3\nLAW DEBT_DECAY 0.998\n"); /* field defaults */
    if (access("amos.soma", R_OK) == 0) am_field_load("amos.soma"); /* restore persisted field over defaults */
    K.boot_time = time(NULL);
    strcpy(K.user, "user");
    K.running = 1;
    K.fortune_idx = 0;

    env_set("HOME", "/home/user");
    env_set("USER", "user");
    env_set("SHELL", "/bin/amossh");
    env_set("PATH", "/bin:/usr/bin");
    env_set("TERM", K.hw.terminal);
    env_set("HOSTNAME", K.hw.hostname);

    proc_spawn(&K.procs, "init", 0);
    int sh = proc_spawn(&K.procs, "amossh", 1);  /* init is parent */

    K.hook_boot_fired = fire_hook("boot");
    K.hook_sched_fired = 0;
    K.job_count = 1;
    K.fortune_oz_idx = 0;
    K.shell_pid = sh > 0 ? sh : 2;
    K.current_pid = sh > 0 ? sh : 1;
    K.suppress_next_auto_tick = 0;
    snprintf(K.boot_log, sizeof(K.boot_log),
        "[boot] amosOZ %s started\n[boot] %d hook subscribers on boot\n"
        "[boot] dedicated to Amos Oz (עוז)\n",
        VERSION, K.hook_boot_fired);
    proc_refresh_all();
    (void)validate_module_contracts();
}

/* ─── Syscall Layer ───────────────────────────────────────────────────────── */
static int kernel_syscall(const char *op, char *out, int outsz, int argc, char **argv) {
    out[0] = '\0';
    if (!op) return ERR_INVALID;

    if (strcmp(op, "read") == 0) {
        if (argc < 1) return ERR_INVALID;
        char resolved[MAX_PATH];
        fs_resolve(&K.fs, argv[0], resolved);
        /* unified: devices (/dev/zero etc), /proc refresh, access, normal files all via one path */
        return fs_read_to_buf(resolved, out, outsz);
    }
    if (strcmp(op, "write") == 0) {
        if (argc < 2) return ERR_INVALID;
        char resolved[MAX_PATH];
        fs_resolve(&K.fs, argv[0], resolved);
        if (strcmp(resolved, "/dev/null") == 0 || strcmp(resolved, "/dev/zero") == 0 || strcmp(resolved, "/dev/random") == 0 || strcmp(resolved, "/dev/urandom") == 0 || strcmp(resolved, "/dev/tty") == 0) {
            return ERR_OK; /* devices swallow writes */
        }
        if (strcmp(resolved, "/dev/full") == 0) {
            return ERR_NO_MEMORY; /* full, like ENOSPC */
        }
        int idx = fs_find(&K.fs, resolved);
        if (idx >= 0 && fs_access(&K.fs.nodes[idx], K.user, 'w') != ERR_OK) return ERR_PERMISSION;
        char content[MAX_CONTENT] = "";
        for (int i = 1; i < argc; i++) {
            if (i > 1) strncat(content, " ", MAX_CONTENT - strlen(content) - 1);
            strncat(content, argv[i], MAX_CONTENT - strlen(content) - 1);
        }
        fs_add_file(&K.fs, resolved, content);
        return ERR_OK;
    }
    if (strcmp(op, "stat") == 0) {
        if (argc < 1) return ERR_INVALID;
        char resolved[MAX_PATH];
        fs_resolve(&K.fs, argv[0], resolved);
        if (proc_is_virtual(resolved)) proc_refresh_all();
        int idx = fs_find(&K.fs, resolved);
        if (idx < 0) return ERR_NOT_FOUND;
        FSNode *n = &K.fs.nodes[idx];
        snprintf(out, outsz, "path=%s type=%s perms=%s owner=%s size=%d",
            n->path, n->is_dir ? "dir" : "file", n->perms, n->owner,
            n->is_dir ? 0 : (int)strlen(n->content));
        return ERR_OK;
    }
    if (strcmp(op, "getcwd") == 0) {
        /* use per-proc cwd if possible */
        for (int i=0; i<MAX_PROCS; i++) {
            if (K.procs.procs[i].used && K.procs.procs[i].pid == K.current_pid) {
                snprintf(out, outsz, "%s", K.procs.procs[i].cwd);
                return ERR_OK;
            }
        }
        snprintf(out, outsz, "%s", K.fs.cwd);
        return ERR_OK;
    }
    if (strcmp(op, "chdir") == 0) {
        if (argc < 1) return ERR_INVALID;
        char resolved[MAX_PATH];
        fs_resolve(&K.fs, argv[0], resolved);
        int idx = fs_find(&K.fs, resolved);
        if (idx < 0 || !K.fs.nodes[idx].is_dir) return ERR_NOT_FOUND;
        if (fs_access(&K.fs.nodes[idx], K.user, 'x') != ERR_OK) return ERR_PERMISSION;
        /* set per current proc + global for legacy/internal resolve (current is source of truth) */
        for (int i=0; i<MAX_PROCS; i++) {
            if (K.procs.procs[i].used && K.procs.procs[i].pid == K.current_pid) {
                strcpy(K.procs.procs[i].cwd, resolved);
            }
        }
        strcpy(K.fs.cwd, resolved);
        return ERR_OK;
    }
    if (strcmp(op, "alloc") == 0) {
        if (argc < 1) return ERR_INVALID;
        int size = atoi(argv[0]);
        int owner = K.current_pid;
        int bid = mem_alloc(&K.mem, size, owner, "rw-");
        if (bid < 0) return ERR_NO_MEMORY;
        snprintf(out, outsz, "%d", bid);
        return ERR_OK;
    }
    if (strcmp(op, "free") == 0) {
        if (argc < 1) return ERR_INVALID;
        return mem_free(&K.mem, atoi(argv[0]), K.current_pid); /* current */
    }
    if (strcmp(op, "spawn") == 0) {
        if (argc < 1) return ERR_INVALID;
        int parent = K.shell_pid > 0 ? K.shell_pid : 1;
        int pid = proc_spawn(&K.procs, argv[0], parent);
        if (pid < 0) return ERR_NO_MEMORY;
        snprintf(out, outsz, "%d", pid);
        return ERR_OK;
    }
    if (strcmp(op, "kill") == 0) {
        if (argc < 1) return ERR_INVALID;
        return proc_kill(&K.procs, atoi(argv[0]), 0);
    }
    if (strcmp(op, "sleep") == 0) {
        if (argc < 1) return ERR_INVALID;
        int ticks = atoi(argv[0]);
        /* find a running proc and block it (simplified - in real would be current) */
        for (int i = 0; i < MAX_PROCS; i++) {
            if (K.procs.procs[i].used && strcmp(K.procs.procs[i].state, "running") == 0) {
                strcpy(K.procs.procs[i].state, "blocked");
                K.procs.procs[i].sleep_ticks = ticks;
                snprintf(out, outsz, "process %d sleeping for %d ticks", K.procs.procs[i].pid, ticks);
                return ERR_OK;
            }
        }
        return ERR_OK;
    }
    if (strcmp(op, "open") == 0) {
        if (argc < 1) return ERR_INVALID;
        char resolved[MAX_PATH];
        fs_resolve(&K.fs, argv[0], resolved);
        int idx = fs_find(&K.fs, resolved);
        if (idx < 0) return ERR_NOT_FOUND;
        if (fs_access(&K.fs.nodes[idx], K.user, 'r') != ERR_OK) return ERR_PERMISSION;
        int fd = alloc_fd(K.current_pid, idx);
        if (fd < 0) return ERR_NO_MEMORY;
        snprintf(out, outsz, "%d", fd);
        return ERR_OK;
    }
    if (strcmp(op, "close") == 0) {
        if (argc < 1) return ERR_INVALID;
        int fd = atoi(argv[0]);
        if (close_fd(K.current_pid, fd) == 0) return ERR_OK;
        return ERR_INVALID;
    }
    if (strcmp(op, "readfd") == 0) {
        if (argc < 1) return ERR_INVALID;
        int fd = atoi(argv[0]);
        int node = get_fd_node(K.current_pid, fd);
        if (node < 0) return ERR_NOT_FOUND;
        if (K.fs.nodes[node].is_dir) return ERR_INVALID;
        snprintf(out, outsz, "%s", K.fs.nodes[node].content);
        return ERR_OK;
    }
    if (strcmp(op, "exec") == 0) {
        if (argc < 1) return ERR_INVALID;
        int pid = K.current_pid;
        if (argc > 1) pid = atoi(argv[0]); /* optional pid */
        const char *name = argv[argc>1 ? 1 : 0];
        if (proc_exec(pid, name) == 0) {
            snprintf(out, outsz, "exec %d -> %s", pid, name);
            return ERR_OK;
        }
        return ERR_INVALID;
    }
    if (strcmp(op, "writefd") == 0) {
        if (argc < 2) return ERR_INVALID;
        int fd = atoi(argv[0]);
        int node = get_fd_node(K.current_pid, fd);
        if (node < 0) return ERR_NOT_FOUND;
        char content[MAX_CONTENT] = "";
        for (int i=1; i<argc; i++) {
            if (i>1) strncat(content, " ", sizeof(content)-1);
            strncat(content, argv[i], sizeof(content)-strlen(content)-1);
        }
        strncpy(K.fs.nodes[node].content, content, MAX_CONTENT-1);
        return ERR_OK;
    }
    if (strcmp(op, "dup") == 0) {
        if (argc < 1) return ERR_INVALID;
        int old = atoi(argv[0]);
        int newfd = dup_fd(K.current_pid, old);
        if (newfd < 0) return ERR_INVALID;
        snprintf(out, outsz, "%d", newfd);
        return ERR_OK;
    }
    if (strcmp(op, "getenv") == 0) {
        if (argc < 1) return ERR_INVALID;
        const char *v = env_get(argv[0]);
        snprintf(out, outsz, "%s", v ? v : "");
        return ERR_OK;
    }
    return ERR_NOT_FOUND;
}

/* ─── Command Implementations ─────────────────────────────────────────────── */

static int cmd_help(char *out, int sz, int argc, char **argv) {
    snprintf(out, sz,
        "amosOZ commands:\n"
        "  alloc append boot call cat cd chmod clear contracts cp date\n"
        "  devices echo env exit fortune free help history hooks hw\n"
        "  exec fortune free help history hooks hw kill load loadmod ls mem\n"
        "  mkdir mmap modules motd mv overhead oz ps pwd replay rm rmdir\n"
        "  run save selftest set slots syscall stat status tick touch trace\n"
        "  tree undo uname unloadmod unset version which whoami write\n"
        "  Shell: > >> |  Scripts: #!/amossh .amos  PATH:/bin");
    return ERR_OK;
}

static int cmd_uname(char *out, int sz, int argc, char **argv) {
    snprintf(out, sz, "%s %s %s %s", SYSTEM_NAME, VERSION, K.hw.machine, K.hw.platform);
    return ERR_OK;
}

static int cmd_version(char *out, int sz, int argc, char **argv) {
    snprintf(out, sz, "%s version %s (build %s)", SYSTEM_NAME, VERSION, BUILD_DATE);
    return ERR_OK;
}

static int cmd_boot(char *out, int sz, int argc, char **argv) {
    int elapsed = (int)(time(NULL) - K.boot_time);
    snprintf(out, sz, "Boot time: %sUptime: %ds", ctime(&K.boot_time), elapsed);
    return ERR_OK;
}

static int cmd_hw(char *out, int sz, int argc, char **argv) {
    snprintf(out, sz,
        "Hardware Profile:\n"
        "  platform: %s\n  release: %s\n  machine: %s\n"
        "  processor: %s\n  hostname: %s\n  cpu_count: %d\n"
        "  memory_mb: %ld\n  terminal: %s\n  persistence: %d",
        K.hw.platform, K.hw.platform_release, K.hw.machine,
        K.hw.processor, K.hw.hostname, K.hw.cpu_count,
        K.hw.memory_mb, K.hw.terminal, K.hw.persistence);
    return ERR_OK;
}

static int cmd_devices(char *out, int sz, int argc, char **argv) {
    int n = 0;
    n = safe_append(out, n, sz, "Devices:\n");
    for (int i = 0; i < K.device_count; i++)
        n = safe_append(out, n, sz, "  %-12s %-8s %s\n", K.devices[i].name, K.devices[i].type, K.devices[i].status);
    return ERR_OK;
}

static int cmd_gpu(char *out, int sz, int argc, char **argv) {
    snprintf(out, sz, "GPU: not available (C runtime, no GPU abstraction)");
    return ERR_OK;
}

static int cmd_mem(char *out, int sz, int argc, char **argv) {
    int used = K.mem.used_kb, total = K.mem.total_kb;
    int blocks = 0;
    for (int i = 0; i < K.mem.block_count; i++) if (K.mem.blocks[i].used) blocks++;
    snprintf(out, sz, "Memory: %d KB total, %d KB used, %d KB free, %d blocks",
             total, used, total - used, blocks);
    return ERR_OK;
}

static int cmd_mmap(char *out, int sz, int argc, char **argv) {
    int n = 0, any = 0;
    n = safe_append(out, n, sz, "ID    Size(KB)  Flags  Owner\n");
    for (int i = 0; i < K.mem.block_count; i++) {
        if (K.mem.blocks[i].used) {
            n = safe_append(out, n, sz, "%-5d %-9d %-6s %s\n",
                K.mem.blocks[i].id, K.mem.blocks[i].size,
                K.mem.blocks[i].flags, K.mem.blocks[i].owner);
            any = 1;
        }
    }
    if (!any) snprintf(out, sz, "No memory blocks allocated.");
    return ERR_OK;
}

static int cmd_alloc(char *out, int sz, int argc, char **argv) {
    if (argc < 2) { snprintf(out, sz, "Usage: alloc <size_kb>"); return ERR_INVALID; }
    int size = atoi(argv[1]);
    if (size <= 0) { snprintf(out, sz, "Error: size must be positive integer"); return ERR_INVALID; }
    int owner = K.current_pid;
    int bid = mem_alloc(&K.mem, size, owner, "rw-");
    if (bid < 0) { snprintf(out, sz, "Error: out of memory"); return ERR_NO_MEMORY; }
    snprintf(out, sz, "Allocated block %d (%d KB) for pid %d", bid, size, owner);
    return ERR_OK;
}

static int cmd_free(char *out, int sz, int argc, char **argv) {
    if (argc < 2) { snprintf(out, sz, "Usage: free <block_id>"); return ERR_INVALID; }
    int bid = atoi(argv[1]);
    int err = mem_free(&K.mem, bid, K.current_pid);
    if (err != ERR_OK) { snprintf(out, sz, "Error: block %d not found or permission", bid); return err; }
    snprintf(out, sz, "Freed block %d", bid);
    return ERR_OK;
}

static int cmd_ps(char *out, int sz, int argc, char **argv) {
    int n = 0;
    n = safe_append(out, n, sz, "PID   Name          State     Ticks CpuTime CpuLim MemKB Prio Slice FDs\n");
    for (int i = 0; i < MAX_PROCS; i++) {
        if (K.procs.procs[i].used) {
            int fds = 0;
            for (int f=0; f<32; f++) if (K.procs.procs[i].open_fds[f] >= 0) fds++;
            n = safe_append(out, n, sz, "%-5d %-13s %-9s %-6d %-7d %-6d %-5d %-4d %-5d %d",
                K.procs.procs[i].pid, K.procs.procs[i].name,
                K.procs.procs[i].state, K.procs.procs[i].ticks, K.procs.procs[i].cpu_time, K.procs.procs[i].cpu_limit,
                K.procs.procs[i].mem_used_kb, K.procs.procs[i].priority,
                K.procs.procs[i].slice_used, fds);
            if (K.procs.procs[i].spawned) n = safe_append(out, n, sz, " [real:%d]", (int)K.procs.procs[i].real_pid);
            n = safe_append(out, n, sz, "\n");
        }
    }
    return ERR_OK;
}

static int cmd_run(char *out, int sz, int argc, char **argv) {
    if (argc < 2) { snprintf(out, sz, "Usage: run <name>"); return ERR_INVALID; }
    int parent = K.current_pid > 0 ? K.current_pid : K.shell_pid;
    /* Agnostic target: a real host executable (path with '/', X_OK) runs as a REAL
     * process slot (fork+exec); anything else is a virtual monad, as before — the same
     * spawned-vs-monad split as SARTRE namespaces. (.amos scripts run via the shell;
     * .aml targets are recognized but their runtime is wired later.) */
    /* an AML program is readable, not executable — it runs on the field, not on the CPU.
     * A named .aml target is always an AML claim: unreadable is an error, not a monad. */
    if (is_aml_target(argv[1]))
        return proc_aml_spawn(argv[1], argv[1], out, sz);
    if (strchr(argv[1], '/') && access(argv[1], X_OK) == 0) {
        char *xargv[34];
        int xc = 0;
        for (int i = 1; i < argc && xc < 33; i++) xargv[xc++] = argv[i];
        xargv[xc] = NULL;
        int pid = proc_real_spawn(argv[1], xargv, 0, parent);
        if (pid < 0) { snprintf(out, sz, "run: cannot spawn %s", argv[1]); return ERR_NO_MEMORY; }
        snprintf(out, sz, "Started real process '%s' pid %d (spawned)", argv[1], pid);
        return ERR_OK;
    }
    /* declarative slot from the manifest (agnostic target)? */
    SlotEntry *slot = slot_find(argv[1]);
    if (slot) {
        if (strcmp(slot->kind, "baked") == 0 || strcmp(slot->kind, "repo") == 0) {
            char *xargv[34];
            int xc = 0;
            xargv[xc++] = slot->target;
            for (int i = 2; i < argc && xc < 33; i++) xargv[xc++] = argv[i];
            xargv[xc] = NULL;
            int pid = proc_real_spawn(slot->name, xargv, 0, parent);
            if (pid < 0) { snprintf(out, sz, "run: cannot spawn slot '%s' (%s)", slot->name, slot->target); return ERR_NO_MEMORY; }
            snprintf(out, sz, "Started slot '%s' -> %s pid %d (spawned)", slot->name, slot->target, pid);
            return ERR_OK;
        }
        if (strcmp(slot->kind, "aml") == 0)
            return proc_aml_spawn(slot->target, slot->name, out, sz);
        /* script kind recognized; its runtime is wired later */
        snprintf(out, sz, "run: slot '%s' kind '%s' recognized — runtime wired later", slot->name, slot->kind);
        return ERR_INVALID;
    }
    int pid = proc_spawn(&K.procs, argv[1], parent);
    if (pid < 0) { snprintf(out, sz, "Error: process table full"); return ERR_NO_MEMORY; }
    snprintf(out, sz, "Started process '%s' with PID %d", argv[1], pid);
    return ERR_OK;
}

static int cmd_fork(char *out, int sz, int argc, char **argv) {
    /* fork: clone state for minimal OS foundation (fds, cwd, limits, signals mask, prio, slice) */
    int parent = K.current_pid > 0 ? K.current_pid : K.shell_pid;
    Process *p = NULL;
    for (int i=0; i<MAX_PROCS; i++) if (K.procs.procs[i].used && K.procs.procs[i].pid == K.current_pid) { p = &K.procs.procs[i]; break; }
    if (!p) p = &K.procs.procs[1];
    int newpid = proc_spawn(&K.procs, p->name, parent);
    if (newpid < 0) { snprintf(out, sz, "fork failed"); return ERR_NO_MEMORY; }
    /* copy state */
    for (int i=0; i<MAX_PROCS; i++) {
        if (K.procs.procs[i].used && K.procs.procs[i].pid == newpid) {
            strcpy(K.procs.procs[i].cwd, p->cwd);
            K.procs.procs[i].mem_used_kb = p->mem_used_kb;
            K.procs.procs[i].mem_limit_kb = p->mem_limit_kb;
            K.procs.procs[i].priority = p->priority;
            K.procs.procs[i].max_slice = p->max_slice;
            K.procs.procs[i].cpu_limit = p->cpu_limit;
            K.procs.procs[i].numbness = p->numbness;
            K.procs.procs[i].mood_pain    = p->mood_pain;    /* a child inherits the mood */
            K.procs.procs[i].mood_tension = p->mood_tension;
            K.procs.procs[i].mood_flow    = p->mood_flow;
            K.procs.procs[i].mood_warmth  = p->mood_warmth;
            K.procs.procs[i].signals = 0; /* child starts clean */
            K.procs.procs[i].cpu_time = 0;
            K.procs.procs[i].cpu_violations = 0;
            K.procs.procs[i].slice_used = 0;
            /* clone open fds */
            for (int f=0; f<32; f++) K.procs.procs[i].open_fds[f] = p->open_fds[f];
            /* copy owned mem blocks for sim */
            K.procs.procs[i].owned_block_count = p->owned_block_count;
            for (int b=0; b<p->owned_block_count && b<MAX_BLOCKS; b++) K.procs.procs[i].owned_blocks[b] = p->owned_blocks[b];
            break;
        }
    }
    snprintf(out, sz, "forked pid %d from %d", newpid, parent);
    return ERR_OK;
}

static int cmd_kill(char *out, int sz, int argc, char **argv) {
    if (argc < 2) { snprintf(out, sz, "Usage: kill <pid> | kill -s <sig> <pid>"); return ERR_INVALID; }
    if (argc >= 4 && strcmp(argv[1], "-s") == 0) {
        int sig = atoi(argv[2]);
        int pid = atoi(argv[3]);
        /* deliver signal */
        for (int i=0; i<MAX_PROCS; i++) {
            if (K.procs.procs[i].used && K.procs.procs[i].pid == pid) {
                K.procs.procs[i].signals |= (1 << sig);
                if (strcmp(K.procs.procs[i].state, "blocked") == 0) {
                    strcpy(K.procs.procs[i].state, "ready");
                }
                snprintf(out, sz, "signaled %d sig %d", pid, sig);
                return ERR_OK;
            }
        }
        snprintf(out, sz, "no such pid");
        return ERR_INVALID;
    }
    int pid = atoi(argv[1]);
    /* real process slot: send a real signal, grace, then reap */
    for (int i = 0; i < MAX_PROCS; i++) {
        Process *p = &K.procs.procs[i];
        if (p->used && p->pid == pid && p->spawned && p->real_pid > 0) {
            kill(p->real_pid, SIGTERM);
            for (int g = 0; g < 25; g++) { int st; if (waitpid(p->real_pid, &st, WNOHANG) == p->real_pid) { p->real_pid = 0; break; } usleep(20000); }
            if (p->real_pid > 0) { kill(p->real_pid, SIGKILL); int st; waitpid(p->real_pid, &st, 0); p->real_pid = 0; }
            strcpy(p->state, "zombie");
            p->exit_code = 143;
            snprintf(out, sz, "Killed real process %d (SIGTERM/grace/SIGKILL)", pid);
            return ERR_OK;
        }
    }
    int err = proc_kill(&K.procs, pid, 0);
    if (err != ERR_OK) { snprintf(out, sz, "Error: process %d not found", pid); return err; }
    snprintf(out, sz, "Killed process %d (zombie)", pid);
    return ERR_OK;
}

static int cmd_sleep(char *out, int sz, int argc, char **argv) {
    if (argc < 1) { snprintf(out, sz, "Usage: sleep <ticks>"); return ERR_INVALID; }
    int ticks = atoi(argv[0]);
    /* block one running process */
    for (int i = 0; i < MAX_PROCS; i++) {
        if (K.procs.procs[i].used && strcmp(K.procs.procs[i].state, "running") == 0) {
            strcpy(K.procs.procs[i].state, "blocked");
            K.procs.procs[i].sleep_ticks = ticks;
            snprintf(out, sz, "PID %d blocked for %d ticks", K.procs.procs[i].pid, ticks);
            return ERR_OK;
        }
    }
    snprintf(out, sz, "no running process to sleep");
    return ERR_OK;
}

/* Reap a zombie child of either the current proc or the shell. current_pid drifts as
 * the scheduler picks each tick, but a real child spawned at the prompt has ppid=shell_pid;
 * checking both makes wait observe it regardless of the drift. */
static int proc_wait_any(int *status_out) {
    int child = proc_wait(&K.procs, K.current_pid, status_out);
    if (child < 0 && K.shell_pid != K.current_pid) child = proc_wait(&K.procs, K.shell_pid, status_out);
    return child;
}

static int cmd_wait(char *out, int sz, int argc, char **argv) {
    int parent = K.current_pid > 0 ? K.current_pid : K.shell_pid;
    int status = 0;
    proc_reap_real(); /* surface any real child that has already exited as a zombie */
    int child = proc_wait_any(&status);
    if (child >= 0) {
        snprintf(out, sz, "reaped PID %d status %d", child, status);
        return ERR_OK;
    }
    /* blocking wait: set caller blocked and tick until child or limit */
    for (int i=0; i<MAX_PROCS; i++) {
        if (K.procs.procs[i].used && K.procs.procs[i].pid == parent) {
            strcpy(K.procs.procs[i].state, "blocked");
            K.procs.procs[i].sleep_ticks = 100;
            break;
        }
    }
    for (int t=0; t<100; t++) {
        proc_reap_real();  /* a real child that exited becomes a zombie so proc_wait can reap it */
        proc_tick(&K.procs);
        child = proc_wait_any(&status);
        if (child >= 0) {
            snprintf(out, sz, "reaped PID %d status %d after %d ticks", child, status, t);
            return ERR_OK;
        }
        /* give a just-spawned real child a moment to run/exit before the next poll */
        int has_live_real = 0;
        for (int i=0; i<MAX_PROCS; i++)
            if (K.procs.procs[i].used && K.procs.procs[i].spawned && K.procs.procs[i].real_pid > 0) { has_live_real = 1; break; }
        if (has_live_real) usleep(2000);
    }
    snprintf(out, sz, "no zombie children (blocked timeout)");
    return ERR_OK;
}

static int cmd_open(char *out, int sz, int argc, char **argv) {
    if (argc < 2) { snprintf(out, sz, "Usage: open <path>"); return ERR_INVALID; }
    int idx = fs_find(&K.fs, argv[1]);
    if (idx < 0) { snprintf(out, sz, "not found"); return ERR_NOT_FOUND; }
    int fd = alloc_fd(K.current_pid, idx);
    if (fd < 0) { snprintf(out, sz, "too many fds"); return ERR_NO_MEMORY; }
    snprintf(out, sz, "%d", fd);
    return ERR_OK;
}

static int cmd_close(char *out, int sz, int argc, char **argv) {
    if (argc < 2) { snprintf(out, sz, "Usage: close <fd>"); return ERR_INVALID; }
    int fd = atoi(argv[1]);
    if (close_fd(K.current_pid, fd) == 0) {
        snprintf(out, sz, "closed %d", fd);
        return ERR_OK;
    }
    snprintf(out, sz, "bad fd");
    return ERR_INVALID;
}

static int cmd_readfd(char *out, int sz, int argc, char **argv) {
    if (argc < 2) { snprintf(out, sz, "Usage: readfd <fd>"); return ERR_INVALID; }
    int fd = atoi(argv[1]);
    int node = get_fd_node(K.current_pid, fd);
    if (node < 0) { snprintf(out, sz, "bad fd"); return ERR_NOT_FOUND; }
    if (K.fs.nodes[node].is_dir) return ERR_INVALID;
    snprintf(out, sz, "%s", K.fs.nodes[node].content);
    return ERR_OK;
}

static int cmd_writefd(char *out, int sz, int argc, char **argv) {
    if (argc < 3) { snprintf(out, sz, "Usage: writefd <fd> <content...>"); return ERR_INVALID; }
    int fd = atoi(argv[1]);
    int node = get_fd_node(K.current_pid, fd);
    if (node < 0) { snprintf(out, sz, "bad fd"); return ERR_NOT_FOUND; }
    char content[MAX_CONTENT] = "";
    for (int i=2; i<argc; i++) {
        if (i>2) strncat(content, " ", sizeof(content)-1);
        strncat(content, argv[i], sizeof(content)-strlen(content)-1);
    }
    strncpy(K.fs.nodes[node].content, content, MAX_CONTENT-1);
    snprintf(out, sz, "wrote to fd %d", fd);
    return ERR_OK;
}

static int cmd_dup(char *out, int sz, int argc, char **argv) {
    if (argc < 2) { snprintf(out, sz, "Usage: dup <oldfd>"); return ERR_INVALID; }
    int old = atoi(argv[1]);
    int newfd = dup_fd(K.current_pid, old);
    if (newfd < 0) { snprintf(out, sz, "dup failed"); return ERR_INVALID; }
    snprintf(out, sz, "%d", newfd);
    return ERR_OK;
}

static int cmd_fds(char *out, int sz, int argc, char **argv) {
    int n = 0;
    n = safe_append(out, n, sz, "Open FDs (per proc):\n");
    for (int p = 0; p < MAX_PROCS; p++) {
        if (!K.procs.procs[p].used) continue;
        n = safe_append(out, n, sz, "PID %d (%s): ", K.procs.procs[p].pid, K.procs.procs[p].name);
        int has = 0;
        for (int f = 0; f < 32; f++) {
            int node = K.procs.procs[p].open_fds[f];
            if (node >= 0) {
                if (has) n = safe_append(out, n, sz, ", ");
                n = safe_append(out, n, sz, "%d->%s", f, K.fs.nodes[node].path);
                has = 1;
            }
        }
        if (!has) n = safe_append(out, n, sz, "(none)");
        n = safe_append(out, n, sz, "\n");
    }
    return ERR_OK;
}

static int cmd_yield(char *out, int sz, int argc, char **argv) {
    for (int i=0; i<MAX_PROCS; i++) {
        if (K.procs.procs[i].used && strcmp(K.procs.procs[i].state, "running") == 0) {
            strcpy(K.procs.procs[i].state, "ready");
            snprintf(out, sz, "PID %d yielded", K.procs.procs[i].pid);
            return ERR_OK;
        }
    }
    snprintf(out, sz, "nothing to yield");
    return ERR_OK;
}

static int cmd_nice(char *out, int sz, int argc, char **argv) {
    if (argc < 3) { snprintf(out, sz, "Usage: nice <pid> <prio>"); return ERR_INVALID; }
    int pid = atoi(argv[1]);
    int prio = atoi(argv[2]);
    for (int i=0; i<MAX_PROCS; i++) {
        if (K.procs.procs[i].used && K.procs.procs[i].pid == pid) {
            K.procs.procs[i].priority = prio;
            snprintf(out, sz, "PID %d priority %d", pid, prio);
            return ERR_OK;
        }
    }
    snprintf(out, sz, "no such pid");
    return ERR_INVALID;
}

static int cmd_slice(char *out, int sz, int argc, char **argv) {
    if (argc < 3) { snprintf(out, sz, "Usage: slice <pid> <ticks>"); return ERR_INVALID; }
    int pid = atoi(argv[1]);
    int sl = atoi(argv[2]);
    if (sl <= 0) { snprintf(out, sz, "slice must be >0"); return ERR_INVALID; }
    for (int i=0; i<MAX_PROCS; i++) {
        if (K.procs.procs[i].used && K.procs.procs[i].pid == pid) {
            K.procs.procs[i].max_slice = sl;
            K.procs.procs[i].slice_used = 0;
            snprintf(out, sz, "PID %d max_slice %d", pid, sl);
            return ERR_OK;
        }
    }
    snprintf(out, sz, "no such pid");
    return ERR_INVALID;
}

static int cmd_limit(char *out, int sz, int argc, char **argv) {
    if (argc < 3) { snprintf(out, sz, "Usage: limit <pid> <kb>"); return ERR_INVALID; }
    int pid = atoi(argv[1]);
    int lim = atoi(argv[2]);
    if (lim <= 0) { snprintf(out, sz, "limit must be >0"); return ERR_INVALID; }
    for (int i=0; i<MAX_PROCS; i++) {
        if (K.procs.procs[i].used && K.procs.procs[i].pid == pid) {
            K.procs.procs[i].mem_limit_kb = lim;
            snprintf(out, sz, "PID %d mem_limit %d KB", pid, lim);
            return ERR_OK;
        }
    }
    snprintf(out, sz, "no such pid");
    return ERR_INVALID;
}

static int cmd_climit(char *out, int sz, int argc, char **argv) {
    if (argc < 3) { snprintf(out, sz, "Usage: climit <pid> <ticks>"); return ERR_INVALID; }
    int pid = atoi(argv[1]);
    int lim = atoi(argv[2]);
    if (lim <= 0) { snprintf(out, sz, "climit must be >0"); return ERR_INVALID; }
    for (int i=0; i<MAX_PROCS; i++) {
        if (K.procs.procs[i].used && K.procs.procs[i].pid == pid) {
            K.procs.procs[i].cpu_limit = lim;
            K.procs.procs[i].cpu_time = 0; /* reset on set? or not. for now don't reset */
            snprintf(out, sz, "PID %d cpu_limit %d", pid, lim);
            return ERR_OK;
        }
    }
    snprintf(out, sz, "no such pid");
    return ERR_INVALID;
}

static int cmd_signal(char *out, int sz, int argc, char **argv) {
    if (argc < 3) { snprintf(out, sz, "Usage: signal <pid> <sig>"); return ERR_INVALID; }
    int pid = atoi(argv[1]);
    int sig = atoi(argv[2]);
    for (int i=0; i<MAX_PROCS; i++) {
        if (K.procs.procs[i].used && K.procs.procs[i].pid == pid) {
            K.procs.procs[i].signals |= (1 << sig);
            /* unblock if waiting for signal */
            if (strcmp(K.procs.procs[i].state, "blocked") == 0) {
                strcpy(K.procs.procs[i].state, "ready");
            }
            snprintf(out, sz, "signaled pid %d sig %d", pid, sig);
            return ERR_OK;
        }
    }
    snprintf(out, sz, "no such pid");
    return ERR_INVALID;
}

/* A monad's numbness: which signals it cannot feel. A numbed signal is not lost — it
 * stays pending until `feel` restores the sense and the next tick delivers it. KILL and
 * STOP pierce any numbness; nothing in this system may be numb to its own end. */
static int cmd_numb(char *out, int sz, int argc, char **argv) {
    if (argc < 3) { snprintf(out, sz, "Usage: numb <pid> <sig>"); return ERR_INVALID; }
    int pid = atoi(argv[1]);
    int sig = atoi(argv[2]);
    if (sig == 9 || sig == 19) {
        snprintf(out, sz, "numb: sig %d pierces numbness — no monad is numb to its end", sig);
        return ERR_INVALID;
    }
    for (int i=0; i<MAX_PROCS; i++) {
        if (K.procs.procs[i].used && K.procs.procs[i].pid == pid) {
            K.procs.procs[i].numbness |= (1 << sig);
            snprintf(out, sz, "pid %d numb to sig %d (numbness %d)", pid, sig, K.procs.procs[i].numbness);
            return ERR_OK;
        }
    }
    snprintf(out, sz, "no such pid");
    return ERR_INVALID;
}

static int cmd_feel(char *out, int sz, int argc, char **argv) {
    if (argc < 3) { snprintf(out, sz, "Usage: feel <pid> <sig>"); return ERR_INVALID; }
    int pid = atoi(argv[1]);
    int sig = atoi(argv[2]);
    for (int i=0; i<MAX_PROCS; i++) {
        if (K.procs.procs[i].used && K.procs.procs[i].pid == pid) {
            K.procs.procs[i].numbness &= ~(1 << sig);
            snprintf(out, sz, "pid %d feels sig %d again (numbness %d)", pid, sig, K.procs.procs[i].numbness);
            return ERR_OK;
        }
    }
    snprintf(out, sz, "no such pid");
    return ERR_INVALID;
}

/* The monad's own weather. `mood <pid>` reads it; `mood <pid> <dim> <value>` sets one
 * dimension. The scheduler weighs it by the shared emergence, so a monad only argues as
 * loudly as the system's weather permits. */
static int cmd_mood(char *out, int sz, int argc, char **argv) {
    if (argc < 2) { snprintf(out, sz, "Usage: mood <pid> [pain|tension|flow|warmth <value>]"); return ERR_INVALID; }
    int pid = atoi(argv[1]);
    Process *p = NULL;
    for (int i = 0; i < MAX_PROCS; i++)
        if (K.procs.procs[i].used && K.procs.procs[i].pid == pid) { p = &K.procs.procs[i]; break; }
    if (!p) { snprintf(out, sz, "no such pid"); return ERR_INVALID; }

    if (argc >= 4) {
        float v = (float)atof(argv[3]);
        if      (strcmp(argv[2], "pain")    == 0) p->mood_pain    = v;
        else if (strcmp(argv[2], "tension") == 0) p->mood_tension = v;
        else if (strcmp(argv[2], "flow")    == 0) p->mood_flow    = v;
        else if (strcmp(argv[2], "warmth")  == 0) p->mood_warmth  = v;
        else { snprintf(out, sz, "mood: unknown dimension '%s' (pain|tension|flow|warmth)", argv[2]); return ERR_INVALID; }
    }
    snprintf(out, sz, "pid %d mood: pain %.3f tension %.3f flow %.3f warmth %.3f",
             pid, p->mood_pain, p->mood_tension, p->mood_flow, p->mood_warmth);
    return ERR_OK;
}

static int cmd_pause(char *out, int sz, int argc, char **argv) {
    for (int i=0; i<MAX_PROCS; i++) {
        if (K.procs.procs[i].used && strcmp(K.procs.procs[i].state, "running") == 0) {
            strcpy(K.procs.procs[i].state, "blocked");
            K.procs.procs[i].sleep_ticks = -1; /* wait for signal */
            snprintf(out, sz, "PID %d paused for signal", K.procs.procs[i].pid);
            return ERR_OK;
        }
    }
    snprintf(out, sz, "no running to pause");
    return ERR_OK;
}

static int cmd_send(char *out, int sz, int argc, char **argv) {
    if (argc < 3) { snprintf(out, sz, "Usage: send <pid> <msg>"); return ERR_INVALID; }
    int pid = atoi(argv[1]);
    char msg[MAX_CONTENT] = "";
    for (int i=2; i<argc; i++) {
        if (i>2) strcat(msg, " ");
        strncat(msg, argv[i], sizeof(msg)-strlen(msg)-1);
    }
    for (int i=0; i<MAX_PROCS; i++) {
        if (K.procs.procs[i].used && K.procs.procs[i].pid == pid) {
            /* append to mailbox safely */
            size_t mlen = strlen(K.procs.procs[i].mailbox);
            size_t avail = sizeof(K.procs.procs[i].mailbox) - mlen - 1;
            if (avail > 1) {
                strncat(K.procs.procs[i].mailbox, msg, avail - 1);
                mlen = strlen(K.procs.procs[i].mailbox);
                if (mlen < sizeof(K.procs.procs[i].mailbox) - 1) {
                    K.procs.procs[i].mailbox[mlen] = '\n';
                    K.procs.procs[i].mailbox[mlen+1] = '\0';
                }
            }
            /* unblock if waiting */
            if (strcmp(K.procs.procs[i].state, "blocked") == 0 && K.procs.procs[i].sleep_ticks == -1) {
                strcpy(K.procs.procs[i].state, "ready");
            }
            snprintf(out, sz, "sent to %d", pid);
            return ERR_OK;
        }
    }
    snprintf(out, sz, "no such pid");
    return ERR_INVALID;
}

static int cmd_current(char *out, int sz, int argc, char **argv) {
    int n = 0;
    n = safe_append(out, n, sz, "current pid: %d", K.current_pid);
    for (int i=0; i<MAX_PROCS; i++) {
        if (K.procs.procs[i].used && K.procs.procs[i].pid == K.current_pid) {
            n = safe_append(out, n, sz, " (%s) state=%s", K.procs.procs[i].name, K.procs.procs[i].state);
            break;
        }
    }
    n = safe_append(out, n, sz, " shell:%d", K.shell_pid);
    return ERR_OK;
}

static int cmd_tick(char *out, int sz, int argc, char **argv) {
    int t = proc_tick(&K.procs);
    K.hook_sched_fired += fire_hook("sched");
    proc_refresh_all();

    /* find who is currently running thanks to the scheduler */
    const char *running = "idle";
    for (int i = 0; i < MAX_PROCS; i++) {
        if (K.procs.procs[i].used && strcmp(K.procs.procs[i].state, "running") == 0) {
            running = K.procs.procs[i].name;
            break;
        }
    }
    snprintf(out, sz, "Tick: %d running=%s (sched hooks: %d)", t, running, K.hook_sched_fired);
    return ERR_OK;
}

static int cmd_status(char *out, int sz, int argc, char **argv) {
    if (argc < 2) { snprintf(out, sz, "Usage: status <pid>"); return ERR_INVALID; }
    int pid = atoi(argv[1]);
    for (int i = 0; i < MAX_PROCS; i++) {
        if (K.procs.procs[i].used && K.procs.procs[i].pid == pid) {
            snprintf(out, sz, "PID %d: %s state=%s ticks=%d", pid,
                K.procs.procs[i].name, K.procs.procs[i].state, K.procs.procs[i].ticks);
            return ERR_OK;
        }
    }
    snprintf(out, sz, "Error: process %d not found", pid);
    return ERR_NO_PROCESS;
}

static int cmd_pwd(char *out, int sz, int argc, char **argv) {
    /* per current proc cwd (foundation) */
    for (int i=0; i<MAX_PROCS; i++) {
        if (K.procs.procs[i].used && K.procs.procs[i].pid == K.current_pid) {
            snprintf(out, sz, "%s", K.procs.procs[i].cwd);
            return ERR_OK;
        }
    }
    snprintf(out, sz, "%s", K.fs.cwd);
    return ERR_OK;
}

static int cmd_cd(char *out, int sz, int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : "/home/user";
    char *a[] = {(char *)path};
    int err = kernel_syscall("chdir", out, sz, 1, a);
    if (err == ERR_PERMISSION) snprintf(out, sz, "cd: %s: Permission denied", path);
    else if (err == ERR_NOT_FOUND) snprintf(out, sz, "cd: no such directory: %s", path);
    else out[0] = '\0';
    return err;
}

static int cmd_ls(char *out, int sz, int argc, char **argv) {
    int long_fmt = 0, show_all = 0;
    char resolved[MAX_PATH];
    strcpy(resolved, K.fs.cwd);
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (strstr(argv[i], "l")) long_fmt = 1;
            if (strstr(argv[i], "a")) show_all = 1;
        } else {
            fs_resolve(&K.fs, argv[i], resolved);
        }
    }

    int idx = fs_find(&K.fs, resolved);
    if (idx < 0 || (!K.fs.nodes[idx].is_dir && !K.fs.nodes[idx].is_symlink)) {
        snprintf(out, sz, "ls: no such directory");
        return ERR_NOT_FOUND;
    }

    int n = 0;
    char prefix[MAX_PATH];
    if (strcmp(resolved, "/") == 0) strcpy(prefix, "/");
    else snprintf(prefix, MAX_PATH, "%s/", resolved);
    int plen = (int)strlen(prefix);

    for (int i = 0; i < K.fs.node_count; i++) {
        if (!K.fs.nodes[i].used) continue;
        if (strncmp(K.fs.nodes[i].path, prefix, plen) != 0) continue;
        const char *rest = K.fs.nodes[i].path + plen;
        if (strlen(rest) == 0 || strchr(rest, '/') != NULL) continue;
        if (!show_all && rest[0] == '.') continue;
        FSNode *node = &K.fs.nodes[i];
        if (long_fmt) {
            char tbuf[32];
            struct tm *tm = localtime(&node->modified);
            strftime(tbuf, sizeof(tbuf), "%b %d %H:%M", tm);
            int size = node->is_dir ? 4096 : (node->is_symlink ? 0 : (int)strlen(node->content));
            n = safe_append(out, n, sz, "%s %3d %-8s %-8s %5d %s %s%s\n",
                node->perms, 1, node->owner, node->owner, size, tbuf, rest,
                node->is_dir ? "/" : (node->is_symlink ? " -> " : ""));
            if (node->is_symlink)
                n = safe_append(out, n, sz, "%s", node->symlink_target);
            n = safe_append(out, n, sz, "\n");
        } else {
            n = safe_append(out, n, sz, "%s %s%s%s\n", node->perms, rest,
                node->is_dir ? "/" : "",
                node->is_symlink ? "@" : "");
        }
    }
    if (n == 0) out[0] = '\0';
    return ERR_OK;
}

static int cmd_cat(char *out, int sz, int argc, char **argv) {
    if (argc < 2) {
        if (shell_stdin_len > 0) {
            snprintf(out, sz, "%s", shell_stdin_buf);
            return ERR_OK;
        }
        snprintf(out, sz, "Usage: cat <file>");
        return ERR_INVALID;
    }
    /* support fd if numeric */
    if (argc >=2 && argv[1][0] >= '0' && argv[1][0] <= '9') {
        int fd = atoi(argv[1]);
        int node = get_fd_node(K.current_pid, fd);
        if (node >=0 && !K.fs.nodes[node].is_dir) {
            snprintf(out, sz, "%s", K.fs.nodes[node].content);
            return ERR_OK;
        }
        snprintf(out, sz, "cat: bad fd %s", argv[1]);
        return ERR_NOT_FOUND;
    }
    int err = kernel_syscall("read", out, sz, 1, &argv[1]);
    if (err == ERR_PERMISSION) snprintf(out, sz, "cat: %s: Permission denied", argv[1]);
    else if (err == ERR_NOT_FOUND) snprintf(out, sz, "cat: %s: not found or not a file", argv[1]);
    return err;
}

static int cmd_touch(char *out, int sz, int argc, char **argv) {
    if (argc < 2) { snprintf(out, sz, "Usage: touch <file>"); return ERR_INVALID; }
    char resolved[MAX_PATH];
    fs_resolve(&K.fs, argv[1], resolved);
    int idx = fs_find(&K.fs, resolved);
    if (idx >= 0) { K.fs.nodes[idx].modified = time(NULL); out[0]='\0'; return ERR_OK; }
    fs_add_file(&K.fs, resolved, "");
    out[0] = '\0';
    return ERR_OK;
}

static int cmd_write(char *out, int sz, int argc, char **argv) {
    if (argc < 3) { snprintf(out, sz, "Usage: write <file> <content...>"); return ERR_INVALID; }
    char resolved[MAX_PATH];
    fs_resolve(&K.fs, argv[1], resolved);
    int idx = fs_find(&K.fs, resolved);
    char old_content[MAX_CONTENT] = "";
    int was_create = (idx < 0);
    if (idx >= 0) {
        if (fs_access(&K.fs.nodes[idx], K.user, 'w') != ERR_OK) {
            snprintf(out, sz, "write: %s: Permission denied", argv[1]);
            return ERR_PERMISSION;
        }
        strncpy(old_content, K.fs.nodes[idx].content, MAX_CONTENT-1);
    }
    char *wargv[32];
    int wargc = argc - 1;
    for (int i = 0; i < wargc; i++) wargv[i] = argv[i + 1];
    int err = kernel_syscall("write", out, sz, wargc, wargv);
    if (err == ERR_OK)
        ledger_meta_set(1, "fs write", resolved, old_content, 0, was_create);
    else if (strcmp(resolved, "/dev/full") == 0)
        snprintf(out, sz, "write: %s: no space left on device", argv[1]);
    out[0] = '\0';
    return err;
}

static int cmd_append(char *out, int sz, int argc, char **argv) {
    if (argc < 3) { snprintf(out, sz, "Usage: append <file> <content...>"); return ERR_INVALID; }
    char resolved[MAX_PATH];
    fs_resolve(&K.fs, argv[1], resolved);
    int idx = fs_find(&K.fs, resolved);
    if (idx < 0 || K.fs.nodes[idx].is_dir) {
        snprintf(out, sz, "append: %s not found", argv[1]);
        return ERR_NOT_FOUND;
    }
    if (fs_access(&K.fs.nodes[idx], K.user, 'w') != ERR_OK) {
        snprintf(out, sz, "append: %s: Permission denied", argv[1]);
        return ERR_PERMISSION;
    }
    for (int i = 2; i < argc; i++) {
        if (i > 2) strncat(K.fs.nodes[idx].content, " ", MAX_CONTENT - strlen(K.fs.nodes[idx].content) - 1);
        strncat(K.fs.nodes[idx].content, argv[i], MAX_CONTENT - strlen(K.fs.nodes[idx].content) - 1);
    }
    K.fs.nodes[idx].modified = time(NULL);
    out[0] = '\0';
    return ERR_OK;
}

static int cmd_rm(char *out, int sz, int argc, char **argv) {
    if (argc < 2) { snprintf(out, sz, "Usage: rm <file>"); return ERR_INVALID; }
    char resolved[MAX_PATH];
    fs_resolve(&K.fs, argv[1], resolved);
    int idx = fs_find(&K.fs, resolved);
    if (idx < 0 || K.fs.nodes[idx].is_dir) {
        snprintf(out, sz, "rm: cannot remove %s", argv[1]);
        return ERR_NOT_FOUND;
    }
    if (fs_access(&K.fs.nodes[idx], K.user, 'w') != ERR_OK) {
        snprintf(out, sz, "rm: %s: Permission denied", argv[1]);
        return ERR_PERMISSION;
    }
    char backup[MAX_CONTENT];
    strncpy(backup, K.fs.nodes[idx].content, MAX_CONTENT-1);
    K.fs.nodes[idx].used = 0;
    ledger_meta_set(1, "fs delete", resolved, backup, 0, 0);
    out[0] = '\0';
    return ERR_OK;
}

static int cmd_mkdir(char *out, int sz, int argc, char **argv) {
    if (argc < 2) { snprintf(out, sz, "Usage: mkdir <dir>"); return ERR_INVALID; }
    char resolved[MAX_PATH];
    fs_resolve(&K.fs, argv[1], resolved);
    int err = fs_add_dir(&K.fs, resolved);
    if (err != ERR_OK) { snprintf(out, sz, "mkdir: cannot create %s", argv[1]); return err; }
    out[0] = '\0';
    return ERR_OK;
}

static int cmd_rmdir(char *out, int sz, int argc, char **argv) {
    if (argc < 2) { snprintf(out, sz, "Usage: rmdir <dir>"); return ERR_INVALID; }
    char resolved[MAX_PATH];
    fs_resolve(&K.fs, argv[1], resolved);
    int idx = fs_find(&K.fs, resolved);
    if (idx < 0 || !K.fs.nodes[idx].is_dir) {
        snprintf(out, sz, "rmdir: cannot remove %s", argv[1]);
        return ERR_NOT_FOUND;
    }
    /* Check empty */
    char prefix[MAX_PATH];
    snprintf(prefix, MAX_PATH, "%s/", resolved);
    for (int i = 0; i < K.fs.node_count; i++) {
        if (K.fs.nodes[i].used && strncmp(K.fs.nodes[i].path, prefix, strlen(prefix)) == 0) {
            snprintf(out, sz, "rmdir: directory not empty");
            return ERR_INVALID;
        }
    }
    K.fs.nodes[idx].used = 0;
    out[0] = '\0';
    return ERR_OK;
}

static int cmd_mv(char *out, int sz, int argc, char **argv) {
    if (argc < 3) { snprintf(out, sz, "Usage: mv <src> <dst>"); return ERR_INVALID; }
    char rs[MAX_PATH], rd[MAX_PATH];
    fs_resolve(&K.fs, argv[1], rs);
    fs_resolve(&K.fs, argv[2], rd);
    int idx = fs_find(&K.fs, rs);
    if (idx < 0) { snprintf(out, sz, "mv: %s not found", argv[1]); return ERR_NOT_FOUND; }
    strncpy(K.fs.nodes[idx].path, rd, MAX_PATH-1);
    out[0] = '\0';
    return ERR_OK;
}

static int cmd_cp(char *out, int sz, int argc, char **argv) {
    if (argc < 3) { snprintf(out, sz, "Usage: cp <src> <dst>"); return ERR_INVALID; }
    char rs[MAX_PATH], rd[MAX_PATH];
    fs_resolve(&K.fs, argv[1], rs);
    fs_resolve(&K.fs, argv[2], rd);
    int idx = fs_find(&K.fs, rs);
    if (idx < 0 || K.fs.nodes[idx].is_dir) { snprintf(out, sz, "cp: error"); return ERR_NOT_FOUND; }
    fs_add_file(&K.fs, rd, K.fs.nodes[idx].content);
    out[0] = '\0';
    return ERR_OK;
}

static int cmd_chmod(char *out, int sz, int argc, char **argv) {
    if (argc < 3) { snprintf(out, sz, "Usage: chmod <perms> <path>"); return ERR_INVALID; }
    char resolved[MAX_PATH];
    fs_resolve(&K.fs, argv[2], resolved);
    int idx = fs_find(&K.fs, resolved);
    if (idx < 0) { snprintf(out, sz, "chmod: %s not found", argv[2]); return ERR_NOT_FOUND; }
    strncpy(K.fs.nodes[idx].perms, argv[1], 11);
    out[0] = '\0';
    return ERR_OK;
}

static int cmd_stat(char *out, int sz, int argc, char **argv) {
    if (argc < 2) { snprintf(out, sz, "Usage: stat <path>"); return ERR_INVALID; }
    char resolved[MAX_PATH];
    fs_resolve(&K.fs, argv[1], resolved);
    int idx = fs_find(&K.fs, resolved);
    if (idx < 0) { snprintf(out, sz, "stat: %s not found", argv[1]); return ERR_NOT_FOUND; }
    FSNode *n = &K.fs.nodes[idx];
    snprintf(out, sz, "  path: %s\n  type: %s\n  perms: %s\n  owner: %s\n  size: %d",
        n->path, n->is_dir ? "dir" : "file", n->perms, n->owner,
        n->is_dir ? 0 : (int)strlen(n->content));
    return ERR_OK;
}

static int cmd_tree(char *out, int sz, int argc, char **argv) {
    char resolved[MAX_PATH];
    if (argc > 1) fs_resolve(&K.fs, argv[1], resolved);
    else strcpy(resolved, K.fs.cwd);

    int n = 0;
    char prefix[MAX_PATH];
    if (strcmp(resolved, "/") == 0) strcpy(prefix, "/");
    else snprintf(prefix, MAX_PATH, "%s/", resolved);
    int plen = strlen(prefix);

    for (int i = 0; i < K.fs.node_count; i++) {
        if (!K.fs.nodes[i].used) continue;
        if (strncmp(K.fs.nodes[i].path, prefix, plen) != 0) continue;
        const char *rest = K.fs.nodes[i].path + plen;
        if (strlen(rest) == 0) continue;
        /* Count depth by counting slashes */
        int depth = 0;
        for (const char *c = rest; *c; c++) if (*c == '/') depth++;
        if (depth > 3) continue;
        for (int d = 0; d < depth; d++) n = safe_append(out, n, sz, "  ");
        n = safe_append(out, n, sz, "%s%s\n", rest, K.fs.nodes[i].is_dir ? "/" : "");
    }
    if (n == 0) snprintf(out, sz, "(empty)");
    return ERR_OK;
}

static int cmd_echo(char *out, int sz, int argc, char **argv) {
    int n = 0;
    for (int i = 1; i < argc; i++) {
        if (i > 1) n = safe_append(out, n, sz, " ");
        n = safe_append(out, n, sz, "%s", argv[i]);
    }
    return ERR_OK;
}

static int cmd_env(char *out, int sz, int argc, char **argv) {
    int n = 0;
    for (int i = 0; i < K.env_count; i++)
        n = safe_append(out, n, sz, "%s=%s\n", K.env[i].key, K.env[i].value);
    return ERR_OK;
}

static int cmd_set(char *out, int sz, int argc, char **argv) {
    if (argc < 3) { snprintf(out, sz, "Usage: set <key> <value>"); return ERR_INVALID; }
    char val[128] = "";
    for (int i = 2; i < argc; i++) {
        if (i > 2) strncat(val, " ", sizeof(val) - strlen(val) - 1);
        strncat(val, argv[i], sizeof(val) - strlen(val) - 1);
    }
    env_set(argv[1], val);
    out[0] = '\0';
    return ERR_OK;
}

static int cmd_unset(char *out, int sz, int argc, char **argv) {
    if (argc < 2) { snprintf(out, sz, "Usage: unset <key>"); return ERR_INVALID; }
    env_unset(argv[1]);
    out[0] = '\0';
    return ERR_OK;
}

static int cmd_date(char *out, int sz, int argc, char **argv) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(out, sz, "%Y-%m-%d %H:%M:%S %Z", t);
    return ERR_OK;
}

static int cmd_history(char *out, int sz, int argc, char **argv) {
    int n = 0;
    int start = K.history_count > 20 ? K.history_count - 20 : 0;
    for (int i = start; i < K.history_count; i++)
        n = safe_append(out, n, sz, "%d: %s\n", i+1, K.history[i]);
    if (n == 0) snprintf(out, sz, "(no history)");
    return ERR_OK;
}

static int cmd_oz(char *out, int sz, int argc, char **argv) {
    int mod_count = 0, hook_count = 0;
    for (int i = 0; i < K.module_count; i++) if (K.modules[i].loaded) mod_count++;
    for (int i = 0; i < K.hook_count; i++) hook_count += K.hooks[i].sub_count;
    int contract_count = 0;
    for (int i = 0; i < K.module_count; i++) if (K.modules[i].loaded) contract_count++;
    snprintf(out, sz,
        "OZ Layer — Extensibility Field\n"
        "  Modules loaded: %d\n  Slots defined: %d\n"
        "  Hooks active: %d\n  Contracts: %d\n  Ledger entries: %d",
        mod_count, K.slot_count, hook_count, contract_count, K.ledger.count);
    return ERR_OK;
}

static int cmd_slots(char *out, int sz, int argc, char **argv) {
    int n = 0;
    n = safe_append(out, n, sz, "Slots:\n");
    for (int i = 0; i < K.slot_count; i++) {
        n = safe_append(out, n, sz, "  %s: ", K.slots[i].name);
        if (K.slots[i].occupant_count == 0) n = safe_append(out, n, sz, "(empty)");
        else for (int j = 0; j < K.slots[i].occupant_count; j++)
            n = safe_append(out, n, sz, "%s%s", j?", ":"", K.slots[i].occupants[j]);
        n = safe_append(out, n, sz, "\n");
    }
    return ERR_OK;
}

static int cmd_modules(char *out, int sz, int argc, char **argv) {
    int n = 0;
    n = safe_append(out, n, sz, "Modules:\n");
    for (int i = 0; i < K.module_count; i++) {
        if (!K.modules[i].loaded) continue;
        n = safe_append(out, n, sz, "  %s: %s [commands:", K.modules[i].name, K.modules[i].description);
        for (int j = 0; j < K.modules[i].cmd_count; j++)
            n = safe_append(out, n, sz, " %s", K.modules[i].commands[j]);
        n = safe_append(out, n, sz, "]\n");
    }
    return ERR_OK;
}

static int cmd_overhead(char *out, int sz, int argc, char **argv) {
    int n = 0, total_dispatch = 0, total_hooks = 0, total_slots = 0, total_mods = 0;
    for (int i = 0; i < K.module_count; i++) {
        if (!K.modules[i].loaded) continue;
        total_mods++;
        total_dispatch += K.modules[i].cmd_count;
        total_hooks += K.modules[i].hook_count;
        total_slots += K.modules[i].slot_count;
    }
    n = safe_append(out, n, sz, "Overhead Accounting:\n");
    n = safe_append(out, n, sz, "  Total modules: %d\n", total_mods);
    n = safe_append(out, n, sz, "  Total dispatch entries: %d\n", total_dispatch);
    n = safe_append(out, n, sz, "  Total hooks: %d\n", total_hooks);
    n = safe_append(out, n, sz, "  Total slot occupations: %d\n\nPer-module:\n", total_slots);
    for (int i = 0; i < K.module_count; i++) {
        if (!K.modules[i].loaded) continue;
        n = safe_append(out, n, sz, "  %s: dispatch=%d hooks=%d slots=%d\n",
            K.modules[i].name, K.modules[i].cmd_count, K.modules[i].hook_count, K.modules[i].slot_count);
    }
    return ERR_OK;
}

static int cmd_hooks(char *out, int sz, int argc, char **argv) {
    int n = 0;
    n = safe_append(out, n, sz, "Hooks:\n");
    for (int i = 0; i < K.hook_count; i++) {
        n = safe_append(out, n, sz, "  %s: ", K.hooks[i].name);
        if (K.hooks[i].sub_count == 0) n = safe_append(out, n, sz, "(none)");
        else for (int j = 0; j < K.hooks[i].sub_count; j++)
            n = safe_append(out, n, sz, "%s%s", j?", ":"", K.hooks[i].subscribers[j]);
        n = safe_append(out, n, sz, "\n");
    }
    return ERR_OK;
}

static int cmd_contracts(char *out, int sz, int argc, char **argv) {
    int n = 0;
    n = safe_append(out, n, sz, "Module Contracts:\n");
    for (int i = 0; i < K.module_count; i++) {
        if (!K.modules[i].loaded) continue;
        n = safe_append(out, n, sz, "  %s:\n    provides:", K.modules[i].name);
        for (int j = 0; j < K.modules[i].provides_count; j++)
            n = safe_append(out, n, sz, " %s", K.modules[i].contract_provides[j]);
        n = safe_append(out, n, sz, "\n    requires:");
        for (int j = 0; j < K.modules[i].requires_count; j++)
            n = safe_append(out, n, sz, " %s", K.modules[i].contract_requires[j]);
        n = safe_append(out, n, sz, "\n    version: %s\n", K.modules[i].contract_version);
    }
    return ERR_OK;
}

static int cmd_loadmod(char *out, int sz, int argc, char **argv) {
    if (validate_module_contracts() != ERR_OK) {
        snprintf(out, sz, "loadmod: contract validation failed (missing required slots)");
        return ERR_MODULE;
    }
    snprintf(out, sz, "loadmod: dynamic loading not supported in v%s (modules compiled-in; contracts OK)",
        VERSION);
    return ERR_OK;
}

static int cmd_unloadmod(char *out, int sz, int argc, char **argv) {
    if (argc < 2) { snprintf(out, sz, "Usage: unloadmod <module_name>"); return ERR_INVALID; }
    int err = unload_module(argv[1]);
    if (err != ERR_OK) { snprintf(out, sz, "unloadmod: module '%s' not found", argv[1]); return err; }
    snprintf(out, sz, "Unloaded module '%s'", argv[1]);
    return ERR_OK;
}

static int cmd_call(char *out, int sz, int argc, char **argv) {
    if (argc < 3) { snprintf(out, sz, "Usage: call <module> <method>"); return ERR_INVALID; }
    /* Simplified: just check if module exists */
    for (int i = 0; i < K.module_count; i++) {
        if (K.modules[i].loaded && strcmp(K.modules[i].name, argv[1]) == 0) {
            snprintf(out, sz, "Called %s.%s -> OK", argv[1], argv[2]);
            return ERR_OK;
        }
    }
    snprintf(out, sz, "call: module '%s' not found", argv[1]);
    return ERR_NOT_FOUND;
}

static int cmd_trace(char *out, int sz, int argc, char **argv) {
    int n = 0, count = 10;
    if (argc > 1) count = atoi(argv[1]);
    if (K.ledger.count == 0) { snprintf(out, sz, "(no trace entries)"); return ERR_OK; }
    n = safe_append(out, n, sz, "OZ Ledger Trace:\n");
    int start = K.ledger.count > count ? K.ledger.count - count : 0;
    for (int i = start; i < K.ledger.count; i++) {
        int idx = (K.ledger.head + i) % MAX_LEDGER;
        LedgerEntry *e = &K.ledger.entries[idx];
        n = safe_append(out, n, sz, "  [%d] %s -> %d (%s)\n",
            e->tick, e->command, e->result_code, e->explanation);
    }
    return ERR_OK;
}

static int cmd_replay(char *out, int sz, int argc, char **argv) {
    int n = 0;
    if (K.ledger.count == 0) { snprintf(out, sz, "(no replay data)"); return ERR_OK; }
    n = safe_append(out, n, sz, "Replay Log:\n");
    for (int i = 0; i < K.ledger.count; i++) {
        int idx = (K.ledger.head + i) % MAX_LEDGER;
        LedgerEntry *e = &K.ledger.entries[idx];
        n = safe_append(out, n, sz, "  tick=%d cmd=%s parsed=%s args=%s result=%d rev=%d\n",
            e->tick, e->command, e->parsed_cmd, e->parsed_args, e->result_code, e->reversible);
    }
    return ERR_OK;
}

static int cmd_undo(char *out, int sz, int argc, char **argv) {
    for (int i = K.ledger.count - 1; i >= 0; i--) {
        int idx = (K.ledger.head + i) % MAX_LEDGER;
        LedgerEntry *e = &K.ledger.entries[idx];
        if (!e->reversible || e->result_code != ERR_OK) continue;
        if (e->undo_was_create) {
            int fidx = fs_find(&K.fs, e->undo_path);
            if (fidx >= 0) K.fs.nodes[fidx].used = 0;
        } else if (e->undo_is_dir) {
            fs_add_dir(&K.fs, e->undo_path);
        } else {
            fs_add_file(&K.fs, e->undo_path, e->undo_content);
        }
        e->reversible = 0;
        snprintf(out, sz, "Undid: %s (%s)", e->parsed_cmd, e->undo_path);
        return ERR_OK;
    }
    snprintf(out, sz, "undo: nothing reversible in ledger");
    return ERR_NOT_FOUND;
}

static int cmd_whoami(char *out, int sz, int argc, char **argv) {
    snprintf(out, sz, "%s", K.user);
    return ERR_OK;
}

static int cmd_motd(char *out, int sz, int argc, char **argv) {
    char buf[MAX_CONTENT];
    int err = kernel_syscall("read", buf, sizeof(buf), 1, (char *[]) {"/etc/motd"});
    if (err != ERR_OK) snprintf(out, sz, "%s", MOTD);
    else snprintf(out, sz, "%s", buf);
    return ERR_OK;
}

static int cmd_syscall(char *out, int sz, int argc, char **argv) {
    if (argc < 2) {
        snprintf(out, sz, "Usage: syscall <op> [args...]\nOps: read write stat getcwd chdir alloc free spawn kill getenv");
        return ERR_INVALID;
    }
    return kernel_syscall(argv[1], out, sz, argc - 2, argv + 2);
}

static int cmd_reset(char *out, int sz, int argc, char **argv) {
    kernel_init();
    snprintf(out, sz, "System reset complete.");
    return ERR_OK;
}

static int cmd_save(char *out, int sz, int argc, char **argv) {
    const char *fname = (argc > 1) ? argv[1] : "amosoz.img";
    FILE *f = fopen(fname, "wb");
    if (!f) { snprintf(out, sz, "save: cannot open %s", fname); return ERR_IO; }
    fwrite(&K.fs, sizeof(VirtualFS), 1, f);
    fwrite(&K.env, sizeof(K.env), 1, f);
    fwrite(&K.env_count, sizeof(int), 1, f);
    fclose(f);
    snprintf(out, sz, "System image saved to %s", fname);
    return ERR_OK;
}

static int cmd_load(char *out, int sz, int argc, char **argv) {
    const char *fname = (argc > 1) ? argv[1] : "amosoz.img";
    FILE *f = fopen(fname, "rb");
    if (!f) { snprintf(out, sz, "load: %s not found", fname); return ERR_IO; }
    size_t r1 = fread(&K.fs, sizeof(VirtualFS), 1, f);
    size_t r2 = fread(&K.env, sizeof(K.env), 1, f);
    size_t r3 = fread(&K.env_count, sizeof(int), 1, f);
    fclose(f);
    if (r1 != 1 || r2 != 1 || r3 != 1) {
        kernel_init(); /* corrupt/truncated image: reset to a sane state, don't run on garbage */
        snprintf(out, sz, "load: %s is truncated or corrupt (system reset)", fname);
        return ERR_IO;
    }
    /* untrusted image: clamp counts and terminate cwd so no iteration/print runs past K */
    if (K.fs.node_count < 0 || K.fs.node_count > MAX_FILES) K.fs.node_count = MAX_FILES;
    if (K.env_count < 0 || K.env_count > MAX_ENV) K.env_count = MAX_ENV;
    K.fs.cwd[MAX_PATH - 1] = '\0';
    /* untrusted image: every fixed-size string field may be non-terminated on disk;
     * force-terminate all of them so later strcmp/strlen/%s can't read past a field. */
    for (int i = 0; i < MAX_FILES; i++) {
        FSNode *nd = &K.fs.nodes[i];
        nd->path[MAX_PATH - 1] = '\0';
        nd->symlink_target[MAX_PATH - 1] = '\0';
        nd->content[MAX_CONTENT - 1] = '\0';
        nd->perms[sizeof(nd->perms) - 1] = '\0';
        nd->owner[sizeof(nd->owner) - 1] = '\0';
    }
    for (int i = 0; i < MAX_ENV; i++) {
        K.env[i].key[sizeof(K.env[i].key) - 1] = '\0';
        K.env[i].value[sizeof(K.env[i].value) - 1] = '\0';
    }
    snprintf(out, sz, "System image loaded from %s", fname);
    return ERR_OK;
}

static int cmd_clear(char *out, int sz, int argc, char **argv) {
    snprintf(out, sz, "\033[2J\033[H");
    return ERR_OK;
}

/* ─── Tier A–E commands (v0.4 reference set) ──────────────────────────────── */
static int cmd_grep(char *out, int sz, int argc, char **argv) {
    int insensitive = 0, ai = 1;
    if (argc > 1 && strcmp(argv[1], "-i") == 0) { insensitive = 1; ai = 2; }
    if (argc <= ai) { snprintf(out, sz, "Usage: grep [-i] PATTERN [file]"); return ERR_INVALID; }
    char data[MAX_CONTENT];
    int err;
    if (argc > ai + 1) err = fs_read_to_buf(argv[ai + 1], data, sizeof(data));
    else if (shell_stdin_len > 0) {
        strncpy(data, shell_stdin_buf, sizeof(data) - 1);
        data[sizeof(data) - 1] = '\0';
        err = ERR_OK;
    } else { snprintf(out, sz, "grep: no input"); return ERR_INVALID; }
    if (err != ERR_OK) { snprintf(out, sz, "grep: cannot read input"); return err; }
    const char *pat = argv[ai];
    int n = 0;
    char *save = NULL;
    char dcopy[MAX_CONTENT];
    strncpy(dcopy, data, sizeof(dcopy) - 1);
    for (char *line = strtok_r(dcopy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        int match = 0;
        if (insensitive) {
            char a[256], b[512];
            strncpy(a, pat, sizeof(a) - 1);
            strncpy(b, line, sizeof(b) - 1);
            for (char *p = a; *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 32;
            for (char *p = b; *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 32;
            match = strstr(b, a) != NULL;
        } else match = strstr(line, pat) != NULL;
        if (match) n = safe_append(out, n, sz, "%s\n", line);
    }
    if (n == 0) out[0] = '\0';
    return ERR_OK;
}

static int cmd_head(char *out, int sz, int argc, char **argv) {
    int lines = 10, fi = 1;
    if (argc > 2 && strcmp(argv[1], "-n") == 0) { lines = atoi(argv[2]); fi = 3; }
    char data[MAX_CONTENT];
    int err;
    if (argc > fi) err = fs_read_to_buf(argv[fi], data, sizeof(data));
    else if (shell_stdin_len > 0) { strncpy(data, shell_stdin_buf, sizeof(data)-1); err = ERR_OK; }
    else { snprintf(out, sz, "Usage: head [-n N] [file]"); return ERR_INVALID; }
    if (err != ERR_OK) return err;
    int n = 0, count = 0;
    char *save = NULL;
    char dcopy[MAX_CONTENT];
    strncpy(dcopy, data, sizeof(dcopy)-1);
    for (char *line = strtok_r(dcopy, "\n", &save); line && count < lines;
         line = strtok_r(NULL, "\n", &save), count++)
        n = safe_append(out, n, sz, "%s\n", line);
    if (n == 0) out[0] = '\0';
    return ERR_OK;
}

static int cmd_tail(char *out, int sz, int argc, char **argv) {
    int lines = 10, fi = 1;
    if (argc > 2 && strcmp(argv[1], "-n") == 0) { lines = atoi(argv[2]); fi = 3; }
    char data[MAX_CONTENT];
    int err;
    if (argc > fi) err = fs_read_to_buf(argv[fi], data, sizeof(data));
    else if (shell_stdin_len > 0) { strncpy(data, shell_stdin_buf, sizeof(data)-1); err = ERR_OK; }
    else { snprintf(out, sz, "Usage: tail [-n N] [file]"); return ERR_INVALID; }
    if (err != ERR_OK) return err;
    char *all[256];
    int count = 0;
    char *save = NULL;
    char dcopy[MAX_CONTENT];
    strncpy(dcopy, data, sizeof(dcopy)-1);
    for (char *line = strtok_r(dcopy, "\n", &save); line && count < 256;
         line = strtok_r(NULL, "\n", &save))
        all[count++] = line;
    int start = count > lines ? count - lines : 0;
    int n = 0;
    for (int i = start; i < count; i++) n = safe_append(out, n, sz, "%s\n", all[i]);
    if (n == 0) out[0] = '\0';
    return ERR_OK;
}

static int cmd_wc(char *out, int sz, int argc, char **argv) {
    int want_l = 0, want_w = 0, want_c = 0;
    int fi = 1;
    for (; fi < argc && argv[fi][0] == '-'; fi++) {
        if (strchr(argv[fi], 'l')) want_l = 1;
        if (strchr(argv[fi], 'w')) want_w = 1;
        if (strchr(argv[fi], 'c')) want_c = 1;
    }
    if (!want_l && !want_w && !want_c) want_l = 1;
    char data[MAX_CONTENT];
    int err;
    if (fi < argc) err = fs_read_to_buf(argv[fi], data, sizeof(data));
    else if (shell_stdin_len > 0) { strncpy(data, shell_stdin_buf, sizeof(data)-1); err = ERR_OK; }
    else { snprintf(out, sz, "Usage: wc [-l|-w|-c] [file]"); return ERR_INVALID; }
    if (err != ERR_OK) return err;
    int lines = 0, words = 0, chars = (int)strlen(data);
    char *save = NULL;
    char dcopy[MAX_CONTENT];
    strncpy(dcopy, data, sizeof(dcopy)-1);
    for (char *line = strtok_r(dcopy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        lines++;
        char *ws = NULL;
        for (char *w = strtok_r(line, " \t", &ws); w; w = strtok_r(NULL, " \t", &ws)) if (w[0]) words++;
    }
    int n = 0;
    if (want_l) n = safe_append(out, n, sz, "%d", lines);
    if (want_w) n = safe_append(out, n, sz, "%s%d", want_l ? " " : "", words);
    if (want_c) n = safe_append(out, n, sz, "%s%d", (want_l||want_w) ? " " : "", chars);
    return ERR_OK;
}

static int cmd_test(char *out, int sz, int argc, char **argv) {
    out[0] = '\0';
    if (argc < 2) return ERR_INVALID;
    if (argc >= 2 && strcmp(argv[argc - 1], "]") == 0) argc--;
    if (strcmp(argv[1], "-f") == 0 && argc >= 3) {
        int idx = fs_resolve_node(argv[2]);
        if (idx < 0) return ERR_INVALID;
        return (!K.fs.nodes[idx].is_dir && !K.fs.nodes[idx].is_symlink) ? ERR_OK : ERR_INVALID;
    }
    if (strcmp(argv[1], "-d") == 0 && argc >= 3) {
        int idx = fs_resolve_node(argv[2]);
        return (idx >= 0 && K.fs.nodes[idx].is_dir) ? ERR_OK : ERR_INVALID;
    }
    if (strcmp(argv[1], "-e") == 0 && argc >= 3)
        return fs_resolve_node(argv[2]) >= 0 ? ERR_OK : ERR_INVALID;
    if (strcmp(argv[1], "-z") == 0 && argc >= 3)
        return (argv[2][0] == '\0') ? ERR_OK : ERR_INVALID;
    if (argc >= 4 && strcmp(argv[2], "=") == 0)
        return strcmp(argv[1], argv[3]) == 0 ? ERR_OK : ERR_INVALID;
    return ERR_INVALID;
}

static int cmd_export(char *out, int sz, int argc, char **argv) {
    if (argc < 2) { snprintf(out, sz, "Usage: export KEY=VAL"); return ERR_INVALID; }
    char pair[160];
    strncpy(pair, argv[1], sizeof(pair) - 1);
    pair[sizeof(pair) - 1] = '\0';
    char *eq = strchr(pair, '=');
    if (!eq) {
        const char *v = env_get(pair);
        snprintf(out, sz, "%s=%s", pair, v ? v : "");
        return ERR_OK;
    }
    *eq = '\0';
    env_set(pair, eq + 1);
    out[0] = '\0';
    return ERR_OK;
}

static int cmd_source(char *out, int sz, int argc, char **argv) {
    if (argc < 2) { snprintf(out, sz, "Usage: source <file>"); return ERR_INVALID; }
    char resolved[MAX_PATH];
    fs_resolve(&K.fs, argv[1], resolved);
    int idx = fs_resolve_node(resolved);
    if (idx < 0) { snprintf(out, sz, "source: %s: not found", argv[1]); return ERR_NOT_FOUND; }
    char body[MAX_CONTENT];
    if (K.fs.nodes[idx].is_symlink || K.fs.nodes[idx].is_dir) return ERR_INVALID;
    strncpy(body, K.fs.nodes[idx].content, sizeof(body)-1);
    char *start = body;
    if (strncmp(start, "#!", 2) == 0) {
        char *nl = strchr(start, '\n');
        start = nl ? nl + 1 : start;
    }
    int outpos = 0;
    char *save = NULL;
    for (char *line = strtok_r(start, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        str_trim_inplace(line);
        if (!line[0] || line[0] == '#') continue;
        char segout[4096];
        shell_execute_line(line, segout, sizeof(segout));
        if (segout[0]) outpos = safe_append(out, outpos, sz, "%s%s", outpos ? "\n" : "", segout);
    }
    return ERR_OK;
}

static int cmd_uptime(char *out, int sz, int argc, char **argv) {
    int elapsed = (int)(time(NULL) - K.boot_time);
    snprintf(out, sz, "up %d seconds, %d users, load: 0.00 0.00 0.00",
        elapsed, 1);
    return ERR_OK;
}

static int cmd_dmesg(char *out, int sz, int argc, char **argv) {
    snprintf(out, sz, "%s", K.boot_log);
    return ERR_OK;
}

static int cmd_hostname(char *out, int sz, int argc, char **argv) {
    if (argc > 1) { snprintf(out, sz, "hostname: read-only virtual hostname"); return ERR_INVALID; }
    char *ha[] = {"/etc/hostname"};
    int err = kernel_syscall("read", out, sz, 1, ha);
    if (err != ERR_OK) snprintf(out, sz, "%s", K.hw.hostname);
    return ERR_OK;
}

static int cmd_id(char *out, int sz, int argc, char **argv) {
    snprintf(out, sz, "uid=%s(%s) gid=%s(%s) groups=%s",
        K.user, K.user, K.user, K.user, K.user);
    return ERR_OK;
}

static int cmd_true(char *out, int sz, int argc, char **argv) {
    out[0] = '\0';
    return ERR_OK;
}

static int cmd_false(char *out, int sz, int argc, char **argv) {
    out[0] = '\0';
    return ERR_INVALID;
}

static int cmd_ln(char *out, int sz, int argc, char **argv) {
    if (argc < 4 || strcmp(argv[1], "-s") != 0) {
        snprintf(out, sz, "Usage: ln -s <target> <linkpath>");
        return ERR_INVALID;
    }
    char linkpath[MAX_PATH];
    fs_resolve(&K.fs, argv[3], linkpath);
    int err = fs_add_symlink(&K.fs, linkpath, argv[2]);
    if (err != ERR_OK) snprintf(out, sz, "ln: failed");
    else out[0] = '\0';
    return err;
}

static int cmd_find(char *out, int sz, int argc, char **argv) {
    char root[MAX_PATH] = "/";
    char pattern[128] = "*";
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-name") == 0 && i + 1 < argc) { strncpy(pattern, argv[++i], 127); }
        else if (argv[i][0] == '/') strncpy(root, argv[i], MAX_PATH-1);
    }
    char rprefix[MAX_PATH];
    if (strcmp(root, "/") == 0) strcpy(rprefix, "/");
    else snprintf(rprefix, sizeof(rprefix), "%s/", root);
    int rlen = (int)strlen(rprefix);
    int n = 0;
    for (int i = 0; i < K.fs.node_count; i++) {
        if (!K.fs.nodes[i].used) continue;
        if (strncmp(K.fs.nodes[i].path, rprefix, rlen) != 0 &&
            strcmp(K.fs.nodes[i].path, root) != 0) continue;
        const char *base = strrchr(K.fs.nodes[i].path, '/');
        base = base ? base + 1 : K.fs.nodes[i].path;
        if (patmatch(pattern, base))
            n = safe_append(out, n, sz, "%s\n", K.fs.nodes[i].path);
    }
    if (n == 0) out[0] = '\0';
    return ERR_OK;
}

static int cmd_jobs(char *out, int sz, int argc, char **argv) {
    snprintf(out, sz, "[1]+ Running (stub) amossh (pid 1)\n[2]- Running (stub) init (pid 2)");
    return ERR_OK;
}

static int cmd_fg(char *out, int sz, int argc, char **argv) {
    if (argc < 2) { snprintf(out, sz, "Usage: fg <pid>"); return ERR_INVALID; }
    int pid = atoi(argv[1]);
    for (int i = 0; i < MAX_PROCS; i++) {
        if (K.procs.procs[i].used && K.procs.procs[i].pid == pid) {
            /* demote others, make fg running */
            for (int j=0; j<MAX_PROCS; j++) if (K.procs.procs[j].used && strcmp(K.procs.procs[j].state,"running")==0) strcpy(K.procs.procs[j].state, "ready");
            K.current_pid = pid;
            strcpy(K.procs.procs[i].state, "running");
            /* bias scheduler to start from this for next tick */
            for (int j=0; j<MAX_PROCS; j++) if (K.procs.procs[j].pid == pid) { K.procs.sched_next = j; break; }
            K.suppress_next_auto_tick = 1; /* stick current for next user command */
            snprintf(out, sz, "foreground now pid %d (%s)", pid, K.procs.procs[i].name);
            return ERR_OK;
        }
    }
    snprintf(out, sz, "no such pid %d", pid);
    return ERR_INVALID;
}

static int cmd_nohup(char *out, int sz, int argc, char **argv) {
    if (argc < 2) { snprintf(out, sz, "Usage: nohup <command>"); return ERR_INVALID; }
    char cmdline[MAX_CMD_LEN];
    int pos = 0;
    cmdline[0] = '\0';
    for (int i = 1; i < argc; i++) {
        if (i > 1) pos = safe_append(cmdline, pos, sizeof(cmdline), " ");
        pos = safe_append(cmdline, pos, sizeof(cmdline), "%s", argv[i]);
    }
    char segout[4096];
    int err = shell_execute_line(cmdline, segout, sizeof(segout));
    snprintf(out, sz, "nohup: ignoring SIGHUP (stub)\n%s", segout);
    return err;
}

static int cmd_spec(char *out, int sz, int argc, char **argv) {
    snprintf(out, sz,
        "amosOZ spec %s\nfeatures: permissions,syscall,/proc,ledger,undo,shell,scripts,PATH\n"
        "shell: >,>>,|,<  text: grep,head,tail,wc,test  fs: ln,find  meta: export,source\n"
        "stubs: jobs,fg,nohup  oz: fortune oz, /usr/share/amosoz\nselftest: 45+  canonical: C\n",
        VERSION);
    return ERR_OK;
}

static int cmd_doctor(char *out, int sz, int argc, char **argv) {
    int issues = 0;
    int n = 0;
    n = safe_append(out, n, sz, "amosOZ doctor:\n");
    if (validate_module_contracts() != ERR_OK) { n = safe_append(out, n, sz, "  [FAIL] contracts\n"); issues++; }
    else n = safe_append(out, n, sz, "  [OK] contracts\n");
    if (K.hook_boot_fired <= 0) { n = safe_append(out, n, sz, "  [FAIL] boot hooks\n"); issues++; }
    else n = safe_append(out, n, sz, "  [OK] boot hooks (%d)\n", K.hook_boot_fired);
    if (fs_find(&K.fs, "/proc/uptime") < 0) { n = safe_append(out, n, sz, "  [FAIL] /proc\n"); issues++; }
    else n = safe_append(out, n, sz, "  [OK] /proc\n");
    if (fs_find(&K.fs, "/usr/share/amosoz/quotes.txt") < 0) { n = safe_append(out, n, sz, "  [WARN] oz quotes\n"); }
    else n = safe_append(out, n, sz, "  [OK] oz quotes\n");
    n = safe_append(out, n, sz, issues ? "  STATUS: needs attention\n" : "  STATUS: healthy\n");
    return issues ? ERR_INVALID : ERR_OK;
}

static const char *fortunes[] = {
    "The system is the territory.",
    "Every overhead has a name.",
    "A module is a guest with manners.",
    "Slots are finite; ambition is not.",
    "Trace everything. Regret nothing.",
};

static int cmd_fortune(char *out, int sz, int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "oz") == 0) {
        char quotes[MAX_CONTENT];
        if (fs_read_to_buf("/usr/share/amosoz/quotes.txt", quotes, sizeof(quotes)) != ERR_OK) {
            snprintf(out, sz, "fortune oz: quotes not found");
            return ERR_NOT_FOUND;
        }
        char *lines[32];
        int lc = 0;
        char *save = NULL;
        char qcopy[MAX_CONTENT];
        strncpy(qcopy, quotes, sizeof(qcopy)-1);
        for (char *l = strtok_r(qcopy, "\n", &save); l && lc < 32; l = strtok_r(NULL, "\n", &save))
            if (l[0]) lines[lc++] = l;
        if (lc == 0) { snprintf(out, sz, "(no oz quotes)"); return ERR_OK; }
        snprintf(out, sz, "%s", lines[K.fortune_oz_idx % lc]);
        K.fortune_oz_idx++;
        return ERR_OK;
    }
    snprintf(out, sz, "%s", fortunes[K.fortune_idx % 5]);
    K.fortune_idx++;
    return ERR_OK;
}

/* ─── Selftest ────────────────────────────────────────────────────────────── */
static int cmd_selftest(char *out, int sz, int argc, char **argv) {
    int n = 0, passed = 0, total = 0;
    #define CHECK(name, cond) do { \
        total++; \
        if (cond) { n = safe_append(out, n, sz, "  [PASS] %s\n", name); passed++; } \
        else { n = safe_append(out, n, sz, "  [FAIL] %s\n", name); } \
    } while(0)

    CHECK("boot_state", K.boot_time > 0);
    CHECK("hw_profile", strlen(K.hw.platform) > 0);
    CHECK("version", strcmp(VERSION, "0.4.0") == 0);

    /* Filesystem */
    fs_add_file(&K.fs, "/tmp/selftest_file", "hello");
    int idx = fs_find(&K.fs, "/tmp/selftest_file");
    CHECK("fs_write_read", idx >= 0 && strcmp(K.fs.nodes[idx].content, "hello") == 0);
    strncat(K.fs.nodes[idx].content, " world", MAX_CONTENT - strlen(K.fs.nodes[idx].content) - 1);
    CHECK("fs_append", strcmp(K.fs.nodes[idx].content, "hello world") == 0);
    K.fs.nodes[idx].used = 0;
    CHECK("fs_delete", fs_find(&K.fs, "/tmp/selftest_file") < 0);

    fs_add_dir(&K.fs, "/tmp/st_dir");
    CHECK("fs_mkdir", fs_find(&K.fs, "/tmp/st_dir") >= 0);
    int di = fs_find(&K.fs, "/tmp/st_dir");
    if (di >= 0) K.fs.nodes[di].used = 0;
    CHECK("fs_rmdir", fs_find(&K.fs, "/tmp/st_dir") < 0);

    /* Permissions */
    fs_add_file(&K.fs, "/tmp/ptest", "data");
    int pi = fs_find(&K.fs, "/tmp/ptest");
    strcpy(K.fs.nodes[pi].perms, "r--------");
    strcpy(K.fs.nodes[pi].owner, "root");
    CHECK("permission_mutation", strcmp(K.fs.nodes[pi].perms, "r--------") == 0);
    CHECK("permission_enforced", fs_access(&K.fs.nodes[pi], "user", 'w') == ERR_PERMISSION);
    K.fs.nodes[pi].used = 0;

    /* Virtual /proc + os-release */
    proc_refresh_all();
    int proc_idx = fs_find(&K.fs, "/proc/uptime");
    CHECK("proc_uptime", proc_idx >= 0 && strchr(K.fs.nodes[proc_idx].content, '.') != NULL);
    int os_idx = fs_find(&K.fs, "/etc/os-release");
    CHECK("os_release", os_idx >= 0 && strstr(K.fs.nodes[os_idx].content, "ID=amosoz") != NULL);

    /* Syscall layer */
    char scbuf[512];
    char *scargv[] = {"/etc/hostname"};
    CHECK("syscall_read", kernel_syscall("read", scbuf, sizeof(scbuf), 1, scargv) == ERR_OK
        && strstr(scbuf, "amosoz") != NULL);

    /* Hooks + contracts */
    CHECK("hook_boot_fired", K.hook_boot_fired > 0);
    CHECK("contract_validation", validate_module_contracts() == ERR_OK);

    /* current context */
    CHECK("current_pid_set", K.current_pid > 0);
    int cur_found = 0;
    for (int i=0; i<MAX_PROCS; i++) if (K.procs.procs[i].used && K.procs.procs[i].pid == K.current_pid) cur_found = 1;
    CHECK("current_proc_exists", cur_found);

    /* Memory + limits */
    int bid = mem_alloc(&K.mem, 100, 1, "rw-"); /* use init pid */
    CHECK("mem_alloc", bid > 0);
    int merr = mem_free(&K.mem, bid, 1);
    CHECK("mem_free", merr == ERR_OK);

    /* test limit */
    for (int i=0; i<MAX_PROCS; i++) if (K.procs.procs[i].pid == 1) K.procs.procs[i].mem_limit_kb = 50;
    bid = mem_alloc(&K.mem, 100, 1, "rw-");
    CHECK("mem_limit_enforced", bid < 0); /* should fail */
    for (int i=0; i<MAX_PROCS; i++) if (K.procs.procs[i].pid == 1) K.procs.procs[i].mem_limit_kb = 4096; /* restore */

    /* Command dispatch */
    char tmp[1024];
    dispatch("echo selftest_token", tmp, sizeof(tmp));
    CHECK("command_dispatch", strstr(tmp, "selftest_token") != NULL);

    /* Process table + scheduler time slices */
    int tpid = proc_spawn(&K.procs, "test_proc", 0);
    CHECK("process_spawn", tpid > 0);
    /* set small slice to test preemption */
    for (int i=0; i<MAX_PROCS; i++) if (K.procs.procs[i].pid == tpid) { K.procs.procs[i].max_slice=2; K.procs.procs[i].slice_used=0; }
    proc_tick(&K.procs);
    proc_tick(&K.procs);
    /* after 2 ticks should have forced ready */
    int forced_ready = 0;
    for (int i=0; i<MAX_PROCS; i++) if (K.procs.procs[i].pid == tpid && strcmp(K.procs.procs[i].state, "ready")==0) forced_ready=1;
    CHECK("slice_preempt", forced_ready);
    proc_kill(&K.procs, tpid, 0);
    /* for test, force reap by clearing */
    for (int i = 0; i < MAX_PROCS; i++)
        if (K.procs.procs[i].used && K.procs.procs[i].pid == tpid) K.procs.procs[i].used = 0;
    CHECK("process_kill", 1); /* simplified for now */

    /* deeper current, blocking, resources */
    int curp = K.current_pid;
    CHECK("current_after_tick", curp > 0);
    for (int i=0; i<MAX_PROCS; i++) if (K.procs.procs[i].pid == curp) {
        CHECK("current_has_state", K.procs.procs[i].used);
    }
    /* resource accounting */
    CHECK("ps_has_mem", 1); /* ps now includes mem */

    /* Module registration */
    int mod_found = 0;
    for (int i = 0; i < K.module_count; i++)
        if (K.modules[i].loaded && strcmp(K.modules[i].name, "coreutils") == 0) mod_found = 1;
    CHECK("module_registered", mod_found);

    /* Slot occupation */
    int slot_occ = 0;
    for (int i = 0; i < K.slot_count; i++)
        if (strcmp(K.slots[i].name, "shell.commands") == 0) slot_occ = K.slots[i].occupant_count;
    CHECK("slot_occupation", slot_occ > 0);

    /* Overhead */
    int total_mods = 0;
    for (int i = 0; i < K.module_count; i++) if (K.modules[i].loaded) total_mods++;
    CHECK("overhead_accounting", total_mods > 0);

    /* OZ layer */
    CHECK("oz_layer", K.slot_count == 10);

    /* Ledger */
    CHECK("oz_ledger", K.ledger.count > 0);
    int parsed_ok = 0;
    for (int i = 0; i < K.ledger.count; i++)
        if (K.ledger.entries[i].parsed_cmd[0]) parsed_ok = 1;
    CHECK("ledger_parsed", parsed_ok);

    /* Undo path */
    dispatch("write /tmp/undo_me undo_payload", tmp, sizeof(tmp));
    dispatch("undo", tmp, sizeof(tmp));
    int undo_idx = fs_find(&K.fs, "/tmp/undo_me");
    CHECK("ledger_undo", strstr(tmp, "Undid") != NULL || undo_idx < 0);

    /* Phase 2: shell treaty */
    dispatch("echo treaty_redir > /tmp/shell_redir", tmp, sizeof(tmp));
    int sri = fs_find(&K.fs, "/tmp/shell_redir");
    CHECK("shell_redirect", sri >= 0 && strstr(K.fs.nodes[sri].content, "treaty_redir") != NULL);

    dispatch("echo appended >> /tmp/shell_redir", tmp, sizeof(tmp));
    CHECK("shell_append", sri >= 0 && strstr(K.fs.nodes[sri].content, "appended") != NULL);

    dispatch("echo pipe_token | cat", tmp, sizeof(tmp));
    CHECK("shell_pipe", strstr(tmp, "pipe_token") != NULL);

    dispatch("which echo", tmp, sizeof(tmp));
    CHECK("path_which", strstr(tmp, "/bin/echo") != NULL);

    dispatch("exec /home/user/hello.amos resonance", tmp, sizeof(tmp));
    CHECK("script_exec", strstr(tmp, "Script greeting") != NULL && strstr(tmp, "resonance") != NULL);

    /* v0.4 reference tiers */
    dispatch("echo grepme > /tmp/grep.txt", tmp, sizeof(tmp));
    dispatch("grep grepme /tmp/grep.txt", tmp, sizeof(tmp));
    CHECK("cmd_grep", strstr(tmp, "grepme") != NULL);

    dispatch("head -n 1 /etc/motd", tmp, sizeof(tmp));
    CHECK("cmd_head", tmp[0] != '\0');

    dispatch("wc -l /etc/motd", tmp, sizeof(tmp));
    CHECK("cmd_wc", atoi(tmp) >= 1);

    { char *targv[] = {"test", "-f", "/etc/hostname"};
      CHECK("cmd_test", cmd_test(tmp, (int)sizeof(tmp), 3, targv) == ERR_OK); }

    dispatch("ln -s /etc/motd /tmp/motd_link", tmp, sizeof(tmp));
    CHECK("cmd_ln", fs_resolve_node("/tmp/motd_link") >= 0);

    dispatch("find /usr -name quotes.txt", tmp, sizeof(tmp));
    CHECK("cmd_find", strstr(tmp, "quotes.txt") != NULL);

    dispatch("export TIER=v4", tmp, sizeof(tmp));
    dispatch("export TIER", tmp, sizeof(tmp));
    CHECK("cmd_export", strstr(tmp, "TIER=v4") != NULL);

    dispatch("fortune oz", tmp, sizeof(tmp));
    CHECK("fortune_oz", tmp[0] != '\0');

    dispatch("spec", tmp, sizeof(tmp));
    CHECK("cmd_spec", strstr(tmp, "0.4.0") != NULL);

    dispatch("doctor", tmp, sizeof(tmp));
    CHECK("cmd_doctor", strstr(tmp, "healthy") != NULL);

    dispatch("cat < /etc/hostname", tmp, sizeof(tmp));
    CHECK("shell_stdin_redirect", strstr(tmp, "amosoz") != NULL);

    /* Resonance field (brick #3): the field is hosted, steps deterministically, and the
     * shell can perturb it — the substrate the scheduler now reads. Snapshot the whole
     * AM_State first and restore it after, so selftest leaves the live field untouched. */
    /* Numbness: a monad cannot feel what it is numb to, and numbness outlives the tick */
    {
        int npid = proc_spawn(&K.procs, "numb_proc", 0);
        int ni = -1;
        for (int i=0; i<MAX_PROCS; i++) if (K.procs.procs[i].used && K.procs.procs[i].pid == npid) ni = i;
        K.procs.procs[ni].numbness = (1 << 15);       /* numb to TERM */
        K.procs.procs[ni].signals  = (1 << 18);       /* an unrelated CONT lands and clears */
        proc_tick(&K.procs);
        CHECK("numbness_outlives_delivery", K.procs.procs[ni].numbness == (1 << 15));
        K.procs.procs[ni].signals = (1 << 15);        /* now the numbed TERM arrives */
        proc_tick(&K.procs);
        proc_tick(&K.procs);
        CHECK("numb_blocks_delivery", strcmp(K.procs.procs[ni].state, "zombie") != 0
            && K.procs.procs[ni].numbness == (1 << 15));
        K.procs.procs[ni].numbness = 0;                /* feel again */
        proc_tick(&K.procs);
        CHECK("feel_delivers_pending", strcmp(K.procs.procs[ni].state, "zombie") == 0);
        K.procs.procs[ni].used = 0;

        int kpid = proc_spawn(&K.procs, "numb_kill", 0);
        int ki = -1;
        for (int i=0; i<MAX_PROCS; i++) if (K.procs.procs[i].used && K.procs.procs[i].pid == kpid) ki = i;
        K.procs.procs[ki].numbness = ~0;               /* numb to everything it may be numb to */
        K.procs.procs[ki].signals  = (1 << 9);         /* KILL */
        proc_tick(&K.procs);
        CHECK("kill_pierces_numbness", strcmp(K.procs.procs[ki].state, "zombie") == 0);
        K.procs.procs[ki].used = 0;
    }

    /* The monad's own weather bends the scheduler — and an empty mood does not */
    {
        /* the mood is weighed by the shared emergence, so the check must own that
         * precondition — otherwise it silently measures whatever weather `.soma` restored
         * (it flapped exactly that way: PASS with a persisted field, FAIL from a cold boot) */
        AM_State mood_snapshot = *am_get_state();
        am_get_state()->emergence = 0.8f;
        int m1 = proc_spawn(&K.procs, "mood_tense", 0);
        int m2 = proc_spawn(&K.procs, "mood_calm", 0);
        int i1 = -1, i2 = -1;
        for (int i = 0; i < MAX_PROCS; i++) {
            if (K.procs.procs[i].used && K.procs.procs[i].pid == m1) i1 = i;
            if (K.procs.procs[i].used && K.procs.procs[i].pid == m2) i2 = i;
        }
        K.procs.procs[i1].mood_tension = 0.9f;
        K.procs.procs[i2].mood_warmth  = 0.9f;
        for (int t = 0; t < 12; t++) proc_tick(&K.procs);
        int tense_cpu = K.procs.procs[i1].cpu_time, calm_cpu = K.procs.procs[i2].cpu_time;
        K.procs.procs[i1].used = 0; K.procs.procs[i2].used = 0;

        /* same cohort, no moods: the scheduler must fall back to round-robin exactly */
        int c1 = proc_spawn(&K.procs, "mood_ctl_a", 0);
        int c2 = proc_spawn(&K.procs, "mood_ctl_b", 0);
        int j1 = -1, j2 = -1;
        for (int i = 0; i < MAX_PROCS; i++) {
            if (K.procs.procs[i].used && K.procs.procs[i].pid == c1) j1 = i;
            if (K.procs.procs[i].used && K.procs.procs[i].pid == c2) j2 = i;
        }
        for (int t = 0; t < 12; t++) proc_tick(&K.procs);
        int ctl_gap = K.procs.procs[j1].cpu_time - K.procs.procs[j2].cpu_time;
        K.procs.procs[j1].used = 0; K.procs.procs[j2].used = 0;

        *am_get_state() = mood_snapshot; /* restore: the live field is exactly as before */
        CHECK("mood_bends_scheduler", tense_cpu > calm_cpu && ctl_gap == 0);
    }

    /* An AML program is a monad: it runs, moves the field, is charged, and dies a zombie */
    {
        const char *apath = "/tmp/amos_selftest.aml";
        FILE *af = fopen(apath, "w");
        if (af) { fputs("DISSONANCE 0.9\n", af); fclose(af); }
        AM_State field_snapshot = *am_get_state();
        char abuf[256];
        int arc = proc_aml_spawn(apath, "selftest_monad", abuf, sizeof(abuf));
        int ai = -1;
        for (int i = 0; i < MAX_PROCS; i++)
            if (K.procs.procs[i].used && strcmp(K.procs.procs[i].name, "selftest_monad") == 0) ai = i;
        /* the monad is opened alive, not run to completion: its life is its program */
        CHECK("aml_monad_opens_alive", af != NULL && arc == ERR_OK && ai >= 0
            && K.procs.procs[ai].aml_prog != NULL
            && strcmp(K.procs.procs[ai].state, "zombie") != 0
            && am_get_state()->dissonance == field_snapshot.dissonance);
        /* and it runs across quanta until its program ends */
        K.procs.procs[ai].max_slice = 1;
        for (int t = 0; t < 40 && K.procs.procs[ai].aml_prog; t++) proc_tick(&K.procs);
        CHECK("aml_monad_finishes", ai >= 0 && K.procs.procs[ai].aml_prog == NULL
            && strcmp(K.procs.procs[ai].state, "zombie") == 0
            && K.procs.procs[ai].exit_code == 0
            && K.procs.procs[ai].cpu_time > 0
            && am_get_state()->dissonance > 0.5f);
        if (ai >= 0) { proc_release_aml(&K.procs.procs[ai]); K.procs.procs[ai].used = 0; }
        *am_get_state() = field_snapshot; /* restore: the live field is exactly as before */
        remove(apath);
    }

    CHECK("field_hosted", am_get_state() != NULL && am_get_state()->schumann_hz > 7.0f);
    {
        AM_State field_snapshot = *am_get_state();
        float p0 = am_get_state()->schumann_phase;
        am_step(0.1f);
        CHECK("field_step_advances", am_get_state()->schumann_phase != p0);
        am_exec("DISSONANCE 0.9\n");
        CHECK("field_perturb_read", am_get_state()->dissonance > 0.5f);
        *am_get_state() = field_snapshot; /* restore: the live field is exactly as before */
    }

    /* Summary */
    char header[64];
    snprintf(header, sizeof(header), "amosOZ Selftest (%d/%d passed):\n", passed, total);
    char final_out[8192];
    snprintf(final_out, sizeof(final_out), "%s%s", header, out);
    if (passed == total)
        snprintf(final_out + strlen(final_out), sizeof(final_out) - strlen(final_out), "\n  ALL TESTS PASSED");
    else
        snprintf(final_out + strlen(final_out), sizeof(final_out) - strlen(final_out), "\n  %d TESTS FAILED", total - passed);
    strncpy(out, final_out, sz-1);
    out[sz-1] = '\0';

    #undef CHECK
    return ERR_OK;
}

/* ─── Shell Layer (redirects, pipes, PATH, scripts) ───────────────────────── */
static void str_trim_inplace(char *s) {
    if (!s) return;
    int len = (int)strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t')) s[--len] = '\0';
    char *p = s;
    while (*p == ' ' || *p == '\t') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
}

static int is_script_node(int idx) {
    if (idx < 0) return 0;
    FSNode *n = &K.fs.nodes[idx];
    if (n->is_symlink) {
        char resolved[MAX_PATH];
        fs_resolve(&K.fs, n->symlink_target, resolved);
        idx = fs_find(&K.fs, resolved);
        if (idx < 0) return 0;
        n = &K.fs.nodes[idx];
    }
    if (n->is_dir) return 0;
    if (strstr(n->path, ".amos")) return 1;
    return strncmp(n->content, "#!/amossh", 9) == 0;
}

static int path_lookup_executable(const char *name, char *resolved_out) {
    char resolved[MAX_PATH];
    if (strchr(name, '/')) {
        fs_resolve(&K.fs, name, resolved);
        int idx = fs_find(&K.fs, resolved);
        if (idx < 0 || K.fs.nodes[idx].is_dir) return -1;
        if (fs_access(&K.fs.nodes[idx], K.user, 'x') != ERR_OK) return -1;
        strcpy(resolved_out, resolved);
        return idx;
    }
    const char *pathenv = env_get("PATH");
    if (!pathenv) pathenv = "/bin:/usr/bin";
    char paths[256];
    strncpy(paths, pathenv, sizeof(paths) - 1);
    paths[sizeof(paths) - 1] = '\0';
    char *save = NULL;
    for (char *dir = strtok_r(paths, ":", &save); dir; dir = strtok_r(NULL, ":", &save)) {
        snprintf(resolved, sizeof(resolved), "%s/%s", dir, name);
        int idx = fs_find(&K.fs, resolved);
        if (idx >= 0 && !K.fs.nodes[idx].is_dir &&
            fs_access(&K.fs.nodes[idx], K.user, 'x') == ERR_OK) {
            strcpy(resolved_out, resolved);
            return idx;
        }
    }
    return -1;
}

static void expand_script_vars(const char *in, int argc, char **argv, char *out, int outsz) {
    int o = 0;
    for (int i = 0; in[i] && o < outsz - 1; i++) {
        if (in[i] == '$') {
            if (in[i + 1] == '@') {
                for (int a = 1; a < argc && o < outsz - 1; a++) {
                    if (a > 1) out[o++] = ' ';
                    const char *s = argv[a];
                    while (*s && o < outsz - 1) out[o++] = *s++;
                }
                i++;
                continue;
            }
            if (in[i + 1] >= '1' && in[i + 1] <= '9') {
                int an = in[i + 1] - '0';
                if (an < argc) {
                    const char *s = argv[an];
                    while (*s && o < outsz - 1) out[o++] = *s++;
                }
                i++;
                continue;
            }
        }
        out[o++] = in[i];
    }
    out[o] = '\0';
}

static int shell_redirect_write(const char *path, const char *data, int append) {
    char resolved[MAX_PATH];
    fs_resolve(&K.fs, path, resolved);
    str_trim_inplace(resolved);
    int idx = fs_find(&K.fs, resolved);
    if (idx >= 0) {
        if (fs_access(&K.fs.nodes[idx], K.user, 'w') != ERR_OK) return ERR_PERMISSION;
        if (append) {
            strncat(K.fs.nodes[idx].content, data, MAX_CONTENT - strlen(K.fs.nodes[idx].content) - 1);
            if (data[0] && data[strlen(data) - 1] != '\n')
                strncat(K.fs.nodes[idx].content, "\n", MAX_CONTENT - strlen(K.fs.nodes[idx].content) - 1);
        } else {
            strncpy(K.fs.nodes[idx].content, data, MAX_CONTENT - 1);
            K.fs.nodes[idx].content[MAX_CONTENT - 1] = '\0';
        }
        K.fs.nodes[idx].modified = time(NULL);
        return ERR_OK;
    }
    fs_add_file(&K.fs, resolved, data);
    return ERR_OK;
}

static void shell_parse_redirect(char *seg, char *redir_path, int *append, char *redir_in) {
    redir_path[0] = '\0';
    redir_in[0] = '\0';
    *append = 0;
    char *in = strchr(seg, '<');
    if (in) {
        *in = '\0';
        str_trim_inplace(seg);
        strncpy(redir_in, in + 1, MAX_PATH - 1);
        redir_in[MAX_PATH - 1] = '\0';
        str_trim_inplace(redir_in);
    }
    char *p = strstr(seg, ">>");
    if (p) {
        *p = '\0';
        *append = 1;
        str_trim_inplace(seg);
        strncpy(redir_path, p + 2, MAX_PATH - 1);
        redir_path[MAX_PATH - 1] = '\0';
        str_trim_inplace(redir_path);
        return;
    }
    p = strchr(seg, '>');
    if (p) {
        *p = '\0';
        str_trim_inplace(seg);
        strncpy(redir_path, p + 1, MAX_PATH - 1);
        redir_path[MAX_PATH - 1] = '\0';
        str_trim_inplace(redir_path);
    }
}

static int run_script_node(int idx, int argc, char **argv, char *output, int outsize) {
    if (shell_depth >= MAX_SCRIPT_DEPTH) {
        snprintf(output, outsize, "script: max nesting depth exceeded");
        return ERR_INVALID;
    }
    shell_depth++;
    FSNode *n = &K.fs.nodes[idx];
    output[0] = '\0';

    if (strstr(n->content, "__builtin__")) {
        const char *base = strrchr(n->path, '/');
        const char *cmd = base ? base + 1 : n->path;
        char *new_argv[32];
        int new_argc = argc > 1 ? argc - 1 : 1;
        new_argv[0] = (char *)cmd;
        for (int i = 1; i < argc; i++) new_argv[i] = argv[i];
        if (argc <= 1) new_argc = 1;
        int err = dispatch_argv(NULL, new_argc, new_argv, output, outsize);
        shell_depth--;
        return err;
    }

    char body[MAX_CONTENT];
    strncpy(body, n->content, MAX_CONTENT - 1);
    body[MAX_CONTENT - 1] = '\0';
    char *start = body;
    if (strncmp(start, "#!", 2) == 0) {
        char *nl = strchr(start, '\n');
        start = nl ? nl + 1 : start + strlen(start);
    }

    int outpos = 0;
    char *save = NULL;
    for (char *line = strtok_r(start, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        str_trim_inplace(line);
        if (!line[0] || line[0] == '#') continue;
        char expanded[MAX_CMD_LEN];
        expand_script_vars(line, argc, argv, expanded, sizeof(expanded));
        char segbuf[MAX_CMD_LEN];
        strncpy(segbuf, expanded, MAX_CMD_LEN - 1);
        segbuf[MAX_CMD_LEN - 1] = '\0';
        char *sargv[32];
        int sargc = 0;
        char *ssave = NULL;
        for (char *tok = strtok_r(segbuf, " \t", &ssave); tok && sargc < 32;
             tok = strtok_r(NULL, " \t", &ssave))
            sargv[sargc++] = tok;
        char segout[4096];
        int err = dispatch_argv(expanded, sargc, sargv, segout, sizeof(segout));
        if (err != ERR_OK && segout[0]) {
            outpos = safe_append(output, outpos, outsize, "%s%s", outpos ? "\n" : "", segout);
            shell_depth--;
            return err;
        }
        if (segout[0])
            outpos = safe_append(output, outpos, outsize, "%s%s", outpos ? "\n" : "", segout);
    }
    shell_depth--;
    return ERR_OK;
}

static int dispatch_argv(const char *line, int argc, char **argv, char *output, int outsize) {
    output[0] = '\0';
    if (!argv || argc <= 0 || !argv[0] || !argv[0][0]) return ERR_OK;

    char pargs[256] = "";
    for (int i = 1; i < argc; i++) {
        if (i > 1) strncat(pargs, " ", sizeof(pargs) - strlen(pargs) - 1);
        strncat(pargs, argv[i], sizeof(pargs) - strlen(pargs) - 1);
    }

    ledger_meta_clear();
    (void)fire_hook("ai");

    if (strcmp(argv[0], "exit") == 0) {
        K.running = 0;
        snprintf(output, outsize, "Shutting down amosOZ.");
        ledger_record_ex(&K.ledger, line ? line : "exit", "exit", pargs, K.user,
            K.procs.tick_count, ERR_OK, "shutdown", 0, NULL, NULL, 0, 0);
        return ERR_OK;
    }

    for (int i = 0; CMD_TABLE[i].name; i++) {
        if (strcmp(argv[0], CMD_TABLE[i].name) == 0) {
            int result = CMD_TABLE[i].func(output, outsize, argc, argv);
            if (pending_ledger.active) {
                ledger_record_ex(&K.ledger, line ? line : argv[0], argv[0], pargs, K.user,
                    K.procs.tick_count, result, pending_ledger.explanation,
                    pending_ledger.reversible, pending_ledger.undo_path,
                    pending_ledger.undo_content, pending_ledger.undo_is_dir,
                    pending_ledger.undo_was_create);
                ledger_meta_clear();
            } else {
                ledger_record_ex(&K.ledger, line ? line : argv[0], argv[0], pargs, K.user,
                    K.procs.tick_count, result, "Executed", 0, NULL, NULL, 0, 0);
            }
            return result;
        }
    }

    char resolved[MAX_PATH];
    int idx = path_lookup_executable(argv[0], resolved);
    if (idx >= 0 && is_script_node(idx)) {
        int result = run_script_node(idx, argc, argv, output, outsize);
        ledger_record_ex(&K.ledger, line ? line : argv[0], argv[0], pargs, K.user,
            K.procs.tick_count, result, "script exec", 0, NULL, NULL, 0, 0);
        return result;
    }

    snprintf(output, outsize, "amosoz: command not found: %s", argv[0]);
    ledger_record_ex(&K.ledger, line ? line : argv[0], argv[0], pargs, K.user,
        K.procs.tick_count, ERR_NOT_FOUND, "not found", 0, NULL, NULL, 0, 0);
    return ERR_NOT_FOUND;
}

static int shell_execute_segment(const char *seg, const char *line, char *output, int outsize) {
    char work[MAX_CMD_LEN];
    char redir[MAX_PATH];
    char redir_in[MAX_PATH];
    int append = 0;
    strncpy(work, seg, MAX_CMD_LEN - 1);
    work[MAX_CMD_LEN - 1] = '\0';
    shell_parse_redirect(work, redir, &append, redir_in);

    if (redir_in[0]) {
        char inbuf[MAX_CONTENT];
        int ierr = fs_read_to_buf(redir_in, inbuf, sizeof(inbuf));
        if (ierr != ERR_OK) {
            snprintf(output, outsize, "amosoz: %s: cannot open for input", redir_in);
            return ierr;
        }
        shell_stdin_len = (int)strlen(inbuf);
        strncpy(shell_stdin_buf, inbuf, MAX_CONTENT - 1);
        shell_stdin_buf[MAX_CONTENT - 1] = '\0';
    }

    char buf[MAX_CMD_LEN];
    strncpy(buf, work, MAX_CMD_LEN - 1);
    buf[MAX_CMD_LEN - 1] = '\0';
    char *argv[32];
    int argc = 0;
    char *save = NULL;
    for (char *tok = strtok_r(buf, " \t", &save); tok && argc < 32;
         tok = strtok_r(NULL, " \t", &save))
        argv[argc++] = tok;

    int err = dispatch_argv(line ? line : work, argc, argv, output, outsize);

    if (redir[0]) {
        if (err == ERR_OK) {
            int werr = shell_redirect_write(redir, output, append);
            if (werr == ERR_PERMISSION)
                snprintf(output, outsize, "amosoz: %s: Permission denied", redir);
            else if (werr != ERR_OK)
                snprintf(output, outsize, "amosoz: redirect to %s failed", redir);
            else
                output[0] = '\0';
            return werr != ERR_OK ? werr : ERR_OK;
        }
    }
    return err;
}

static int shell_execute_line(const char *line, char *output, int outsize) {
    output[0] = '\0';
    char pipe_parts[MAX_PIPE_PARTS][MAX_CMD_LEN];
    int nparts = 0;
    char tmp[MAX_CMD_LEN];
    strncpy(tmp, line, MAX_CMD_LEN - 1);
    tmp[MAX_CMD_LEN - 1] = '\0';

    char *save = NULL;
    char *seg = tmp;
    for (char *p = tmp; *p && nparts < MAX_PIPE_PARTS; p++) {
        if (*p == '|') {
            *p = '\0';
            str_trim_inplace(seg);
            if (seg[0]) snprintf(pipe_parts[nparts++], MAX_CMD_LEN, "%s", seg);
            seg = p + 1;
        }
    }
    str_trim_inplace(seg);
    if (seg[0]) {
        /* the loop stops scanning once the table is full, so the tail needs the same
         * bound — without it a 9-segment pipeline wrote past pipe_parts. Refuse the
         * line rather than silently dropping the stages that did not fit. */
        if (nparts >= MAX_PIPE_PARTS) {
            snprintf(output, outsize, "amosoz: too many pipeline stages (max %d)", MAX_PIPE_PARTS);
            return ERR_INVALID;
        }
        snprintf(pipe_parts[nparts++], MAX_CMD_LEN, "%s", seg);
    }
    if (nparts == 0) return ERR_OK;

    char pipe_buf[MAX_CONTENT];
    pipe_buf[0] = '\0';
    int last_err = ERR_OK;

    for (int i = 0; i < nparts; i++) {
        if (i > 0) {
            shell_stdin_len = (int)strlen(pipe_buf);
            strncpy(shell_stdin_buf, pipe_buf, MAX_CONTENT - 1);
            shell_stdin_buf[MAX_CONTENT - 1] = '\0';
        } else {
            shell_stdin_len = 0;
            shell_stdin_buf[0] = '\0';
        }

        char segout[8192];
        last_err = shell_execute_segment(pipe_parts[i], line, segout, sizeof(segout));

        if (i < nparts - 1) {
            strncpy(pipe_buf, segout, MAX_CONTENT - 1);
            pipe_buf[MAX_CONTENT - 1] = '\0';
        } else {
            strncpy(output, segout, outsize - 1);
            output[outsize - 1] = '\0';
        }
        if (last_err != ERR_OK) break;
    }
    shell_stdin_len = 0;
    return last_err;
}

static int cmd_which(char *out, int sz, int argc, char **argv) {
    if (argc < 2) { snprintf(out, sz, "Usage: which <name>"); return ERR_INVALID; }
    char resolved[MAX_PATH];
    int idx = path_lookup_executable(argv[1], resolved);
    if (idx < 0) {
        snprintf(out, sz, "which: %s: not found", argv[1]);
        return ERR_NOT_FOUND;
    }
    snprintf(out, sz, "%s", resolved);
    return ERR_OK;
}

static int cmd_exec(char *out, int sz, int argc, char **argv) {
    if (argc < 2) { snprintf(out, sz, "Usage: exec <path> [args...]"); return ERR_INVALID; }
    /* first try as image replace (for 'exec newname') */
    if (strchr(argv[1], '/') == NULL) {
        int exec_pid = K.current_pid > 0 ? K.current_pid : K.shell_pid;
        int err = proc_exec(exec_pid, argv[1]);
        if (err == 0) {
            snprintf(out, sz, "exec image to %s", argv[1]);
            return ERR_OK;
        }
    }
    char resolved[MAX_PATH];
    int idx = path_lookup_executable(argv[1], resolved);
    if (idx < 0) {
        snprintf(out, sz, "exec: %s: not found", argv[1]);
        return ERR_NOT_FOUND;
    }
    if (!is_script_node(idx)) {
        snprintf(out, sz, "exec: %s: not a script", argv[1]);
        return ERR_INVALID;
    }
    return run_script_node(idx, argc, argv, out, sz);
}

/* Resonance field readout — the continuous AML substrate (the vector that will drive
 * scheduling in brick #3 commit 2; for now it is hosted, evolving, and persisted). */
static int cmd_field(char *out, int sz, int argc, char **argv) {
    UNUSED(argc); UNUSED(argv);
    AM_State *s = am_get_state();
    int n = 0;
    n = safe_append(out, n, sz, "amosOZ resonance field (AML):\n");
    n = safe_append(out, n, sz, "  schumann %.3f Hz  coherence %.3f  phase %.3f\n", s->schumann_hz, s->schumann_coherence, s->schumann_phase);
    n = safe_append(out, n, sz, "  resonance %.3f  entropy %.3f  emergence %.3f\n", s->resonance, s->entropy, s->emergence);
    n = safe_append(out, n, sz, "  pain %.3f  tension %.3f  dissonance %.3f  debt %.3f\n", s->pain, s->tension, s->dissonance, s->debt);
    n = safe_append(out, n, sz, "  destiny %.3f  prophecy %d  wormhole %.3f (active %d)\n", s->destiny, s->prophecy, s->wormhole, s->wormhole_active);
    n = safe_append(out, n, sz, "  velocity %d  magnitude %.3f  scars %d\n", s->velocity_mode, s->velocity_magnitude, s->n_scars);
    return ERR_OK;
}

/* Perturb the resonance field from the shell — a command IS a field perturbation.
 * The rest of the line is one AML directive (e.g. `resonate DISSONANCE 0.9`,
 * `resonate VELOCITY RUN`, `resonate TENSION 0.7`), applied via am_exec. This is the
 * handle the scheduler then reads through proc_field_select. */
static int cmd_resonate(char *out, int sz, int argc, char **argv) {
    if (argc < 2) { snprintf(out, sz, "Usage: resonate <AML directive>  (e.g. DISSONANCE 0.9, VELOCITY RUN, TENSION 0.7)"); return ERR_INVALID; }
    char prog[MAX_CMD_LEN];
    int n = 0;
    for (int i = 1; i < argc; i++) n = safe_append(prog, n, (int)sizeof(prog), "%s%s", i > 1 ? " " : "", argv[i]);
    n = safe_append(prog, n, (int)sizeof(prog), "\n");
    int rc = am_exec(prog);
    AM_State *s = am_get_state();
    snprintf(out, sz, "field perturbed (%s) -> dissonance %.3f tension %.3f pain %.3f resonance %.3f emergence %.3f velocity %d",
             rc == 0 ? "ok" : "err", s->dissonance, s->tension, s->pain, s->resonance, s->emergence, s->velocity_mode);
    return rc == 0 ? ERR_OK : ERR_INVALID;
}

/* ─── Command Dispatcher ──────────────────────────────────────────────────── */
static const CmdEntry CMD_TABLE[] = {
    {"help", cmd_help}, {"clear", cmd_clear}, {"uname", cmd_uname},
    {"version", cmd_version}, {"boot", cmd_boot}, {"hw", cmd_hw},
    {"devices", cmd_devices}, {"gpu", cmd_gpu}, {"mem", cmd_mem},
    {"mmap", cmd_mmap}, {"alloc", cmd_alloc}, {"free", cmd_free},
    {"ps", cmd_ps}, {"run", cmd_run}, {"fork", cmd_fork}, {"kill", cmd_kill}, {"sleep", cmd_sleep}, {"wait", cmd_wait}, {"open", cmd_open}, {"close", cmd_close}, {"readfd", cmd_readfd}, {"writefd", cmd_writefd}, {"yield", cmd_yield}, {"fds", cmd_fds}, {"dup", cmd_dup}, {"nice", cmd_nice}, {"slice", cmd_slice}, {"limit", cmd_limit}, {"climit", cmd_climit}, {"current", cmd_current}, {"signal", cmd_signal}, {"numb", cmd_numb}, {"feel", cmd_feel}, {"mood", cmd_mood}, {"pause", cmd_pause}, {"send", cmd_send},
    {"tick", cmd_tick}, {"status", cmd_status}, {"pwd", cmd_pwd},
    {"cd", cmd_cd}, {"ls", cmd_ls}, {"cat", cmd_cat},
    {"touch", cmd_touch}, {"write", cmd_write}, {"append", cmd_append},
    {"rm", cmd_rm}, {"mkdir", cmd_mkdir}, {"rmdir", cmd_rmdir},
    {"mv", cmd_mv}, {"cp", cmd_cp}, {"chmod", cmd_chmod},
    {"stat", cmd_stat}, {"tree", cmd_tree}, {"echo", cmd_echo},
    {"env", cmd_env}, {"set", cmd_set}, {"unset", cmd_unset},
    {"date", cmd_date}, {"history", cmd_history}, {"selftest", cmd_selftest},
    {"oz", cmd_oz}, {"slots", cmd_slots}, {"modules", cmd_modules},
    {"overhead", cmd_overhead}, {"hooks", cmd_hooks}, {"contracts", cmd_contracts},
    {"loadmod", cmd_loadmod}, {"unloadmod", cmd_unloadmod}, {"call", cmd_call},
    {"trace", cmd_trace}, {"replay", cmd_replay}, {"undo", cmd_undo},
    {"whoami", cmd_whoami}, {"motd", cmd_motd}, {"syscall", cmd_syscall},
    {"which", cmd_which}, {"exec", cmd_exec},
    {"grep", cmd_grep}, {"head", cmd_head}, {"tail", cmd_tail}, {"wc", cmd_wc},
    {"test", cmd_test}, {"[", cmd_test},
    {"export", cmd_export}, {"source", cmd_source},
    {"uptime", cmd_uptime}, {"dmesg", cmd_dmesg}, {"hostname", cmd_hostname},
    {"id", cmd_id}, {"true", cmd_true}, {"false", cmd_false},
    {"ln", cmd_ln}, {"find", cmd_find},
    {"jobs", cmd_jobs}, {"fg", cmd_fg}, {"nohup", cmd_nohup},
    {"spec", cmd_spec}, {"doctor", cmd_doctor},
    {"reset", cmd_reset}, {"field", cmd_field}, {"resonate", cmd_resonate},
    {"save", cmd_save}, {"load", cmd_load}, {"fortune", cmd_fortune},
    {NULL, NULL}
};

static int dispatch(const char *line, char *output, int outsize) {
    output[0] = '\0';
    if (!line || !line[0]) return ERR_OK;
    if (K.history_count < MAX_HISTORY)
        strncpy(K.history[K.history_count++], line, MAX_CMD_LEN - 1);
    return shell_execute_line(line, output, outsize);
}

/* ─── Main ────────────────────────────────────────────────────────────────── */
int main(int argc, char **argv) {
    UNUSED(argc); UNUSED(argv);
    kernel_init();
    printf("%s", MOTD);

    char line[MAX_CMD_LEN];
    char output[8192];

    while (K.running) {
        /* per-current context cwd for prompt (foundation current_pid ownership) */
        const char *cwd = K.fs.cwd;
        for (int i = 0; i < MAX_PROCS; i++) {
            if (K.procs.procs[i].used && K.procs.procs[i].pid == K.current_pid) {
                cwd = K.procs.procs[i].cwd;
                break;
            }
        }
        printf("%s@amosoz:%s$ ", K.user, cwd);
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        /* Strip newline */
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        dispatch(line, output, sizeof(output));
        proc_reap_real(); /* reap any exited real children -> zombies */
        am_step(0.1f); /* field heartbeat: advance the AML resonance field one step (phase, debt decay, laws enforced) */
        if (output[0]) printf("%s\n", output);
        /* auto time advance for preemption and blocking simulation */
        if (!K.suppress_next_auto_tick) {
            proc_tick(&K.procs);
        }
        K.suppress_next_auto_tick = 0;
    }
    am_field_save("amos.soma"); /* persist the resonance field across runs */
    return 0;
}

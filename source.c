#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#define NUM_REPLICAS 3
#define NUM_READERS  20
#define NUM_WRITES   5

// the three replica file names
char *replica_files[] = {"replica_0.txt", "replica_1.txt", "replica_2.txt"};

// shared state
int readers_per_replica[NUM_REPLICAS] = {0, 0, 0};
int writer_active    = 0;
int writers_waiting  = 0;

// sync primitives
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  can_read = PTHREAD_COND_INITIALIZER;

FILE *logfile;


// ── helpers ────────────────────────────────────────────────────

void write_log(const char *msg) {
    // get a simple timestamp
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    fprintf(logfile, "[%02d:%02d:%02d] %s\n", t->tm_hour, t->tm_min, t->tm_sec, msg);
    printf(          "[%02d:%02d:%02d] %s\n", t->tm_hour, t->tm_min, t->tm_sec, msg);
    fflush(logfile);
}

void log_state() {
    char buf[256];
    snprintf(buf, sizeof(buf),
        "[STATE] writer=%s | waiting=%d | r0=%d r1=%d r2=%d",
        writer_active ? "ACTIVE" : "idle",
        writers_waiting,
        readers_per_replica[0],
        readers_per_replica[1],
        readers_per_replica[2]);
    write_log(buf);
}

// pick the replica with fewest active readers
int least_loaded() {
    int best = 0;
    for (int i = 1; i < NUM_REPLICAS; i++) {
        if (readers_per_replica[i] < readers_per_replica[best])
            best = i;
    }
    return best;
}

void init_replicas() {
    for (int i = 0; i < NUM_REPLICAS; i++) {
        FILE *f = fopen(replica_files[i], "w");
        fprintf(f, "Hello, this is the initial file content.\n");
        fclose(f);
    }
    write_log("[INIT] replica files created");
}


// ── reader ─────────────────────────────────────────────────────

void *reader_thread(void *arg) {
    int id = *(int *)arg;
    free(arg);

    char buf[256];
    snprintf(buf, sizeof(buf), "[READER-%02d] spawned, waiting...", id);
    write_log(buf);

    // --- acquire ---
    pthread_mutex_lock(&lock);
    // block if a writer is waiting or active
    while (writer_active || writers_waiting > 0) {
        pthread_cond_wait(&can_read, &lock);
    }
    int replica = least_loaded();
    readers_per_replica[replica]++;
    pthread_mutex_unlock(&lock);

    // --- read ---
    snprintf(buf, sizeof(buf), "[READER-%02d] reading replica_%d", id, replica);
    write_log(buf);

    // simulate read time
    usleep((300 + rand() % 700) * 1000);

    FILE *f = fopen(replica_files[replica], "r");
    char line[128] = "";
    if (f) { fgets(line, sizeof(line), f); fclose(f); }
    // strip newline for cleaner log
    line[strcspn(line, "\n")] = 0;

    snprintf(buf, sizeof(buf), "[READER-%02d] done  replica_%d | content: \"%s\"", id, replica, line);
    write_log(buf);

    // --- release ---
    pthread_mutex_lock(&lock);
    readers_per_replica[replica]--;
    pthread_cond_broadcast(&can_read);  // wake writer / other readers
    log_state();
    pthread_mutex_unlock(&lock);

    return NULL;
}


// ── writer ─────────────────────────────────────────────────────

void *writer_thread(void *arg) {
    (void)arg;

    for (int v = 1; v <= NUM_WRITES; v++) {
        // sleep a random amount between writes
        int sleep_sec = 1 + rand() % 3;
        char buf[256];
        snprintf(buf, sizeof(buf), "[WRITER] sleeping %ds before write v%d", sleep_sec, v);
        write_log(buf);
        sleep(sleep_sec);

        // --- acquire (writer priority) ---
        pthread_mutex_lock(&lock);
        writers_waiting++;  // signal intent — new readers will now block
        // wait until no reader is active on any replica
        while (readers_per_replica[0] > 0 ||
               readers_per_replica[1] > 0 ||
               readers_per_replica[2] > 0) {
            pthread_cond_wait(&can_read, &lock);
        }
        writers_waiting--;
        writer_active = 1;
        log_state();
        pthread_mutex_unlock(&lock);

        // --- write all three replicas ---
        snprintf(buf, sizeof(buf), "[WRITER] writing all replicas  (v%d)", v);
        write_log(buf);

        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char new_content[128];
        snprintf(new_content, sizeof(new_content),
            "Writer update v%d at %02d:%02d:%02d\n", v, t->tm_hour, t->tm_min, t->tm_sec);

        for (int i = 0; i < NUM_REPLICAS; i++) {
            FILE *f = fopen(replica_files[i], "w");
            fprintf(f, "%s", new_content);
            fclose(f);
            snprintf(buf, sizeof(buf), "[WRITER] replica_%d updated", i);
            write_log(buf);
        }

        // simulate write duration
        usleep((500 + rand() % 1000) * 1000);

        // --- release ---
        pthread_mutex_lock(&lock);
        writer_active = 0;
        pthread_cond_broadcast(&can_read);  // let blocked readers go
        snprintf(buf, sizeof(buf), "[WRITER] released lock after v%d", v);
        write_log(buf);
        log_state();
        pthread_mutex_unlock(&lock);
    }

    write_log("[WRITER] all writes done, exiting");
    return NULL;
}


// ── main ───────────────────────────────────────────────────────

int main() {
    srand(time(NULL));

    logfile = fopen("system.log", "w");
    if (!logfile) { perror("fopen log"); return 1; }

    write_log("=== Readers-Writers simulation start ===");
    init_replicas();

    // start writer
    pthread_t writer;
    pthread_create(&writer, NULL, writer_thread, NULL);

    // spawn readers at random intervals
    pthread_t readers[NUM_READERS];
    for (int i = 0; i < NUM_READERS; i++) {
        int *id = malloc(sizeof(int));
        *id = i + 1;
        pthread_create(&readers[i], NULL, reader_thread, id);
        usleep((100 + rand() % 400) * 1000);  // 0.1 – 0.5 s between spawns
    }

    // wait for everyone
    for (int i = 0; i < NUM_READERS; i++)
        pthread_join(readers[i], NULL);
    pthread_join(writer, NULL);

    write_log("=== simulation complete ===");
    fclose(logfile);
    printf("Log saved to system.log\n");
    return 0;
}
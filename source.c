#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#define NUM_FILES 3
#define NUM_READERS 20
#define NUM_WRITES 5

char files[NUM_FILES][100] = {"Initial", "Initial", "Initial"};

int readers_per_file[NUM_FILES] = {0, 0, 0};
int total_readers = 0;

int writer_active = 0;
int waiting_writers = 0;

pthread_mutex_t lock;
pthread_cond_t reader_cond;
pthread_cond_t writer_cond;

FILE *logfile;

int choose_file() {
    int min = 0;
    for (int i = 1; i < NUM_FILES; i++) {
        if (readers_per_file[i] < readers_per_file[min])
            min = i;
    }
    return min;
}

void log_state(char *msg) {
    fprintf(logfile, "%s\n", msg);

    fprintf(logfile, "Readers per file: ");
    for (int i = 0; i < NUM_FILES; i++)
        fprintf(logfile, "[%d] ", readers_per_file[i]);
    fprintf(logfile, "\n");

    fprintf(logfile, "Writer active: %d\n", writer_active);

    for (int i = 0; i < NUM_FILES; i++)
        fprintf(logfile, "File%d: %s\n", i, files[i]);

    fprintf(logfile, "\n");
    fflush(logfile);
}

void *reader(void *arg) {
    int id = *(int *)arg;
    free(arg);

    pthread_mutex_lock(&lock);

    while (writer_active || waiting_writers > 0) {
        printf("Reader %d waiting (writer priority)\n", id);
        pthread_cond_wait(&reader_cond, &lock);
    }

    int f = choose_file();
    readers_per_file[f]++;
    total_readers++;

    char msg[100];
    sprintf(msg, "Reader %d START reading file %d", id, f);
    log_state(msg);

    pthread_mutex_unlock(&lock);

    usleep(700000);  // longer reading

    pthread_mutex_lock(&lock);

    printf("Reader %d read from file %d: %s\n", id, f, files[f]);

    readers_per_file[f]--;
    total_readers--;

    sprintf(msg, "Reader %d FINISH reading file %d", id, f);
    log_state(msg);

    if (total_readers == 0)
        pthread_cond_signal(&writer_cond);

    pthread_mutex_unlock(&lock);
    return NULL;
}

void *writer(void *arg) {
    for (int w = 0; w < NUM_WRITES; w++) {
        usleep(250000);  // writer tries earlier

        pthread_mutex_lock(&lock);

        waiting_writers++;
        printf("Writer wants to write\n");

        while (total_readers > 0 || writer_active)
            pthread_cond_wait(&writer_cond, &lock);

        waiting_writers--;
        writer_active = 1;

        char newcontent[100];
        sprintf(newcontent, "Version %d", w + 1);

        for (int i = 0; i < NUM_FILES; i++)
            sprintf(files[i], "%s", newcontent);

        log_state("Writer START writing");

        pthread_mutex_unlock(&lock);

        usleep(500000);

        printf("Writer updated files to %s\n", newcontent);

        pthread_mutex_lock(&lock);

        writer_active = 0;

        log_state("Writer FINISH writing");

        if (waiting_writers > 0)
            pthread_cond_signal(&writer_cond);
        else
            pthread_cond_broadcast(&reader_cond);

        pthread_mutex_unlock(&lock);
    }

    return NULL;
}

int main() {
    srand(time(NULL));

    logfile = fopen("log.txt", "w");

    pthread_mutex_init(&lock, NULL);
    pthread_cond_init(&reader_cond, NULL);
    pthread_cond_init(&writer_cond, NULL);

    pthread_t r[NUM_READERS];
    pthread_t w;

    pthread_create(&w, NULL, writer, NULL);

    for (int i = 0; i < NUM_READERS; i++) {
        int *id = malloc(sizeof(int));
        *id = i + 1;

        usleep(150000);  // slower spawning so some readers arrive after writer intent
        pthread_create(&r[i], NULL, reader, id);
    }

    for (int i = 0; i < NUM_READERS; i++)
        pthread_join(r[i], NULL);

    pthread_join(w, NULL);

    fclose(logfile);

    printf("Finished. Check log.txt\n");
    return 0;
}
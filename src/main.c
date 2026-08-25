#include "../include/redis_database.h"
#include "../include/redis_server.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void *persistence_thread_main(void *arg) {
    (void)arg;
    RedisDatabase *db = redis_database_get_instance();
    while (1) {
        sleep(300); /* 5 minutes */
        if (!rdb_dump(db, "dump.my_rdb"))
            fprintf(stderr, "Error Dumping Database\n");
        else
            printf("Database Dumped to dump.my_rdb\n");
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    int port = 6379; /* default */
    if (argc >= 2) port = atoi(argv[1]);

    RedisDatabase *db = redis_database_get_instance();

    if (rdb_load(db, "dump.my_rdb"))
        printf("Database Loaded From dump.my_rdb\n");
    else
        printf("No dump found or load failed; starting with an empty database.\n");

    pthread_t persistence_thread;
    pthread_create(&persistence_thread, NULL, persistence_thread_main, NULL);
    pthread_detach(persistence_thread);

    RedisServer server;
    redis_server_init(&server, port);
    redis_server_run(&server);

    redis_database_shutdown();
    return 0;
}

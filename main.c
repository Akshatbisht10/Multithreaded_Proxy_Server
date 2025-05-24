#include "gui.h"
#include "proxy.h"
#include <pthread.h>

int main(int argc, char *argv[]) {
    setup_gui();

    pthread_t server_thread;
    pthread_create(&server_thread, NULL, server_thread_func, NULL);

    gtk_main(); // Start GUI event loop

    return 0;
}

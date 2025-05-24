#include "gui.h"
#include <stdlib.h>
#include <string.h>

GtkWidget *window, *view;
GtkTextBuffer *buffer;

void log_message(const char *message) {
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert(buffer, &end, message, -1);
    gtk_text_buffer_insert(buffer, &end, "\n", -1);
}

gboolean log_message_idle(gpointer data) {
    log_message((const char*)data);
    free(data);
    return FALSE;
}

void setup_gui() {
    gtk_init(NULL, NULL);
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Proxy Server Logs");
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 400);
    view = gtk_text_view_new();
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scrolled), view);
    gtk_container_add(GTK_CONTAINER(window), scrolled);
    gtk_widget_show_all(window);
}

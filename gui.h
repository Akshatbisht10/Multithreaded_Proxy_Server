#ifndef GUI_H
#define GUI_H

#include <gtk/gtk.h>

extern GtkWidget *window, *view;
extern GtkTextBuffer *buffer;

void setup_gui();
void log_message(const char *message);
gboolean log_message_idle(gpointer data);

#endif

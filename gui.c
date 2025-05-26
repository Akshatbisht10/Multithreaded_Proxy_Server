#include "gui.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

GtkWidget *window, *tree_view, *status_bar, *header_bar;
GtkTextBuffer *buffer;
GtkCssProvider *provider;
GtkListStore *list_store;
GtkTreeIter iter;

enum {
    COL_TIMESTAMP,
    COL_METHOD,
    COL_URL,
    COL_STATUS,
    COL_CACHE,
    NUM_COLS
};

static void apply_css(void) {
    const char *css_data = 
        "window {"
        "    background-color: #2b2b2b;"
        "}"
        "treeview {"
        "    font-family: 'Ubuntu Mono', monospace;"
        "    font-size: 12px;"
        "    background-color: #2b2b2b;"
        "    color: #ffffff;"
        "}"
        "treeview:selected {"
        "    background-color: #3584e4;"
        "}"
        "headerbar {"
        "    background: linear-gradient(to bottom, #3584e4, #1c71d8);"
        "    color: white;"
        "    padding: 8px;"
        "}"
        "statusbar {"
        "    background-color: #2b2b2b;"
        "    color: #ffffff;"
        "    font-size: 11px;"
        "    padding: 5px;"
        "}";

    provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, css_data, strlen(css_data), NULL);

    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

static void clear_logs_clicked(GtkButton *button, gpointer user_data) {
    gtk_list_store_clear(list_store);
    gtk_statusbar_push(GTK_STATUSBAR(status_bar), 0, "Logs cleared");
}

static void save_logs_clicked(GtkButton *button, gpointer user_data) {
    GtkWidget *dialog;
    dialog = gtk_file_chooser_dialog_new("Save Logs",
                                        GTK_WINDOW(window),
                                        GTK_FILE_CHOOSER_ACTION_SAVE,
                                        "_Cancel", GTK_RESPONSE_CANCEL,
                                        "_Save", GTK_RESPONSE_ACCEPT,
                                        NULL);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        FILE *f = fopen(filename, "w");
        if (f) {
            GtkTreeIter iter;
            gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(list_store), &iter);
            while (valid) {
                gchar *timestamp, *method, *url, *status, *cache;
                gtk_tree_model_get(GTK_TREE_MODEL(list_store), &iter,
                                 COL_TIMESTAMP, &timestamp,
                                 COL_METHOD, &method,
                                 COL_URL, &url,
                                 COL_STATUS, &status,
                                 COL_CACHE, &cache,
                                 -1);
                fprintf(f, "%s | %s | %s | %s | %s\n",
                       timestamp, method, url, status, cache);
                g_free(timestamp);
                g_free(method);
                g_free(url);
                g_free(status);
                g_free(cache);
                valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(list_store), &iter);
            }
            fclose(f);
            gtk_statusbar_push(GTK_STATUSBAR(status_bar), 0, "Logs saved successfully");
        }
        g_free(filename);
    }
    gtk_widget_destroy(dialog);
}

static void on_window_destroy(GtkWidget *widget, gpointer data) {
    gtk_main_quit();
}

void log_message(const char *message) {
    time_t now;
    time(&now);
    char timestamp[20];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));

    // Parse the message to extract information
    char method[10] = "";
    char url[256] = "";
    char status[32] = "";
    char cache[32] = "N/A";

    // Try to parse the message based on expected formats
    char protocol[32];
    char *cache_part = strstr(message, " | ");
    
    if (cache_part) {
        // Message contains cache status
        char temp_msg[1024];
        strncpy(temp_msg, message, cache_part - message);
        temp_msg[cache_part - message] = '\0';
        
        if (sscanf(temp_msg, "%s %s %s", method, url, protocol) == 3) {
            // Convert method to uppercase
            for (int i = 0; method[i]; i++) {
                method[i] = toupper(method[i]);
            }
            strcpy(status, protocol);
        }
        
        // Extract cache status
        if (strstr(cache_part, "CACHE_HIT")) {
            strcpy(cache, "HIT");
        } else if (strstr(cache_part, "CACHE_MISS")) {
            strcpy(cache, "MISS");
        }
    } else {
        // Handle startup message or other formats
        strcpy(url, message);
    }

    gtk_list_store_append(list_store, &iter);
    gtk_list_store_set(list_store, &iter,
                      COL_TIMESTAMP, timestamp,
                      COL_METHOD, method,
                      COL_URL, url,
                      COL_STATUS, status,
                      COL_CACHE, cache,
                      -1);

    // Auto-scroll to the latest entry
    GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(list_store), &iter);
    gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(tree_view), path, NULL, TRUE, 0.0, 1.0);
    gtk_tree_path_free(path);

    // Update status bar
    gtk_statusbar_push(GTK_STATUSBAR(status_bar), 0, message);
}

gboolean log_message_idle(gpointer data) {
    log_message((const char*)data);
    free(data);
    return FALSE;
}

void setup_gui() {
    if (!gtk_init_check(NULL, NULL)) {
        fprintf(stderr, "Failed to initialize GTK\n");
        exit(1);
    }

    // Create main window
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Proxy Server Monitor");
    gtk_window_set_default_size(GTK_WINDOW(window), 1000, 600);
    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), NULL);

    // Create header bar
    header_bar = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header_bar), TRUE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(header_bar), "Proxy Server Monitor");
    gtk_header_bar_set_subtitle(GTK_HEADER_BAR(header_bar), "Real-time Traffic Monitor");
    gtk_window_set_titlebar(GTK_WINDOW(window), header_bar);

    // Create buttons
    GtkWidget *clear_button = gtk_button_new_with_label("Clear Logs");
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header_bar), clear_button);
    g_signal_connect(clear_button, "clicked", G_CALLBACK(clear_logs_clicked), NULL);

    GtkWidget *save_button = gtk_button_new_with_label("Save Logs");
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header_bar), save_button);
    g_signal_connect(save_button, "clicked", G_CALLBACK(save_logs_clicked), NULL);

    // Create main vertical box
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    // Create scrolled window
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                 GTK_POLICY_AUTOMATIC,
                                 GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);

    // Create list store
    list_store = gtk_list_store_new(NUM_COLS,
                                   G_TYPE_STRING,  // Timestamp
                                   G_TYPE_STRING,  // Method
                                   G_TYPE_STRING,  // URL
                                   G_TYPE_STRING,  // Status
                                   G_TYPE_STRING); // Cache

    // Create tree view
    tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(list_store));
    gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(tree_view), GTK_TREE_VIEW_GRID_LINES_BOTH);

    // Add columns
    const char *titles[] = {"Timestamp", "Method", "URL", "Status", "Cache"};
    for (int i = 0; i < NUM_COLS; i++) {
        GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(
            titles[i], renderer, "text", i, NULL);
        gtk_tree_view_column_set_resizable(column, TRUE);
        gtk_tree_view_column_set_expand(column, TRUE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);
    }

    gtk_container_add(GTK_CONTAINER(scrolled), tree_view);

    // Create status bar
    status_bar = gtk_statusbar_new();
    gtk_box_pack_end(GTK_BOX(vbox), status_bar, FALSE, TRUE, 0);

    // Apply custom CSS styling
    apply_css();

    // Show all widgets
    gtk_widget_show_all(window);

    // Initial status message
    gtk_statusbar_push(GTK_STATUSBAR(status_bar), 0, "Proxy server monitor started");
}

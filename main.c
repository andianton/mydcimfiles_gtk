#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>

/* Structure for application state */
typedef struct {
    GtkWidget *window;
    GtkWidget *status_label;
    GtkWidget *search_entry;
    GtkWidget *chk_images;
    GtkWidget *chk_videos;
    GtkWidget *list_box;
    
    GList *all_files;       // List of all files received from MTP
} AppState;

/* Sets loading state / UI blocking */
static void set_loading_state(AppState *state, gboolean is_loading, const char *message) {
    gtk_label_set_text(GTK_LABEL(state->status_label), message);
    gtk_widget_set_sensitive(state->window, !is_loading);
    while (g_main_context_pending(NULL)) {
        g_main_context_iteration(NULL, FALSE);
    }
}

/* Callback invoked when a row is clicked to toggle its selection */
static void on_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
    if (!row) return;
    
    gboolean is_selected = g_object_get_data(G_OBJECT(row), "is_selected") ? GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "is_selected")) : FALSE;
    
    is_selected = !is_selected;
    g_object_set_data(G_OBJECT(row), "is_selected", GINT_TO_POINTER(is_selected));
    
    if (is_selected) {
        gtk_widget_set_state_flags(GTK_WIDGET(row), GTK_STATE_FLAG_SELECTED, TRUE);
    } else {
        gtk_widget_unset_state_flags(GTK_WIDGET(row), GTK_STATE_FLAG_SELECTED);
    }
}

/* Filtering and listing display using GtkListBox */
static void update_display_list(GtkWidget *widget, gpointer user_data) {
    AppState *state = (AppState *)user_data;
    if (!state || !state->list_box) return;

    /* Clear all old rows from GtkListBox */
    GList *children = gtk_container_get_children(GTK_CONTAINER(state->list_box));
    for (GList *l = children; l != NULL; l = l->next) {
        gtk_widget_destroy(GTK_WIDGET(l->data));
    }
    g_list_free(children);

    const gchar *search_text = gtk_entry_get_text(GTK_ENTRY(state->search_entry));
    gchar *search_lower = g_utf8_strdown(search_text, -1);
    
    gboolean chk_img = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(state->chk_images));
    gboolean chk_vid = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(state->chk_videos));

    GList *l;
    for (l = state->all_files; l != NULL; l = l->next) {
        char *file_name = (char *)l->data;

        if (search_lower && strlen(search_lower) > 0) {
            gchar *name_lower = g_utf8_strdown(file_name, -1);
            gboolean matches = (strstr(name_lower, search_lower) != NULL);
            g_free(name_lower);
            if (!matches) continue;
        }

        gboolean is_img = (g_str_has_suffix(file_name, ".jpg") || g_str_has_suffix(file_name, ".JPG") ||
                           g_str_has_suffix(file_name, ".png") || g_str_has_suffix(file_name, ".PNG") ||
                           g_str_has_suffix(file_name, ".jpeg"));
        gboolean is_vid = (g_str_has_suffix(file_name, ".mp4") || g_str_has_suffix(file_name, ".MP4") ||
                           g_str_has_suffix(file_name, ".mkv") || g_str_has_suffix(file_name, ".avi"));

        gboolean add_item = FALSE;
        if (!chk_img && !chk_vid) add_item = TRUE;
        else if (chk_img && is_img) add_item = TRUE;
        else if (chk_vid && is_vid) add_item = TRUE;

        if (add_item) {
            GtkWidget *row = gtk_list_box_row_new();
            g_object_set_data(G_OBJECT(row), "is_selected", GINT_TO_POINTER(FALSE));

            GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
            
            GtkWidget *lbl_name = gtk_label_new(file_name);
            gtk_widget_set_halign(lbl_name, GTK_ALIGN_START);
            gtk_widget_set_hexpand(lbl_name, TRUE);

            gtk_widget_set_margin_start(row_box, 10);
            gtk_widget_set_margin_end(row_box, 10);
            gtk_widget_set_margin_top(row_box, 6);
            gtk_widget_set_margin_bottom(row_box, 6);

            gtk_container_add(GTK_CONTAINER(row_box), lbl_name);
            gtk_container_add(GTK_CONTAINER(row), row_box);
            gtk_container_add(GTK_CONTAINER(state->list_box), row);
        }
    }

    if (search_lower) g_free(search_lower);
    gtk_widget_show_all(state->list_box);
}

/* Refresh list by running aft-mtp-cli */
static void on_refresh_clicked(GtkButton *button, gpointer user_data) {
    AppState *state = (AppState *)user_data;
    set_loading_state(state, TRUE, "⏳ Scanning files... please wait");
    gtk_window_set_title(GTK_WINDOW(state->window), "MyDCIMFiles : Connecting...");

    g_list_free_full(state->all_files, g_free);
    state->all_files = NULL;

    FILE *fp = popen("aft-mtp-cli \"ls DCIM/Camera\"", "r");
    if (!fp) {
        gtk_window_set_title(GTK_WINDOW(state->window), "MyDCIMFiles : Disconnected");
        set_loading_state(state, FALSE, "Error: Check connection.");
        return;
    }

    char path[1024];
    while (fgets(path, sizeof(path), fp) != NULL) {
        path[strcspn(path, "\r\n")] = 0;
        if (strlen(path) == 0) continue;

        char *space = strchr(path, ' ');
        if (!space) continue;

        char *actual_name = space;
        while (*actual_name == ' ' || *actual_name == '\t') {
            actual_name++;
        }

        if (strlen(actual_name) == 0 || strstr(actual_name, ".pending") || actual_name[0] == '.')
            continue;

        state->all_files = g_list_append(state->all_files, g_strdup(actual_name));
    }
    pclose(fp);

    state->all_files = g_list_sort(state->all_files, (GCompareFunc)g_ascii_strcasecmp);

    gtk_window_set_title(GTK_WINDOW(state->window), "MyDCIMFiles : Connected");
    update_display_list(NULL, state);

    char done_msg[128];
    snprintf(done_msg, sizeof(done_msg), "Done! %u files found.", g_list_length(state->all_files));
    set_loading_state(state, FALSE, done_msg);
}

/* Download action for selected files via single click */
static void on_download_clicked(GtkButton *button, gpointer user_data) {
    AppState *state = (AppState *)user_data;
    
    GList *children = gtk_container_get_children(GTK_CONTAINER(state->list_box));
    GList *selected_rows = NULL;
    
    for (GList *l = children; l != NULL; l = l->next) {
        GtkWidget *row = GTK_WIDGET(l->data);
        gboolean is_selected = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "is_selected"));
        if (is_selected) {
            selected_rows = g_list_append(selected_rows, row);
        }
    }
    g_list_free(children);

    if (!selected_rows) {
        g_print("Log: No row selected!\n");
        set_loading_state(state, FALSE, "⚠️ No file selected!");
        return;
    }

    GtkWidget *dialog = gtk_file_chooser_dialog_new("Select Destination Folder",
                                                   GTK_WINDOW(state->window),
                                                   GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                                   ("_Cancel"), GTK_RESPONSE_CANCEL,
                                                   ("_Select"), GTK_RESPONSE_ACCEPT,
                                                   NULL);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *folder_path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        
        int total = g_list_length(selected_rows);
        int current = 0;

        g_print("\n--- Starting Batch Download --- \n");

        for (GList *l = selected_rows; l != NULL; l = l->next) {
            GtkListBoxRow *row = GTK_LIST_BOX_ROW(l->data);
            GtkWidget *row_box = gtk_bin_get_child(GTK_BIN(row));
            if (!row_box || !GTK_IS_CONTAINER(row_box)) continue;

            GList *row_children = gtk_container_get_children(GTK_CONTAINER(row_box));
            if (!row_children) continue;
            
            GtkWidget *lbl_name = GTK_WIDGET(row_children->data);
            const gchar *remote_name_str = gtk_label_get_text(GTK_LABEL(lbl_name));
            
            char remote_name[512];
            strncpy(remote_name, remote_name_str, sizeof(remote_name));
            g_strstrip(remote_name);
            g_list_free(row_children);

            current++;
            char status_msg[512];
            snprintf(status_msg, sizeof(status_msg), "⏳ Downloading (%d/%d): %s", current, total, remote_name);
            set_loading_state(state, TRUE, status_msg);

            char local_file_path[1024];
            snprintf(local_file_path, sizeof(local_file_path), "%s/%s", folder_path, remote_name);

            char cmd[2048];
            snprintf(cmd, sizeof(cmd), "aft-mtp-cli \"get \\\"DCIM/Camera/%s\\\" \\\"%s\\\"\"", remote_name, local_file_path);
            
            g_print("Log: [%d/%d] Downloading %s ...\n", current, total, remote_name);
            int ret = system(cmd);
            
            if (ret == 0) {
                g_print("Log: -> OK [%s]\n", remote_name);
            } else {
                g_print("Log: -> FAILED for %s (Exit code: %d)\n", remote_name, ret);
            }
        }

        set_loading_state(state, FALSE, "✅ Batch download complete!");
        g_print("--- Batch Download Finished ---\n\n");
        
        g_free(folder_path);
    }
    
    gtk_widget_destroy(dialog);
    g_list_free(selected_rows);
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    AppState state;
    state.all_files = NULL;

    /* Main window */
    state.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(state.window), "MyDCIMFiles (GTK)");
    gtk_window_set_default_size(GTK_WINDOW(state.window), 600, 700);
    g_signal_connect(state.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    /* Main layout */
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(state.window), vbox);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);

    /* Status Label */
    state.status_label = gtk_label_new("Initializing...");
    gtk_box_pack_start(GTK_BOX(vbox), state.status_label, FALSE, FALSE, 5);

    /* Filters (Checkboxes) */
    GtkWidget *hbox_filter = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    state.chk_images = gtk_check_button_new_with_label("Images");
    state.chk_videos = gtk_check_button_new_with_label("Videos");
    
    g_signal_connect(state.chk_images, "toggled", G_CALLBACK(update_display_list), &state);
    g_signal_connect(state.chk_videos, "toggled", G_CALLBACK(update_display_list), &state);
    
    gtk_box_pack_start(GTK_BOX(hbox_filter), state.chk_images, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(hbox_filter), state.chk_videos, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hbox_filter, FALSE, FALSE, 0);

    /* Search field */
    state.search_entry = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(state.search_entry), "Search file...");
    g_signal_connect(state.search_entry, "changed", G_CALLBACK(update_display_list), &state);
    gtk_box_pack_start(GTK_BOX(vbox), state.search_entry, FALSE, FALSE, 0);

    /* Selectable file list (GtkListBox inside GtkScrolledWindow) */
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_vexpand(scrolled, TRUE);
    
    state.list_box = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(state.list_box), GTK_SELECTION_NONE);
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(state.list_box), TRUE);
    g_signal_connect(state.list_box, "row-activated", G_CALLBACK(on_row_activated), &state);

    gtk_container_add(GTK_CONTAINER(scrolled), state.list_box);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);

    /* Bottom buttons */
    GtkWidget *hbox_buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *btn_refresh = gtk_button_new_with_label("Refresh List");
    GtkWidget *btn_download = gtk_button_new_with_label("Download Selected");
    
    g_signal_connect(btn_refresh, "clicked", G_CALLBACK(on_refresh_clicked), &state);
    g_signal_connect(btn_download, "clicked", G_CALLBACK(on_download_clicked), &state);

    gtk_box_pack_start(GTK_BOX(hbox_buttons), btn_refresh, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox_buttons), btn_download, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox_buttons, FALSE, FALSE, 0);

    gtk_widget_show_all(state.window);

    /* Automatic scan on startup */
    on_refresh_clicked(NULL, &state);

    gtk_main();

    g_list_free_full(state.all_files, g_free);
    return 0;
}

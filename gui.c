#include <stdio.h>
#include <math.h>
#include <gtk/gtk.h>

#include "xiwilib.h"
#include "common.h"
#include "map.h"
#include "mysql.h"
#include "gui.h"
#include "cairohelper.h"

vstr_t *vstr;
GtkWidget *window;
GtkTextBuffer *text_buf;
GtkWidget *statusbar;
guint statusbar_context_id;
int do_tred = 1;

const char *included_papers_string = NULL;
bool update_running = true;
bool mouse_held = false;
bool mouse_dragged;
double mouse_last_x = 0, mouse_last_y = 0;
paper_t *mouse_paper = NULL;

static int add_counter = 0;
static gboolean map_env_update(map_env_t *map_env) {
    if (add_counter > 0) {
        add_counter -= 1;
    }
    for (int i = 0; i < 2; i++) {
        if (map_env_iterate(map_env, do_tred, mouse_paper)) {
            if (add_counter == 0) {
                add_counter = 10;
                map_env_inc_num_papers(map_env, 100);
            }
            break;
        }
    }
    // force a redraw
    gtk_widget_queue_draw(window);
    return TRUE; // yes, we want to be called again
}

static gboolean draw_callback(GtkWidget *widget, cairo_t *cr, map_env_t *map_env) {
    guint width = gtk_widget_get_allocated_width(widget);
    guint height = gtk_widget_get_allocated_height(widget);

    vstr_reset(vstr);
    vstr_printf(vstr, "included papers: %s\n", included_papers_string);
    map_env_draw(map_env, cr, width, height, do_tred, vstr);

    // draw info to canvas
    cairo_identity_matrix(cr);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_helper_draw_text_lines(cr, 10, 20, vstr);

    return FALSE;
}

static gboolean key_press_event_callback(GtkWidget *widget, GdkEventKey *event, map_env_t *map_env) {
    //printf("here %d %d\n", event->state, event->keyval);

    guint width = gtk_widget_get_allocated_width(widget);
    guint height = gtk_widget_get_allocated_height(widget);

    if (event->keyval == GDK_KEY_space) {
        if (update_running) {
            g_idle_remove_by_data(map_env);
            update_running = false;
            printf("update not running\n");
        } else {
            g_idle_add((GSourceFunc)map_env_update, map_env);
            update_running = true;
            printf("update running\n");
        }

    } else if (event->keyval == GDK_KEY_a) {
        map_env_inc_num_papers(map_env, 1);
    } else if (event->keyval == GDK_KEY_b) {
        map_env_inc_num_papers(map_env, 10);
    } else if (event->keyval == GDK_KEY_c) {
        map_env_inc_num_papers(map_env, 100);
    } else if (event->keyval == GDK_KEY_d) {
        map_env_inc_num_papers(map_env, 1000);
    } else if (event->keyval == GDK_KEY_e) {
        map_env_inc_num_papers(map_env, 10000);

    } else if (event->keyval == GDK_KEY_j) {
        map_env_jolt(map_env, 0.5);
    } else if (event->keyval == GDK_KEY_k) {
        map_env_jolt(map_env, 2.5);
    } else if (event->keyval == GDK_KEY_g) {
        map_env_toggle_draw_grid(map_env);
    } else if (event->keyval == GDK_KEY_l) {
        map_env_toggle_draw_paper_links(map_env);

    } else if (event->keyval == GDK_KEY_1) {
        map_env_adjust_anti_gravity(map_env, 0.9);
    } else if (event->keyval == GDK_KEY_2) {
        map_env_adjust_anti_gravity(map_env, 1.1);

    } else if (event->keyval == GDK_KEY_3) {
        map_env_adjust_link_strength(map_env, 0.9);
    } else if (event->keyval == GDK_KEY_4) {
        map_env_adjust_link_strength(map_env, 1.1);

    } else if (event->keyval == GDK_KEY_t) {
        do_tred = 1 - do_tred;
        if (do_tred) {
            printf("transitive reduction turned on\n");
        } else {
            printf("transitive reduction turned off\n");
        }
    } else if (event->keyval == GDK_KEY_plus || event->keyval == GDK_KEY_equal) {
        map_env_zoom(map_env, width / 2, height / 2, 1.2);
    } else if (event->keyval == GDK_KEY_minus) {
        map_env_zoom(map_env, width / 2, height / 2, 0.8);
    } else if (event->keyval == GDK_KEY_q) {
        gtk_main_quit();
    }

    if (!update_running) {
        gtk_widget_queue_draw(window);
    }

    return TRUE; // we handled the event, stop processing
}

static gboolean button_press_event_callback(GtkWidget *widget, GdkEventButton *event, map_env_t *map_env) {
    if (event->button == GDK_BUTTON_PRIMARY) {
        mouse_held = TRUE;
        mouse_dragged = FALSE;
        mouse_last_x = event->x;
        mouse_last_y = event->y;
        mouse_paper = map_env_get_paper_at(map_env, event->x, event->y);
    }

    return TRUE; // we handled the event, stop processing
}

static gboolean button_release_event_callback(GtkWidget *widget, GdkEventButton *event, map_env_t *map_env) {
    if (event->button == GDK_BUTTON_PRIMARY) {
        mouse_held = FALSE;
        if (!mouse_dragged) {
            if (mouse_paper != NULL) {
                paper_t *p = mouse_paper;
                vstr_reset(vstr);
                vstr_printf(vstr, "paper[%d] = %d (%d refs, %d cites) %s -- %s\n", p->index, p->id, p->num_refs, p->num_cites, p->authors, p->title);
                gtk_statusbar_push(GTK_STATUSBAR(statusbar), statusbar_context_id, vstr_str(vstr));
            }
        }
        mouse_paper = NULL;
    }

    return TRUE; // we handled the event, stop processing
}

static gboolean scroll_event_callback(GtkWidget *widget, GdkEventScroll *event, map_env_t *map_env) {
    if (event->direction == GDK_SCROLL_UP) {
        map_env_zoom(map_env, event->x, event->y, 1.2);
    } else if (event->direction == GDK_SCROLL_DOWN) {
        map_env_zoom(map_env, event->x, event->y, 0.8);
    }

    if (!update_running) {
        gtk_widget_queue_draw(window);
    }

    return TRUE; // we handled the event, stop processing
}

static gboolean pointer_motion_event_callback(GtkWidget *widget, GdkEventMotion *event, map_env_t *map_env) {
    if (mouse_held) {
        if (!mouse_dragged) {
            if (fabs(mouse_last_x - event->x) > 4 || fabs(mouse_last_y - event->y) > 4) {
                mouse_dragged = TRUE;
            }
        } else {
            if (mouse_paper == NULL) {
                // mouse dragged on background
                double dx = event->x - mouse_last_x;
                double dy = event->y - mouse_last_y;
                map_env_scroll(map_env, dx, dy);
            } else {
                // mouse dragged on paper
                double x = event->x;
                double y = event->y;
                map_env_screen_to_world(map_env, &x, &y);
                mouse_paper->x = x;
                mouse_paper->y = y;
            }
            mouse_last_x = event->x;
            mouse_last_y = event->y;
        }

        if (!update_running) {
            gtk_widget_queue_draw(window);
        }
    }
    return TRUE;
}

// for a gtk example, see: http://git.gnome.org/browse/gtk+/tree/demos/gtk-demo/drawingarea.c

void build_gui(map_env_t *map_env, const char *papers_string) {
    vstr = vstr_new();
    included_papers_string = papers_string;

    // create the main, top level window
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    // give it a title
    gtk_window_set_title(GTK_WINDOW(window), "Map generator");

    // create the outer box
    GtkWidget *box_outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), box_outer);

    // create the controls
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    GtkWidget *ctrl_tred = gtk_toggle_button_new_with_label("transitive reduction");
    GtkWidget *ctrl_draw_links = gtk_toggle_button_new_with_label("draw links");
    GtkWidget *ctrl_draw_grid = gtk_toggle_button_new_with_label("draw grid");
    GtkWidget *xxx = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.1, 10, 0.1);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ctrl_tred), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ctrl_draw_links), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ctrl_draw_grid), FALSE);
    gtk_scale_set_digits(GTK_SCALE(xxx), 3);
    gtk_widget_set_size_request(xxx, 80, -1);
    gtk_box_pack_start(GTK_BOX(box), ctrl_tred, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), ctrl_draw_links, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), ctrl_draw_grid, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), xxx, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box_outer), box, FALSE, FALSE, 0);

    // create the drawing area
    GtkWidget *drawing_area = gtk_drawing_area_new();
    GtkWidget *drawing_area_frame = gtk_frame_new(NULL);
    gtk_container_add(GTK_CONTAINER(drawing_area_frame), drawing_area);
    gtk_frame_set_shadow_type(GTK_FRAME(drawing_area_frame), GTK_SHADOW_IN);
    //gtk_widget_set_size_request(drawing_area_frame, 50, -1);
    gtk_box_pack_start(GTK_BOX(box_outer), drawing_area_frame, TRUE, TRUE, 0);

    // create the console box
    /*
    GtkWidget *text_area = gtk_text_view_new();
    GtkWidget *scrolled_text_area = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *console_frame = gtk_frame_new("Output console");
    gtk_container_add(GTK_CONTAINER(scrolled_text_area), text_area);
    gtk_container_add(GTK_CONTAINER(console_frame), scrolled_text_area);
    gtk_frame_set_shadow_type(GTK_FRAME(console_frame), GTK_SHADOW_IN);
    gtk_widget_set_size_request(console_frame, 50, -1);
    gtk_paned_pack2(GTK_PANED(paned), console_frame, FALSE, FALSE);

    text_buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_area));
    */

    // create the status bar
    statusbar = gtk_statusbar_new();
    statusbar_context_id = gtk_statusbar_get_context_id(GTK_STATUSBAR(statusbar), "status");
    gtk_box_pack_end(GTK_BOX(box_outer), statusbar, FALSE, FALSE, 0);

    /*
    ** Map the destroy signal of the window to gtk_main_quit;
    ** When the window is about to be destroyed, we get a notification and
    ** stop the main GTK+ loop by returning 0
    */
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    g_signal_connect(window, "key-press-event", G_CALLBACK(key_press_event_callback), map_env);

    //g_timeout_add(100 /* milliseconds */, (GSourceFunc)map_env_update, map_env);
    g_idle_add((GSourceFunc)map_env_update, map_env);
    g_signal_connect(drawing_area, "draw", G_CALLBACK(draw_callback), map_env);
    g_signal_connect(drawing_area, "button-press-event", G_CALLBACK(button_press_event_callback), map_env);
    g_signal_connect(drawing_area, "button-release-event", G_CALLBACK(button_release_event_callback), map_env);
    g_signal_connect(drawing_area, "scroll-event", G_CALLBACK(scroll_event_callback), map_env);
    g_signal_connect(drawing_area, "motion-notify-event", G_CALLBACK(pointer_motion_event_callback), map_env);

    /* Ask to receive events the drawing area doesn't normally subscribe to
    */
    gtk_widget_set_events(drawing_area, gtk_widget_get_events(drawing_area)
        | GDK_LEAVE_NOTIFY_MASK
        | GDK_BUTTON_PRESS_MASK
        | GDK_BUTTON_RELEASE_MASK
        | GDK_SCROLL_MASK
        | GDK_POINTER_MOTION_MASK
        | GDK_POINTER_MOTION_HINT_MASK);

    /* Make sure that everything, window and label, are visible */
    gtk_widget_show_all(window);

    // print some help text
    printf(
        "key bindings\n"
        " space- play/pause the physics update\n"
        "    a - add 1 paper\n"
        "    b - add 10 papers\n"
        "    c - add 100 papers\n"
        "    d - add 1000 papers\n"
        "    e - add 10000 papers\n"
        "    t - turn tred on/off\n"
        "    l - turn links on/off\n"
        "    j - make a small jolt\n"
        "    k - make a large jolt\n"
        "    q - quit\n"
        "mouse bindings\n"
        "    left click - show info about a paper\n"
        "    left drag - move a paper around / pan the view\n"
        "       scroll - zoom in/out\n"
    );
}

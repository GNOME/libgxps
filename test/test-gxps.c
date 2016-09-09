#include <glib.h>
#include <gio/gio.h>
#include <cairo.h>
#include <gtk/gtk.h>
#include <stdlib.h>

#include <libgxps/gxps.h>

typedef struct {
	GtkWidget *darea;
	GtkWidget *spin_button;

	GXPSDocument *doc;
	cairo_surface_t *surface;
} GXPSView;

static gboolean
drawing_area_draw (GtkWidget *drawing_area,
                   cairo_t   *cr,
                   GXPSView  *view)
{
	if (!view->surface)
		return FALSE;

        cairo_set_source_rgb (cr, 1., 1., 1.);
        cairo_rectangle (cr, 0, 0,
                         cairo_image_surface_get_width (view->surface),
                         cairo_image_surface_get_height (view->surface));
	cairo_fill (cr);
	cairo_set_source_surface (cr, view->surface, 0, 0);
	cairo_paint (cr);

	return TRUE;
}

static void
page_changed_callback (GtkSpinButton *button,
		       GXPSView      *view)
{
	GXPSPage *xps_page;
	gint      page;
	gdouble   width, height;
	cairo_t  *cr;
	GError   *error = NULL;

	page = gtk_spin_button_get_value_as_int (button);

	xps_page = gxps_document_get_page (view->doc, page, &error);
	if (error) {
		g_printerr ("Error getting page %d: %s\n", page, error->message);
		g_error_free (error);

		return;
	}
	gxps_page_get_size (xps_page, &width, &height);

	cairo_surface_destroy (view->surface);
	view->surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
						    width, height);

	cr = cairo_create (view->surface);
	gxps_page_render (xps_page, cr, &error);
	if (error) {
		g_printerr ("Error rendering page: %d: %s\n", page, error->message);
		g_error_free (error);
	}
	cairo_destroy (cr);

	gtk_widget_set_size_request (view->darea, width, height);
	gtk_widget_queue_draw (view->darea);

	g_object_unref (xps_page);
}

static gchar *
format_date (time_t utime)
{
        time_t time = (time_t) utime;
        char s[256];
        const char *fmt_hack = "%c";
        size_t len;
#ifdef HAVE_LOCALTIME_R
        struct tm t;
        if (time == 0 || !localtime_r (&time, &t))
                return NULL;
        len = strftime (s, sizeof (s), fmt_hack, &t);
#else
        struct tm *t;
        if (time == 0 || !(t = localtime (&time)) )
                return NULL;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
        len = strftime (s, sizeof (s), fmt_hack, t);
#pragma GCC diagnostic pop
#endif

        if (len == 0 || s[0] == '\0')
                return NULL;

        return g_locale_to_utf8 (s, -1, NULL, NULL, NULL);
}


static void
append_row_to_table (GtkWidget   *table,
                     const gchar *key,
                     const gchar *value)
{
        GtkWidget *key_label;
        GtkWidget *value_label;

        if (!value)
                return;

        key_label = gtk_label_new (NULL);
        g_object_set (G_OBJECT (key_label), "xalign", 0.0, NULL);
        gtk_label_set_markup (GTK_LABEL (key_label), key);
        gtk_container_add (GTK_CONTAINER (table), key_label);
        gtk_widget_show (key_label);

        value_label = gtk_label_new (value);
        g_object_set (G_OBJECT (value_label),
                      "xalign", 0.0,
                      "selectable", TRUE,
                      "ellipsize", PANGO_ELLIPSIZE_END,
                      "hexpand", TRUE,
                      NULL);
        gtk_grid_attach_next_to (GTK_GRID (table),
                                 value_label,
                                 key_label,
                                 GTK_POS_RIGHT,
                                 1, 1);
        gtk_widget_show (value_label);
}

static void
properties_button_clicked (GtkWidget *button, GXPSFile *xps)
{
        GtkWidget          *dialog;
        GtkWidget          *table;
        GXPSCoreProperties *core_props;
        gchar              *date;
        GError             *error = NULL;

        core_props = gxps_file_get_core_properties (xps, &error);
        if (!core_props) {
                if (error)  {
                        g_printerr ("Error getting core properties: %s\n", error->message);
                        g_error_free (error);
                }
                return;
        }

        dialog = gtk_dialog_new_with_buttons ("Document Properties",
                                              GTK_WINDOW (gtk_widget_get_toplevel (button)),
                                              GTK_DIALOG_DESTROY_WITH_PARENT,
                                              "Close", GTK_RESPONSE_CLOSE,
                                              NULL);
        g_signal_connect (dialog, "response", G_CALLBACK (gtk_widget_destroy), NULL);

        table = gtk_grid_new ();
        gtk_container_set_border_width (GTK_CONTAINER (table), 5);
        gtk_orientable_set_orientation (GTK_ORIENTABLE (table), GTK_ORIENTATION_VERTICAL);
        gtk_grid_set_column_spacing (GTK_GRID (table), 6);
        gtk_grid_set_row_spacing (GTK_GRID (table), 6);

        append_row_to_table (table, "<b>Title:</b>", gxps_core_properties_get_title (core_props));
        append_row_to_table (table, "<b>Creator:</b>", gxps_core_properties_get_creator (core_props));
        append_row_to_table (table, "<b>Description:</b>", gxps_core_properties_get_description (core_props));
        append_row_to_table (table, "<b>Subject:</b>", gxps_core_properties_get_subject (core_props));
        append_row_to_table (table, "<b>Keywords:</b>", gxps_core_properties_get_keywords (core_props));
        append_row_to_table (table, "<b>Version:</b>", gxps_core_properties_get_version (core_props));
        append_row_to_table (table, "<b>Revision:</b>", gxps_core_properties_get_revision (core_props));
        append_row_to_table (table, "<b>Identifier:</b>", gxps_core_properties_get_identifier (core_props));
        append_row_to_table (table, "<b>Language:</b>", gxps_core_properties_get_language (core_props));
        append_row_to_table (table, "<b>Category:</b>", gxps_core_properties_get_category (core_props));
        append_row_to_table (table, "<b>Content Status:</b>", gxps_core_properties_get_content_status (core_props));
        append_row_to_table (table, "<b>Content Type:</b>", gxps_core_properties_get_content_type (core_props));
        date = format_date (gxps_core_properties_get_created (core_props));
        append_row_to_table (table, "<b>Created:</b>", date);
        g_free (date);
        append_row_to_table (table, "<b>Last Modified By:</b>", gxps_core_properties_get_last_modified_by (core_props));
        date = format_date (gxps_core_properties_get_modified (core_props));
        append_row_to_table (table, "<b>Modified:</b>", date);
        g_free (date);
        date = format_date (gxps_core_properties_get_last_printed (core_props));
        append_row_to_table (table, "<b>Las Printed:</b>", date);
        g_free (date);

        gtk_container_add (GTK_CONTAINER (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), table);
        gtk_widget_show (table);

        gtk_widget_show (dialog);

        g_object_unref (core_props);
}

gint main (gint argc, gchar **argv)
{
	GFile     *file;
	GXPSFile  *xps;
	GXPSView  *view;
	GtkWidget *win;
	GtkWidget *hbox, *vbox, *sw;
        GtkWidget *button;
	guint      page = 0;
	GError    *error = NULL;

	if (argc < 2) {
		g_printerr ("Use: test-xps file\n");

		return 1;
	}

	gtk_init (&argc, &argv);

	file = g_file_new_for_commandline_arg (argv[1]);
	xps = gxps_file_new (file, &error);
	g_object_unref (file);

	if (error) {
		g_printerr ("Error creating file: %s\n", error->message);
		g_error_free (error);
		g_object_unref (xps);

		return 1;
	}

	view = g_new0 (GXPSView, 1);
	view->doc = gxps_file_get_document (xps, 0, &error);
	if (error) {
		g_printerr ("Error getting document 0: %s\n", error->message);
		g_error_free (error);
		g_object_unref (xps);

		return 1;
	}

	win = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size (GTK_WINDOW (win), 600, 600);
	g_signal_connect (win, "delete-event",
			  G_CALLBACK (gtk_main_quit),
			  NULL);

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);

	hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	view->spin_button = gtk_spin_button_new_with_range  (0,
							     gxps_document_get_n_pages (view->doc) - 1,
							     1);
	g_signal_connect (view->spin_button,
			  "value-changed",
			  G_CALLBACK (page_changed_callback),
			  view);
	gtk_box_pack_end (GTK_BOX (hbox), view->spin_button, FALSE, TRUE, 0);
	gtk_widget_show (view->spin_button);

        button = gtk_button_new ();
        g_signal_connect (button, "clicked",
                          G_CALLBACK (properties_button_clicked),
                          xps);
        gtk_button_set_image (GTK_BUTTON (button),
                              gtk_image_new_from_icon_name ("document-properties", GTK_ICON_SIZE_SMALL_TOOLBAR));
        gtk_box_pack_end (GTK_BOX (hbox), button, FALSE, FALSE, 0);
        gtk_widget_show (button);

	gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);
	gtk_widget_show (hbox);

	view->darea = gtk_drawing_area_new ();
	g_signal_connect (view->darea, "draw",
			  G_CALLBACK (drawing_area_draw),
			  view);

	sw = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
					GTK_POLICY_AUTOMATIC,
					GTK_POLICY_AUTOMATIC);
	gtk_container_add (GTK_CONTAINER (sw), view->darea);
	gtk_widget_show (view->darea);

	gtk_box_pack_end (GTK_BOX (vbox), sw, TRUE, TRUE, 0);
	gtk_widget_show (sw);

	gtk_container_add (GTK_CONTAINER (win), vbox);
	gtk_widget_show (vbox);

	gtk_widget_show (win);

	if (argc > 2)
		page = atoi (argv[2]);
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (view->spin_button), page);
	if (page == 0)
		page_changed_callback (GTK_SPIN_BUTTON (view->spin_button), view);

	gtk_main ();

	g_object_unref (view->doc);
	g_object_unref (xps);
	cairo_surface_destroy (view->surface);
	g_free (view);

	return 0;
}

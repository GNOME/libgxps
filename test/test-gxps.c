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
	guint     width, height;
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

gint main (gint argc, gchar **argv)
{
	GFile     *file;
	GXPSFile  *xps;
	GXPSView  *view;
	GtkWidget *win;
	GtkWidget *hbox, *vbox, *sw;
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

	vbox = gtk_vbox_new (FALSE, 6);

	hbox = gtk_hbox_new (FALSE, 6);
	view->spin_button = gtk_spin_button_new_with_range  (0,
							     gxps_document_get_n_pages (view->doc) - 1,
							     1);
	g_signal_connect (view->spin_button,
			  "value-changed",
			  G_CALLBACK (page_changed_callback),
			  view);
	gtk_box_pack_end (GTK_BOX (hbox), view->spin_button, FALSE, TRUE, 0);
	gtk_widget_show (view->spin_button);

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
	gtk_scrolled_window_add_with_viewport (GTK_SCROLLED_WINDOW (sw),
					       view->darea);
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

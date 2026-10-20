/*
 * Copyright (c) 2008-2014 Lu, Chao-Ming (Tetralet).  All rights reserved.
 *
 * This file is part of LilyTerm.
 *
 * LilyTerm is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * LilyTerm is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LilyTerm.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <locale.h>

#include "notebook.h"

// extern GtkWidget *active_window;
extern GtkWidget *menu_active_window;
extern gboolean proc_exist;
extern gchar *init_LC_CTYPE;
extern gchar *init_LANGUAGE;

extern gint dialog_activated;
gboolean menu_activated = FALSE;

extern struct Process_Data *process_data;

extern gboolean force_to_quit;
extern gboolean using_kill;
gboolean confirm_to_execute = TRUE;
gchar *login_shell_str[] = {NULL, "-", NULL};
gchar *full_argv[] = {"/bin/sh", "/bin/sh", NULL};

static void collect_pages(GtkWidget *widget, gpointer pages);
static gboolean spawn_in_vte(GtkWidget *vte, VtePtyFlags pty_flags, const gchar *working_directory,
			     gchar **argv, gchar **envv, GSpawnFlags spawn_flags, GPid *child_pid, GError **error);

// Set by split_page() for the duration of one add_page() call: the new vte is
// then a new pane next to split_from, in the same tab, instead of a new tab.
static struct Page *split_from = NULL;
static GtkOrientation split_orientation = GTK_ORIENTATION_HORIZONTAL;

// ---- Split panes ---- //
//
// A tab (page_data->tab_box, the notebook page) holds one pane, or a tree of
// GtkPaned whose leaves are panes (page_data->hbox: a vte and its scroll bar).
// Every pane has its own struct Page. The panes of a tab share the tab label;
// the label's "VteBox" is the pane that had the focus last, and it is the one
// get_page_data_from_nth_page() returns for the tab.

// TRUE if the tab that page_data is in holds more than one pane.
gboolean page_is_split(struct Page *page_data)
{
	if ((page_data==NULL) || (page_data->hbox==NULL) || (page_data->tab_box==NULL)) return FALSE;
	return gtk_widget_get_parent(page_data->hbox) != page_data->tab_box;
}

static GtkWidget *new_paned(GtkOrientation orientation)
{
	return gtk_paned_new(orientation);
}

// Put new_child where old_child is in parent: a GtkPaned, or the tab box.
// The caller has to hold a reference on old_child if it wants to keep it.
static void replace_child(GtkWidget *parent, GtkWidget *old_child, GtkWidget *new_child)
{
	if (GTK_IS_PANED(parent))
	{
		gboolean first = (gtk_paned_get_child1(GTK_PANED(parent)) == old_child);
		gtk_container_remove(GTK_CONTAINER(parent), old_child);
		if (first)
			gtk_paned_pack1(GTK_PANED(parent), new_child, TRUE, FALSE);
		else
			gtk_paned_pack2(GTK_PANED(parent), new_child, TRUE, FALSE);
	}
	else
	{
		gtk_container_remove(GTK_CONTAINER(parent), old_child);
		gtk_box_pack_start(GTK_BOX(parent), new_child, TRUE, TRUE, 0);
	}
}

// Put the pane of page_data next to the pane of from, in a new GtkPaned.
static void insert_split_pane(struct Window *win_data, struct Page *from, struct Page *page_data,
			      GtkOrientation orientation)
{
#ifdef DETAIL
	g_debug("! Launch insert_split_pane() with from = %p, page_data = %p, orientation = %d",
		from, page_data, orientation);
#endif
	GtkWidget *paned = new_paned(orientation);
	g_object_ref(from->hbox);
	replace_child(gtk_widget_get_parent(from->hbox), from->hbox, paned);
	gtk_paned_pack1(GTK_PANED(paned), from->hbox, TRUE, FALSE);
	gtk_paned_pack2(GTK_PANED(paned), page_data->hbox, TRUE, FALSE);
	g_object_unref(from->hbox);
	gtk_widget_show(paned);

	// what notebook_page_added() does for a new tab
	page_data->tab_box = from->tab_box;
	page_data->page_no = from->page_no;
	page_data->window = win_data->window;
	page_data->notebook = win_data->notebook;
	// Not supported yet: dragging a split tab to another window.
	gtk_notebook_set_tab_detachable(GTK_NOTEBOOK(win_data->notebook), page_data->tab_box, FALSE);
	if (page_data->pid > 0) monitor_cmdline(page_data);
	update_window_hint(win_data, page_data);
}

// Remove the pane of page_data from its tab: its sibling takes the place of
// their GtkPaned. The vte is destroyed with it. page_data itself is not freed.
static void remove_split_pane(struct Window *win_data, struct Page *page_data)
{
#ifdef DETAIL
	g_debug("! Launch remove_split_pane() with page_data = %p", page_data);
#endif
	GtkWidget *paned = gtk_widget_get_parent(page_data->hbox);
	GtkWidget *sibling = (gtk_paned_get_child1(GTK_PANED(paned)) == page_data->hbox) ?
			     gtk_paned_get_child2(GTK_PANED(paned)) : gtk_paned_get_child1(GTK_PANED(paned));
	gboolean had_focus = (win_data->current_vte == page_data->vte);
	gboolean on_label = (g_object_get_data(G_OBJECT(page_data->label), "VteBox") == page_data->vte);

	g_object_ref(sibling);
	gtk_container_remove(GTK_CONTAINER(paned), sibling);
	// destroys the paned, and the pane of page_data with it
	replace_child(gtk_widget_get_parent(paned), paned, sibling);
	g_object_unref(sibling);

	// the pane that takes over the tab label, and the focus
	GPtrArray *rest = g_ptr_array_new();
	collect_pages(sibling, rest);
	struct Page *next = rest->len ? g_ptr_array_index(rest, 0) : NULL;
	g_ptr_array_unref(rest);
	if (next==NULL) return;

	if (on_label) g_object_set_data(G_OBJECT(page_data->label), "VteBox", next->vte);
	if (had_focus)
	{
		win_data->current_vte = NULL;
		gtk_widget_grab_focus(next->vte);
	}
	if (! page_is_split(next))
		gtk_notebook_set_tab_detachable(GTK_NOTEBOOK(win_data->notebook), next->tab_box, TRUE);
	update_window_hint(win_data, next);
}

// Split the pane of page_data in two: the new pane runs the default shell,
// in the directory of page_data. HORIZONTAL: side by side; VERTICAL: one above the other.
struct Page *split_page(struct Window *win_data, struct Page *page_data, GtkOrientation orientation)
{
#ifdef DETAIL
	g_debug("! Launch split_page() with win_data = %p, page_data = %p, orientation = %d",
		win_data, page_data, orientation);
#endif
#ifdef SAFEMODE
	if ((win_data==NULL) || (page_data==NULL) || (page_data->vte==NULL)) return NULL;
#endif
	split_from = page_data;
	split_orientation = orientation;
	struct Page *new_page = add_page(win_data,
					 page_data,
					 NULL,
					 page_data->encoding_str,
					 page_data->encoding_str,
					 page_data->locale,
					 NULL,
					 page_data->environ,
					 page_data->VTE_CJK_WIDTH_STR,
					 FALSE);
	split_from = NULL;
	if (new_page) gtk_widget_grab_focus(new_page->vte);
	return new_page;
}

// The right click menu items for split panes: they act on the current pane.
void menu_split_right(GtkWidget *menuitem, struct Window *win_data)
{
#ifdef SAFEMODE
	if ((win_data==NULL) || (win_data->current_vte==NULL)) return;
#endif
	split_page(win_data, get_page_data_from_vte(win_data->current_vte, win_data, -1), GTK_ORIENTATION_HORIZONTAL);
}

void menu_split_down(GtkWidget *menuitem, struct Window *win_data)
{
#ifdef SAFEMODE
	if ((win_data==NULL) || (win_data->current_vte==NULL)) return;
#endif
	split_page(win_data, get_page_data_from_vte(win_data->current_vte, win_data, -1), GTK_ORIENTATION_VERTICAL);
}

void menu_close_pane(GtkWidget *menuitem, struct Window *win_data)
{
#ifdef SAFEMODE
	if ((win_data==NULL) || (win_data->current_vte==NULL)) return;
#endif
	close_page(win_data->current_vte, CLOSE_WITH_KEY_STRIKE);
}

// Close the tab that page_data is in: every pane of it, the last one first.
// Stops (and returns FALSE) if the user does not confirm closing a pane.
gboolean close_tab(struct Page *page_data, gint close_type)
{
#ifdef DETAIL
	g_debug("! Launch close_tab() with page_data = %p, close_type = %d", page_data, close_type);
#endif
#ifdef SAFEMODE
	if (page_data==NULL) return FALSE;
#endif
	GPtrArray *panes = get_pages_in_tab(page_data);
	gint i;
	gboolean closed = TRUE;
	for (i=panes->len-1; i>-1; i--)
		if (! (closed = close_page(((struct Page *)g_ptr_array_index(panes, i))->vte, close_type)))
			break;
	g_ptr_array_unref(panes);
	return closed;
}

// Move the focus to the next pane of the current tab.
void focus_next_pane(struct Window *win_data)
{
#ifdef DETAIL
	g_debug("! Launch focus_next_pane() with win_data = %p", win_data);
#endif
#ifdef SAFEMODE
	if ((win_data==NULL) || (win_data->current_vte==NULL)) return;
#endif
	struct Page *page_data = get_page_data_from_vte(win_data->current_vte, win_data, -1);
	GPtrArray *panes = get_pages_in_tab(page_data);
	guint i;
	for (i=0; i<panes->len; i++)
	{
		if (g_ptr_array_index(panes, i) == page_data)
		{
			struct Page *next = g_ptr_array_index(panes, (i+1) % panes->len);
			gtk_widget_grab_focus(next->vte);
			break;
		}
	}
	g_ptr_array_unref(panes);
}

// prev_vte: the new vte will be the clone of prev_vte.
// menuitem_encoding: using right menu to open a new vtw with specified locale.
//		      Lilyterm will update page_data->encoding_str with it.
// locale: the environ LANG, LANGUAGE, and LC_ALL used in vte_terminal_fork_command().
// user_environ: it will be used in vte_terminal_fork_command(),
//		 and will be saved in page_data->environ.
//		 should be separated with <tab>.
// run_once: some data that should be inited for the first vte of a LilyTerm window.
// encoding: the encoding come from environ *or default_encoding in profile*. only for new window.
// runtime_locale_encoding: the encoding come from environ.
struct Page *add_page(struct Window *win_data,
		      struct Page *page_data_prev,
		      GtkWidget *menuitem_encoding,
		      gchar *encoding,
		      gchar *runtime_locale_encoding,
		      gchar *locale,
		      gchar *environments,
		      gchar *user_environ,
		      const gchar *VTE_CJK_WIDTH_STR,
		      gboolean add_to_next)
{
#ifdef DETAIL
	if (menuitem_encoding)
		g_debug("! Launch add_page() with win_data = %p, page_data_prev = %p, "
			"menuitem_encoding->name = %s, encoding = %s,"
			"locale = %s, environ = %s, VTE_CJK_WIDTH_STR = %s",
			win_data, page_data_prev, gtk_widget_get_name(menuitem_encoding), encoding,
			locale, user_environ, VTE_CJK_WIDTH_STR);
	else
		g_debug("! Launch add_page() with win_data = %p, page_data_prev = %p, "
			"encoding = %s, locale = %s, environ = %s, VTE_CJK_WIDTH_STR = %s",
			win_data, page_data_prev, encoding,
			locale, user_environ, VTE_CJK_WIDTH_STR);
#endif

#ifdef SAFEMODE
	if (win_data==NULL) return NULL;
#endif

// ---- Clone the page_data ---- //

	struct Page *page_data = g_new0(struct Page, 1);
#ifdef SAFEMODE
	if (page_data==NULL) return NULL;
#endif
	if (page_data_prev)
		page_data_dup(page_data_prev, page_data);
	else
		init_page_parameters(win_data, page_data);

// ---- deal the parameters and set the environs ----//

	GString *environ_str;
	if (environments)
		environ_str = g_string_new(environments);
	else
		environ_str = g_string_new(win_data->environment);

	// set the encoding
	if ((encoding && (encoding[0]!='\0')) || menuitem_encoding)
	{
		g_free(page_data->encoding_str);

		if (encoding && (encoding[0]!='\0'))
			page_data->encoding_str = g_strdup(encoding);
		else
			page_data->encoding_str = g_strdup(gtk_widget_get_name(menuitem_encoding));
	}

	// set the locale.
	if (locale && (locale[0]!='\0'))
	{
		gchar *lang = get_lang_str_from_locale(locale, ".");
		gchar *language = get_language_str_from_locales(locale, win_data->default_locale);

		g_free(page_data->locale);
		page_data->locale = g_strdup(locale);
		g_string_append_printf (environ_str,
					"\tLANG=%s\tLANGUAGE=%s\tLC_ALL=%s",
					lang, language, locale);
		g_free(lang);
		g_free(language);
	}

	// set the environ that user specify in profile
	if (user_environ && user_environ[0] != '\0')
	{
		g_free(page_data->environ);
		page_data->environ = g_strdup(user_environ);
#ifdef SAFEMODE
		if (environ_str && environ_str->len)
#else
		if (environ_str->len)
#endif
			environ_str = g_string_append(environ_str, "\t");
		g_string_append_printf (environ_str, "%s", user_environ);
	}
#ifdef SAFEMODE
	if (environ_str && environ_str->len)
#else
	if (environ_str->len)
#endif
		environ_str = g_string_append(environ_str, "\t");

	// set colorterm
	g_string_append_printf (environ_str, "\tCOLORTERM=lilyterm");

// ---- Setting the VTE_CJK_WIDTH environment ---- //

	if (VTE_CJK_WIDTH_STR && (VTE_CJK_WIDTH_STR[0] != '\0'))
	{
		g_setenv("VTE_CJK_WIDTH", VTE_CJK_WIDTH_STR, TRUE);
		g_free(page_data->VTE_CJK_WIDTH_STR);
		page_data->VTE_CJK_WIDTH_STR = g_strdup(VTE_CJK_WIDTH_STR);
	}
	else
	{
		if (page_data->VTE_CJK_WIDTH_STR && (page_data->VTE_CJK_WIDTH_STR[0] != '\0'))
			g_setenv("VTE_CJK_WIDTH", page_data->VTE_CJK_WIDTH_STR, TRUE);
		else
			g_unsetenv("VTE_CJK_WIDTH");
	}

	if (page_data->VTE_CJK_WIDTH_STR && (page_data->VTE_CJK_WIDTH_STR[0] != '\0'))
	{
#ifdef SAFEMODE
		if (environ_str && environ_str->len)
#else
		if (environ_str->len)
#endif
			environ_str = g_string_append(environ_str, "\t");
		g_string_append_printf (environ_str, "VTE_CJK_WIDTH=%s", page_data->VTE_CJK_WIDTH_STR);
	}

// ---- Get the final environment data ---- //

	gchar **new_environs = NULL;
#ifdef SAFEMODE
		if (environ_str && environ_str->len)
#else
		if (environ_str->len)
#endif
			new_environs = sanitize_environs(split_string(environ_str->str, "\t", -1));
	g_string_free(environ_str, TRUE);

// ---- create vte ---- //
	page_data->vte = vte_terminal_new();

	// save the data first
	g_object_set_data(G_OBJECT(page_data->vte), "Page_Data", page_data);

#ifdef SAFEMODE
	if (page_data->encoding_str && (page_data->encoding_str[0]!='\0'))
#endif
		lilyterm_vte_set_encoding(VTE_TERMINAL(page_data->vte), page_data->encoding_str);
	page_data->adjustment = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(page_data->vte));
// ---- Execute programs in the vte ---- //

#ifdef DETAIL
	if (win_data->command)
		g_debug("! add_page(): command line = %s", win_data->command);
	if (win_data->argv)
		print_array("! add_page(): argv", win_data->argv);
#endif

	if (page_data_prev)
	{
		page_data->pwd = get_init_dir(get_tpgid(page_data_prev->pid), NULL, win_data->home);
	}
	else
	{
		page_data->pwd = get_init_dir(-1, win_data->init_dir, win_data->home);
	}

	// extern char **environ;
	// extern gchar **empty_environ;

	// FIXME: Why clear the environ here?

	gboolean argv_need_be_free = FALSE;
	gchar **argv = get_argv(win_data, &argv_need_be_free);
	gchar **final_argv = argv;
	gboolean fork_stats;
	// G_SPAWN_CHILD_INHERITS_STDIN is refused by vte >= 0.62 ("forbidden spawn flags"),
	// and it never had a meaning here: the child always gets the pty as its stdin.
	GSpawnFlags spawn_flags = G_SPAWN_SEARCH_PATH;
	GError *error = NULL;
	gint final_argv_need_be_free = FALSE;

	login_shell_str[0] = win_data->shell;
#  ifdef SAFEMODE
	if (login_shell_str[0]==NULL) login_shell_str[0] = "/bin/sh";
#  endif
	if (win_data->argv==NULL)
	{
		if (win_data->login_shell)
		{
			final_argv = login_shell_str;
			final_argv_need_be_free = FALSE;
			spawn_flags |= G_SPAWN_FILE_AND_ARGV_ZERO;
		}
	}
	else
	{

		spawn_flags |= G_SPAWN_FILE_AND_ARGV_ZERO;

		gchar *argv_str = convert_array_to_string(argv, SEPARATE_CHAR);
		gchar *final_argv_str = g_strdup_printf("%s%c%s", win_data->command, SEPARATE_CHAR, argv_str);
		final_argv = split_string(final_argv_str, SEPARATE_STR, -1);
		final_argv_need_be_free = TRUE;
#  ifdef SAFEMODE
		if (final_argv==NULL)
		{
			final_argv = full_argv;
			final_argv_need_be_free = FALSE;
		}
#  endif
		g_free(argv_str);
		g_free(final_argv_str);
	}

	gint pty_flags = VTE_PTY_DEFAULT;
	if (win_data->utmp)
		pty_flags = (VTE_PTY_NO_LASTLOG | VTE_PTY_NO_UTMP | VTE_PTY_NO_WTMP | VTE_PTY_DEFAULT);

	fork_stats = spawn_in_vte(page_data->vte,
				  pty_flags,
				  page_data->pwd,
				  final_argv,
				  new_environs,
				  spawn_flags,
				  &(page_data->pid),
				  &error);
	if (final_argv_need_be_free) g_strfreev(final_argv);
	login_shell_str[0] = NULL;
	if (argv_need_be_free) g_strfreev(argv);

	g_strfreev(new_environs);
	// treat '-e option' as `custom_page_name'
	if (win_data->argv || (win_data->custom_tab_names_total > win_data->custom_tab_names_current))
	{
		if (win_data->custom_tab_names_total > win_data->custom_tab_names_current)
		{
			page_data->custom_page_name = g_strdup(win_data->custom_tab_names_strs[win_data->custom_tab_names_current]);
			win_data->custom_tab_names_current++;
		}
		else if (win_data->page_shows_current_cmdline)
			page_data->custom_page_name = get_cmdline(page_data->pid);
		else
			page_data->custom_page_name = convert_array_to_string(win_data->argv, ' ');
	}

// ---	Create a page and init the page data ---- //

	// create label

	if (split_from)
	{
		// A pane shares the label of its tab, and the name of the tab.
		page_data->label = split_from->label;
		page_data->label_text = split_from->label_text;
		page_data->label_button = split_from->label_button;
		page_data->tab_color = split_from->tab_color;
		g_free(page_data->custom_page_name);
		page_data->custom_page_name = g_strdup(split_from->custom_page_name);
	}
	else
	{
	page_data->label=dirty_gtk_hbox_new(FALSE, 0);
	g_object_set_data(G_OBJECT(page_data->label), "VteBox", page_data->vte);
	set_widget_thickness(page_data->label, 0);

	page_data->label_text = gtk_label_new(win_data->page_name);
	gtk_box_pack_start(GTK_BOX(page_data->label), page_data->label_text, TRUE, TRUE, 0);
	set_page_width(win_data, page_data);
	gtk_label_set_ellipsize(GTK_LABEL(page_data->label_text), PANGO_ELLIPSIZE_MIDDLE);
	page_data->label_button = gtk_button_new();
	set_widget_thickness(page_data->label_button, 0);
	GtkWidget *image = gtk_image_new_from_stock(GTK_FAKE_STOCK_CLOSE, GTK_ICON_SIZE_MENU);
	gtk_container_add(GTK_CONTAINER(page_data->label_button), image);
	gtk_button_set_relief(GTK_BUTTON(page_data->label_button), GTK_RELIEF_NONE);
	gtk_button_set_focus_on_click(GTK_BUTTON(page_data->label_button), FALSE);

	// int w, h;

	gtk_box_pack_start(GTK_BOX(page_data->label), page_data->label_button, FALSE, FALSE, 0);
	g_signal_connect(G_OBJECT(page_data->label_button), "clicked", G_CALLBACK(close_page), GINT_TO_POINTER(CLOSE_WITH_TAB_CLOSE_BUTTON));
	gtk_widget_show_all(page_data->label);
	}
	// done in notebook_page_added()

	// create a hbox
	page_data->hbox = dirty_gtk_hbox_new(FALSE, 0);
#ifdef GEOMETRY
	g_signal_connect(G_OBJECT(page_data->hbox), "size-allocate",
			 G_CALLBACK(widget_size_allocate), "hbox");
#endif

	// Get current vte size. for init a new tab.
	glong column=SYSTEM_COLUMN, row=SYSTEM_ROW;
#ifdef SAFEMODE
	if (page_data_prev && (page_data_prev->vte))
#else
	if (page_data_prev)
#endif
	{
		column = vte_terminal_get_column_count(VTE_TERMINAL(page_data_prev->vte));
		row = vte_terminal_get_row_count(VTE_TERMINAL(page_data_prev->vte));
		if (split_from)
		{
			// Halve the size the two panes ask for, or the window grows to fit
			// two full sized terminals. One cell is left for the handle.
			if (split_orientation == GTK_ORIENTATION_HORIZONTAL)
				column = MAX((column - 1) / 2, 2);
			else
				row = MAX((row - 1) / 2, 1);
			vte_terminal_set_size(VTE_TERMINAL(split_from->vte), column, row);
		}
	}
	else
	{
		column = win_data->default_column;
		row = win_data->default_row;
	}

#ifdef GEOMETRY
	g_debug("@ add_page(): Init New vte with %ld x %ld!", column, row);
	g_debug("@ add_page(): Using the font : %s", page_data->font_name);
#endif

// ---- Init the new page ---- //

	// Init new page. run_once: some settings only need run once.
	// run_once only = TRUE when initing LilyTerm in main().
	init_new_page(win_data, page_data, column, row);

	page_data->scroll_bar = gtk_vscrollbar_new(gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(page_data->vte)));
	pack_vte_and_scroll_bar_to_hbox(win_data, page_data);
	show_and_hide_scroll_bar(page_data, check_show_or_hide_scroll_bar(win_data));
	gtk_widget_set_no_show_all(page_data->scroll_bar, TRUE);

#if defined(GEOMETRY)
	g_signal_connect(G_OBJECT(page_data->vte), "size-allocate",
			 G_CALLBACK(vte_size_allocate), page_data);
#endif
	g_signal_connect(G_OBJECT(page_data->vte), "decrease-font-size",
			 G_CALLBACK(vte_size_changed), GINT_TO_POINTER(FONT_ZOOM_IN));
	g_signal_connect(G_OBJECT(page_data->vte), "increase-font-size",
			 G_CALLBACK(vte_size_changed), GINT_TO_POINTER(FONT_ZOOM_OUT));
	// the close page event
	if (! (win_data->hold && win_data->command))
		page_data->child_exited_handler_id = g_signal_connect(G_OBJECT(page_data->vte), "child_exited", G_CALLBACK(close_page), GINT_TO_POINTER(CLOSE_TAB_NORMAL));

	// when get focus, update `current_vte', hints, and window title
	g_signal_connect(G_OBJECT(page_data->vte), "grab-focus", G_CALLBACK(vte_grab_focus), NULL);

	// show the menu
	g_signal_connect(G_OBJECT(page_data->vte), "button-press-event",
			 G_CALLBACK(vte_button_press), page_data);
	g_signal_connect(G_OBJECT(page_data->vte), "button-release-event",
			 G_CALLBACK(vte_button_release), page_data);

	add_remove_window_title_changed_signal(page_data);

//	GdkRGBA root_color;

// ---- Init other datas of the page ---- //

	// set the tab name.
	page_data->current_tpgid = page_data->pid;
	page_data->displayed_tpgid = 0;
	page_data->is_root = check_is_root(page_data->pid);
	page_data->is_bold = win_data->bold_current_page_name;

// ---- add the new page to notebook ---- //

	// Note that due to historical reasons,
	// GtkNotebook refuses to switch to a page unless the child widget is visible.
	// Therefore, it is recommended to show child widgets before adding them to a notebook.
	gtk_widget_show_all(page_data->hbox);

	if (split_from)
	{
		insert_split_pane(win_data, split_from, page_data, split_orientation);
		goto PAGE_IN_PLACE;
	}

	// The notebook page is a container of panes, so that a pane can be split later
	// without removing the page from the notebook.
	page_data->tab_box = dirty_gtk_vbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(page_data->tab_box), page_data->hbox, TRUE, TRUE, 0);
	gtk_widget_show(page_data->tab_box);

	// FIXME: dirty hack
	page_data->window = NULL;
#ifdef SAFEMODE
	if (page_data->notebook) {
#endif
		page_data->page_no = gtk_notebook_append_page(GTK_NOTEBOOK(page_data->notebook),
						      page_data->tab_box, page_data->label);

		// move the page to next to original page
		if (page_data_prev && add_to_next)
		{
			gtk_notebook_reorder_child(GTK_NOTEBOOK(page_data->notebook), page_data->tab_box,
								page_data_prev->page_no + 1);
		}
#ifdef SAFEMODE
	}
#endif
PAGE_IN_PLACE:
	win_data->current_vte = page_data->vte;

// ---- Monitor cmdline ---- //

	if ((proc_exist) &&
	    (win_data->page_shows_current_cmdline ||
	     win_data->page_shows_current_dir ||
	     win_data->page_shows_window_title))
	{
		add_remove_page_timeout_id(win_data, page_data);
	}
	else
	{
		win_data->page_names_no++;
	}

// ---- Restore the VTE_CJK_WIDTH environment ---- //

	restore_SYSTEM_VTE_CJK_WIDTH_STR();

// ---- Finish ---- //

	// page_data->pid < 0: Error occurred when creating sub process.
	// FIXME: how about "current_vte != page_data->vte"??
	if (fork_stats==0)
	{
		extern gchar *init_encoding;
		gchar *arg_str = NULL;
		if (win_data->argv) arg_str = convert_array_to_string(win_data->argv, ' ');

		if ((compare_strings(init_encoding, runtime_locale_encoding, FALSE)) ||
		    (compare_strings(init_encoding, "UTF-8", FALSE)))
			create_utf8_child_process_failed_dialog (win_data, arg_str, runtime_locale_encoding);
		else
#ifdef SAFEMODE
		{
			if (error)
#endif
				create_child_process_failed_dialog(win_data, error->message,
								   win_data->runtime_encoding);
#ifdef SAFEMODE
		}
		if (error)
#endif
			g_clear_error(&error);
		g_free(arg_str);
		clear_arg(win_data);
		close_page (page_data->vte, CLOSE_TAB_NORMAL);
		return NULL;
	}
	else
	{
		// We MUST clear command_line and parameters after ran -e option.
		clear_arg(win_data);
		// FIXME: Calling notebook_page_added() here
		return page_data;
	}
}

gchar **get_argv(struct Window *win_data, gboolean *argv_need_be_free)
{
#ifdef DETAIL
	g_debug("! Launch get_argv()! with win_data = %p", win_data);
#endif
#ifdef SAFEMODE
	if ((win_data==NULL) || (argv_need_be_free==NULL)) return NULL;
#endif
	gchar **argv = win_data->argv;
	*argv_need_be_free = FALSE;

#ifdef SAFEMODE
	if ((argv==NULL) && win_data->shell && (win_data->shell[0]!='\0'))
#else
	if ((argv==NULL) && (win_data->shell[0]!='\0'))
#endif
	{
		argv = split_string(win_data->shell, " ", -1);
		*argv_need_be_free = TRUE;
#ifdef SAFEMODE
	}

	if (argv==NULL)
	{
		argv = g_strsplit("/bin/sh", " ", -1);
		*argv_need_be_free = TRUE;
	}

	if (argv==NULL)
	{
		argv = login_shell_str;
		*argv_need_be_free = FALSE;
#endif
	}
	return argv;
}

void create_utf8_child_process_failed_dialog (struct Window *win_data, gchar *message, gchar *encoding)
{
#ifdef DETAIL
	g_debug("! Launch create_utf8_child_process_failed_dialog()! with win_data = %p, message = %s, "
		"encoding = %s", win_data, message, encoding);
#endif
	gchar *utf8_arg_str = convert_str_to_utf8(message, encoding);
	create_child_process_failed_dialog(win_data, utf8_arg_str, NULL);
	g_free(utf8_arg_str);
}

void create_child_process_failed_dialog(struct Window *win_data, gchar *message, gchar *encoding)
{
#ifdef DETAIL
	g_debug("! Launch create_child_process_failed_dialog() with message = %s, encoding = %s",
		message, encoding);
#endif
#ifdef SAFEMODE
	if (win_data==NULL) return;
#endif
	gint i;
	gchar *temp_str[3] = {NULL};
	temp_str[0] = convert_str_to_utf8(message, encoding);
	if (temp_str[0]!=NULL)
		temp_str[1] = g_markup_escape_text(temp_str[0], -1);
	if (temp_str[1])
		temp_str[2] = g_strconcat(_("Error while creating the child process:"),
					  "\n\n\t<b><span foreground=\"",
					  "blue",
					  "\">",
					  temp_str[1],
					  "</span></b>",
					  NULL);
	else
		temp_str[2] = _("Error while creating the child process!\n"
				"Please make sure the /dev/pts is mounted as a devpts file system!");
	// error_dialog(GtkWidget *window, gchar *title, gchar *icon, gchar *message, gchar *encoding)
	GtkWidget *window = NULL;
#ifdef SAFEMODE
	if (win_data)
#endif
		window = win_data->window;
	error_dialog(window,
		     _("Error when creating child process"),
		     "Error when creating child process",
		     GTK_FAKE_STOCK_DIALOG_ERROR,
		     temp_str[2],
		     NULL);
	if (temp_str[1]) g_free(temp_str[2]);
	for (i=0; i<2; i++)
		g_free(temp_str[i]);
}

void clear_arg(struct Window *win_data)
{
#ifdef DETAIL
	g_debug("! Launch clear_arg()");
#endif
#ifdef SAFEMODE
	if (win_data==NULL) return;
#endif

	win_data->command = NULL;
	win_data->hold = FALSE;
	win_data->argc = 0;
	win_data->argv = NULL;
}

#if defined(GEOMETRY)
void vte_size_allocate (GtkWidget *vte, GtkAllocation *allocation, struct Page *page_data)
{
#  ifdef DETAIL
	g_debug("! Launch vte_size_allocate() with vte = %p, page_data = %p", vte, page_data);
#  endif
#  ifdef SAFEMODE
	if ((page_data==NULL) || (page_data->window==NULL)) return;
#  endif
	struct Window *win_data = (struct Window *)g_object_get_data(G_OBJECT(page_data->window), "Win_Data");

	glong column = vte_terminal_get_column_count(VTE_TERMINAL(vte));
	glong row = vte_terminal_get_row_count(VTE_TERMINAL(vte));
	gboolean check_col_row = (column < 80) || (row < 24);

	if (win_data->hints_type==HINTS_NONE)
	{
		GtkBorder padding;
		get_vte_padding(vte, &padding);
		column = vte_terminal_get_char_width(VTE_TERMINAL(vte))*column + padding.left + padding.right;
		row = vte_terminal_get_char_height(VTE_TERMINAL(vte))*row + padding.top + padding.bottom;
	}
	check_col_row |= (column != win_data->geometry_width);
	check_col_row |= (row != win_data->geometry_height);

	if (((win_data->window_status!=WINDOW_NORMAL) && (win_data->window_status!=WINDOW_RESIZING_TO_NORMAL)) && check_col_row)
	{
		fprintf(stderr, "\033[1;31m!! vte_size_allocate(win_data %p)(vte %p): the allocated size is %d x %d (%ldx%ld) (saved: %ld x %ld)\033[0m\n",
			win_data, vte, allocation->width, allocation->height, column, row, win_data->geometry_width, win_data->geometry_height);
	}
	else
		widget_size_allocate (vte, allocation, "vte");
}
#endif

void vte_size_changed(VteTerminal *vte, Font_Set_Type type)
{
#ifdef DETAIL
	g_debug("! Launch vte_size_changed() with vte = %p, type = %d", vte, type);
#endif
	set_vte_font(NULL, type);
}

// close_type = confirm to exit foreground running command or not
// close_type > 0: Not using 'exit' or '<Ctrl><D>' to close this page.
gboolean close_page(GtkWidget *vte, gint close_type)
{
#ifdef DETAIL
	g_debug("! Launch close_page() with vte = %p, and close_type = %d, force_to_quit = %d",
		vte, close_type, force_to_quit);
#endif
#ifdef SAFEMODE
	if (vte==NULL) return FALSE;
#endif
	if (close_type==CLOSE_WITH_TAB_CLOSE_BUTTON)
		vte=(GtkWidget *)g_object_get_data(G_OBJECT(gtk_widget_get_parent(vte)), "VteBox");

	struct Page *page_data = (struct Page *)g_object_get_data(G_OBJECT(vte), "Page_Data");
#ifdef SAFEMODE
	if ((page_data==NULL) || (page_data->window==NULL)) return FALSE;
#endif

	struct Window *win_data = (struct Window *)g_object_get_data(G_OBJECT(page_data->window),
									     "Win_Data");
#ifdef SAFEMODE
	if (win_data==NULL) return FALSE;
#endif

	// The close button of a split tab closes every pane of it.
	if ((close_type==CLOSE_WITH_TAB_CLOSE_BUTTON) && page_is_split(page_data))
		return close_tab(page_data, CLOSE_WITH_KEY_STRIKE);

	if (win_data->confirm_to_kill_running_command == FALSE)
	{
		force_to_quit=TRUE;
		close_type=CLOSE_TAB_NORMAL;
	}

	GString *child_process_list = g_string_new(NULL);
	// close_type = TRUE: Not using 'exit' or '<Ctrl><D>' to close this page.
	if (close_type)
	{
		if (process_data==NULL)
		{
			process_data = g_new0(struct Process_Data, PID_MAX_DEFAULT);
#ifdef SAFEMODE
			if (process_data)
			{
#endif
				child_process_list = get_child_process_list(0, 0, 0, child_process_list,
									    page_data->pid, win_data, FALSE);
				clean_process_data();
#ifdef SAFEMODE
			}
#endif
		}
		else
		{
			gint i;
			for (i=2; i<PID_MAX_DEFAULT; i++)
			{
				if ((process_data[i].page_no == ((page_data->page_no)+1)) &&
				    ((process_data[i].window==0) || (process_data[i].window==page_data->window)))
				{
					gchar *cmdline = get_cmdline(i);
#ifdef SAFEMODE
					if (cmdline)
#endif
						g_string_append_printf(child_process_list ,"\t(%d) %s\n", i, cmdline);
					g_free(cmdline);
				}
			}
		}
	}

	// Confirm to close current running command...
	if ((! force_to_quit) && (page_data->pid>1) && (! win_data->kill_color_demo_vte))
	{
		pid_t tpgid = get_tpgid(page_data->pid);
		gchar **stats = get_pid_stat(tpgid, 4);
		if (stats)
		{
			if (! check_string_in_array(stats[2], win_data->running_process_whitelists))
			{

				// We need to set "win_data->current_vte = vte", or wrong page may be closed.
				GtkWidget *current_vte_orig = win_data->current_vte;
				win_data->current_vte = vte;

				GtkResponseType return_value = dialog(NULL, CONFIRM_TO_CLOSE_RUNNING_APPLICATION);
				// restore win_data->current_vte
				win_data->current_vte = current_vte_orig;

				if (return_value != GTK_RESPONSE_OK)
				{
					g_string_free(child_process_list, TRUE);
					g_strfreev(stats);
					return FALSE;
				}
			}
		}
		g_strfreev(stats);
	}

	// Confirm to close tab if it have child process...
#ifdef SAFEMODE
	if ((! force_to_quit) &&
	    (child_process_list && child_process_list->len) &&
	    (! win_data->kill_color_demo_vte))
#else
	if ((! force_to_quit) && child_process_list->len && (! win_data->kill_color_demo_vte))
#endif
	{
		// Switch win_data->current_vte here,
		// Or the "%d tab" of dialog will incorrect if we click the [X] on a tab that is not on focus.
		GtkWidget *oldvte = win_data->current_vte;
		win_data->current_vte = vte;
		if (!display_child_process_dialog(child_process_list, win_data,
						  CONFIRM_TO_CLOSE_A_TAB_WITH_CHILD_PROCESS))
		{
			g_string_free(child_process_list, TRUE);
			win_data->current_vte = oldvte;
			return FALSE;
		}
		// Switch back
		win_data->current_vte = oldvte;
	}

	if (win_data->auto_save &&
	    (gtk_notebook_get_n_pages(GTK_NOTEBOOK(win_data->notebook))==1))
		save_user_settings(NULL, win_data);

	// remove timeout event for page_shows_current_cmdline
	if (page_data->timeout_id)
		g_source_remove (page_data->timeout_id);
	if (page_data->urgent_bell_handler_id)
		g_signal_handler_disconnect(G_OBJECT(page_data->vte), page_data->urgent_bell_handler_id);
	if (page_data->child_exited_handler_id)
		g_signal_handler_disconnect(G_OBJECT(page_data->vte), page_data->child_exited_handler_id);

	// kill running shell
#ifdef SAFEMODE
	if (using_kill && child_process_list && child_process_list->len)
#else
	if (using_kill && child_process_list->len)
#endif
	{
		kill(page_data->pid, SIGKILL);
	}
	g_string_free(child_process_list, TRUE);

	// restore using_kill if only one page is closed.
	if (! force_to_quit)
		using_kill = FALSE;

	// One pane of a split tab: only that pane goes away, the tab stays.
	if (page_is_split(page_data))
	{
		remove_split_pane(win_data, page_data);
		g_free(page_data->page_name);
		g_free(page_data->pid_cmdline);
		g_free(page_data->custom_page_name);
		g_free(page_data->pwd);
		g_free(page_data->font_name);
		g_free(page_data->encoding_str);
		g_free(page_data->locale);
		g_free(page_data->VTE_CJK_WIDTH_STR);
		g_free(page_data->window_title_pwd);
		g_free(page_data->environ);
		g_free(page_data);
		return TRUE;
	}

	if (vte==win_data->current_vte)
	{
		win_data->current_vte = NULL;
		// set the current page
		if (page_data->page_no==(gtk_notebook_get_n_pages(GTK_NOTEBOOK(page_data->notebook))-1))
			gtk_notebook_set_current_page(GTK_NOTEBOOK(page_data->notebook), page_data->page_no-1);
		else
			gtk_notebook_set_current_page(GTK_NOTEBOOK(page_data->notebook), page_data->page_no+1);
	}

	g_free(page_data->page_name);

	// Note that due to historical reasons,
	// GtkNotebook refuses to switch to a page unless the child widget is visible.
	// Therefore, it is recommended to show child widgets before adding them to a notebook.
	gtk_widget_hide_all(page_data->hbox);

	// remove current page
	// use page_data->page_no. DANGEROUS!

	// Due to the bugs in GTK3+(?), Using gtk_widget_destroy(windows) instead of closing the last page!
	gint total_page = gtk_notebook_get_n_pages(GTK_NOTEBOOK(page_data->notebook));
	gboolean run_quit_gtk = FALSE;
	if (total_page==1)
	{
		run_quit_gtk = gtk_widget_get_mapped(win_data->window);
		clear_window(win_data);
	}
	else
		gtk_notebook_remove_page(GTK_NOTEBOOK(page_data->notebook), page_data->page_no);

	// free the memory used by this page
	g_free(page_data->pid_cmdline);
	g_free(page_data->custom_page_name);
	g_free(page_data->pwd);
	g_free(page_data->font_name);
	g_free(page_data->encoding_str);
	g_free(page_data->locale);
	g_free(page_data->VTE_CJK_WIDTH_STR);
	g_free(page_data->window_title_pwd);
	g_free(page_data->environ);

	// FIXME: Calling remove_notebook_page() here
	// GtkNotebook may drag a page to itself,
	// So a GtkNotebook may be page_removed() -> page_added() with the same page
	// but the GtkNotebook may be destroyed after page_removed()
	// So that we should call page_removed() here, not using "page-removed" signal... -_-|||
	if (total_page==1)
		remove_notebook_page (NULL, NULL, page_data->page_no, win_data, run_quit_gtk);
	else
		remove_notebook_page (GTK_NOTEBOOK(page_data->notebook), NULL, page_data->page_no, win_data, run_quit_gtk);

	g_free(page_data);

	return TRUE;
}

void vte_grab_focus(GtkWidget *vte, gpointer user_data)
{
#ifdef DETAIL
	g_debug("! Launch vte_grab_focus() with vte = %p", vte);
#endif
#ifdef SAFEMODE
	if (vte==NULL) return;
#endif
	struct Page *page_data = (struct Page *)g_object_get_data(G_OBJECT(vte), "Page_Data");
#ifdef SAFEMODE
	if (page_data==NULL) return;
#endif
	struct Window *win_data = (struct Window *)g_object_get_data(G_OBJECT(page_data->window), "Win_Data");
#ifdef SAFEMODE
	if (win_data==NULL) return;
#endif
	// restore match_regex first...
	if (! page_data->match_regex_setted) set_hyperlink(win_data, page_data);

	// Recover the dim text of vte
	dim_vte_text (win_data, page_data, 0);

	// The tab shows the pane that has the focus.
	gboolean pane_changed = FALSE;
	if (page_data->label && (g_object_get_data(G_OBJECT(page_data->label), "VteBox") != vte))
	{
		g_object_set_data(G_OBJECT(page_data->label), "VteBox", vte);
		pane_changed = TRUE;
	}

	// Don't update page name when win_data->kill_color_demo_vte.
	// Or LilyTerm will got warning: "Failed to set text from markup due to error parsing markup"
	if ((win_data->current_vte != vte || (win_data->current_vte == NULL)) && (! win_data->kill_color_demo_vte))
	{
		// win_data->current_vte == NULL: when the creation of sub process failed.
		// like 'lilyterm -e xxx' in a lilyterm
		if (win_data->current_vte != NULL)
		{
			struct Page *prev_data = (struct Page *)g_object_get_data(
								G_OBJECT(win_data->current_vte), "Page_Data");
#ifdef SAFEMODE
			if (prev_data)
			{
#endif
				if (!win_data->show_close_button_on_all_tabs)
					gtk_widget_hide (prev_data->label_button);

				if ((win_data->bold_current_page_name || win_data->bold_action_page_name))
				{
					prev_data->is_bold = FALSE;
					prev_data->should_be_bold = FALSE;
					update_page_name (prev_data->window, win_data->current_vte, prev_data->page_name,
							  prev_data->label_text, prev_data->page_no+1,
							  prev_data->custom_page_name,
							  prev_data->tab_color, prev_data->is_root, FALSE,
							  compare_strings(win_data->runtime_encoding,
									  prev_data->encoding_str,
									  FALSE),
							  prev_data->encoding_str, prev_data->custom_window_title,
							  FALSE);
				}
#ifdef SAFEMODE
			}
#endif
		}
		win_data->current_vte = vte;

		// We will not update the page name when close color demo vte

		//	// we should bind the hints information on current vte.
		//	// Or the geometry of vte may be changed when deleting the vte hold hints info.
		//	// It can help to hold the correct vte size.
		//	g_debug("Update hints!")

		// update the window title
		if (win_data->window_title_shows_current_page && (! page_data->custom_window_title))
			update_window_title(page_data->window, page_data->page_name,
					    win_data->window_title_append_package_name);

		show_close_button_on_tab(win_data, page_data);

		// bold/un_bold current page
		if (win_data->bold_current_page_name || win_data->bold_action_page_name)
		{
			page_data->is_bold = win_data->bold_current_page_name;
			//	g_debug("win_data->default_menuitem_encoding->name = %s, "
			//		"page_data->encoding_str = %s",
			update_page_name (page_data->window, vte, page_data->page_name, page_data->label_text,
					  page_data->page_no+1, page_data->custom_page_name, page_data->tab_color,
					  page_data->is_root, page_data->is_bold,
					  compare_strings(win_data->runtime_encoding,
							  page_data->encoding_str,
							  FALSE),
					  page_data->encoding_str, page_data->custom_window_title,
					  FALSE);
		}
		else if (pane_changed)
			update_page_name (page_data->window, vte, page_data->page_name, page_data->label_text,
					  page_data->page_no+1, page_data->custom_page_name, page_data->tab_color,
					  page_data->is_root, page_data->is_bold,
					  compare_strings(win_data->runtime_encoding,
							  page_data->encoding_str,
							  FALSE),
					  page_data->encoding_str, page_data->custom_window_title,
					  FALSE);
	}
}

// dim_text = 0: dim the vte
// dim_text = 1: light up the vte
// dim_text = 2: auto detect
// NOTE: page_data may be NULL here!
void dim_vte_text (struct Window *win_data, struct Page *page_data, gint dim_text)
{
#ifdef DETAIL
	g_debug("! Launch dim_vte_text() with win_data = %p, page_data = %p, dim_text = %d",
		win_data, page_data, dim_text);
#endif
#ifdef SAFEMODE
	if (win_data==NULL) return;
#endif
	// The page_data==NULL && win_data->current_vte==NULL when -e options failed.
	if (page_data==NULL && (win_data->current_vte==NULL)) return;

	// dim_vte = TRUE: dim the vte
	// dim_vte = FALSE: light up the vte
	gint dim_vte = FALSE;

	// if page_data==NULL, using "win_data->current_vte"
	if (! page_data)
		page_data = get_page_data_from_vte(win_data->current_vte, win_data, -1);

	if (dim_text==2)
	{
		if (win_data->dim_text)
			dim_vte = page_data->dim_text_expect;
		else
			dim_vte = FALSE;
	}
	else
	{
		page_data->dim_text_expect = FALSE;
		if (dim_text)
		{
			// Trying to check if we should dim the vte
			if ((! menu_activated) && (dialog_activated==0) && (win_data->lost_focus))
			{
				page_data->dim_text_expect = TRUE;
				if (win_data->dim_text)
					dim_vte = TRUE;
			}
		}
	}

	// FIXME: Sometimes we should to force light up the vte

#ifdef SAFEMODE
	if (page_data->vte==NULL) return;
#endif
	// for performance, if the vte was dimmed already, don't dim it again.
	if (page_data->vte_is_inactivated != dim_vte)
	{
		if (dim_vte)
			set_vte_color(page_data->vte, TRUE, win_data->custom_cursor_color, win_data->cursor_color,
				      win_data->color_inactive, TRUE, (win_data->color_theme_index==(THEME-1)));
		else
			set_vte_color(page_data->vte, TRUE, win_data->custom_cursor_color, win_data->cursor_color,
				      win_data->color, TRUE, (win_data->color_theme_index==(THEME-1)));
		page_data->vte_is_inactivated = dim_vte;
	}
}

gboolean vte_button_press(GtkWidget *vte, GdkEventButton *event, struct Page *page_data)
{
#ifdef DETAIL
	g_debug("! Launch vte_button_press() for vte %p (page_data = %p)", vte, page_data);
#endif
#ifdef SAFEMODE
	if ((page_data==NULL) || (event==NULL)) return FALSE;
#endif
	menu_active_window = page_data->window;
	struct Window *win_data = (struct Window *)g_object_get_data(G_OBJECT(page_data->window), "Win_Data");
#ifdef SAFEMODE
	if (win_data==NULL) return FALSE;
#endif
	if (! win_data->enable_key_binding) return FALSE;

	if (event->button == 2)
	{
		if (win_data->confirm_to_paste)
			return confirm_to_paste_form_clipboard(SELECTION_PRIMARY, win_data, page_data);
		else
			return FALSE;
	}
	else if (event->button == 3)
	{
		// The menu acts on the current pane: make it the one that was clicked.
		if (win_data->current_vte != vte) gtk_widget_grab_focus(vte);
		menu_activated = TRUE;
		// Find/Add the encoding of current tab on right click menu
		if (win_data->locale_sub_menu)
		{
			// We will not update coding here.
			win_data->checking_menu_item = TRUE;

			// menu is inited after add_page(). so that the encoding_str of 1st page is NULL.

			// Trying to find the right encoding menuitem...
			if (win_data->encoding_sub_menu)
			{
				GSList *menuitem_list = gtk_radio_menu_item_get_group(
								GTK_RADIO_MENU_ITEM(
									win_data->default_menuitem_encoding));
				while (menuitem_list)
				{
					if ( ! compare_win_page_encoding(menuitem_list->data,
									 page_data->encoding_str))
					{
						gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM(
											menuitem_list->data), TRUE);
						break;
					}
					// Not find in menu item list. Add it!
					if (menuitem_list->next == NULL)
					{
						GtkWidget *menuitem = add_radio_menuitem_to_sub_menu (
										    gtk_radio_menu_item_get_group (
											GTK_RADIO_MENU_ITEM (menuitem_list->data)),
										    win_data->encoding_sub_menu,
										    page_data->encoding_str,
										    G_CALLBACK(set_encoding),
										    NULL);
#ifdef SAFEMODE
						if (menuitem)
						{
#endif
							gtk_widget_show (menuitem);
							gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM(menuitem),
											TRUE);
#ifdef SAFEMODE
						}
#endif
					}
					menuitem_list = menuitem_list->next;
				}
			}
			// query done
			win_data->checking_menu_item = FALSE;
		}
		// Update the profile list
		refresh_profile_list(win_data);
		win_data->checking_menu_item = TRUE;
#ifdef SAFEMODE
		if (win_data->current_menuitem_theme)
#endif
			gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM(win_data->current_menuitem_theme), TRUE);
#ifdef SAFEMODE
		if (win_data->current_menuitem_erase_binding)
#endif
			gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM(win_data->current_menuitem_erase_binding), TRUE);
#  ifdef SAFEMODE
		if (win_data->current_menuitem_cursor_shape)
#  endif
			gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM(win_data->current_menuitem_cursor_shape), TRUE);
#ifdef SAFEMODE
		if (win_data->menuitem_invert_color)
#endif
			gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM(win_data->menuitem_invert_color), win_data->invert_color);
#  ifdef SAFEMODE
		if (win_data->menuitem_auto_save)
#  endif
			gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM(win_data->menuitem_auto_save), win_data->auto_save);
		win_data->checking_menu_item = FALSE;

		if (win_data->show_background_menu)
		{
			if (win_data->use_rgba == -1)
#  ifdef SAFEMODE
				if (win_data->menuitem_trans_win)
#  endif
					gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM(win_data->menuitem_trans_win),
									win_data->transparent_window);
			// The following codes is only working with vte > 0.19
			// gint transparent;
		}

		if (win_data->show_copy_paste_menu)
		{
			// enable win_data->menuitem_copy_url or not
			if ( win_data->enable_hyperlink)
			{
				gint tag;
				gchar *url = get_url(event, page_data, &tag);
				gtk_widget_set_name (win_data->menuitem_copy_url, url);
				gtk_widget_set_sensitive (win_data->menuitem_copy_url, url!=NULL);
				g_free(url);
			}

			// enable win_data->menuitem_copy or not
			gtk_widget_set_sensitive (win_data->menuitem_copy,
						  vte_terminal_get_has_selection (VTE_TERMINAL(vte)));

			// enable win_data->menuitem_paste or not
			extern GtkClipboard *selection_clipboard;
			gchar *temp_str = NULL;
#ifdef SAFEMODE
			if (selection_clipboard)
#endif
				temp_str = gtk_clipboard_wait_for_text (selection_clipboard);
			gtk_widget_set_sensitive (win_data->menuitem_paste,
						  (temp_str != NULL));
			gtk_widget_set_sensitive (win_data->menuitem_clipboard,
						  (temp_str != NULL));

			extern GtkClipboard *selection_primary;
#ifdef SAFEMODE
			if (selection_primary)
#endif
				temp_str = gtk_clipboard_wait_for_text (selection_primary);
			gtk_widget_set_sensitive (win_data->menuitem_primary,
						  (temp_str != NULL));
			g_free(temp_str);
		}

		if (win_data->dim_text)
#ifdef SAFEMODE
			if (win_data->menuitem_dim_text)
#endif
				gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM(win_data->menuitem_dim_text),
								win_data->dim_text);
		if ((win_data->dim_window) && (win_data->use_rgba == -1))
#  ifdef SAFEMODE
			if (win_data->menuitem_dim_window)
#  endif
				gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM(win_data->menuitem_dim_window),
								win_data->dim_window);
		gboolean blinks, blinks_original = win_data->cursor_blinks;
		if (win_data->cursor_blinks ==0)
			g_object_get(gtk_widget_get_settings(vte), "gtk-cursor-blink", &blinks, NULL);
		else
			blinks = (win_data->cursor_blinks==1) ? TRUE : FALSE;
		gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM(win_data->menuitem_cursor_blinks), blinks);
		win_data->cursor_blinks = blinks_original;
		gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM(win_data->menuitem_allow_bold_text),
						win_data->allow_bold_text);

		gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM(win_data->menuitem_open_url_with_ctrl_pressed),
						win_data->open_url_with_ctrl_pressed);

		gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM(win_data->menuitem_disable_url_when_ctrl_pressed),
						win_data->disable_url_when_ctrl_pressed);

		gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM(win_data->menuitem_audible_bell),
						win_data->audible_bell);
		gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM(win_data->menuitem_urgent_bell),
						win_data->urgent_bell);
		if (gtk_notebook_get_show_tabs GTK_NOTEBOOK(win_data->notebook))
		{
			gtk_widget_hide (win_data->menuitem_show_tabs_bar);
			gtk_widget_show (win_data->menuitem_hide_tabs_bar);
			gtk_widget_hide (win_data->menuitem_always_show_tabs_bar);
			gtk_widget_show (win_data->menuitem_always_hide_tabs_bar);
		}
		else
		{
			gtk_widget_show (win_data->menuitem_show_tabs_bar);
			gtk_widget_hide (win_data->menuitem_hide_tabs_bar);
			gtk_widget_show (win_data->menuitem_always_show_tabs_bar);
			gtk_widget_hide (win_data->menuitem_always_hide_tabs_bar);
		}

		// gtk_menu_popup() is deprecated since gtk+ 3.22. And only with the
		// triggering event at hand can a Wayland compositor place the menu.
		gtk_menu_popup_at_pointer(GTK_MENU(win_data->menu), (GdkEvent *)event);
		return TRUE;
	}
	else if (event->button == 1)
	{
		// return if hyperlink is disabled.
		if (! win_data->enable_hyperlink) return FALSE;

#ifdef __APPLE__
		// Cmd click, as in the other terminals on macOS, or Ctrl click
		if (win_data->open_url_with_ctrl_pressed && ((event->state & (GDK_CONTROL_MASK | COMMAND_MASK))==0))
#else
		if (win_data->open_url_with_ctrl_pressed && ((event->state & GDK_CONTROL_MASK)==0))
#endif
		{
			// clean the url first...
			clean_hyperlink(win_data, page_data);
			return FALSE;
		}

		gint tag;
		gchar *url = get_url(event, page_data, &tag);

		// url = NULL -> we don't need to free it.
		gboolean response = FALSE;
		if (url != NULL)
			response = open_url_with_external_command (url, tag, win_data, page_data);
		g_free(url);
		return response;
	}
	return FALSE;
}

gboolean vte_button_release(GtkWidget *vte, GdkEventButton *event, struct Page *page_data)
{
#ifdef DETAIL
	g_debug("! Launch vte_button_release() for vte %p", vte);
#endif
#ifdef SAFEMODE
	if ((page_data==NULL) || (event==NULL)) return FALSE;
#endif

	if (page_data->match_regex_setted == FALSE)
	{
		struct Window *win_data = (struct Window *)g_object_get_data(G_OBJECT(page_data->window), "Win_Data");
#ifdef SAFEMODE
		if (win_data==NULL) return FALSE;
#endif
		if (! win_data->enable_hyperlink) return FALSE;
		set_hyperlink(win_data, page_data);
	}

	return FALSE;
}

gboolean open_url_with_external_command (gchar *url, gint tag, struct Window *win_data, struct Page *page_data)
{
#ifdef DETAIL
	g_debug("! Launch open_url_with_external_command() with url = %s, tag = %d, win_data = %p, page_data = %p",
		url, tag, win_data, page_data);
#endif
#ifdef SAFEMODE
	if ((win_data==NULL) || (page_data==NULL)) return FALSE;
#endif

	gchar *full_command = g_strdup_printf("%s\t%s", win_data->user_command[tag].command, url);
	gchar **argv = split_string(full_command, "\t", -1);
#ifdef SAFEMODE
	if (argv==NULL)
	{
		g_free(full_command);
		return FALSE;
	}
#endif
	gint argc = 0;
	while (argv[argc])
		argc++;

	switch (win_data->user_command[tag].method)
	{
		case 0:
		{
			// 0: new tab
			win_data->command = argv[0];
			win_data->argv = argv;
			win_data->argc = argc;
			gchar *locale = page_data->locale;
#ifdef SAFEMODE
			if (win_data->user_command[tag].locale &&
			    (win_data->user_command[tag].locale[0] != '\0'))
#else
			if (win_data->user_command[tag].locale[0] != '\0')
#endif
				locale = win_data->user_command[tag].locale;

			gchar *encoding = get_encoding_from_locale(locale);
			if (encoding == NULL)
			{
				if (check_and_add_locale_to_warned_locale_list(win_data,
					win_data->user_command[tag].locale))
					create_invalid_locale_error_message(locale);
				locale = page_data->locale;
				encoding = g_strdup(page_data->encoding_str);
			}

			// g_debug("win_data->user_command[tag].VTE_CJK_WIDTH = %d, "
			//	"get_VTE_CJK_WIDTH_str(win_data->user_command[tag].VTE_CJK_WIDTH) = %s",
			//	win_data->user_command[tag].VTE_CJK_WIDTH ,

			// add_page(struct Window *win_data,
			//	    struct Page *page_data_prev,
			//	    GtkWidget *menuitem_encoding,
			//	    gchar *encoding,
			//	    gchar *locale,
			//	    gchar *environments,
			//	    gchar *user_environ,
			//	    gchar *VTE_CJK_WIDTH_STR,
			//	    gboolean add_to_next)
			add_page(win_data,
				 page_data,
				 NULL,
				 encoding,
				 page_data->encoding_str,
				 locale,
				 NULL,
				 win_data->user_command[tag].environ,
				 get_VTE_CJK_WIDTH_str(win_data->user_command[tag].VTE_CJK_WIDTH),
				 win_data->new_tab_next_to_current);
			g_free(encoding);
			break;
		}
		case 1:
		{
			// FIXME: use vfork()/execvp() may cause zombie process.
			// But we can't get if the child process is succeed or not

			set_VTE_CJK_WIDTH_environ(win_data->user_command[tag].VTE_CJK_WIDTH);
			GString *environ_str = g_string_new(win_data->environment);
			if (win_data->user_command[tag].environ &&
			    win_data->user_command[tag].environ[0] != '\0')
				g_string_append_printf (environ_str,
							"\t%s",
							win_data->user_command[tag].environ);
#ifdef SAFEMODE
			if (win_data->user_command[tag].locale &&
			    win_data->user_command[tag].locale[0] != '\0')
#else
			if (win_data->user_command[tag].locale[0]!='\0')
#endif
			{
				gchar *lang = get_lang_str_from_locale(win_data->user_command[tag].locale, ".");
				gchar *language = get_language_str_from_locales(win_data->user_command[tag].locale, win_data->default_locale);
				g_string_append_printf (environ_str,
							"\tLANG=%s\tLANGUAGE=%s\tLC_ALL=%s",
							lang,
							language,
							win_data->user_command[tag].locale);
				g_free(language);
				g_free(lang);
			}
			gchar **new_environs = NULL;
#ifdef SAFEMODE
			if (environ_str && environ_str->len)
#else
			if (environ_str->len)
#endif
				new_environs = sanitize_environs(split_string(environ_str->str, "\t", -1));

			// gboolean g_spawn_async_with_pipes (const gchar *working_directory,
			//				      gchar **argv,
			//				      gchar **envp,
			//				      GSpawnFlags flags,
			//				      GSpawnChildSetupFunc child_setup,
			//				      gpointer user_data,
			//				      GPid *child_pid,
			//				      gint *standard_input,
			//				      gint *standard_output,
			//				      gint *standard_error,
			//				      GError **error);
			if (g_spawn_async_with_pipes (NULL,
						      argv,
						      new_environs,
						      G_SPAWN_SEARCH_PATH,
						      NULL,
						      NULL,
						      NULL,
						      NULL,
						      NULL,
						      NULL,
						      NULL) == FALSE)
			{
				gint i=0;
				while (full_command[i])
				{
					if (full_command[i]=='\t')
						full_command[i]=' ';
					i++;
				}
				create_child_process_failed_dialog(win_data,
									  full_command,
									  NULL);
			}
			restore_SYSTEM_VTE_CJK_WIDTH_STR();
			g_string_free(environ_str, TRUE);
			g_strfreev(new_environs);
			break;
		}
		case 2:
		{
			confirm_to_execute = FALSE;
			gchar *new_command = g_strdup_printf("-u\t%s\t-e\t%s",
							     win_data->profile,
							     full_command);
			gchar **new_argv = split_string(new_command, "\t", -1);
			gchar *locale = win_data->default_locale;
#ifdef SAFEMODE
			if (win_data->user_command[tag].locale &&
			    (win_data->user_command[tag].locale[0]!='\0'))
#else
			if (win_data->user_command[tag].locale[0]!='\0')
#endif
				locale = win_data->user_command[tag].locale;
			gchar *encoding = (gchar *)get_encoding_from_locale(locale);
			if (encoding == NULL)
			{
				if (check_and_add_locale_to_warned_locale_list(win_data, locale))
					create_invalid_locale_error_message(locale);
				locale = page_data->locale;
				encoding = g_strdup(page_data->encoding_str);
			}

			gchar *lang = get_lang_str_from_locale(locale, ".");
			gchar *language = get_language_str_from_locales(locale, win_data->default_locale);
			gchar *new_environs = g_strdup_printf("%s\tLANG=%s\tLANGUAGE=%s\tLC_ALL=%s",
							      win_data->user_command[tag].environ,
							      lang, language, locale);
			g_free(lang);
			g_free(language);
			gchar *pwd = get_init_dir(get_tpgid(page_data->pid), page_data->pwd, win_data->home);
			//GtkNotebook *new_window(int argc,
			//			char *argv[],
			//			gchar *shell,
			//			gchar *environment,
			//			gchar *locale_list,
			//			gchar *PWD,
			//			gchar *HOME,
			//			gchar *VTE_CJK_WIDTH_STR,
			//			gboolean VTE_CJK_WIDTH_STR_overwrite_profile,
			//			gchar *wmclass_name,
			//			gchar *wmclass_class,
			//			gchar *user_environ,
			//			gchar *encoding,
			//			gboolean encoding_overwrite_profile,
			//			gchar *lc_messages,
			//			struct Window *win_data_orig,
			//			struct Page *page_data_orig)
			new_window(argc+3,
				   new_argv,
				   win_data->shell,
				   win_data->environment,
				   win_data->runtime_locale_list,
				   pwd,
				   win_data->home,
				   get_VTE_CJK_WIDTH_str(win_data->user_command[tag].VTE_CJK_WIDTH),
				   TRUE,
				   win_data->wmclass_name,
				   win_data->wmclass_class,
				   new_environs,
				   encoding,
				   TRUE,
				   locale,
				   NULL,
				   NULL);
			g_free(new_environs);
			g_free(encoding);
			g_free (new_command);
			g_strfreev(new_argv);
			g_free(pwd);
			confirm_to_execute = TRUE;
			break;
		}
		default:
#ifdef FATAL
			print_switch_out_of_range_error_dialog("vte_button_press",
							       "win_data->user_command[tag].method",
							       win_data->user_command[tag].method);
#endif
			break;
	}
	g_strfreev(argv);
	g_free(full_command);
	setlocale(LC_CTYPE, init_LC_CTYPE);
	return TRUE;
}

gboolean compare_win_page_encoding(GtkWidget *menu_item_encoding, gchar *encoding_str)
{
#ifdef DETAIL
	g_debug("! Launch compare_win_page_encoding() with menu_item_encoding = %p, encoding_str = %s",
		 menu_item_encoding, encoding_str);
#endif
	// FIXME: Should we return TRUE or FALSE here?
	if (menu_item_encoding==NULL || encoding_str==NULL)
		return FALSE;

	return compare_strings(gtk_widget_get_name (menu_item_encoding), encoding_str, FALSE);
}

// The returned string should be freed when no longer needed.
gchar *get_url(GdkEventButton *event, struct Page *page_data, gint *tag)
{
#ifdef DETAIL
	if (tag)
		g_debug("! Launch get_url() with page_data = %p, tag = %d", page_data, *tag);
	else
		g_debug("! Launch get_url() with page_data = %p", page_data);
#endif
#ifdef SAFEMODE
	if ((page_data==NULL) || (event==NULL)) return NULL;
#endif
	gint vte_tag = -1;
	gchar *url = vte_terminal_match_check_event (VTE_TERMINAL(page_data->vte), (GdkEvent*)event, &vte_tag);
	if (url==NULL) return NULL;

	// VTE numbers the regexes of a terminal on, also after vte_terminal_match_remove_all():
	// find which command (TAG_WWW, ...) the matched regex belongs to.
	gint i;
	for (i=0; i<COMMAND; i++)
	{
		if (page_data->tag[i]==vte_tag)
		{
			if (tag) *tag = i;
			return url;
		}
	}
	g_free(url);
	return NULL;
}

void page_data_dup(struct Page *page_data_prev, struct Page *page_data)
{
#ifdef DETAIL
	g_debug("! Launch page_data_dup() with page_data_prev = %p, page_data = %p", page_data_prev, page_data);
#endif
#ifdef SAFEMODE
	if ((page_data==NULL) || (page_data_prev==NULL)) return;
#endif
	memcpy( page_data, page_data_prev, sizeof(* page_data_prev));

	// page_data->window;
	// page_data->notebook;
	page_data->page_no = 0;

	page_data->label = NULL;
	page_data->label_text = NULL;
	page_data->label_button = NULL;
	page_data->hbox = NULL;
	page_data->tab_box = NULL;
	page_data->vte = NULL;
	page_data->scroll_bar = NULL;

	page_data->encoding_str = g_strdup(page_data_prev->encoding_str);
	page_data->locale = g_strdup(page_data_prev->locale);
	page_data->environ = g_strdup(page_data_prev->environ);
	page_data->VTE_CJK_WIDTH_STR = g_strdup(page_data_prev->VTE_CJK_WIDTH_STR);

	page_data->page_name = NULL;
	page_data->pid = -1;
	page_data->pid_cmdline = NULL;
	page_data->current_tpgid = 0;
	page_data->displayed_tpgid = 0;
	page_data->page_name = NULL;
	page_data->custom_page_name = NULL;
	page_data->tab_color = NULL;

	page_data->pwd = NULL;
	page_data->is_root = 0;
	page_data->is_bold = 0;

	page_data->window_title_updated = -1;
	page_data->page_update_method = PAGE_METHOD_AUTOMATIC;
	page_data->window_title_signal = 0;

	page_data->timeout_id = 0;
	page_data->urgent_bell_handler_id = 0;
	page_data->child_exited_handler_id = 0;

	page_data->font_name = g_strdup(page_data_prev->font_name);

	// page_data->check_root_privileges;
	// page_data->page_shows_window_title;
	// page_data->page_shows_current_dir;
	// page_data->page_shows_current_cmdline;
	// page_data->bold_action_page_name;
	// page_data->window_title_tpgid;
	// page_data->lost_focus;
	// page_data->keep_vte_size;
	// page_data->current_vte;
	page_data->window_title_pwd = NULL;
	// page_data->custom_window_title;

	gint i;
	for (i=0; i<COMMAND; i++)
		page_data->tag[i] = 0;
}

struct Page *get_page_data_from_nth_page(struct Window *win_data, guint page_no)
{
#ifdef DETAIL
	g_debug("! Launch get_page_data_from_nth_page() with win_data = %p, page_no = %d", win_data, page_no);
#endif
#ifdef SAFEMODE
	if ((win_data==NULL) || (win_data->notebook==NULL)) return NULL;
#endif
	GtkWidget *vte = (GtkWidget *)g_object_get_data(G_OBJECT(gtk_notebook_get_tab_label(
					GTK_NOTEBOOK(win_data->notebook),
						gtk_notebook_get_nth_page(
							GTK_NOTEBOOK(win_data->notebook),
									 page_no))),
							"VteBox");
	return (struct Page *)g_object_get_data(G_OBJECT(vte), "Page_Data");
}

// gtk_container_foreach() callback: add the page data of every vte found under widget.
static void collect_pages(GtkWidget *widget, gpointer pages)
{
	if (VTE_IS_TERMINAL(widget))
	{
		struct Page *page_data = (struct Page *)g_object_get_data(G_OBJECT(widget), "Page_Data");
		if (page_data) g_ptr_array_add((GPtrArray *)pages, page_data);
	}
	else if (GTK_IS_CONTAINER(widget))
		gtk_container_foreach(GTK_CONTAINER(widget), collect_pages, pages);
}

// All the panes (vte terminals) of the tab that page_data is in, in the order they are shown.
// Free it with g_ptr_array_unref().
GPtrArray *get_pages_in_tab(struct Page *page_data)
{
#ifdef DETAIL
	g_debug("! Launch get_pages_in_tab() with page_data = %p", page_data);
#endif
	GPtrArray *pages = g_ptr_array_new();
#ifdef SAFEMODE
	if (page_data==NULL) return pages;
#endif
	if (page_data->tab_box)
		collect_pages(page_data->tab_box, pages);
	else
		g_ptr_array_add(pages, page_data);
	return pages;
}

// All the panes (vte terminals) of a window: every pane of every tab, tab by tab.
// Use it instead of looping over the notebook pages whenever something is about
// the terminals, not about the tabs. Free it with g_ptr_array_unref().
GPtrArray *get_pages_in_window(struct Window *win_data)
{
#ifdef DETAIL
	g_debug("! Launch get_pages_in_window() with win_data = %p", win_data);
#endif
	GPtrArray *pages = g_ptr_array_new();
#ifdef SAFEMODE
	if ((win_data==NULL) || (win_data->notebook==NULL)) return pages;
#endif
	gint i;
	for (i=0; i<gtk_notebook_get_n_pages(GTK_NOTEBOOK(win_data->notebook)); i++)
		collect_pages(gtk_notebook_get_nth_page(GTK_NOTEBOOK(win_data->notebook), i), pages);
	return pages;
}

struct Page *get_page_data_from_vte(GtkWidget *vte, struct Window *win_data, gint page_no)
{
#ifdef DETAIL
	g_debug("! Launch get_page_data_from_vte() with vte = %p, win_data = %p, page_no = %d", vte, win_data, page_no);
#endif
#ifdef SAFEMODE
	if ((win_data==NULL) || (win_data->notebook==NULL)) return NULL;
#endif
	struct Page *page_data = NULL;
	if (vte) page_data = (struct Page *)g_object_get_data(G_OBJECT(vte), "Page_Data");

	if (page_data==NULL)
	{
		if (page_no < 0)
			page_no = gtk_notebook_get_current_page(GTK_NOTEBOOK(win_data->notebook));

		page_data = get_page_data_from_nth_page(win_data, page_no);
#ifdef SAFEMODE
		if (page_data==NULL) return NULL;
#endif
		win_data->current_vte = page_data->vte;
	}

	return page_data;
}

// Get LANG str from old and new locale data
gchar *get_language_str_from_locales(const gchar *new_locale, const gchar *old_locale)
{
#ifdef DETAIL
	g_debug("! Launch get_language_str_from_locales() with old_locale = %s, new_locale = %s, init_LANGUAGE = %s",
		old_locale, new_locale, init_LANGUAGE);
#endif
	gint i, j;
	const gchar *locale_list[4] = {new_locale, old_locale, init_LANGUAGE, "en"};
	GString *language_list = g_string_new(":");

	// Join the locales into "zh_TW.UTF-8:ja_JP.UTF8:zh_CN:en"
	for (i=0; i<4; i++)
	{
		if (locale_list[i])
		{
			g_string_append_printf(language_list, "%s:", locale_list[i]);
		}
	}

	// Convert "zh_TW.UTF-8:ja_JP.UTF8:zh_CN:en" into "zh_TW:zh:ja_JP:ja:zh_CN:zh:en"
	GString *final_lang_list = g_string_new(":");
	gchar **lang_lists = split_string(language_list->str, ":", -1);
	gchar *new_lang[2];
	i=-1;
	while (lang_lists[++i])
	{
		new_lang[0] = get_lang_str_from_locale(lang_lists[i], ".");
		if (new_lang[0])
		{
			g_string_append_printf(final_lang_list, "%s:", new_lang[0]);
			new_lang[1] = get_lang_str_from_locale(new_lang[0], "_");
			if (new_lang[1])
			{
				g_string_append_printf(final_lang_list, "%s:", new_lang[1]);
				g_free(new_lang[1]);
			}
			g_free(new_lang[0]);
		}
	}
	g_strfreev(lang_lists);
	g_string_free(language_list, TRUE);

	// Join "zh_TW:zh:ja_JP:ja:zh_CN:zh:en" into "zh_TW:zh:ja_JP:ja:zh_CN:en"
	GString *join_lang_list = g_string_new(NULL);
	lang_lists = split_string(final_lang_list->str, ":", -1);
	i=-1;
	while (lang_lists[++i])
	{
		gboolean different_strings = TRUE;
		if (lang_lists[i][0] == '\0')
			different_strings = FALSE;
		else
		{
			for (j=0; j<i; j++)
			{
				different_strings = compare_strings(lang_lists[i], lang_lists[j], TRUE);
				if (different_strings==FALSE) break;
			}
		}
		if (different_strings)
		{
			if (join_lang_list->str[0] != '\0')
				g_string_append_printf(join_lang_list, ":%s", lang_lists[i]);
			else
				g_string_append_printf(join_lang_list, "%s", lang_lists[i]);
		}
	}
	g_strfreev(lang_lists);
	g_string_free(final_lang_list, TRUE);

	return g_string_free(join_lang_list, FALSE);
}

// convert zh_TW.UTF-8 -> zh_TW
// See https://www.gnu.org/software/gettext/manual/html_node/The-LANGUAGE-variable.html for more details.
gchar *get_lang_str_from_locale(const gchar *locale, const gchar *split)
{
#ifdef DETAIL
	g_debug("! Launch get_lang_str_from_locale() with locale = %s", locale);
#endif
#ifdef SAFEMODE
	if (locale==NULL) return NULL;
#endif
	gchar *language = NULL;
	gchar **split_locales = split_string(locale, split, 2);

	if (split_locales)
		language=g_strdup(split_locales[0]);
	else
		language=g_strdup(locale);

	g_strfreev(split_locales);
	return language;
}

// The child: the pty is its controlling terminal, and it starts with the
// default signal handling (GLib and GTK+ may block or ignore some).
static void spawn_child_setup(gpointer pty)
{
	vte_pty_child_setup(VTE_PTY(pty));

	sigset_t set;
	sigemptyset(&set);
	sigprocmask(SIG_SETMASK, &set, NULL);
	gint sig;
	for (sig = 1; sig < NSIG; sig++)
		if ((sig != SIGKILL) && (sig != SIGSTOP))
			signal(sig, SIG_DFL);
}

// vte_terminal_spawn_sync() is deprecated (vte 0.48). This does the same, and
// still knows the pid at once (vte_terminal_spawn_async() only tells it later):
// a pty of the size of the terminal, the command spawned on it, and VTE
// watching it. Like VTE, it sets TERM, COLORTERM and VTE_VERSION.
static gboolean spawn_in_vte(GtkWidget *vte, VtePtyFlags pty_flags, const gchar *working_directory,
			     gchar **argv, gchar **envv, GSpawnFlags spawn_flags, GPid *child_pid, GError **error)
{
#ifdef DETAIL
	g_debug("! Launch spawn_in_vte() with vte = %p, working_directory = %s", vte, working_directory);
#endif
	VtePty *pty = vte_terminal_pty_new_sync(VTE_TERMINAL(vte), pty_flags, NULL, error);
	if (pty==NULL) return FALSE;

	gchar **env = g_strdupv(envv);
	env = g_environ_setenv(env, "TERM", "xterm-256color", TRUE);
	env = g_environ_setenv(env, "COLORTERM", "truecolor", TRUE);
	gchar *vte_version = g_strdup_printf("%u", vte_get_major_version() * 10000 +
						   vte_get_minor_version() * 100 +
						   vte_get_micro_version());
	env = g_environ_setenv(env, "VTE_VERSION", vte_version, TRUE);
	g_free(vte_version);
	env = g_environ_unsetenv(env, "COLUMNS");
	env = g_environ_unsetenv(env, "LINES");

	gboolean spawned = g_spawn_async(working_directory, argv, env,
					 spawn_flags | G_SPAWN_DO_NOT_REAP_CHILD,
					 spawn_child_setup, pty, child_pid, error);
	g_strfreev(env);
	if (spawned)
	{
		vte_terminal_set_pty(VTE_TERMINAL(vte), pty);
		vte_terminal_watch_child(VTE_TERMINAL(vte), *child_pid);
	}
	g_object_unref(pty);
	return spawned;
}

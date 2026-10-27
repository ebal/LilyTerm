/*
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

// The search bar: a small bar over the top right corner of the window, which
// finds text in the scrollback of the current terminal as you type.
//
//   Enter, Up		the match before (older)
//   Shift Enter, Down	the match after (newer)
//   Esc			close the bar, back to the terminal
//
// It floats over the terminal (a GtkOverlay), so the window keeps its size in
// characters. It is made the first time it is shown: a window that never
// searches pays nothing for it.

#include "search.h"

static void search_bar_create(struct Window *win_data);
static gboolean search_entry_key_press(GtkWidget *entry, GdkEventKey *event, struct Window *win_data);
static void search_entry_changed(GtkWidget *entry, struct Window *win_data);
static void search_option_toggled(GtkWidget *button, struct Window *win_data);
static void search_button_clicked(GtkWidget *button, struct Window *win_data);
static gboolean search_set_regex(struct Window *win_data, GtkWidget *vte);
static void search_show_status(struct Window *win_data, const gchar *status);

// Put the notebook of WIN_DATA in the overlay that will hold the search bar
GtkWidget *search_overlay_new(struct Window *win_data)
{
#ifdef DETAIL
	g_debug("! Launch search_overlay_new() with win_data = %p", win_data);
#endif
	win_data->overlay = gtk_overlay_new();
	gtk_container_add(GTK_CONTAINER(win_data->overlay), win_data->notebook);
	return win_data->overlay;
}

// Show the search bar and put the focus in it. It starts with the first line
// of the selection, or else with the text of the last search.
void search_bar_show(struct Window *win_data)
{
#ifdef DETAIL
	g_debug("! Launch search_bar_show() with win_data = %p", win_data);
#endif
#ifdef SAFEMODE
	if ((win_data==NULL) || (win_data->overlay==NULL) || (win_data->current_vte==NULL)) return;
#endif
	if (win_data->search_bar==NULL) search_bar_create(win_data);

	gchar *selection = NULL;
	if (vte_terminal_get_has_selection(VTE_TERMINAL(win_data->current_vte)))
	{
#if VTE_CHECK_VERSION(0,76,0)
		selection = vte_terminal_get_text_selected(VTE_TERMINAL(win_data->current_vte), VTE_FORMAT_TEXT);
#else
		extern GtkClipboard *selection_primary;
		if (selection_primary) selection = gtk_clipboard_wait_for_text(selection_primary);
#endif
	}
	// NULL for an empty selection, or one of line breaks only
	gchar *first_line = selection ? strtok(selection, "\n\r") : NULL;
	if (first_line)
		gtk_entry_set_text(GTK_ENTRY(win_data->search_entry), first_line);
	else if (win_data->find_string)
		gtk_entry_set_text(GTK_ENTRY(win_data->search_entry), win_data->find_string);
	g_free(selection);

	gtk_widget_show_all(win_data->search_bar);
	search_show_status(win_data, NULL);
	gtk_widget_grab_focus(win_data->search_entry);
}

// Close the search bar, and give the focus back to the terminal. The match
// stays selected.
void search_bar_hide(struct Window *win_data)
{
#ifdef DETAIL
	g_debug("! Launch search_bar_hide() with win_data = %p", win_data);
#endif
#ifdef SAFEMODE
	if (win_data==NULL) return;
#endif
	if (win_data->search_bar) gtk_widget_hide(win_data->search_bar);
	if (win_data->current_vte) gtk_widget_grab_focus(win_data->current_vte);
}

// TRUE if the focus is in the search bar: the keys of text editing are for
// it then, not for the key bindings of the terminal.
gboolean search_bar_has_focus(struct Window *win_data)
{
#ifdef SAFEMODE
	if (win_data==NULL) return FALSE;
#endif
	return win_data->search_entry && gtk_widget_has_focus(win_data->search_entry);
}

// TRUE if the key binding BINDING is for the entry of the search bar while it
// has the focus: copy, paste and select all (Cmd C / Cmd V / Cmd A on macOS,
// Ctrl Delete / Ctrl Insert / Shift Insert / Ctrl O here), and Ctrl Home /
// Ctrl End, which move to the start and the end of the text there.
gboolean search_bar_key(gint binding)
{
	switch (binding)
	{
		case KEY_SELECT_ALL:
		case KEY_COPY_CLIPBOARD:
		case KEY_PASTE_CLIPBOARD:
		case KEY_COPY_PRIMARY:
		case KEY_PASTE_PRIMARY:
		case KEY_FIRST_TAB:
		case KEY_LAST_TAB:
			return TRUE;
	}
	return FALSE;
}

// Find the text of the last search in the current terminal, before (FIND_PREV)
// or after (FIND_NEXT) the current match, starting over at the other end.
// Returns FALSE when there is nothing to find, or no match.
gboolean search_find(struct Window *win_data, Dialog_Find_Type type)
{
#ifdef DETAIL
	g_debug("! Launch search_find() with win_data = %p, type = %d", win_data, type);
#endif
#ifdef SAFEMODE
	if ((win_data==NULL) || (win_data->current_vte==NULL)) return FALSE;
#endif
	// The regex is one of each terminal: the current one may be another
	// tab than the last search was in.
	if (! search_set_regex(win_data, win_data->current_vte)) return FALSE;

	VteTerminal *vte = VTE_TERMINAL(win_data->current_vte);
	vte_terminal_search_set_wrap_around(vte, TRUE);
	gboolean found = (type == FIND_NEXT) ? vte_terminal_search_find_next(vte):
					       vte_terminal_search_find_previous(vte);
	search_show_status(win_data, found ? NULL : _("No match"));
	return found;
}

static void search_bar_create(struct Window *win_data)
{
#ifdef DETAIL
	g_debug("! Launch search_bar_create() with win_data = %p", win_data);
#endif
	// A frame of the theme's background: the bar must hide the text under it
	win_data->search_bar = gtk_frame_new(NULL);
	gtk_style_context_add_class(gtk_widget_get_style_context(win_data->search_bar), "app-notification");
	gtk_widget_set_halign(win_data->search_bar, GTK_ALIGN_END);
	gtk_widget_set_valign(win_data->search_bar, GTK_ALIGN_START);
	gtk_widget_set_margin_end(win_data->search_bar, 12);
	gtk_widget_set_margin_top(win_data->search_bar, 4);

	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
	gtk_style_context_add_class(gtk_widget_get_style_context(box), "background");
	gtk_container_set_border_width(GTK_CONTAINER(box), 4);
	gtk_container_add(GTK_CONTAINER(win_data->search_bar), box);

	win_data->search_entry = gtk_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(win_data->search_entry), _("Find"));
	gtk_entry_set_width_chars(GTK_ENTRY(win_data->search_entry), 24);
	gtk_box_pack_start(GTK_BOX(box), win_data->search_entry, TRUE, TRUE, 0);
	g_signal_connect(G_OBJECT(win_data->search_entry), "key-press-event",
			 G_CALLBACK(search_entry_key_press), win_data);
	g_signal_connect(G_OBJECT(win_data->search_entry), "changed",
			 G_CALLBACK(search_entry_changed), win_data);

	// "No match", or what is wrong with the regex
	win_data->search_status = gtk_label_new(NULL);
	gtk_widget_set_no_show_all(win_data->search_status, TRUE);
	gtk_box_pack_start(GTK_BOX(box), win_data->search_status, FALSE, FALSE, 4);

	// Text, not icons: a static LilyTerm cannot load the SVG of icon themes
	struct {
		const gchar *label;
		const gchar *tooltip;
		Dialog_Find_Type type;
	} buttons[] = {
		{ "\xe2\x96\xb2", N_("Find previous (older): Enter, Up"), FIND_PREV },	// ▲
		{ "\xe2\x96\xbc", N_("Find next (newer): Shift+Enter, Down"), FIND_NEXT },	// ▼
	};
	guint i;
	for (i=0; i<G_N_ELEMENTS(buttons); i++)
	{
		GtkWidget *button = gtk_button_new_with_label(buttons[i].label);
		gtk_widget_set_tooltip_text(button, _(buttons[i].tooltip));
		gtk_widget_set_can_focus(button, FALSE);
		g_object_set_data(G_OBJECT(button), "Find_Type", GINT_TO_POINTER(buttons[i].type));
		g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(search_button_clicked), win_data);
		gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
	}

	win_data->search_case_button = gtk_toggle_button_new_with_label("Aa");
	gtk_widget_set_tooltip_text(win_data->search_case_button, _("Case sensitive"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(win_data->search_case_button),
				     win_data->find_case_sensitive);
	win_data->search_regex_button = gtk_toggle_button_new_with_label(".*");
	gtk_widget_set_tooltip_text(win_data->search_regex_button, _("Use Perl compatible regular expressions"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(win_data->search_regex_button),
				     win_data->find_use_perl_regular_expressions);
	GtkWidget *options[] = { win_data->search_case_button, win_data->search_regex_button };
	for (i=0; i<G_N_ELEMENTS(options); i++)
	{
		gtk_widget_set_can_focus(options[i], FALSE);
		g_signal_connect(G_OBJECT(options[i]), "toggled", G_CALLBACK(search_option_toggled), win_data);
		gtk_box_pack_start(GTK_BOX(box), options[i], FALSE, FALSE, 0);
	}

	GtkWidget *close_button = gtk_button_new_with_label("\xe2\x9c\x95");	// ✕
	gtk_widget_set_tooltip_text(close_button, _("Close: Esc"));
	gtk_widget_set_can_focus(close_button, FALSE);
	gtk_button_set_relief(GTK_BUTTON(close_button), GTK_RELIEF_NONE);
	g_signal_connect_swapped(G_OBJECT(close_button), "clicked", G_CALLBACK(search_bar_hide), win_data);
	gtk_box_pack_start(GTK_BOX(box), close_button, FALSE, FALSE, 0);

	gtk_overlay_add_overlay(GTK_OVERLAY(win_data->overlay), win_data->search_bar);
}

static gboolean search_entry_key_press(GtkWidget *entry, GdkEventKey *event, struct Window *win_data)
{
#ifdef SAFEMODE
	if ((win_data==NULL) || (event==NULL)) return FALSE;
#endif
	guint mods = event->state & gtk_accelerator_get_default_mod_mask();
	switch (event->keyval)
	{
		case GDK_KEY_Escape:
			search_bar_hide(win_data);
			return TRUE;
		case GDK_KEY_Return:
		case GDK_KEY_KP_Enter:
			if ((mods & ~GDK_SHIFT_MASK) != 0) return FALSE;
			search_find(win_data, (mods & GDK_SHIFT_MASK) ? FIND_NEXT : FIND_PREV);
			return TRUE;
		case GDK_KEY_Up:
		case GDK_KEY_Down:
			if (mods) return FALSE;
			search_find(win_data, (event->keyval == GDK_KEY_Down) ? FIND_NEXT : FIND_PREV);
			return TRUE;
	}
	return FALSE;
}

// As you type: the match nearest to the end of the scrollback
static void search_entry_changed(GtkWidget *entry, struct Window *win_data)
{
#ifdef SAFEMODE
	if ((win_data==NULL) || (win_data->current_vte==NULL)) return;
#endif
	g_free(win_data->find_string);
	win_data->find_string = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
	// Without a selection, VTE searches from the end
	vte_terminal_unselect_all(VTE_TERMINAL(win_data->current_vte));
	if (win_data->find_string[0] == '\0')
	{
		vte_terminal_search_set_regex(VTE_TERMINAL(win_data->current_vte), NULL, 0);
		search_show_status(win_data, NULL);
	}
	else
		search_find(win_data, FIND_PREV);
}

static void search_option_toggled(GtkWidget *button, struct Window *win_data)
{
#ifdef SAFEMODE
	if (win_data==NULL) return;
#endif
	win_data->find_case_sensitive = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(win_data->search_case_button));
	win_data->find_use_perl_regular_expressions =
		gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(win_data->search_regex_button));
	search_entry_changed(win_data->search_entry, win_data);
	gtk_widget_grab_focus(win_data->search_entry);
}

static void search_button_clicked(GtkWidget *button, struct Window *win_data)
{
	search_find(win_data, GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "Find_Type")));
}

// Give VTE the regex of the last search, with the options of the bar.
// Returns FALSE when there is nothing to search for, or the regex is wrong.
static gboolean search_set_regex(struct Window *win_data, GtkWidget *vte)
{
	gchar *pattern = get_search_pattern(win_data->find_string, win_data->find_use_perl_regular_expressions);
	if (pattern==NULL)
	{
		vte_terminal_search_set_regex(VTE_TERMINAL(vte), NULL, 0);
		return FALSE;
	}

	guint32 flags = PCRE2_UTF | PCRE2_NO_UTF_CHECK | PCRE2_MULTILINE;
	if (! win_data->find_case_sensitive) flags |= PCRE2_CASELESS;
	GError *error = NULL;
	VteRegex *regex = vte_regex_new_for_search(pattern, -1, flags, &error);
	g_free(pattern);
	vte_terminal_search_set_regex(VTE_TERMINAL(vte), regex, 0);
	if (regex==NULL)
	{
		search_show_status(win_data, error ? error->message : _("Invalid regular expression"));
		g_clear_error(&error);
		return FALSE;
	}
	vte_regex_unref(regex);
	return TRUE;
}

// STATUS: why nothing was found, or NULL when all is well
static void search_show_status(struct Window *win_data, const gchar *status)
{
	if (win_data->search_entry==NULL) return;

	GtkStyleContext *context = gtk_widget_get_style_context(win_data->search_entry);
	if (status)
	{
		gtk_style_context_add_class(context, GTK_STYLE_CLASS_ERROR);
		gtk_label_set_text(GTK_LABEL(win_data->search_status), status);
		gtk_widget_show(win_data->search_status);
	}
	else
	{
		gtk_style_context_remove_class(context, GTK_STYLE_CLASS_ERROR);
		gtk_widget_hide(win_data->search_status);
	}
}

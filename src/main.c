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

#include "main.h"

// for using in socket
gboolean single_process = TRUE;
GtkClipboard *selection_clipboard = NULL;
GtkClipboard *selection_primary = NULL;

// gchar *command_line_path;
// gchar **empty_environ;
gchar *system_locale_list;
gchar *init_LC_CTYPE;
gchar *init_LANGUAGE;
gchar *init_encoding;
gchar *init_LC_MESSAGES;
const gchar *SYSTEM_VTE_CJK_WIDTH_STR = NULL;

const gchar *wmclass_name = NULL;
gchar *wmclass_class = NULL;
const gchar *shell = NULL;
gchar *pwd = NULL;
const gchar *home = NULL;

GList *window_list = NULL;
gchar *profile_dir = NULL;
gboolean proc_exist = FALSE;
gchar *proc_file_system_path = NULL;

extern gboolean force_to_quit;
extern gchar *restricted_locale_message;

int main( int   argc,
	  char *argv[])
{

	// i18n support. We need to support i18n under console, too.
	setlocale(LC_ALL, "");
	bindtextdomain(BINARY, LOCALEDIR);
	bind_textdomain_codeset(BINARY, "UTF-8");
	textdomain(BINARY);

#if ! defined(SAFEMODE) && defined(DEVELOP)
	g_message("Running %s without SAFE MODE!", PACKAGE);
#endif

#ifdef OUT_OF_MEMORY
#  undef g_strdup_printf
#endif
	const gchar *user_config_dir = g_get_user_config_dir();
	if (user_config_dir) profile_dir = g_strdup_printf("%s/%s", user_config_dir, BINARY);
#ifdef OUT_OF_MEMORY
	#define g_strdup_printf(...) NULL
#endif
	// in BSD system, /proc may not exist.
	proc_exist = check_if_default_proc_dir_exist(NULL);

	shell = g_getenv("SHELL");
	if (shell==NULL) shell = "";

	gboolean pwd_need_be_free = FALSE;
	pwd = (gchar *)g_getenv("PWD");
	if (pwd==NULL)
	{
		pwd_need_be_free = TRUE;
		pwd = g_get_current_dir();
	}
	if (pwd==NULL)
	{
		pwd_need_be_free = FALSE;
		pwd = "";
	}

	home = g_getenv("HOME");
	if (home==NULL) home = "";

	// deal the command line options
	command_option(argc, argv);
	if (wmclass_name==NULL) wmclass_name = g_getenv("RESOURCE_NAME");
	if (wmclass_name==NULL) wmclass_name = "";
	if (wmclass_class==NULL) wmclass_class = "";

	// init the gtk+2 engine

	// GTK3: get gtk_test_widget_click() working...

	gtk_init(&argc, &argv);
	// FIXME: we should get the single_process from profile. Current is command-line option only.
	if (single_process)
	{
		gchar *socket_str = convert_socket_data_to_string(argv);
		if (init_gtk_socket(SOCKET_FILE, socket_str, (GSourceFunc)convert_string_to_socket_data) == UNIX_SOCKET_DATA_SENT)
		{
			g_free(profile_dir);
			exit (0);
		}
		g_free(socket_str);
	}

	// start LilyTerm

	extern gchar **environ;
	gchar *environ_str = convert_array_to_string(environ, '\t');
	selection_clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
	selection_primary = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
	system_locale_list = get_locale_list();
	init_LC_CTYPE = g_strdup(get_default_lc_data(LC_CTYPE));
	init_LANGUAGE = g_strdup(get_default_lc_data(LANGUAGE));
	init_LC_MESSAGES = g_strdup(get_default_lc_data(LC_MESSAGES));
	init_encoding = (gchar *)get_encoding_from_locale(NULL);
	if (! compare_strings(init_encoding, "ANSI_X3.4-1968", TRUE))
	{
		g_free(init_encoding);
		init_encoding = g_strdup("UTF-8");
	}
	SYSTEM_VTE_CJK_WIDTH_STR = g_getenv("VTE_CJK_WIDTH");
	// FIXME: signal(SIGCHLD, SIG_IGN);
	// The first window of LilyTerm

	// Convert the GdkColor to GdkRGBA
	convert_system_color_to_rgba();

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

	if ((new_window(argc,
			argv,
			(gchar *) shell,
			environ_str,
			system_locale_list,
			(gchar *) pwd,
			(gchar *) home,
			SYSTEM_VTE_CJK_WIDTH_STR,
			FALSE,
			wmclass_name,
			wmclass_class,
			NULL,
			init_encoding,
			FALSE,
			init_LC_MESSAGES,
			NULL,
			NULL)) ||
	     g_list_length(window_list))
	{
		// The argv of "main" LilyTerm can't be free.
		// Set it to NULL here to avoid double_free().
		argv=NULL;
		gdk_notify_startup_complete_with_id(PACKAGE);
		if (! gtk_main_level())
			gtk_main();
	}
#ifdef DETAIL
	else
	{
//
		g_debug("??? The creation of first window is FAIL!!!");
	}
#endif
	extern struct KeyValue system_keys[KEYS];
	gint i;
	for (i=KEY_SWITCH_TO_TAB_1; i<=KEY_SWITCH_TO_TAB_12; i++)
	{
		g_free(system_keys[i].name);
		g_free(system_keys[i].topic);
		g_free(system_keys[i].comment);
		g_free(system_keys[i].translation);
	}

	g_free(environ_str);
	g_free(init_encoding);
	g_free(system_locale_list);
	g_free(profile_dir);
	if (pwd_need_be_free) g_free(pwd);
	g_free(restricted_locale_message);
	g_list_free(window_list);
	g_free(init_LC_CTYPE);
	g_free(init_LC_MESSAGES);
	return 0;
}

// it will return TRUE if scucceed
gchar *convert_socket_data_to_string(char *argv[])
{
#ifdef DETAIL
	print_array("! convert_socket_data_to_string() argv", argv);
#endif
	extern gchar **environ;

	gchar *locale_list = get_locale_list();

	const gchar *VTE_CJK_WIDTH_STR = g_getenv("VTE_CJK_WIDTH");
	// VTE_CJK_WIDTH can't = NULL
	if (VTE_CJK_WIDTH_STR == NULL) VTE_CJK_WIDTH_STR = "";

	gchar *encoding = get_encoding_from_locale(NULL);
	if (! compare_strings(encoding, "ANSI_X3.4-1968", TRUE))
	{
		g_free(encoding);
		encoding = g_strdup("UTF-8");
	}
	gchar *lc_messages = g_strdup(get_default_lc_data(LC_MESSAGES));

	gchar *environ_str = convert_array_to_string(environ, '\t');
	gchar *argv_str = convert_array_to_string(argv, SEPARATE_CHAR);
#ifdef SAFEMODE
	gboolean need_free_argv_str = TRUE;
	if (argv_str==NULL) argv_str=g_strdup("");
	if (argv_str==NULL)
	{
		need_free_argv_str = FALSE;
		argv_str = "";
	}
#endif

	//	      0			  1	2	    3	     4		 5   6	  7		    8		 9	       10      11
	// send data: SOCKET_DATA_VERSION SHELL LOCALE_LIST ENCODING LC_MESSAGES PWD HOME VTE_CJK_WIDTH_STR wmclass_name wmclass_class ENVIRON ARGV
	//				  0	1     2	    3	  4	5     6	    7	  8	9     10    11
	gchar *arg_str = g_strdup_printf("%s%c%s%c%s%c%s%c%s%c%s%c%s%c%s%c%s%c%s%c%s%c%s",
					 SOCKET_DATA_VERSION,
					 SEPARATE_CHAR,
					 shell,
					 SEPARATE_CHAR,
					 locale_list,
					 SEPARATE_CHAR,
					 encoding,
					 SEPARATE_CHAR,
					 lc_messages,
					 SEPARATE_CHAR,
					 pwd,
					 SEPARATE_CHAR,
					 home,
					 SEPARATE_CHAR,
					 VTE_CJK_WIDTH_STR,
					 SEPARATE_CHAR,
					 wmclass_name,
					 SEPARATE_CHAR,
					 wmclass_class,
					 SEPARATE_CHAR,
					 environ_str,
					 SEPARATE_CHAR,
					 argv_str);
	g_free(locale_list);
	g_free(encoding);
	g_free(lc_messages);
	g_free(environ_str);
#ifdef SAFEMODE
	if (need_free_argv_str)
#endif
		g_free(argv_str);

	return arg_str;
}

// it will return TRUE if succeed
gboolean convert_string_to_socket_data(gchar *socket_str)
{
#ifdef DETAIL
	g_debug("! Launch convert_string_to_socket_data() with socket_str = %s", socket_str);
#endif
	//	     0			 1     2	   3	    4		5   6	 7		   8		9	      10      11
	// get data: SOCKET_DATA_VERSION SHELL LOCALE_LIST ENCODING LC_MESSAGES PWD HOME VTE_CJK_WIDTH_STR wmclass_name wmclass_class ENVIRON ARGV

	gchar **datas = split_string(socket_str, SEPARATE_STR, 12);

	if (datas==NULL)
	{
		// A dirty hack for sometimes the received socket datas is empty.
		g_warning("Got a NULL string from the socket!");
		new_window(0,
			   NULL,
			   NULL,
			   NULL,
			   NULL,
			   NULL,
			   NULL,
			   NULL,
			   FALSE,
			   NULL,
			   NULL,
			   NULL,
			   NULL,
			   FALSE,
			   NULL,
			   NULL,
			   NULL);
	}
	else if (compare_strings(SOCKET_DATA_VERSION, datas[0], TRUE))
	{
		// The SOCKET_DATA_VERSION != the data sent via socket
		gchar *received_socket_version = NULL;
		if (datas) received_socket_version = datas[0];

		gchar *message = g_strdup_printf(_("The data got from socket seems incorrect.\n\n"
						   "\tReceived socket version: %s\n"
						   "\tExpected socket version: %s\n\n"
						   "If you just updated %s recently,\n"
						   "Please close all the windows of %s and try again."),
						   received_socket_version, SOCKET_DATA_VERSION,
						   PACKAGE, PACKAGE);
		error_dialog(NULL,
			     _("The format of socket data is out of date"),
			     "The format of socket data is out of date",
			     GTK_FAKE_STOCK_DIALOG_ERROR,
			     message,
			     NULL);
		g_free(message);
	}
	else
	{
		gchar **argv = split_string(datas[11], SEPARATE_STR, -1);
		gint argc = 0;
		if (argv)
			while (argv[argc])
				argc ++;

		//GtkNotebook *new_window(int argc,
		//			char *argv[],
		//			gchar *shell,					// 1
		//			gchar *environment,				// 10
		//			gchar *locale_list,				// 2
		//			gchar *PWD,					// 5
		//			gchar *HOME,					// 6
		//			gchar *VTE_CJK_WIDTH_STR,			// 7
		//			gboolean VTE_CJK_WIDTH_STR_overwrite_profile,
		//			gchar *wmclass_name,				// 8
		//			gchar *wmclass_class,				// 9
		//			gchar *user_environ,
		//			gchar *encoding,				// 3
		//			gboolean encoding_overwrite_profile,
		//			gchar *lc_messages,				// 4
		//			struct Window *win_data_orig,
		//			struct Page *page_data_orig)

		if ((new_window(argc,
				argv,
				datas[1],
				datas[10],
				datas[2],
				datas[5],
				datas[6],
				datas[7],
				FALSE,
				datas[8],
				datas[9],
				NULL,
				datas[3],
				FALSE,
				datas[4],
				NULL,
				NULL) == NULL && (g_list_length(window_list) == 0)))
			// Sometimes, new_window() will return NULL. Therefore LilyTerm may should quit.
			quit_gtk();
		g_strfreev(argv);
	}
	g_strfreev(datas);

	// return FALSE means this connection is finish.
	return FALSE;
}

void main_quit(GtkWidget *widget, struct Window *win_data)
{
#ifdef DETAIL
	g_debug("! Launch main_quit() with win_data = %p!", win_data);
#endif
	if (g_list_length(window_list)==1)
	{
#ifdef SAFEMODE
		if (win_data==NULL) return quit_gtk();
#endif
		window_quit(win_data->window, NULL, win_data);
	}
	else
	{
		GString *all_process_list = g_string_new(NULL);
		GString *child_process_list;
		GList *win_list = window_list;
		struct Window *temp_win_data;
		gint i = 1;
		while (win_list)
		{
			temp_win_data = win_list->data;
			child_process_list = close_multi_tabs(temp_win_data, i);
#ifdef SAFEMODE
			if (child_process_list)
#endif
				g_string_append (all_process_list, child_process_list->str);
			g_string_free(child_process_list, TRUE);
			win_list = win_list->next;
			i++;
		}

#ifdef SAFEMODE
		if ((all_process_list==NULL) || (all_process_list->len==0) ||
		    (display_child_process_dialog (all_process_list, win_data,
						   CONFIRM_TO_EXIT_WITH_CHILD_PROCESS)))
#else
		if ((all_process_list->len==0) ||
		    (display_child_process_dialog (all_process_list, win_data,
						   CONFIRM_TO_EXIT_WITH_CHILD_PROCESS)))
#endif
		{
			force_to_quit = TRUE;
			quit_gtk();
		}
		g_string_free(all_process_list, TRUE);
	}
}

void quit_gtk(void)
{
#ifdef DETAIL
	g_debug("! Launch quit_gtk()");
#endif
	if (gtk_main_level()) gtk_main_quit();
}

gchar *get_locale_list(void)
{
#ifdef DETAIL
	g_debug("! Launch get_locale_list()!");
#endif
#ifdef OUT_OF_MEMORY
#  undef g_getenv
#endif
	return join_strings_to_string(' ',
				      14,
				      g_getenv("LANG"),
				      g_getenv("LC_CTYPE"),
				      g_getenv("LC_NUMERIC"),
				      g_getenv("LC_TIME"),
				      g_getenv("LC_COLLATE"),
				      g_getenv("LC_MONETARY"),
				      g_getenv("LC_MESSAGES"),
				      g_getenv("LC_PAPER"),
				      g_getenv("LC_NAME"),
				      g_getenv("LC_ADDRESS"),
				      g_getenv("LC_TELEPHONE"),
				      g_getenv("LC_MEASUREMENT"),
				      g_getenv("LC_IDENTIFICATION"),
				      g_getenv("LC_ALL"));
#ifdef OUT_OF_MEMORY
	#define g_getenv(x) NULL
#endif
}

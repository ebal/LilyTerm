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

#include "misc.h"
#ifdef __APPLE__
#  include <sys/sysctl.h>
#endif
extern gboolean proc_exist;
extern gchar *proc_file_system_path;

gboolean gtk_alt_dialog_button_order(void)
{
#ifdef DETAIL
	g_debug("! Launch gtk_alt_dialog_button_order()");
#endif
	gboolean result;
	g_object_get (gtk_settings_get_default(), "gtk-alternative-button-order", &result, NULL);
	return result;
}

gboolean check_if_proc_dir_exist(gchar *proc_dir)
{
#ifdef DETAIL
	g_debug("! Launch check_if_proc_dir_exist() with proc_dir = %s", proc_dir);
#endif
	if (proc_dir == NULL) return FALSE;

	if (g_file_test(proc_dir, G_FILE_TEST_EXISTS))
	{
		gboolean proc_is_exist = FALSE;
		GDir *dir = g_dir_open (proc_dir, 0, NULL);
		if (dir)
		{
			const gchar *entry = g_dir_read_name(dir);
			if (entry)
			{
				g_free(proc_file_system_path);
				proc_file_system_path = g_strdup(proc_dir);
				proc_is_exist = TRUE;
			}
		}
		g_dir_close(dir);
		return proc_is_exist;
	}

	return FALSE;
}

gboolean check_if_default_proc_dir_exist(gchar *proc_dir)
{
#ifdef DETAIL
	g_debug("! Launch check_if_default_proc_dir_exist() with proc_dir = %s", proc_dir);
#endif
	if (check_if_proc_dir_exist(proc_dir)) return TRUE;
#ifdef BSD
	if (check_if_proc_dir_exist("/compat/linux/proc")) return TRUE;
#endif
	if (check_if_proc_dir_exist("/proc")) return TRUE;
	return FALSE;
}

// The returned string should be freed when no longer needed.
gchar *convert_array_to_string(gchar **array, gchar separator)
{
#ifdef DETAIL
	g_debug("! Launch convert_array_to_string()");
#endif
#ifdef SAFEMODE
	if (array==NULL) return NULL;
#endif
	GString *array_str = g_string_new (NULL);
	gint i=0;

	while (array[i]!=NULL)
	{
		if (array_str->len && (separator!='\0'))
			g_string_append_printf(array_str, "%c%s", separator, array[i]);
		else
			g_string_append_printf(array_str, "%s", array[i]);
		i++;
	}
	return g_string_free(array_str, FALSE);
}

// please vist http://en.wikipedia.org/wiki/Escape_sequences_in_C for more details
gchar *convert_escape_sequence_to_string(const gchar *string)
{
	if (string == NULL) return NULL;

	GString *new_string = g_string_new("");

	long i=-1;
	while (string[++i])
	{
		switch (string[i])
		{
			case '\a':
				g_string_append(new_string, "\\a");
				break;
			case '\b':
				g_string_append(new_string, "\\b");
				break;
			case '\f':
				g_string_append(new_string, "\\f");
				break;
			case '\n':
				g_string_append(new_string, "\\n");
				break;
			case '\r':
				g_string_append(new_string, "\\r");
				break;
			case '\t':
				g_string_append(new_string, "\\t");
				break;
			case '\v':
				g_string_append(new_string, "\\v");
				break;
			case '\\':
				g_string_append(new_string, "\\\\");
				break;
			case '\'':
				g_string_append(new_string, "\\'");
				break;
			case '\"':
				g_string_append(new_string, "\\\"");
				break;
			default:
				g_string_append_printf(new_string, "%c", string[i]);
				break;
		}
	}
	return g_string_free(new_string, FALSE);
}

gchar *convert_escape_sequence_from_string(const gchar *string)
{
	if (string == NULL) return NULL;

	GString *new_string = g_string_new("");

	long i=-1;
	while (string[++i])
	{
		// a backslash at the very end escapes nothing: keep it, and do not read past the end
		if ((string[i]=='\\') && (string[i+1]!='\0'))
		{
			switch (string[++i])
			{
				case 'a':
					g_string_append(new_string, "\a");
					break;
				case 'b':
					g_string_append(new_string, "\b");
					break;
				case 'f':
					g_string_append(new_string, "\f");
					break;
				case 'n':
					g_string_append(new_string, "\n");
					break;
				case 'r':
					g_string_append(new_string, "\r");
					break;
				case 't':
					g_string_append(new_string, "\t");
					break;
				case 'v':
					g_string_append(new_string, "\v");
					break;
				case '\\':
					g_string_append(new_string, "\\");
					break;
				case '\'':
					g_string_append(new_string, "'");
					break;
				case '"':
					g_string_append(new_string, "\"");
					break;
				default:
					g_string_append_printf(new_string, "%c", string[i]);
					break;
			}
		}
		else
			g_string_append_printf(new_string, "%c", string[i]);
	}
	return g_string_free(new_string, FALSE);
}

// return FALSE if the strings are the same.
gboolean compare_strings(const gchar *string_a, const gchar *string_b, gboolean case_sensitive)
{
#ifdef FULL
	g_debug("! Launch compare_strings()! with string_a = %s, string_b = %s", string_a, string_b);
#endif
	if ((string_a == NULL) && (string_b == NULL)) return FALSE;
	if ((string_a == NULL) || (string_b == NULL)) return TRUE;
	if (case_sensitive)
	{
		if (strcmp(string_a, string_b)) return TRUE;
	}
	else
	{
		if (strcasecmp(string_a, string_b)) return TRUE;
	}
	return FALSE;
}

// The returned string should be freed when no longer needed.
gchar *convert_str_to_utf8(gchar *string, gchar *encoding_str)
{
#ifdef DETAIL
	g_debug("! Launch convert_str_to_utf8() with string = %s, encoding_str = %s", string, encoding_str);
#endif
	if (string==NULL) return NULL;

	gchar *local_string = NULL;
	if (encoding_str)
	{
		local_string = g_convert_with_fallback (string,
							-1,
							"UTF-8",
							encoding_str,
							"_",
							NULL,
							NULL,
							NULL);

		if ((local_string == NULL ) || (local_string[0]=='\0'))
		{
			g_free(local_string);
			local_string = g_strdup(string);
			gint i = 0;
			while (local_string[i])
			{
				if (local_string[i] < 32 || local_string[i]>126)
					local_string[i] = '?';
				i++;
			}
		}

		return local_string;
	}
	else
		return g_strdup(string);
}

void set_VTE_CJK_WIDTH_environ(gint VTE_CJK_WIDTH)
{
#ifdef DETAIL
	g_debug("! Launch set_VTE_CJK_WIDTH_environ() with VTE_CJK_WIDTH = %d", VTE_CJK_WIDTH);
#endif
	// Set the VTE_CJK_WIDTH environment
	switch (VTE_CJK_WIDTH)
	{
		case 0:
			// Don't touch VTE_CJK_WIDTH
			break;
		//case 1:
		//	// The VTE_CJK_WIDTH will be setted depend on the LC environ in newer libvte version.
		case 1:
			// VTE_CJK_WIDTH=narrow only work for vte >= 0.16.14
			setenv("VTE_CJK_WIDTH", "narrow", TRUE);
			break;
		case 2:
			setenv("VTE_CJK_WIDTH", "wide", TRUE);
			break;
		default:
#ifdef FATAL
			print_switch_out_of_range_error_dialog("set_VTE_CJK_WIDTH_environ",
							       "VTE_CJK_WIDTH",
							       VTE_CJK_WIDTH);
#endif
			break;

	}
}

gchar *get_VTE_CJK_WIDTH_str(gint VTE_CJK_WIDTH)
{
#ifdef DETAIL
	g_debug("! Launch get_VTE_CJK_WIDTH_str() with VTE_CJK_WIDTH = %d", VTE_CJK_WIDTH);
#endif
	switch (VTE_CJK_WIDTH)
	{
		case 0:
			return NULL;
		case 1:
			return "narrow";
		case 2:
			return "wide";
		default:
#ifdef FATAL
			print_switch_out_of_range_error_dialog("get_VTE_CJK_WIDTH_str",
							       "VTE_CJK_WIDTH",
							       VTE_CJK_WIDTH);
#endif
			break;
	}
	return NULL;
}

void restore_SYSTEM_VTE_CJK_WIDTH_STR(void)
{
#ifdef DETAIL
	g_debug("! Launch restore_SYSTEM_VTE_CJK_WIDTH_STR()");
#endif
	extern gchar *SYSTEM_VTE_CJK_WIDTH_STR;
	if (SYSTEM_VTE_CJK_WIDTH_STR)
		g_setenv("VTE_CJK_WIDTH", SYSTEM_VTE_CJK_WIDTH_STR, TRUE);
	else
		g_unsetenv("VTE_CJK_WIDTH");
}

// get default locale from environ
// The returned string CAN NOT be free()!
const gchar *get_default_lc_data(gint lc_type)
{
#ifdef DETAIL
	g_debug("! Launch get_default_lc_data() with lc_type= %d", lc_type);
#endif
	const char *lc_data;
	switch (lc_type)
	{
		case LC_CTYPE:
			lc_data = g_getenv("LC_CTYPE");
			break;
		case LC_MESSAGES:
			lc_data = g_getenv("LC_MESSAGES");
			break;
		case LANGUAGE:
			return g_getenv("LANGUAGE");
		default:
#ifdef FATAL
			print_switch_out_of_range_error_dialog("get_default_lc_data", "lc_type", lc_type);
#endif
			return "";
	}
	const char *lc_all = g_getenv("LC_ALL");
	const char *lang = g_getenv("LANG");

	if ( (!lc_data) && lang)
		lc_data = lang;

	if (lc_all)
		lc_data = lc_all;

	if (lc_data)
		return lc_data;
	else
		return "";
}

// The returned string should be freed when no longer needed.
gchar *get_encoding_from_locale(const gchar *locale)
{
#ifdef DETAIL
	g_debug("! Launch get_encoding_from_locale() with locale = %s", locale);
#endif
#ifdef OUT_OF_MEMORY
#  undef g_strdup
#endif
	// locale==NULL: get the init encoding.

	const char *locale_encoding = NULL;
	if (setlocale(LC_CTYPE, locale)) g_get_charset(&locale_encoding);
	return g_strdup(locale_encoding);

#ifdef OUT_OF_MEMORY
	#define g_strdup fake_g_strdup
#endif
}

gboolean check_string_in_array(gchar *str, gchar **lists)
{
#ifdef DETAIL
	g_debug("! Launch check_string_in_array() with str = %s", str);
#endif
#ifdef SAFEMODE
	if ((str==NULL) || (lists==NULL)) return FALSE;
#endif
	gint i=-1;

	while (lists[++i])
	{
		if (strcmp(str, lists[i])==0) return TRUE;
	}
	return FALSE;
}

//gchar *get_proc_data(pid_t pid, gchar *file, gsize *length)
//
//
//

#ifdef __APPLE__
// macOS has no /proc: make up the two files LilyTerm reads from it.

// The kernel's data of process PID, for every process (like ps)
static gboolean macos_kinfo_proc(pid_t pid, struct kinfo_proc *kp)
{
	int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, pid };
	size_t size = sizeof(*kp);
	memset(kp, 0, size);
	return (sysctl(mib, 4, kp, &size, NULL, 0) == 0) && (size == sizeof(*kp));
}

// /proc/PID/cmdline: the arguments, each one ended with a '\0'.
// Only for the processes of our own user.
static gchar *macos_proc_cmdline(pid_t pid, gsize *length)
{
	int argmax_mib[2] = { CTL_KERN, KERN_ARGMAX };
	int argmax = 0;
	size_t size = sizeof(argmax);
	if ((sysctl(argmax_mib, 2, &argmax, &size, NULL, 0) < 0) || (argmax <= 0)) return NULL;

	// argc, the path of the executable, '\0' padding, then the arguments
	int mib[3] = { CTL_KERN, KERN_PROCARGS2, pid };
	gchar *buffer = g_malloc(argmax);
	size = argmax;
	if ((sysctl(mib, 3, buffer, &size, NULL, 0) < 0) || (size <= sizeof(int)))
	{
		g_free(buffer);
		return NULL;
	}
	int argc;
	memcpy(&argc, buffer, sizeof(argc));
	const gchar *end = buffer + size;
	const gchar *str = buffer + sizeof(argc);
	while ((str < end) && *str) str++;
	while ((str < end) && (! *str)) str++;
	const gchar *args = str;
	gint found = 0;
	while ((str < end) && (found < argc))
		if (! *str++) found++;

	gchar *cmdline = NULL;
	if (str > args)
	{
		// one more '\0', in case the last argument was cut
		cmdline = g_malloc0(str - args + 1);
		memcpy(cmdline, args, str - args);
		*length = str - args;
	}
	g_free(buffer);
	return cmdline;
}

// /proc/PID/stat, as far as LilyTerm reads it:
// pid (comm) state ppid pgrp session tty_nr tpgid ...
static gchar *macos_proc_stat(pid_t pid, gsize *length)
{
	struct kinfo_proc kp;
	if (! macos_kinfo_proc(pid, &kp)) return NULL;

	gchar comm[MAXCOMLEN + 1] = {0};
	memcpy(comm, kp.kp_proc.p_comm, MAXCOMLEN);
	pid_t session = getsid(pid);
	gchar *stat = g_strdup_printf("%d (%s) S %d %d %d %d %d 0 0 0 0\n",
				      (gint) pid, comm, (gint) kp.kp_eproc.e_ppid, (gint) kp.kp_eproc.e_pgid,
				      (gint) ((session > 0)? session: 0), (gint) kp.kp_eproc.e_tdev,
				      (gint) kp.kp_eproc.e_tpgid);
	*length = strlen(stat);
	return stat;
}

// TRUE if process PID runs as root
gboolean macos_proc_is_root(pid_t pid)
{
	struct kinfo_proc kp;
	return macos_kinfo_proc(pid, &kp) &&
	       ((kp.kp_eproc.e_ucred.cr_uid == 0) || (kp.kp_eproc.e_pcred.p_rgid == 0));
}
#endif

gchar *get_proc_data(pid_t pid, gchar *file, gsize *length)
{
#ifdef FULL
	g_debug("! Launch get_proc_data() with pid = %d, file = %s", pid, file);
#endif
	if (! proc_exist) return NULL;
	if (pid<1) return NULL;
#ifdef __APPLE__
	*length = 0;
	if (strcmp(file, "cmdline") == 0) return macos_proc_cmdline(pid, length);
	if (strcmp(file, "stat") == 0) return macos_proc_stat(pid, length);
	return NULL;
#endif
	gchar *contents=NULL;
	gint timeout=0;
	gchar *proc_path = g_strdup_printf("%s/%d", proc_file_system_path, (gint)pid);
	gchar *file_path = g_strdup_printf("%s/%s", proc_path, file);

#if defined(OUT_OF_MEMORY)
	if (proc_path && file_path)
	{
#endif
		while (g_file_test(proc_path, G_FILE_TEST_EXISTS) &&
		       g_file_get_contents (file_path, &contents, length, NULL))
		{
			if (*length==0)
			{
				//
				g_message("Waiting for %s/%d/%s...", proc_file_system_path, (gint)pid, file);
				// we should wait until "/proc/%d/file" is not empty
				usleep(100000);
				timeout++;
				// contents = "" here
				g_free(contents);
				contents = NULL;
			}
			else
				break;

			// we only try for 3 times
			if (timeout>2)
			{
#ifdef FATAL
				g_message("Failed when waiting for %s/%d/%s. Abort!", proc_file_system_path, (gint)pid, file);
#else
				g_warning("Failed when waiting for %s/%d/%s. Abort!", proc_file_system_path, (gint)pid, file);
#endif
				break;
			}
		}
#if defined(OUT_OF_MEMORY)
	}
#endif
	g_free(file_path);
	g_free(proc_path);
	return contents;
}

// it will check if the count of returned string array is < max_tokens
gchar **split_string(const gchar *str, const gchar *split, gint max_tokens)
{
#ifdef FULL
	g_debug("! Launch split_string with str = %s, split = %s, max_tokens = %d", str, split, max_tokens);
#endif
#ifdef SAFEMODE
	if ((str==NULL) || (split==NULL) || (split[0]=='\0')) return NULL;
#endif
	gchar **datas = g_strsplit_set(str, split, max_tokens);
	gint i = -1;

#ifdef SAFEMODE
	if (datas && (max_tokens>0))
#else
	if (max_tokens>0)
#endif
	{
		while (datas[++i]);
		if (i<max_tokens)
		{
#ifdef DEBUG
			g_debug("split_string(%d) WARNING: can NOT splite \"%s\" into %d string(s) !!", i, str, max_tokens);
#else
			g_warning("split_string(%d) WARNING: can NOT splite \"%s\" into %d string(s) !!", i, str, max_tokens);
#endif
			g_strfreev(datas);
			datas = NULL;
		}
	}
	return datas;
}

gint count_char_in_string(const gchar *str, const gchar split)
{
#ifdef FULL
	g_debug("! Launch count_char_in_string with str = %s, split = %c", str, split);
#endif
	if (str==NULL) return -1;

	gint i = -1, count = 0;
	while (str[++i])
		if (str[i]==split) count++;

	return count;
}

gchar **get_pid_stat(pid_t pid, gint max_tokens)
{
#ifdef FULL
	g_debug("! Launch get_pid_stat with pid = %d, max_tokens = %d", pid, max_tokens);
#endif
	if (pid<1) return NULL;

	gsize length=0;
	gchar *stat = get_proc_data(pid, "stat", &length);
	gchar **stats = split_pid_stat(stat, max_tokens);
	g_free(stat);
	return stats;
}

// Split the "pid (comm) state ppid ..." of /proc/PID/stat as
// split_string(stat, " ()", max_tokens) does: { pid, "", comm, "", state,
// ppid, ... }. But comm, the name of the program, may hold spaces and
// parentheses itself, which would shift every field after it: it ends at the
// *last* ')', and stays one field.
gchar **split_pid_stat(const gchar *stat, gint max_tokens)
{
#ifdef FULL
	g_debug("! Launch split_pid_stat with stat = %s, max_tokens = %d", stat, max_tokens);
#endif
	if (stat==NULL) return NULL;

	const gchar *comm_start = strchr(stat, '(');
	const gchar *comm_end = strrchr(stat, ')');
	if ((comm_start==NULL) || (comm_end==NULL) || (comm_end < comm_start))
		return split_string(stat, " ()", max_tokens);

	// split a copy whose comm has no separators left, then put the real comm back
	gchar *plain = g_strdup(stat);
	gchar *p;
	for (p = plain + (comm_start - stat) + 1; p < plain + (comm_end - stat); p++)
		if ((*p==' ') || (*p=='(') || (*p==')')) *p = '_';
	gchar **stats = split_string(plain, " ()", max_tokens);
	g_free(plain);
	if (stats && stats[0] && stats[1] && stats[2])
	{
		g_free(stats[2]);
		stats[2] = g_strndup(comm_start + 1, comm_end - comm_start - 1);
	}
	return stats;
}

// The returned string should be freed when no longer needed.
gchar *convert_text_to_html(StrAddr **text, gboolean free_text, gchar *color, StrLists *tag, ...)
{
#ifdef DETAIL
	g_debug("! Launch convert_text_to_html() with text = %s, color = %s, tag = %s",
		*text, color, tag);
#endif
#ifdef SAFEMODE
	if ((text==NULL) || (*text==NULL)) return NULL;
#endif
	gchar *markup_escape_text = g_markup_escape_text(*text, -1);

	if (color)
	{
		gchar *color_profile = g_strdup_printf ("<span foreground=\"%s\">%s</span>",
							color, markup_escape_text);
		g_free(markup_escape_text);
		markup_escape_text = color_profile;
	}

	va_list arg_ptr;
	va_start(arg_ptr, tag);
	while (tag)
	{
		gchar *new_html_string = g_strdup_printf("<%s>%s</%s>",
							 tag, markup_escape_text, tag);
		g_free(markup_escape_text);
		markup_escape_text = new_html_string;
		tag = va_arg(arg_ptr, char *);
	}
	va_end(arg_ptr);

	if (free_text)
	{
		g_free(*text);
#ifdef SAFEMODE
		*text = NULL;
#endif
	}
	return markup_escape_text;
}

// The returned string should be freed when no longer needed.
gchar *join_strings_to_string(const gchar separator, const gint total, const StrLists *string, ...)
{
#ifdef DETAIL
	g_debug("! Launch join_strings_to_string() with separator = '%c', total = %d",
		separator, total);
#endif
#ifdef SAFEMODE
	if (separator=='\0') return NULL;
#endif
	GString *strings = g_string_new ("");
	gint i;
	va_list arg_ptr;
	va_start(arg_ptr, string);
	for (i=0; i<total; i++)
	{
		if (string && string[0]!='\0')
		{
			if (strings->len)
				g_string_append_printf(strings, "%c%s", separator, string);
			else
				g_string_append_printf(strings, "%s", string);
		}
		string = va_arg(arg_ptr, char *);
	}
	va_end(arg_ptr);

	return g_string_free(strings, FALSE);
}

// return NULL: No, it is less than MAX
// return String: Yes, It is more then MAX, and have been cut short to OUTPUT_LINE lines.
gchar *colorful_max_new_lines(gchar *string, gint max, gint output_line)
{
#ifdef DETAIL
	g_debug("! Launch count_max_new_lines() with string = '%s', max = %d",
		string, max);
#endif
	if (string==NULL) return NULL;

	gint i = 0;
	gint total = 0;
	gchar *color_str = g_strdup("");
	gint star_start = 0;

	while (string[i])
	{
		char separate;

		if ((string[i]=='\n') || (string[i]=='\r') || (string[i+1]=='\0'))
		{
			if ((string[i]=='\n') || (string[i]=='\r'))
				total++;

			if (total < output_line)
			{
				separate = string[i];
				if ((string[i]=='\n') || (string[i]=='\r'))
					string[i] = 0;
				gchar *str = &(string[star_start]);
				gchar *markup_str;
				if ((str==NULL) || (str[0] == '\0'))
					markup_str = g_strdup("");
				else
				{
					gchar *tmp_str = g_markup_escape_text(str, -1);
					markup_str = g_strconcat("<b><span foreground=\"blue\">",
								 tmp_str, "</span></b>", NULL);
					g_free(tmp_str);
				}
				gchar *new_str = NULL;
				if ((separate=='\n') || (separate=='\r'))
				{
					// TRANSLATE NOTE: The "↲" is a replace character for <Enter> (<CR>, or <New Line>).
					// TRANSLATE NOTE: You may translate it into something like "¶", "↵" or "↩".
					gchar *markup_enter = g_markup_escape_text(_("↲"), -1);
					if (string[i+1])
						new_str = g_strconcat(color_str,
								      markup_str,
								      "<small><span foreground=\"darkgray\">",
								      markup_enter,
								      "</span></small>\n", NULL);
					else
						new_str = g_strconcat(color_str,
								      markup_str,
								      "<small><span foreground=\"darkgray\">",
								      markup_enter,
								      "</span></small>", NULL);
					g_free(markup_enter);
				}
				else
					new_str = g_strconcat(color_str, markup_str, NULL);
				g_free(markup_str);
				g_free(color_str);
				color_str = new_str;
				string[i] = separate;
				star_start = i+1;

			}
		}
		if (total>=output_line)
		{
			separate = string[i];
			string[i] = 0;
			gchar *output_str = g_strdup_printf("<tt>%s%s</tt>",
							    color_str, "<b>...</b>");
			g_free(color_str);
			string[i] = separate;
			return output_str;
		}
		i++;
	}
	if (total > max)
	{
		gchar *output_str = g_strdup_printf("<tt>%s</tt>", color_str);
		g_free(color_str);
		return output_str;
	}
	g_free(color_str);
	return NULL;
}

// The "join the lines" paste: a backslash at the end of a line (a shell line
// continuation) joins it to the next line, any other line break becomes a space,
// and the "> " prompt in front of a continued line is dropped. A run of spaces
// and tabs becomes one space; leading and trailing ones are dropped.
// The returned string should be freed when no longer needed.
gchar *join_pasted_lines(const gchar *str)
{
#ifdef DETAIL
	g_debug("! Launch join_pasted_lines() with str = %s", str);
#endif
	if (str==NULL) return NULL;

	GString *joined = g_string_new(NULL);
	gboolean need_space = FALSE;
	const gchar *p = str;
	while (*p)
	{
		gboolean continued = FALSE;
		gboolean line_break = FALSE;
		if ((p[0]=='\\') && ((p[1]=='\n') || (p[1]=='\r')))
		{
			continued = TRUE;
			p++;
		}
		if ((*p=='\n') || (*p=='\r'))
		{
			line_break = TRUE;
			// \r\n is one line break
			if ((p[0]=='\r') && (p[1]=='\n')) p++;
			p++;
			if ((p[0]=='>') && (p[1]==' ')) p += 2;
			if (! continued) need_space = TRUE;
		}
		if (line_break) continue;

		if ((*p==' ') || (*p=='\t'))
			need_space = TRUE;
		else
		{
			if (need_space && joined->len) g_string_append_c(joined, ' ');
			need_space = FALSE;
			g_string_append_c(joined, *p);
		}
		p++;
	}
	return g_string_free(joined, FALSE);
}

// The command that opens a link, for macOS: a profile saved on Linux, or by
// LilyTerm before 0.11.2, says xdg-open, which macOS does not have; open(1)
// does the same there. COMMAND is the program and its arguments, separated by
// tabs. Returns the command with open(1) instead of xdg-open as a new string,
// or NULL when COMMAND does not run xdg-open.
gchar *replace_xdg_open(const gchar *command)
{
#ifdef DETAIL
	g_debug("! Launch replace_xdg_open() with command = %s", command);
#endif
	if (command==NULL) return NULL;

	const gchar *args = strchr(command, '\t');
	if (args==NULL) args = command + strlen(command);
	gchar *program = g_strndup(command, args - command);
	gchar *name = g_path_get_basename(program);
	gboolean is_xdg_open = (strcmp(name, "xdg-open") == 0);
	g_free(name);
	g_free(program);

	if (! is_xdg_open) return NULL;
	return g_strconcat("open", args, NULL);
}

// The email address ADDRESS that was clicked, as a mailto: URL. The link may
// say "user@host" alone, but open(1) and xdg-open take that for a file, and
// mail clients take mailto: URLs too. Free the returned string with g_free().
gchar *get_mailto_url(const gchar *address)
{
#ifdef DETAIL
	g_debug("! Launch get_mailto_url() with address = %s", address);
#endif
	if (address==NULL) return NULL;

	if (g_ascii_strncasecmp(address, "mailto:", 7)==0)
		return g_strdup(address);
	return g_strconcat("mailto:", address, NULL);
}

// The kind of link (TAG_WWW, TAG_FTP, TAG_FILE or TAG_MAIL) of URI, a
// hyperlink that a program marked with OSC 8 (ls --hyperlink, gcc, ...),
// and in *URL the URL to open for it. -1 for one that LilyTerm does not open:
// another scheme (ssh:, a custom handler, ...), or a file on another machine
// than HOST_NAME (ls on a server over ssh says file://SERVER/...). A local
// file:// URL becomes file:///PATH, which open(1) and xdg-open both take.
// Free *URL with g_free().
gint get_hyperlink_tag(const gchar *uri, const gchar *host_name, gchar **url)
{
#ifdef DETAIL
	g_debug("! Launch get_hyperlink_tag() with uri = %s, host_name = %s", uri, host_name);
#endif
	*url = NULL;
	if (uri==NULL) return -1;

	// NULL without a valid scheme; any case (HTTP: is http:)
	gchar *scheme = g_uri_parse_scheme(uri);
	if (scheme==NULL) return -1;
	gint tag = -1;
	if ((g_ascii_strcasecmp(scheme, "http")==0) || (g_ascii_strcasecmp(scheme, "https")==0))
		tag = TAG_WWW;
	else if ((g_ascii_strcasecmp(scheme, "ftp")==0) || (g_ascii_strcasecmp(scheme, "ftps")==0))
		tag = TAG_FTP;
	else if (g_ascii_strcasecmp(scheme, "mailto")==0)
		tag = TAG_MAIL;
	else if (g_ascii_strcasecmp(scheme, "file")==0)
	{
		gchar *path = get_local_dir_from_uri(uri, host_name);
		if (path) *url = g_filename_to_uri(path, NULL, NULL);
		g_free(path);
		if (*url) tag = TAG_FILE;
	}
	g_free(scheme);

	if ((tag >= 0) && (*url==NULL)) *url = g_strdup(uri);
	return tag;
}

// The pattern of a path in the terminal, that a click opens:
//   /etc/hosts  ~/notes.txt  ./build  ../lib/x.c	(from /, ~, . or ..)
//   src/main.c				(relative: a / and an .ext)
//   main.c:42				(a bare name: only with a line)
// each with :LINE or :LINE:COL after it, as compilers and grep print them.
// It starts after a space, a quote, a bracket or a "=" (--file=/x): never in
// the middle of a URL or a word ("and/or" and "10/27" are no paths), and
// does not end with a "." (the end of a sentence).
#define PATH_CHAR "[^\\s\"'`<>()\\[\\]{}|;,:=]"
const gchar *get_path_regex(void)
{
	return "(?<![^\\s\"'`(\\[<=])"
	       "(?:(?:~|\\.\\.?)?/" PATH_CHAR "+(?:/" PATH_CHAR "+)*"
	       "|" PATH_CHAR "+/(?:" PATH_CHAR "+/)*" PATH_CHAR "*\\.[A-Za-z0-9_]+"
	       "|" PATH_CHAR "+\\.[A-Za-z0-9_]+(?=:[0-9]))"
	       "(?<!\\.)"
	       "(?::[0-9]+(?::[0-9]+)?)?";
}

// The file that TEXT, a path that the pattern above found, names: with the
// :LINE[:COL] dropped, ~ for HOME, and a relative one in DIR (the directory
// of the shell). NULL when there is no such file or directory: the pattern
// only says that it looks like one. Free it with g_free().
gchar *get_clicked_path(const gchar *text, const gchar *dir, const gchar *home)
{
#ifdef DETAIL
	g_debug("! Launch get_clicked_path() with text = %s, dir = %s, home = %s", text, dir, home);
#endif
	if ((text==NULL) || (text[0]=='\0')) return NULL;

	gchar *path = g_strdup(text);
	// :LINE and :LINE:COL
	gint i;
	for (i=0; i<2; i++)
	{
		gchar *colon = strrchr(path, ':');
		if ((colon==NULL) || (colon[1]=='\0') || (strspn(colon+1, "0123456789") != strlen(colon+1))) break;
		*colon = '\0';
	}

	gchar *full = NULL;
	if (path[0]=='/')
		full = g_canonicalize_filename(path, NULL);
	else if ((path[0]=='~') && ((path[1]=='/') || (path[1]=='\0')))
	{
		if (home && (home[0]=='/')) full = g_canonicalize_filename(path[1] ? path + 2 : ".", home);
	}
	else if (dir && (dir[0]=='/'))
		full = g_canonicalize_filename(path, dir);
	g_free(path);

	if (full && g_file_test(full, G_FILE_TEST_EXISTS)) return full;
	g_free(full);
	return NULL;
}

// What a click on PATH (a path, or a file:// link) opens with the file
// manager command: PATH itself, but the directory of a program, which
// open(1) would run (a Unix executable runs in Terminal.app, an .app starts).
// A click shows where a program is; it does not start it. Free it with
// g_free().
gchar *get_file_to_open(const gchar *path)
{
	if (path==NULL) return NULL;

	gboolean program = FALSE;
	if (g_file_test(path, G_FILE_TEST_IS_DIR))
	{
		gsize len = strlen(path);
		while ((len > 1) && (path[len-1]=='/')) len--;
		program = (len > 4) && (g_ascii_strncasecmp(path + len - 4, ".app", 4)==0);
	}
	else
		program = g_file_test(path, G_FILE_TEST_IS_EXECUTABLE);

	if (! program) return g_strdup(path);
	gchar *trimmed = g_strdup(path);
	gsize len = strlen(trimmed);
	while ((len > 1) && (trimmed[len-1]=='/')) trimmed[--len] = '\0';
	gchar *dir = g_path_get_dirname(trimmed);
	g_free(trimmed);
	return dir;
}

// TRUE if PROGRAM (the name of the foreground program of a tab) logs in to
// another machine: the paths it prints are not the ones of this machine.
gboolean is_remote_login(const gchar *program)
{
	const gchar *remote[] = { "ssh", "mosh-client", "et", "telnet", "autossh", NULL };
	return program && check_string_in_array((gchar *)program, (gchar **)remote);
}

// TEXT without the spaces and tabs at the end of each of its lines: the
// padding of a table, or of ls, which a terminal keeps as it was written.
// Free it with g_free().
gchar *strip_trailing_blanks(const gchar *text)
{
	if (text==NULL) return NULL;

	GString *stripped = g_string_sized_new(strlen(text));
	const gchar *line = text;
	while (*line)
	{
		const gchar *end = strchr(line, '\n');
		gsize len = end ? (gsize)(end - line) : strlen(line);
		gsize keep = len;
		while ((keep > 0) && ((line[keep-1]==' ') || (line[keep-1]=='\t'))) keep--;
		// keep a \r of a \r\n, but not the blanks before it
		if ((keep > 0) && (line[keep-1]=='\r'))
		{
			gsize before = keep - 1;
			while ((before > 0) && ((line[before-1]==' ') || (line[before-1]=='\t'))) before--;
			g_string_append_len(stripped, line, before);
			g_string_append_c(stripped, '\r');
		}
		else
			g_string_append_len(stripped, line, keep);
		if (end==NULL) break;
		g_string_append_c(stripped, '\n');
		line = end + 1;
	}
	return g_string_free(stripped, FALSE);
}

// DIR as a tab shows it: ~ for HOME, and ~/REST for a directory in it, as
// shells say it; any other DIR as it is. A HOME of / stays /. Free it with
// g_free().
gchar *get_short_dir_name(const gchar *dir, const gchar *home)
{
	if (dir==NULL) return NULL;
	if ((home==NULL) || (home[0]!='/')) return g_strdup(dir);

	gsize home_len = strlen(home);
	while ((home_len > 1) && (home[home_len-1]=='/')) home_len--;
	// a home of "/" would make every directory ~/...
	if (home_len <= 1) return g_strdup(dir);

	if (strncmp(dir, home, home_len)==0)
	{
		const gchar *rest = dir + home_len;
		while (rest[0]=='/' && rest[1]=='/') rest++;
		if ((rest[0]=='\0') || ((rest[0]=='/') && (rest[1]=='\0'))) return g_strdup("~");
		if (rest[0]=='/') return g_strconcat("~", rest, NULL);
	}
	return g_strdup(dir);
}

// The regex that finds TEXT: TEXT itself with USE_REGEX, else TEXT with its
// special characters escaped. NULL when there is nothing to find. TEXT stays
// as it is: the search bar escaped the text of the search in place before,
// again with each search ("a.b", "a\.b", "a\\\.b", ...).
// Free the returned string with g_free().
gchar *get_search_pattern(const gchar *text, gboolean use_regex)
{
	if ((text==NULL) || (text[0]=='\0')) return NULL;
	if (use_regex) return g_strdup(text);
	return g_regex_escape_string(text, -1);
}

// The directory of URI, a "file://HOST/PATH" that a shell reported with OSC 7,
// if HOST is this machine (HOST_NAME, "localhost" or none). The one of a shell
// on another machine (over ssh) is not a directory here. The names must be
// the same, but for a ".local" at the end ("mac" and "mac.local" are the same
// machine on macOS). Free the returned string with g_free().
gchar *get_local_dir_from_uri(const gchar *uri, const gchar *host_name)
{
#ifdef DETAIL
	g_debug("! Launch get_local_dir_from_uri() with uri = %s, host_name = %s", uri, host_name);
#endif
	if ((uri==NULL) || (uri[0]=='\0')) return NULL;

	gchar *host = NULL;
	gchar *dir = g_filename_from_uri(uri, &host, NULL);
	if (dir==NULL) return NULL;

	gboolean local = (host==NULL) || (host[0]=='\0') || (g_ascii_strcasecmp(host, "localhost")==0);
	if ((! local) && host_name)
	{
		gsize host_len = strlen(host);
		gsize host_name_len = strlen(host_name);
		if ((host_len > 6) && (g_ascii_strcasecmp(host + host_len - 6, ".local")==0)) host_len -= 6;
		if ((host_name_len > 6) && (g_ascii_strcasecmp(host_name + host_name_len - 6, ".local")==0)) host_name_len -= 6;
		local = (host_len == host_name_len) && (g_ascii_strncasecmp(host, host_name, host_len)==0);
	}
	g_free(host);
	if (local) return dir;
	g_free(dir);
	return NULL;
}

// The key of a US keyboard at HARDWARE_KEYCODE (a kVK_ANSI_* virtual key code
// of macOS), as the key bindings name it. GDK reports what the current layout
// types there: a Greek psi for the C key, and with Option an Option character,
// but Cmd C is the key of C in every macOS application. 0: none. (It is
// built everywhere, for the unit tests.)
gint macos_us_keyval(guint16 hardware_keycode, gboolean shift)
{
	static const gint keys[] = {
		[0x00] = GDK_KEY_A, [0x01] = GDK_KEY_S, [0x02] = GDK_KEY_D, [0x03] = GDK_KEY_F,
		[0x04] = GDK_KEY_H, [0x05] = GDK_KEY_G, [0x06] = GDK_KEY_Z, [0x07] = GDK_KEY_X,
		[0x08] = GDK_KEY_C, [0x09] = GDK_KEY_V, [0x0B] = GDK_KEY_B, [0x0C] = GDK_KEY_Q,
		[0x0D] = GDK_KEY_W, [0x0E] = GDK_KEY_E, [0x0F] = GDK_KEY_R, [0x10] = GDK_KEY_Y,
		[0x11] = GDK_KEY_T, [0x12] = GDK_KEY_1, [0x13] = GDK_KEY_2, [0x14] = GDK_KEY_3,
		[0x15] = GDK_KEY_4, [0x16] = GDK_KEY_6, [0x17] = GDK_KEY_5, [0x18] = GDK_KEY_equal,
		[0x19] = GDK_KEY_9, [0x1A] = GDK_KEY_7, [0x1B] = GDK_KEY_minus, [0x1C] = GDK_KEY_8,
		[0x1D] = GDK_KEY_0, [0x1E] = GDK_KEY_bracketright, [0x1F] = GDK_KEY_O, [0x20] = GDK_KEY_U,
		[0x21] = GDK_KEY_bracketleft, [0x22] = GDK_KEY_I, [0x23] = GDK_KEY_P, [0x25] = GDK_KEY_L,
		[0x26] = GDK_KEY_J, [0x27] = GDK_KEY_apostrophe, [0x28] = GDK_KEY_K, [0x29] = GDK_KEY_semicolon,
		[0x2A] = GDK_KEY_backslash, [0x2B] = GDK_KEY_comma, [0x2C] = GDK_KEY_slash, [0x2D] = GDK_KEY_N,
		[0x2E] = GDK_KEY_M, [0x2F] = GDK_KEY_period, [0x32] = GDK_KEY_grave,
	};
	if (hardware_keycode >= G_N_ELEMENTS(keys)) return 0;
	gint keyval = keys[hardware_keycode];
	// Cmd+Shift [ and ] are the brace keys, as GDK reports them with a US layout
	if (shift && (keyval == GDK_KEY_bracketleft)) return GDK_KEY_braceleft;
	if (shift && (keyval == GDK_KEY_bracketright)) return GDK_KEY_braceright;
	return keyval;
}

GdkRGBA convert_color_to_rgba(GdkColor color)
{
	GdkRGBA rgba;
	rgba.red = (gdouble)(color.red>>8)/0xFF;
	rgba.green = (gdouble)(color.green>>8)/0xFF;
	rgba.blue = (gdouble)(color.blue>>8)/0xFF;
	// VTE refuses a color with an alpha out of [0, 1]: never leave it uninitialized
	rgba.alpha = 1.0;

	return rgba;
}

// The returned string should be freed when no longer needed.
gchar *dirty_gdk_rgba_to_string(GdkRGBA *rgba)
{
#ifdef SAFEMODE
	if (rgba==NULL) return NULL;
#endif
	return g_strdup_printf("#%04x%04x%04x", (unsigned int)(rgba->red*0xFFFF), (unsigned int)(rgba->green*0xFFFF), (unsigned int)(rgba->blue*0xFFFF));
}

#if defined(GEOMETRY)
void widget_size_allocate (GtkWidget *widget, GtkAllocation *allocation, gchar *name)
{
#  ifdef DETAIL
	g_debug("! Launch widget_size_allocate() with widget = %p, name = %s", widget, name);
#  endif
	// 200x200: the default size of a GtkWindow
	gint ansi_color = ANSI_COLOR_BLACK;
#ifdef SAFEMODE
	if (allocation==NULL) return;
#endif
	if ((allocation->width <= 200) || (allocation->height <= 200))
		ansi_color = ANSI_COLOR_RED;
	fprintf(stderr, "\033[1;%dm!! %s_size_allocate(%p): the allocated size is %d x %d\033[0m\n",
		ansi_color, name, widget, allocation->width, allocation->height);
}
#endif

gboolean dirty_gdk_color_parse(const gchar *spec, GdkRGBA *color)
{
#ifdef DETAIL
	g_debug("! Launch dirty_gdk_color_parse() with spec = %s", spec);
#endif
	if (spec==NULL) return FALSE;

	gchar *new_spec = g_strdup(spec);
#ifdef OUT_OF_MEMORY
	if (new_spec==NULL) return FALSE;
#endif
	new_spec = g_strstrip(new_spec);
	gboolean response = gdk_rgba_parse(color, new_spec);
	g_free(new_spec);
	return response;
}

GtkWidget *dirty_gtk_vbox_new(gboolean homogeneous, gint spacing)
{
#ifdef DETAIL
	g_debug("! Launch dirty_gtk_vbox_new() with homogeneous = %d, spacing = %d", homogeneous, spacing);
#endif
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, spacing);
	gtk_box_set_homogeneous(GTK_BOX(box), homogeneous);
	return box;
}

GtkWidget *dirty_gtk_hbox_new(gboolean homogeneous, gint spacing)
{
#ifdef DETAIL
	g_debug("! Launch dirty_gtk_hbox_new() with homogeneous = %d, spacing = %d", homogeneous, spacing);
#endif
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, spacing);
	gtk_box_set_homogeneous(GTK_BOX(box), homogeneous);
	return box;
}

#if defined(OUT_OF_MEMORY)
gchar *fake_g_strdup(const gchar *str)
{
#  ifdef DETAIL
	g_debug("! fake_g_strdup(): Trying to strdup(%s)...", str);
#  endif

#  undef g_strdup
#  undef g_strsplit
	if (str==NULL) return NULL;

	gchar *return_str = NULL;
	gchar **strs = g_strsplit(str, " ", 2);
	if (strs)
	{
		if ((! compare_strings(strs[0], "Alt", FALSE)) ||
		    (! compare_strings(strs[0], "Shift", FALSE)) ||
		    (! compare_strings(strs[0], "Ctrl", FALSE)))
		{
			return_str = g_strdup(str);
		}
	}
	g_strfreev (strs);
	return return_str;
#  define g_strdup fake_g_strdup
#  define g_strsplit fake_g_strsplit
}

gchar *fake_g_strdup_printf(const StrLists *format, ...)
{
#  ifdef DETAIL
	g_debug("! Trying to strdup_printf(%s)...", format);
#  endif
	return NULL;
}

gchar** fake_g_strsplit(const gchar *string, const gchar *delimiter, gint max_tokens)
{
#  ifdef DETAIL
	g_debug("! fake_g_strsplit(): Trying to strsplit('%s', '%s', %d)...", string, delimiter, max_tokens);
#  endif

	if ((string==NULL) || (delimiter==NULL) || (delimiter[0]=='\0')) return NULL;

#  undef g_strsplit

	gchar **return_array = NULL;
	gchar **strs = g_strsplit(string, " ", 2);
	if (strs)
	{
		if ((strs[0]=='\0') ||
		    (! compare_strings(strs[0], "Alt", FALSE)) ||
		    (! compare_strings(strs[0], "Shift", FALSE)) ||
		    (! compare_strings(strs[0], "Ctrl", FALSE)))
		{
			return_array = g_strsplit (string, delimiter, max_tokens);
		}
	}
	g_strfreev (strs);

	if (! return_array)
	{
		if (! compare_strings(delimiter, "+", TRUE))
		{
			return_array = g_strsplit (string, delimiter, max_tokens);
		}
	}

	return return_array;

#  define g_strsplit fake_g_strsplit
}

// A very dirty fix for unit test error.
gchar **g_listenv (void) { return NULL; }
#endif

// A toolkit-independent replacement for XParseGeometry(), so that LilyTerm
// needs no Xlib and behaves the same under X11 and Wayland.
// It parses [=][<width>{xX}<height>][{+-}<xoffset>{+-}<yoffset>]
// and returns a bitmask of GEOMETRY_HAS_* for the fields that were found.
// Returns 0 (and touches nothing) if the string is not a valid geometry.
gint parse_geometry_str(const gchar *geometry_str, gint *offset_x, gint *offset_y, guint *width, guint *height)
{
#ifdef DETAIL
	g_debug("! Launch parse_geometry_str() with geometry_str = %s", geometry_str);
#endif
	if ((geometry_str==NULL) || (geometry_str[0]=='\0')) return 0;

	const gchar *str = geometry_str;
	gchar *end = NULL;
	gint mask = 0;
	guint64 new_width = 0, new_height = 0;
	gint64 new_x = 0, new_y = 0;

	if (*str == '=') str++;

	if (g_ascii_isdigit(*str))
	{
		new_width = g_ascii_strtoull(str, &end, 10);
		if ((end==str) || (new_width > G_MAXINT)) return 0;
		mask |= GEOMETRY_HAS_WIDTH;
		str = end;
	}
	if ((*str == 'x') || (*str == 'X'))
	{
		str++;
		if (! g_ascii_isdigit(*str)) return 0;
		new_height = g_ascii_strtoull(str, &end, 10);
		if ((end==str) || (new_height > G_MAXINT)) return 0;
		mask |= GEOMETRY_HAS_HEIGHT;
		str = end;
	}
	if ((*str == '+') || (*str == '-'))
	{
		gboolean negative = (*str == '-');
		str++;
		if (! g_ascii_isdigit(*str)) return 0;
		new_x = g_ascii_strtoull(str, &end, 10);
		if ((end==str) || (new_x > G_MAXINT)) return 0;
		if (negative) { new_x = -new_x; mask |= GEOMETRY_X_NEGATIVE; }
		mask |= GEOMETRY_HAS_X;
		str = end;

		if ((*str == '+') || (*str == '-'))
		{
			negative = (*str == '-');
			str++;
			if (! g_ascii_isdigit(*str)) return 0;
			new_y = g_ascii_strtoull(str, &end, 10);
			if ((end==str) || (new_y > G_MAXINT)) return 0;
			if (negative) { new_y = -new_y; mask |= GEOMETRY_Y_NEGATIVE; }
			mask |= GEOMETRY_HAS_Y;
			str = end;
		}
	}
	// trailing garbage: not a geometry string
	if (*str != '\0') return 0;

	if ((mask & GEOMETRY_HAS_WIDTH) && width) *width = new_width;
	if ((mask & GEOMETRY_HAS_HEIGHT) && height) *height = new_height;
	if ((mask & GEOMETRY_HAS_X) && offset_x) *offset_x = new_x;
	if ((mask & GEOMETRY_HAS_Y) && offset_y) *offset_y = new_y;
	return mask;
}

// Make an environment array acceptable for vte_terminal_spawn_*():
// newer libvte refuses to spawn anything (assertion `_vte_pty_check_envv') if
// a single entry is not in the "NAME=value" form. Such entries show up here
// because the environment travels as a tab separated string, so a value that
// holds a tab itself, or an empty field, ends up as an entry of its own.
// Also, a variable that is listed more than once keeps its *last* value, so
// the LANG/TERM/... that LilyTerm appends really override the inherited ones.
// It takes the ownership of environs. Free the returned array with g_strfreev().
gchar **sanitize_environs(gchar **environs)
{
#ifdef DETAIL
	g_debug("! Launch sanitize_environs() with environs = %p", environs);
#endif
	if (environs==NULL) return NULL;

	GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	GPtrArray *cleaned = g_ptr_array_new();
	gint total = g_strv_length(environs), i;

	// walk backwards: the first one we meet for a name is the last one set.
	for (i=total-1; i>=0; i--)
	{
		const gchar *equal = strchr(environs[i], '=');
		if ((equal==NULL) || (equal==environs[i]))
		{
#ifdef DEBUG
			g_debug("sanitize_environs(): drop the invalid environment entry \"%s\"", environs[i]);
#endif
			g_free(environs[i]);
			continue;
		}
		gchar *name = g_strndup(environs[i], equal - environs[i]);
		if (g_hash_table_contains(seen, name))
		{
			g_free(name);
			g_free(environs[i]);
			continue;
		}
		g_hash_table_add(seen, name);
		g_ptr_array_add(cleaned, environs[i]);
	}
	g_hash_table_destroy(seen);
	// only the container: the strings are owned by cleaned or already freed.
	g_free(environs);

	// restore the original order
	guint head = 0, tail = cleaned->len;
	while (tail && (head < --tail))
	{
		gpointer swap = cleaned->pdata[head];
		cleaned->pdata[head++] = cleaned->pdata[tail];
		cleaned->pdata[tail] = swap;
	}
	g_ptr_array_add(cleaned, NULL);
	return (gchar **) g_ptr_array_free(cleaned, FALSE);
}

// The icon names that GTK+ 3 itself uses for the stock ids LilyTerm uses.
static const struct { const gchar *stock_id; const gchar *icon_name; } stock_icons[] = {
	{ "gtk-about",			"help-about" },
	{ "gtk-add",			"list-add" },
	{ "gtk-clear",			"edit-clear" },
	{ "gtk-close",			"window-close" },
	{ "gtk-copy",			"edit-copy" },
	{ "gtk-dialog-authentication",	"dialog-password" },
	{ "gtk-dialog-error",		"dialog-error" },
	{ "gtk-dialog-info",		"dialog-information" },
	{ "gtk-dialog-question",	"dialog-question" },
	{ "gtk-dialog-warning",		"dialog-warning" },
	{ "gtk-execute",		"system-run" },
	{ "gtk-file",			"text-x-generic" },
	{ "gtk-find",			"edit-find" },
	{ "gtk-go-back",		"go-previous" },
	{ "gtk-go-down",		"go-down" },
	{ "gtk-go-up",			"go-up" },
	{ "gtk-goto-bottom",		"go-bottom" },
	{ "gtk-goto-top",		"go-top" },
	{ "gtk-help",			"help-browser" },
	{ "gtk-new",			"document-new" },
	{ "gtk-open",			"document-open" },
	{ "gtk-paste",			"edit-paste" },
	{ "gtk-preferences",		"preferences-system" },
	{ "gtk-properties",		"document-properties" },
	{ "gtk-quit",			"application-exit" },
	{ "gtk-refresh",		"view-refresh" },
	{ "gtk-revert-to-saved",	"document-revert" },
	{ "gtk-save",			"document-save" },
	{ "gtk-save-as",		"document-save-as" },
	{ "gtk-zoom-100",		"zoom-original" },
	{ "gtk-zoom-in",		"zoom-in" },
	{ "gtk-zoom-out",		"zoom-out" },
};

// An image for a stock id, from the current icon theme.
// Small icons prefer the symbolic variant, as GTK+ 3 applications do, and dialog
// icons the full color one; each falls back to the other. Icons that are only
// there as SVG are skipped by a GTK+ without SVG support (the static build),
// which then finds PNG ones (an older theme, or GTK+'s own built-in icons).
// If no theme has the icon at all, the image stays empty: better than a
// "missing image" placeholder.
GtkWidget *lilyterm_image_new_from_stock(const gchar *stock_id, GtkIconSize size)
{
#ifdef DETAIL
	g_debug("! Launch lilyterm_image_new_from_stock() with stock_id = %s, size = %d", stock_id, size);
#endif
	const gchar *icon_name = NULL;
	guint i;
	for (i=0; stock_id && (i<G_N_ELEMENTS(stock_icons)); i++)
		if (! strcmp(stock_id, stock_icons[i].stock_id))
			icon_name = stock_icons[i].icon_name;
	if (icon_name==NULL) return gtk_image_new();

	gchar *symbolic = g_strconcat(icon_name, "-symbolic", NULL);
	gchar *names[3] = {symbolic, (gchar *)icon_name, NULL};
	if (size >= GTK_ICON_SIZE_DND)
	{
		names[0] = (gchar *)icon_name;
		names[1] = symbolic;
	}
	GIcon *gicon = g_themed_icon_new_from_names(names, 2);

	gint width = 16, height = 16;
	gtk_icon_size_lookup(size, &width, &height);
	GtkIconInfo *info = gtk_icon_theme_lookup_by_gicon(gtk_icon_theme_get_default(), gicon,
							   MAX(width, height), 0);
	GtkWidget *image;
	if (info)
	{
		image = gtk_image_new_from_gicon(gicon, size);
		g_object_unref(info);
	}
	else
		image = gtk_image_new();
	g_object_unref(gicon);
	g_free(symbolic);
	return image;
}

// VTE deprecated these without a replacement: other encodings than UTF-8
// (vte 0.54) and allow_bold (vte 0.60). They still work, so LilyTerm keeps
// them, here in one place.
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
const gchar *lilyterm_vte_get_encoding(VteTerminal *vte)
{
	return vte_terminal_get_encoding(vte);
}

void lilyterm_vte_set_encoding(VteTerminal *vte, const gchar *encoding)
{
	vte_terminal_set_encoding(vte, encoding);
}

void lilyterm_vte_set_allow_bold(VteTerminal *vte, gboolean allow_bold)
{
	vte_terminal_set_allow_bold(vte, allow_bold);
}
G_GNUC_END_IGNORE_DEPRECATIONS

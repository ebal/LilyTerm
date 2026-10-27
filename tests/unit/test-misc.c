/*
 * Unit tests for the string and parsing helpers of src/misc.c: the ones that
 * read the profile, the command line and the socket data.
 *
 * tests/unit/run.sh builds and runs them; docker/run-tests.sh runs that too,
 * also with the sanitizers (SANITIZE=1).
 */

#include <glib/gstdio.h>

#include "misc.h"

// The globals of the rest of LilyTerm that misc.c uses
gboolean proc_exist = FALSE;
gchar *proc_file_system_path = NULL;
const gchar *SYSTEM_VTE_CJK_WIDTH_STR = NULL;

// ---- escape sequences (profile values) ----

static void test_escape_round_trip(void)
{
	const gchar *strings[] = {
		"plain", "", "tab\there", "new\nline", "back\\slash", "quote\" and '",
		"\a\b\f\n\r\t\v", "end with a backslash\\", NULL };
	gint i;
	for (i=0; strings[i]; i++)
	{
		gchar *escaped = convert_escape_sequence_to_string(strings[i]);
		gchar *back = convert_escape_sequence_from_string(escaped);
		g_assert_cmpstr(back, ==, strings[i]);
		g_free(escaped);
		g_free(back);
	}
}

static void test_escape_from_string(void)
{
	struct { const gchar *in, *out; } cases[] = {
		{ "a\\nb", "a\nb" },
		{ "\\\\", "\\" },
		{ "\\x", "x" },			// an unknown escape keeps the character
		{ "abc\\", "abc\\" },		// a backslash at the very end stays
		{ "\\", "\\" },
		{ "", "" },
	};
	guint i;
	for (i=0; i<G_N_ELEMENTS(cases); i++)
	{
		gchar *out = convert_escape_sequence_from_string(cases[i].in);
		g_assert_cmpstr(out, ==, cases[i].out);
		g_free(out);
	}
	g_assert_null(convert_escape_sequence_from_string(NULL));
	g_assert_null(convert_escape_sequence_to_string(NULL));
}

// ---- strings and arrays ----

static void test_convert_array_to_string(void)
{
	gchar *three[] = { "a", "b", "c", NULL };
	gchar *none[] = { NULL };

	gchar *str = convert_array_to_string(three, '\t');
	g_assert_cmpstr(str, ==, "a\tb\tc");
	g_free(str);

	str = convert_array_to_string(three, '\0');
	g_assert_cmpstr(str, ==, "abc");
	g_free(str);

	str = convert_array_to_string(none, ',');
	g_assert_cmpstr(str, ==, "");
	g_free(str);

	g_assert_null(convert_array_to_string(NULL, ','));
}

static void test_split_string(void)
{
	gchar **datas = split_string("a\xFF" "b\xFF" "c", SEPARATE_STR, 3);
	g_assert_nonnull(datas);
	g_assert_cmpuint(g_strv_length(datas), ==, 3);
	g_assert_cmpstr(datas[0], ==, "a");
	g_assert_cmpstr(datas[2], ==, "c");
	g_strfreev(datas);

	// The last token keeps the rest, separators included
	datas = split_string("a,b,c,d", ",", 2);
	g_assert_cmpstr(datas[1], ==, "b,c,d");
	g_strfreev(datas);

	// Fewer tokens than asked for (truncated socket data): NULL, with a warning
	g_test_expect_message(G_LOG_DOMAIN, G_LOG_LEVEL_WARNING, "*can NOT splite*");
	g_assert_null(split_string("a,b", ",", 3));
	g_test_assert_expected_messages();

	g_assert_null(split_string(NULL, ",", 2));
	g_assert_null(split_string("a,b", NULL, 2));
	g_assert_null(split_string("a,b", "", 2));
}

static void test_count_and_compare(void)
{
	g_assert_cmpint(count_char_in_string("a,b,,c", ','), ==, 3);
	g_assert_cmpint(count_char_in_string("", ','), ==, 0);
	g_assert_cmpint(count_char_in_string(NULL, ','), ==, -1);

	// compare_strings() returns FALSE when they are the same
	g_assert_false(compare_strings("UTF-8", "UTF-8", TRUE));
	g_assert_true(compare_strings("UTF-8", "utf-8", TRUE));
	g_assert_false(compare_strings("UTF-8", "utf-8", FALSE));
	g_assert_false(compare_strings(NULL, NULL, TRUE));
	g_assert_true(compare_strings("a", NULL, TRUE));

	gchar *list[] = { "bash", "zsh", NULL };
	g_assert_true(check_string_in_array("zsh", list));
	g_assert_false(check_string_in_array("fish", list));
	g_assert_false(check_string_in_array(NULL, list));
	g_assert_false(check_string_in_array("zsh", NULL));
}

// ---- -g / --geometry ----

static void test_parse_geometry(void)
{
	gint x = -1, y = -1;
	guint w = 0, h = 0;

	g_assert_cmpint(parse_geometry_str("80x24", &x, &y, &w, &h), ==,
			GEOMETRY_HAS_WIDTH | GEOMETRY_HAS_HEIGHT);
	g_assert_cmpuint(w, ==, 80);
	g_assert_cmpuint(h, ==, 24);

	g_assert_cmpint(parse_geometry_str("=100x30+10-20", &x, &y, &w, &h), ==,
			GEOMETRY_HAS_WIDTH | GEOMETRY_HAS_HEIGHT | GEOMETRY_HAS_X |
			GEOMETRY_HAS_Y | GEOMETRY_Y_NEGATIVE);
	g_assert_cmpuint(w, ==, 100);
	g_assert_cmpint(x, ==, 10);
	g_assert_cmpint(y, ==, -20);

	g_assert_cmpint(parse_geometry_str("x40", NULL, NULL, NULL, &h), ==, GEOMETRY_HAS_HEIGHT);
	g_assert_cmpuint(h, ==, 40);
	g_assert_cmpint(parse_geometry_str("-5+7", &x, &y, NULL, NULL), ==,
			GEOMETRY_HAS_X | GEOMETRY_X_NEGATIVE | GEOMETRY_HAS_Y);
	g_assert_cmpint(x, ==, -5);

	// Not geometries
	const gchar *bad[] = { "", "80x", "80x24z", "x", "+", "80+", "abc",
			       "99999999999x24", "80x24+1+2+3", NULL };
	gint i;
	for (i=0; bad[i]; i++)
		g_assert_cmpint(parse_geometry_str(bad[i], &x, &y, &w, &h), ==, 0);
	g_assert_cmpint(parse_geometry_str(NULL, &x, &y, &w, &h), ==, 0);
}

// ---- the environment of a new terminal ----

static void test_sanitize_environs(void)
{
	gchar **env = g_new0(gchar *, 7);
	env[0] = g_strdup("PATH=/bin");
	env[1] = g_strdup("not-a-variable");	// a value that held a tab
	env[2] = g_strdup("=no-name");
	env[3] = g_strdup("LANG=C");
	env[4] = g_strdup("PATH=/usr/bin");	// set again: the last one wins
	env[5] = g_strdup("EMPTY=");

	gchar **clean = sanitize_environs(env);
	g_assert_cmpuint(g_strv_length(clean), ==, 3);
	// the order of the kept ones does not change
	g_assert_cmpstr(clean[0], ==, "LANG=C");
	g_assert_cmpstr(clean[1], ==, "PATH=/usr/bin");
	g_assert_cmpstr(clean[2], ==, "EMPTY=");
	g_strfreev(clean);

	g_assert_null(sanitize_environs(NULL));
}

// ---- /proc/PID/stat ----

static void test_split_pid_stat(void)
{
	const gchar *stats[] = {
		"1234 (bash) S 1000 1234 1234 34816 5678 4194304 0\n",
		"1234 (my prog) S 1000 1234 1234 34816 5678 4194304 0\n",
		"1234 (a) (b)) S 1000 1234 1234 34816 5678 4194304 0\n",
		"1234 (x(y) S 1000 1234 1234 34816 5678 4194304 0\n",
	};
	const gchar *comms[] = { "bash", "my prog", "a) (b)", "x(y" };
	guint i;
	for (i=0; i<G_N_ELEMENTS(stats); i++)
	{
		gchar **fields = split_pid_stat(stats[i], 11);
		g_assert_nonnull(fields);
		g_assert_cmpstr(fields[0], ==, "1234");
		g_assert_cmpstr(fields[2], ==, comms[i]);
		g_assert_cmpstr(fields[4], ==, "S");
		g_assert_cmpstr(fields[5], ==, "1000");	// ppid
		g_assert_cmpstr(fields[9], ==, "5678");	// tpgid
		g_strfreev(fields);

		// 4 fields: pid, "", comm and the rest
		fields = split_pid_stat(stats[i], 4);
		g_assert_nonnull(fields);
		g_assert_cmpstr(fields[2], ==, comms[i]);
		g_assert_null(fields[4]);
		g_strfreev(fields);
	}
	g_test_expect_message(G_LOG_DOMAIN, G_LOG_LEVEL_WARNING, "*can NOT splite*");
	g_assert_null(split_pid_stat("1234 (bash) S", 11));	// too few fields
	g_test_assert_expected_messages();
	g_assert_null(split_pid_stat(NULL, 11));
}

// ---- xdg-open -> open(1) on macOS ----

static void test_replace_xdg_open(void)
{
	struct { const gchar *in, *out; } cases[] = {
		{ "xdg-open", "open" },
		{ "/usr/bin/xdg-open", "open" },
		{ "xdg-open\t--some-flag", "open\t--some-flag" },
		{ "open", NULL },
		{ "firefox", NULL },
		{ "xdg-openx", NULL },
		{ "my-xdg-open", NULL },
		{ "", NULL },
	};
	guint i;
	for (i=0; i<G_N_ELEMENTS(cases); i++)
	{
		gchar *out = replace_xdg_open(cases[i].in);
		g_assert_cmpstr(out, ==, cases[i].out);
		g_free(out);
	}
	g_assert_null(replace_xdg_open(NULL));
}

// ---- a clicked email address -> mailto: URL ----

static void test_get_mailto_url(void)
{
	struct { const gchar *in, *out; } cases[] = {
		{ "user@example.com", "mailto:user@example.com" },
		{ "mailto:user@example.com", "mailto:user@example.com" },
		{ "MailTo:user@example.com", "MailTo:user@example.com" },
		{ "mailto", "mailto:mailto" },
		{ "", "mailto:" },
	};
	guint i;
	for (i=0; i<G_N_ELEMENTS(cases); i++)
	{
		gchar *out = get_mailto_url(cases[i].in);
		g_assert_cmpstr(out, ==, cases[i].out);
		g_free(out);
	}
	g_assert_null(get_mailto_url(NULL));
}

// ---- OSC 8 hyperlinks: which ones open, with what ----

static void test_get_hyperlink_tag(void)
{
	struct { const gchar *uri; gint tag; const gchar *url; } cases[] = {
		{ "https://example.com/a?b=c#d", TAG_WWW, "https://example.com/a?b=c#d" },
		{ "HTTP://example.com/", TAG_WWW, "HTTP://example.com/" },
		{ "ftp://example.com/pub", TAG_FTP, "ftp://example.com/pub" },
		{ "ftps://example.com/pub", TAG_FTP, "ftps://example.com/pub" },
		{ "mailto:lily@example.com", TAG_MAIL, "mailto:lily@example.com" },
		// ls --hyperlink: file://HOST/PATH, opened only on HOST
		{ "file://mybox/etc/hosts", TAG_FILE, "file:///etc/hosts" },
		{ "file://mybox.local/etc/hosts", TAG_FILE, "file:///etc/hosts" },
		{ "file://localhost/etc/hosts", TAG_FILE, "file:///etc/hosts" },
		{ "file:///etc/hosts", TAG_FILE, "file:///etc/hosts" },
		{ "file:///tmp/a%20b", TAG_FILE, "file:///tmp/a%20b" },
		{ "file://server/etc/hosts", -1, NULL },
		// never opened: another scheme, or none
		{ "ssh://example.com", -1, NULL },
		{ "javascript:alert(1)", -1, NULL },
		{ "x-custom-handler:run", -1, NULL },
		{ "/etc/hosts", -1, NULL },
		{ "-rf", -1, NULL },
		{ "", -1, NULL },
	};
	guint i;
	for (i=0; i<G_N_ELEMENTS(cases); i++)
	{
		gchar *url = (gchar *)"unchanged";
		gint tag = get_hyperlink_tag(cases[i].uri, "mybox", &url);
		g_assert_cmpint(tag, ==, cases[i].tag);
		g_assert_cmpstr(url, ==, cases[i].url);
		g_free(url);
	}
	gchar *url = (gchar *)"unchanged";
	g_assert_cmpint(get_hyperlink_tag(NULL, "mybox", &url), ==, -1);
	g_assert_null(url);
}

// ---- paths that a click opens ----

static void test_path_regex(void)
{
	// the text, and what the pattern finds in it (NULL: nothing)
	struct { const gchar *text, *match; } cases[] = {
		{ "cat /etc/hosts", "/etc/hosts" },
		{ "see /etc/hosts.", "/etc/hosts" },
		{ "~/notes.txt", "~/notes.txt" },
		{ "./build/x", "./build/x" },
		{ "../lib/x.c:12", "../lib/x.c:12" },
		{ "src/main.c:42:5: error: x", "src/main.c:42:5" },
		{ "main.c:42: warning", "main.c:42" },
		{ "In file src/a-b_c.h,", "src/a-b_c.h" },
		{ "--output=/tmp/out.log", "/tmp/out.log" },
		{ "\"/etc/hosts\"", "/etc/hosts" },
		{ "(/usr/lib)", "/usr/lib" },
		// no paths
		{ "this and/or that", NULL },
		{ "on 10/27 at noon", NULL },
		{ "main.c without a line", NULL },
		{ "https://example.com/a/b.html", NULL },
		{ "user@host:/srv/x", NULL },
		{ "a/b", NULL },
		{ "/", NULL },
	};
	GRegex *regex = g_regex_new(get_path_regex(), G_REGEX_OPTIMIZE, 0, NULL);
	g_assert_nonnull(regex);
	guint i;
	for (i=0; i<G_N_ELEMENTS(cases); i++)
	{
		GMatchInfo *info = NULL;
		gchar *match = NULL;
		if (g_regex_match(regex, cases[i].text, 0, &info))
			match = g_match_info_fetch(info, 0);
		if (g_strcmp0(match, cases[i].match))
			g_test_message("'%s': found '%s', expected '%s'", cases[i].text, match, cases[i].match);
		g_assert_cmpstr(match, ==, cases[i].match);
		g_free(match);
		g_match_info_free(info);
	}
	g_regex_unref(regex);
}

static void test_get_clicked_path(void)
{
	gchar *top = g_dir_make_tmp("lilyterm-path-XXXXXX", NULL);
	g_assert_nonnull(top);
	gchar *src = g_build_filename(top, "src", NULL);
	gchar *file = g_build_filename(top, "src", "main.c", NULL);
	gchar *notes = g_build_filename(top, "notes.txt", NULL);
	g_assert_cmpint(g_mkdir(src, 0700), ==, 0);
	g_assert_true(g_file_set_contents(file, "", 0, NULL));
	g_assert_true(g_file_set_contents(notes, "", 0, NULL));

	struct { const gchar *text; const gchar *dir; const gchar *want; } cases[] = {
		{ "src/main.c", top, file },
		{ "src/main.c:42", top, file },
		{ "src/main.c:42:5", top, file },
		{ "./src/main.c", top, file },
		{ "main.c:3", src, file },
		{ "../notes.txt", src, notes },
		{ "~/notes.txt", "/nowhere", notes },
		{ "~", "/nowhere", top },
		{ file, "/nowhere", file },
		{ "src", top, src },
		// no such file: nothing to open
		{ "src/other.c", top, NULL },
		{ "src/main.c", "/nowhere", NULL },
		{ "src/main.c", NULL, NULL },
		{ "", top, NULL },
	};
	guint i;
	for (i=0; i<G_N_ELEMENTS(cases); i++)
	{
		// "~" is TOP as the home
		gchar *path = get_clicked_path(cases[i].text, cases[i].dir, top);
		g_assert_cmpstr(path, ==, cases[i].want);
		g_free(path);
	}
	g_assert_null(get_clicked_path(NULL, top, top));
	g_assert_null(get_clicked_path("~/notes.txt", top, NULL));

	g_remove(file);
	g_remove(notes);
	g_rmdir(src);
	g_rmdir(top);
	g_free(file);
	g_free(notes);
	g_free(src);
	g_free(top);
}

// ---- a click on a program shows its directory, it does not run it ----

static void test_get_file_to_open(void)
{
	gchar *top = g_dir_make_tmp("lilyterm-open-XXXXXX", NULL);
	g_assert_nonnull(top);
	gchar *script = g_build_filename(top, "build.sh", NULL);
	gchar *text = g_build_filename(top, "notes.txt", NULL);
	gchar *dir = g_build_filename(top, "src", NULL);
	gchar *app = g_build_filename(top, "Calculator.app", NULL);
	gchar *app_slash = g_strconcat(app, "/", NULL);
	g_assert_true(g_file_set_contents(script, "#!/bin/sh\n", -1, NULL));
	g_assert_cmpint(g_chmod(script, 0755), ==, 0);
	g_assert_true(g_file_set_contents(text, "", 0, NULL));
	g_assert_cmpint(g_mkdir(dir, 0700), ==, 0);
	g_assert_cmpint(g_mkdir(app, 0700), ==, 0);

	struct { const gchar *path, *want; } cases[] = {
		{ script, top },	// a program: its directory
		{ app, top },		// an application bundle (macOS)
		{ app_slash, top },
		{ text, text },		// a file: itself
		{ dir, dir },		// a directory: itself
		{ "/nowhere/x", "/nowhere/x" },
	};
	guint i;
	for (i=0; i<G_N_ELEMENTS(cases); i++)
	{
		gchar *out = get_file_to_open(cases[i].path);
		g_assert_cmpstr(out, ==, cases[i].want);
		g_free(out);
	}
	g_assert_null(get_file_to_open(NULL));

	g_remove(script);
	g_remove(text);
	g_rmdir(dir);
	g_rmdir(app);
	g_rmdir(top);
	g_free(app_slash);
	g_free(app);
	g_free(dir);
	g_free(text);
	g_free(script);
	g_free(top);
}

static void test_is_remote_login(void)
{
	const gchar *remote[] = { "ssh", "mosh-client", "et", "telnet", "autossh" };
	const gchar *local[] = { "bash", "zsh", "vim", "make", "less", "sshd", "ssh-agent", "sshfs", "" };
	guint i;
	for (i=0; i<G_N_ELEMENTS(remote); i++)
		g_assert_true(is_remote_login(remote[i]));
	for (i=0; i<G_N_ELEMENTS(local); i++)
		g_assert_false(is_remote_login(local[i]));
	g_assert_false(is_remote_login(NULL));
}

// ---- copy: no blanks at the end of the lines ----

static void test_strip_trailing_blanks(void)
{
	struct { const gchar *in, *out; } cases[] = {
		{ "copy me   ", "copy me" },
		{ "a  \nb\t\t\nc", "a\nb\nc" },
		{ "file1    file2    \nfile3    \n", "file1    file2\nfile3\n" },
		{ "dos  \r\nline \r\n", "dos\r\nline\r\n" },
		{ "   \n", "\n" },
		{ "  lead and  inner  ", "  lead and  inner" },
		{ "\n\n", "\n\n" },
		{ "", "" },
	};
	guint i;
	for (i=0; i<G_N_ELEMENTS(cases); i++)
	{
		gchar *out = strip_trailing_blanks(cases[i].in);
		g_assert_cmpstr(out, ==, cases[i].out);
		g_free(out);
	}
	g_assert_null(strip_trailing_blanks(NULL));
}

// ---- the directory as a tab shows it ----

static void test_get_short_dir_name(void)
{
	struct { const gchar *dir, *home, *want; } cases[] = {
		{ "/home/lily", "/home/lily", "~" },
		{ "/home/lily/", "/home/lily", "~" },
		{ "/home/lily/src/LilyTerm", "/home/lily", "~/src/LilyTerm" },
		{ "/home/lily/src", "/home/lily/", "~/src" },
		{ "/Users/lily/Documents", "/Users/lily", "~/Documents" },
		// not in the home
		{ "/home/lilyterm", "/home/lily", "/home/lilyterm" },
		{ "/home", "/home/lily", "/home" },
		{ "/etc", "/home/lily", "/etc" },
		// no usable home
		{ "/etc", "/", "/etc" },
		{ "/home/lily", NULL, "/home/lily" },
		{ "/home/lily", "", "/home/lily" },
		{ "/home/lily", "relative", "/home/lily" },
	};
	guint i;
	for (i=0; i<G_N_ELEMENTS(cases); i++)
	{
		gchar *name = get_short_dir_name(cases[i].dir, cases[i].home);
		g_assert_cmpstr(name, ==, cases[i].want);
		g_free(name);
	}
	g_assert_null(get_short_dir_name(NULL, "/home/lily"));
}

// ---- the regex of the search bar ----

static void test_get_search_pattern(void)
{
	struct { const gchar *in; gboolean regex; const gchar *out; } cases[] = {
		{ "needle", FALSE, "needle" },
		{ "a.b", FALSE, "a\\.b" },
		{ "(x)*[y]?", FALSE, "\\(x\\)\\*\\[y\\]\\?" },
		{ "a.b", TRUE, "a.b" },
		{ "", FALSE, NULL },
		{ "", TRUE, NULL },
	};
	guint i;
	for (i=0; i<G_N_ELEMENTS(cases); i++)
	{
		gchar *out = get_search_pattern(cases[i].in, cases[i].regex);
		g_assert_cmpstr(out, ==, cases[i].out);
		g_free(out);
	}
	g_assert_null(get_search_pattern(NULL, FALSE));

	// The text of the search stays as it is: each search escapes it once
	// (it was escaped in place before, again with every search)
	gchar text[] = "a.b";
	gint n;
	for (n=0; n<3; n++)
	{
		gchar *out = get_search_pattern(text, FALSE);
		g_assert_cmpstr(out, ==, "a\\.b");
		g_free(out);
	}
	g_assert_cmpstr(text, ==, "a.b");
}

// ---- the directory a shell reports with OSC 7 ----

static void test_get_local_dir_from_uri(void)
{
	struct { const gchar *uri, *host_name, *dir; } cases[] = {
		{ "file:///home/me", "box", "/home/me" },
		{ "file://localhost/tmp", "box", "/tmp" },
		{ "file://box/srv/a%20b", "box", "/srv/a b" },
		{ "file://BOX/srv", "box", "/srv" },
		{ "file://mac.local/Users/me", "mac", "/Users/me" },
		{ "file://mac/Users/me", "mac.local", "/Users/me" },
		{ "file://server/home/me", "box", NULL },	// a shell over ssh
		{ "file://boxer/home/me", "box", NULL },
		{ "file://build.prod.example/srv", "build.home", NULL },	// only the first label is the same
		{ "file://box.example/srv", "box", NULL },
		{ "http://box/home", "box", NULL },
		{ "/home/me", "box", NULL },
		{ "", "box", NULL },
	};
	guint i;
	for (i=0; i<G_N_ELEMENTS(cases); i++)
	{
		gchar *dir = get_local_dir_from_uri(cases[i].uri, cases[i].host_name);
		g_assert_cmpstr(dir, ==, cases[i].dir);
		g_free(dir);
	}
	g_assert_null(get_local_dir_from_uri(NULL, "box"));
}

// ---- the US key of a macOS key code (Cmd keys with other layouts) ----

static void test_macos_us_keyval(void)
{
	g_assert_cmpint(macos_us_keyval(0x08, FALSE), ==, GDK_KEY_C);	// kVK_ANSI_C
	g_assert_cmpint(macos_us_keyval(0x09, FALSE), ==, GDK_KEY_V);
	g_assert_cmpint(macos_us_keyval(0x11, TRUE), ==, GDK_KEY_T);	// a letter stays upper case
	g_assert_cmpint(macos_us_keyval(0x04, FALSE), ==, GDK_KEY_H);
	g_assert_cmpint(macos_us_keyval(0x12, FALSE), ==, GDK_KEY_1);
	g_assert_cmpint(macos_us_keyval(0x1D, FALSE), ==, GDK_KEY_0);
	g_assert_cmpint(macos_us_keyval(0x18, FALSE), ==, GDK_KEY_equal);
	g_assert_cmpint(macos_us_keyval(0x21, FALSE), ==, GDK_KEY_bracketleft);
	g_assert_cmpint(macos_us_keyval(0x21, TRUE), ==, GDK_KEY_braceleft);
	g_assert_cmpint(macos_us_keyval(0x1E, TRUE), ==, GDK_KEY_braceright);
	g_assert_cmpint(macos_us_keyval(0x0A, FALSE), ==, 0);		// no key there
	g_assert_cmpint(macos_us_keyval(0x24, FALSE), ==, 0);		// Return: not a key of the table
	g_assert_cmpint(macos_us_keyval(0xFFFF, FALSE), ==, 0);
}

// ---- the "join the lines" paste ----

static void test_join_pasted_lines(void)
{
	struct { const gchar *in, *out; } cases[] = {
		{ "a\nb", "a b" },
		{ "a\r\nb", "a b" },
		{ "ls \\\n  -l", "ls -l" },		// a line continuation
		{ "ls\\\n-l", "ls-l" },
		{ "a\\\n> b", "ab" },			// the "> " prompt of a continued line
		{ "a\n> b", "a b" },
		{ "  a \t b  \n", "a b" },
		{ "a\\\n", "a" },			// ends with a continuation: read past the end before 0.12.0
		{ "a\\\n> ", "a" },
		{ "\\\n", "" },
		{ "\n", "" },				// nothing left: read before the start before 0.12.0
		{ "  \n ", "" },
		{ "", "" },
		{ "a\\b", "a\\b" },			// a backslash in the line stays
	};
	guint i;
	for (i=0; i<G_N_ELEMENTS(cases); i++)
	{
		gchar *out = join_pasted_lines(cases[i].in);
		g_assert_cmpstr(out, ==, cases[i].out);
		g_free(out);
	}
	g_assert_null(join_pasted_lines(NULL));
}

int main(int argc, char *argv[])
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/misc/escape/round-trip", test_escape_round_trip);
	g_test_add_func("/misc/escape/from-string", test_escape_from_string);
	g_test_add_func("/misc/convert-array-to-string", test_convert_array_to_string);
	g_test_add_func("/misc/split-string", test_split_string);
	g_test_add_func("/misc/count-and-compare", test_count_and_compare);
	g_test_add_func("/misc/parse-geometry", test_parse_geometry);
	g_test_add_func("/misc/sanitize-environs", test_sanitize_environs);
	g_test_add_func("/misc/join-pasted-lines", test_join_pasted_lines);
	g_test_add_func("/misc/split-pid-stat", test_split_pid_stat);
	g_test_add_func("/misc/replace-xdg-open", test_replace_xdg_open);
	g_test_add_func("/misc/mailto-url", test_get_mailto_url);
	g_test_add_func("/misc/search-pattern", test_get_search_pattern);
	g_test_add_func("/misc/hyperlink-tag", test_get_hyperlink_tag);
	g_test_add_func("/misc/path-regex", test_path_regex);
	g_test_add_func("/misc/clicked-path", test_get_clicked_path);
	g_test_add_func("/misc/short-dir-name", test_get_short_dir_name);
	g_test_add_func("/misc/strip-trailing-blanks", test_strip_trailing_blanks);
	g_test_add_func("/misc/file-to-open", test_get_file_to_open);
	g_test_add_func("/misc/remote-login", test_is_remote_login);
	g_test_add_func("/misc/local-dir-from-uri", test_get_local_dir_from_uri);
	g_test_add_func("/misc/macos-us-keyval", test_macos_us_keyval);
	return g_test_run();
}

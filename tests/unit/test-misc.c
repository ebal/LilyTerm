/*
 * Unit tests for the string and parsing helpers of src/misc.c: the ones that
 * read the profile, the command line and the socket data.
 *
 * tests/unit/run.sh builds and runs them; docker/run-tests.sh runs that too,
 * also with the sanitizers (SANITIZE=1).
 */

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
	g_test_add_func("/misc/local-dir-from-uri", test_get_local_dir_from_uri);
	g_test_add_func("/misc/macos-us-keyval", test_macos_us_keyval);
	return g_test_run();
}

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

#define _GNU_SOURCE

#include <gtk/gtk.h>
#include <vte/vte.h>
// for L10n
#include <locale.h>
#include <glib/gi18n.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "lilyterm.h"
void get_hint_min_size(GtkWidget *notebook, GtkWidget *scrollbar, gint *min_width, gint *min_height);

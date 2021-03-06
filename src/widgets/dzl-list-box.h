/* dzl-list-box.h
 *
 * Copyright (C) 2016 Christian Hergert <chergert@redhat.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef DZL_LIST_BOX_H
#define DZL_LIST_BOX_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define DZL_TYPE_LIST_BOX (dzl_list_box_get_type())

G_DECLARE_DERIVABLE_TYPE (DzlListBox, dzl_list_box, DZL, LIST_BOX, GtkListBox)

struct _DzlListBoxClass
{
  GtkListBoxClass parent_class;

  gpointer _reserved1;
  gpointer _reserved2;
  gpointer _reserved3;
  gpointer _reserved4;
};

DzlListBox  *dzl_list_box_new               (GType        row_type,
                                             const gchar *property_name);
GType        dzl_list_box_get_row_type      (DzlListBox  *self);
const gchar *dzl_list_box_get_property_name (DzlListBox  *self);
GListModel  *dzl_list_box_get_model         (DzlListBox  *self);
void         dzl_list_box_set_model         (DzlListBox  *self,
                                             GListModel  *model);

G_END_DECLS

#endif /* DZL_LIST_BOX_H */

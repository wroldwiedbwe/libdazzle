/* dzl-shortcut-manager.c
 *
 * Copyright (C) 2016 Christian Hergert <chergert@redhat.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
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

#define G_LOG_DOMAIN "dzl-shortcut-manager.h"

#include "dzl-shortcut-private.h"

#include "dzl-shortcut-controller.h"
#include "dzl-shortcut-label.h"
#include "dzl-shortcut-manager.h"
#include "dzl-shortcut-private.h"
#include "dzl-shortcuts-group.h"
#include "dzl-shortcuts-section.h"
#include "dzl-shortcuts-shortcut.h"

typedef struct
{
  DzlShortcutTheme *theme;
  GPtrArray        *themes;
  gchar            *user_dir;
  GNode            *root;
  GQueue            search_path;
} DzlShortcutManagerPrivate;

enum {
  PROP_0,
  PROP_THEME,
  PROP_THEME_NAME,
  PROP_USER_DIR,
  N_PROPS
};

enum {
  CHANGED,
  N_SIGNALS
};

static void list_model_iface_init (GListModelInterface *iface);
static void initable_iface_init   (GInitableIface      *iface);

G_DEFINE_TYPE_WITH_CODE (DzlShortcutManager, dzl_shortcut_manager, G_TYPE_OBJECT,
                         G_ADD_PRIVATE (DzlShortcutManager)
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, initable_iface_init)
                         G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, list_model_iface_init))

static GParamSpec *properties [N_PROPS];
static guint signals [N_SIGNALS];

static gboolean
free_node_data (GNode    *node,
                gpointer  user_data)
{
  DzlShortcutNodeData *data = node->data;

  g_slice_free (DzlShortcutNodeData, data);

  return FALSE;
}

static void
dzl_shortcut_manager_finalize (GObject *object)
{
  DzlShortcutManager *self = (DzlShortcutManager *)object;
  DzlShortcutManagerPrivate *priv = dzl_shortcut_manager_get_instance_private (self);

  if (priv->root != NULL)
    {
      g_node_traverse (priv->root, G_IN_ORDER, G_TRAVERSE_ALL, -1, free_node_data, NULL);
      g_node_destroy (priv->root);
      priv->root = NULL;
    }

  g_clear_pointer (&priv->themes, g_ptr_array_unref);
  g_clear_pointer (&priv->user_dir, g_free);
  g_clear_object (&priv->theme);

  G_OBJECT_CLASS (dzl_shortcut_manager_parent_class)->finalize (object);
}

static void
dzl_shortcut_manager_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  DzlShortcutManager *self = (DzlShortcutManager *)object;

  switch (prop_id)
    {
    case PROP_THEME:
      g_value_set_object (value, dzl_shortcut_manager_get_theme (self));
      break;

    case PROP_THEME_NAME:
      g_value_set_string (value, dzl_shortcut_manager_get_theme_name (self));
      break;

    case PROP_USER_DIR:
      g_value_set_string (value, dzl_shortcut_manager_get_user_dir (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
dzl_shortcut_manager_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  DzlShortcutManager *self = (DzlShortcutManager *)object;

  switch (prop_id)
    {
    case PROP_THEME:
      dzl_shortcut_manager_set_theme (self, g_value_get_object (value));
      break;

    case PROP_THEME_NAME:
      dzl_shortcut_manager_set_theme_name (self, g_value_get_string (value));
      break;

    case PROP_USER_DIR:
      dzl_shortcut_manager_set_user_dir (self, g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
dzl_shortcut_manager_class_init (DzlShortcutManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = dzl_shortcut_manager_finalize;
  object_class->get_property = dzl_shortcut_manager_get_property;
  object_class->set_property = dzl_shortcut_manager_set_property;

  properties [PROP_THEME] =
    g_param_spec_object ("theme",
                         "Theme",
                         "The current key theme.",
                         DZL_TYPE_SHORTCUT_THEME,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_THEME_NAME] =
    g_param_spec_string ("theme-name",
                         "Theme Name",
                         "The name of the current theme",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_USER_DIR] =
    g_param_spec_string ("user-dir",
                         "User Dir",
                         "The directory for saved user modifications",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  signals [CHANGED] =
    g_signal_new ("changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
dzl_shortcut_manager_init (DzlShortcutManager *self)
{
  DzlShortcutManagerPrivate *priv = dzl_shortcut_manager_get_instance_private (self);

  priv->themes = g_ptr_array_new_with_free_func (g_object_unref);
  priv->root = g_node_new (NULL);
}

static void
dzl_shortcut_manager_load_directory (DzlShortcutManager  *self,
                                     const gchar         *directory,
                                     GCancellable        *cancellable)
{
  g_autoptr(GDir) dir = NULL;
  const gchar *name;

  g_assert (DZL_IS_SHORTCUT_MANAGER (self));
  g_assert (directory != NULL);
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  if (!g_file_test (directory, G_FILE_TEST_IS_DIR))
    return;

  if (NULL == (dir = g_dir_open (directory, 0, NULL)))
    return;

  while (NULL != (name = g_dir_read_name (dir)))
    {
      g_autofree gchar *path = g_build_filename (directory, name, NULL);
      g_autoptr(DzlShortcutTheme) theme = NULL;
      g_autoptr(GError) local_error = NULL;

      theme = dzl_shortcut_theme_new (NULL);

      if (dzl_shortcut_theme_load_from_path (theme, path, cancellable, &local_error))
        dzl_shortcut_manager_add_theme (self, theme);
      else
        g_warning ("%s", local_error->message);
    }
}

static void
dzl_shortcut_manager_load_resources (DzlShortcutManager *self,
                                     const gchar        *resource_dir,
                                     GCancellable       *cancellable)
{
  g_auto(GStrv) children = NULL;

  g_assert (DZL_IS_SHORTCUT_MANAGER (self));
  g_assert (resource_dir != NULL);
  g_assert (g_str_has_prefix (resource_dir, "resource://"));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  children = g_resources_enumerate_children (resource_dir, 0, NULL);

  if (children != NULL)
    {
      for (guint i = 0; children[i] != NULL; i++)
        {
          g_autofree gchar *path = g_build_filename (resource_dir, children[i], NULL);
          g_autoptr(DzlShortcutTheme) theme = NULL;
          g_autoptr(GError) local_error = NULL;
          g_autoptr(GBytes) bytes = NULL;
          const gchar *data;
          gsize len = 0;

          if (NULL == (bytes = g_resources_lookup_data (path, 0, NULL)))
            continue;

          data = g_bytes_get_data (bytes, &len);
          theme = dzl_shortcut_theme_new (NULL);

          if (dzl_shortcut_theme_load_from_data (theme, data, len, &local_error))
            dzl_shortcut_manager_add_theme (self, theme);
          else
            g_warning ("%s", local_error->message);
        }
    }
}

static gboolean
dzl_shortcut_manager_initiable_init (GInitable     *initable,
                                     GCancellable  *cancellable,
                                     GError       **error)
{
  DzlShortcutManager *self = (DzlShortcutManager *)initable;
  DzlShortcutManagerPrivate *priv = dzl_shortcut_manager_get_instance_private (self);

  g_assert (DZL_IS_SHORTCUT_MANAGER (self));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  for (const GList *iter = priv->search_path.tail; iter != NULL; iter = iter->prev)
    {
      const gchar *directory = iter->data;

      if (g_str_has_prefix (directory, "resource://"))
        dzl_shortcut_manager_load_resources (self, directory, cancellable);
      else
        dzl_shortcut_manager_load_directory (self, directory, cancellable);
    }

  return TRUE;
}

static void
initable_iface_init (GInitableIface *iface)
{
  iface->init = dzl_shortcut_manager_initiable_init;
}


/**
 * dzl_shortcut_manager_get_default:
 *
 * Gets the singleton #DzlShortcutManager for the process.
 *
 * Returns: (transfer none) (not nullable): An #DzlShortcutManager.
 */
DzlShortcutManager *
dzl_shortcut_manager_get_default (void)
{
  static DzlShortcutManager *instance;

  if (instance == NULL)
    {
      instance = g_object_new (DZL_TYPE_SHORTCUT_MANAGER, NULL);
      g_object_add_weak_pointer (G_OBJECT (instance), (gpointer *)&instance);
    }

  return instance;
}

/**
 * dzl_shortcut_manager_get_theme:
 * @self: (nullable): A #DzlShortcutManager or %NULL
 *
 * Gets the "theme" property.
 *
 * Returns: (transfer none) (not nullable): An #DzlShortcutTheme.
 */
DzlShortcutTheme *
dzl_shortcut_manager_get_theme (DzlShortcutManager *self)
{
  DzlShortcutManagerPrivate *priv;

  g_return_val_if_fail (!self || DZL_IS_SHORTCUT_MANAGER (self), NULL);

  if (self == NULL)
    self = dzl_shortcut_manager_get_default ();

  priv = dzl_shortcut_manager_get_instance_private (self);

  if (priv->theme == NULL)
    priv->theme = g_object_new (DZL_TYPE_SHORTCUT_THEME,
                                "name", "default",
                                NULL);

  return priv->theme;
}

/**
 * dzl_shortcut_manager_set_theme:
 * @self: An #DzlShortcutManager
 * @theme: (not nullable): An #DzlShortcutTheme
 *
 * Sets the theme for the shortcut manager.
 */
void
dzl_shortcut_manager_set_theme (DzlShortcutManager *self,
                                DzlShortcutTheme   *theme)
{
  DzlShortcutManagerPrivate *priv = dzl_shortcut_manager_get_instance_private (self);

  g_return_if_fail (DZL_IS_SHORTCUT_MANAGER (self));
  g_return_if_fail (DZL_IS_SHORTCUT_THEME (theme));

  /*
   * It is important that DzlShortcutController instances watch for
   * notify::theme so that they can reset their state. Otherwise, we
   * could be transitioning between incorrect contexts.
   */

  if (g_set_object (&priv->theme, theme))
    {
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_THEME]);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_THEME_NAME]);
    }
}

/**
 * dzl_shortcut_manager_handle_event:
 * @self: (nullable): An #DzlShortcutManager
 * @toplevel: A #GtkWidget or %NULL.
 * @event: A #GdkEventKey event to handle.
 *
 * This function will try to dispatch @event to the proper widget and
 * #DzlShortcutContext. If the event is handled, then %TRUE is returned.
 *
 * You should call this from #GtkWidget::key-press-event handler in your
 * #GtkWindow toplevel.
 *
 * Returns: %TRUE if the event was handled.
 */
gboolean
dzl_shortcut_manager_handle_event (DzlShortcutManager *self,
                                   const GdkEventKey  *event,
                                   GtkWidget          *toplevel)
{
  GtkWidget *widget;
  GtkWidget *focus;
  GdkModifierType modifier;

  g_return_val_if_fail (!self || DZL_IS_SHORTCUT_MANAGER (self), FALSE);
  g_return_val_if_fail (!toplevel || GTK_IS_WINDOW (toplevel), FALSE);
  g_return_val_if_fail (event != NULL, FALSE);

  if (self == NULL)
    self = dzl_shortcut_manager_get_default ();

  if (toplevel == NULL)
    {
      gpointer user_data;

      gdk_window_get_user_data (event->window, &user_data);
      g_return_val_if_fail (GTK_IS_WIDGET (user_data), FALSE);

      toplevel = gtk_widget_get_toplevel (user_data);
      g_return_val_if_fail (GTK_IS_WINDOW (toplevel), FALSE);
    }

  if (event->type != GDK_KEY_PRESS)
    return GDK_EVENT_PROPAGATE;

  g_assert (DZL_IS_SHORTCUT_MANAGER (self));
  g_assert (GTK_IS_WINDOW (toplevel));
  g_assert (event != NULL);

  modifier = event->state & gtk_accelerator_get_default_mod_mask ();
  widget = focus = gtk_window_get_focus (GTK_WINDOW (toplevel));

  while (widget != NULL)
    {
      g_autoptr(GtkWidget) widget_hold = g_object_ref (widget);
      DzlShortcutController *controller;
      gboolean use_binding_sets = TRUE;

      if (NULL != (controller = dzl_shortcut_controller_find (widget)))
        {
          DzlShortcutContext *context = dzl_shortcut_controller_get_context (controller);

          /*
           * Fetch this property first as the controller context could change
           * during activation of the handle_event().
           */
          if (context != NULL)
            g_object_get (context,
                          "use-binding-sets", &use_binding_sets,
                          NULL);

          /*
           * Now try to activate the event using the controller.
           */
          if (dzl_shortcut_controller_handle_event (controller, event))
            return GDK_EVENT_STOP;
        }

      /*
       * If the current context at activation indicates that we can
       * dispatch using the default binding sets for the widget, go
       * ahead and try to do that.
       */
      if (use_binding_sets)
        {
          GtkStyleContext *style_context;
          g_autoptr(GPtrArray) sets = NULL;

          style_context = gtk_widget_get_style_context (widget);
          gtk_style_context_get (style_context,
                                 gtk_style_context_get_state (style_context),
                                 "-gtk-key-bindings", &sets,
                                 NULL);

          if (sets != NULL)
            {
              for (guint i = 0; i < sets->len; i++)
                {
                  GtkBindingSet *set = g_ptr_array_index (sets, i);

                  if (gtk_binding_set_activate (set, event->keyval, modifier, G_OBJECT (widget)))
                    return GDK_EVENT_STOP;
                }
            }

          /*
           * Only if this widget is also our focus, try to activate the default
           * keybindings for the widget.
           */
          if (widget == focus)
            {
              GtkBindingSet *set = gtk_binding_set_by_class (G_OBJECT_GET_CLASS (widget));

              if (gtk_binding_set_activate (set, event->keyval, modifier, G_OBJECT (widget)))
                return GDK_EVENT_STOP;
            }
        }

      widget = gtk_widget_get_parent (widget);
    }

  return GDK_EVENT_PROPAGATE;
}

const gchar *
dzl_shortcut_manager_get_theme_name (DzlShortcutManager *self)
{
  DzlShortcutManagerPrivate *priv = dzl_shortcut_manager_get_instance_private (self);
  const gchar *ret = NULL;

  g_return_val_if_fail (DZL_IS_SHORTCUT_MANAGER (self), NULL);

  if (priv->theme != NULL)
    ret = dzl_shortcut_theme_get_name (priv->theme);

  return ret;
}

void
dzl_shortcut_manager_set_theme_name (DzlShortcutManager *self,
                                     const gchar        *name)
{
  DzlShortcutManagerPrivate *priv = dzl_shortcut_manager_get_instance_private (self);

  g_return_if_fail (DZL_IS_SHORTCUT_MANAGER (self));

  if (name == NULL)
    name = "default";

  for (guint i = 0; i < priv->themes->len; i++)
    {
      DzlShortcutTheme *theme = g_ptr_array_index (priv->themes, i);
      const gchar *theme_name = dzl_shortcut_theme_get_name (theme);

      if (g_strcmp0 (name, theme_name) == 0)
        {
          dzl_shortcut_manager_set_theme (self, theme);
          return;
        }
    }

  g_warning ("No such shortcut theme “%s”", name);
}

static guint
dzl_shortcut_manager_get_n_items (GListModel *model)
{
  DzlShortcutManager *self = (DzlShortcutManager *)model;
  DzlShortcutManagerPrivate *priv = dzl_shortcut_manager_get_instance_private (self);

  g_return_val_if_fail (DZL_IS_SHORTCUT_MANAGER (self), 0);

  return priv->themes->len;
}

static GType
dzl_shortcut_manager_get_item_type (GListModel *model)
{
  return DZL_TYPE_SHORTCUT_THEME;
}

static gpointer
dzl_shortcut_manager_get_item (GListModel *model,
                               guint       position)
{
  DzlShortcutManager *self = (DzlShortcutManager *)model;
  DzlShortcutManagerPrivate *priv = dzl_shortcut_manager_get_instance_private (self);

  g_return_val_if_fail (DZL_IS_SHORTCUT_MANAGER (self), NULL);
  g_return_val_if_fail (position < priv->themes->len, NULL);

  return g_object_ref (g_ptr_array_index (priv->themes, position));
}

static void
list_model_iface_init (GListModelInterface *iface)
{
  iface->get_n_items = dzl_shortcut_manager_get_n_items;
  iface->get_item_type = dzl_shortcut_manager_get_item_type;
  iface->get_item = dzl_shortcut_manager_get_item;
}

void
dzl_shortcut_manager_add_theme (DzlShortcutManager *self,
                                DzlShortcutTheme   *theme)
{
  DzlShortcutManagerPrivate *priv = dzl_shortcut_manager_get_instance_private (self);
  guint position;

  g_return_if_fail (DZL_IS_SHORTCUT_MANAGER (self));
  g_return_if_fail (DZL_IS_SHORTCUT_THEME (theme));

  for (guint i = 0; i < priv->themes->len; i++)
    {
      if (g_ptr_array_index (priv->themes, i) == theme)
        {
          g_warning ("%s named %s has already been added",
                     G_OBJECT_TYPE_NAME (theme),
                     dzl_shortcut_theme_get_name (theme));
          return;
        }
    }

  position = priv->themes->len;

  g_ptr_array_add (priv->themes, g_object_ref (theme));

  _dzl_shortcut_theme_set_manager (theme, self);

  g_list_model_items_changed (G_LIST_MODEL (self), position, 0, 1);
}

void
dzl_shortcut_manager_remove_theme (DzlShortcutManager *self,
                                   DzlShortcutTheme   *theme)
{
  DzlShortcutManagerPrivate *priv = dzl_shortcut_manager_get_instance_private (self);

  g_return_if_fail (DZL_IS_SHORTCUT_MANAGER (self));
  g_return_if_fail (DZL_IS_SHORTCUT_THEME (theme));

  for (guint i = 0; i < priv->themes->len; i++)
    {
      if (g_ptr_array_index (priv->themes, i) == theme)
        {
          _dzl_shortcut_theme_set_manager (theme, NULL);
          g_ptr_array_remove_index (priv->themes, i);
          g_list_model_items_changed (G_LIST_MODEL (self), i, 1, 0);
          break;
        }
    }
}

const gchar *
dzl_shortcut_manager_get_user_dir (DzlShortcutManager *self)
{
  DzlShortcutManagerPrivate *priv = dzl_shortcut_manager_get_instance_private (self);

  g_return_val_if_fail (DZL_IS_SHORTCUT_MANAGER (self), NULL);

  if (priv->user_dir == NULL)
    {
      priv->user_dir = g_build_filename (g_get_user_data_dir (),
                                         g_get_prgname (),
                                         NULL);
    }

  return priv->user_dir;
}

void
dzl_shortcut_manager_set_user_dir (DzlShortcutManager *self,
                                   const gchar        *user_dir)
{
  DzlShortcutManagerPrivate *priv = dzl_shortcut_manager_get_instance_private (self);

  g_return_if_fail (DZL_IS_SHORTCUT_MANAGER (self));

  if (g_strcmp0 (user_dir, priv->user_dir) != 0)
    {
      g_free (priv->user_dir);
      priv->user_dir = g_strdup (user_dir);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_USER_DIR]);
    }
}

void
dzl_shortcut_manager_remove_search_path (DzlShortcutManager *self,
                                         const gchar        *directory)
{
  DzlShortcutManagerPrivate *priv;

  g_return_if_fail (!self || DZL_IS_SHORTCUT_MANAGER (self));
  g_return_if_fail (directory != NULL);

  if (self == NULL)
    self = dzl_shortcut_manager_get_default ();

  priv = dzl_shortcut_manager_get_instance_private (self);

  for (GList *iter = priv->search_path.head; iter != NULL; iter = iter->next)
    {
      gchar *path = iter->data;

      if (g_strcmp0 (path, directory) == 0)
        {
          /* TODO: Remove any merged keybindings */
          g_queue_delete_link (&priv->search_path, iter);
          g_free (path);
        }
    }
}

void
dzl_shortcut_manager_append_search_path (DzlShortcutManager *self,
                                         const gchar        *directory)
{
  DzlShortcutManagerPrivate *priv;

  g_return_if_fail (!self || DZL_IS_SHORTCUT_MANAGER (self));
  g_return_if_fail (directory != NULL);

  if (self == NULL)
    self = dzl_shortcut_manager_get_default ();

  priv = dzl_shortcut_manager_get_instance_private (self);

  g_queue_push_tail (&priv->search_path, g_strdup (directory));

  /* TODO: Reload keythemes */
}

void
dzl_shortcut_manager_prepend_search_path (DzlShortcutManager *self,
                                          const gchar        *directory)
{
  DzlShortcutManagerPrivate *priv;

  g_return_if_fail (!self || DZL_IS_SHORTCUT_MANAGER (self));
  g_return_if_fail (directory != NULL);

  if (self == NULL)
    self = dzl_shortcut_manager_get_default ();

  priv = dzl_shortcut_manager_get_instance_private (self);

  g_queue_push_head (&priv->search_path, g_strdup (directory));

  /* TODO: Reload keythemes */
}

/**
 * dzl_shortcut_manager_get_search_path:
 * @self: A #DzlShortcutManager
 *
 * This function will get the list of search path entries. These are used to
 * load themes for the application. You should set this search path for
 * themes before calling g_initable_init() on the search manager.
 *
 * Returns: (transfer none) (element-type utf8): A #GList containing each of
 *   the search path items used to load shortcut themes.
 */
const GList *
dzl_shortcut_manager_get_search_path (DzlShortcutManager *self)
{
  DzlShortcutManagerPrivate *priv;

  if (self == NULL)
    self = dzl_shortcut_manager_get_default ();

  priv = dzl_shortcut_manager_get_instance_private (self);

  return priv->search_path.head;
}

static GNode *
dzl_shortcut_manager_find_child (DzlShortcutManager  *self,
                                 GNode               *parent,
                                 DzlShortcutNodeType  type,
                                 const gchar         *name)
{
  DzlShortcutNodeData *data;

  g_assert (DZL_IS_SHORTCUT_MANAGER (self));
  g_assert (parent != NULL);
  g_assert (type != 0);
  g_assert (name != NULL);

  for (GNode *iter = parent->children; iter != NULL; iter = iter->next)
    {
      data = iter->data;

      if (data->type == type && data->name == name)
        return iter;
    }

  return NULL;
}

static GNode *
dzl_shortcut_manager_get_group (DzlShortcutManager *self,
                                const gchar        *section,
                                const gchar        *group)
{
  DzlShortcutManagerPrivate *priv = dzl_shortcut_manager_get_instance_private (self);
  DzlShortcutNodeData *data;
  GNode *parent;
  GNode *node;

  g_assert (DZL_IS_SHORTCUT_MANAGER (self));
  g_assert (section != NULL);
  g_assert (group != NULL);

  node = dzl_shortcut_manager_find_child (self, priv->root, DZL_SHORTCUT_NODE_SECTION, section);

  if (node == NULL)
    {
      data = g_slice_new0 (DzlShortcutNodeData);
      data->type = DZL_SHORTCUT_NODE_SECTION;
      data->name = g_intern_string (section);
      data->title = g_intern_string (section);
      data->subtitle = NULL;

      node = g_node_append_data (priv->root, data);
    }

  parent = node;

  node = dzl_shortcut_manager_find_child (self, parent, DZL_SHORTCUT_NODE_GROUP, group);

  if (node == NULL)
    {
      data = g_slice_new0 (DzlShortcutNodeData);
      data->type = DZL_SHORTCUT_NODE_GROUP;
      data->name = g_intern_string (group);
      data->title = g_intern_string (group);
      data->subtitle = NULL;

      node = g_node_append_data (parent, data);
    }

  g_assert (node != NULL);

  return node;
}

void
dzl_shortcut_manager_add_action (DzlShortcutManager *self,
                                 const gchar        *detailed_action_name,
                                 const gchar        *section,
                                 const gchar        *group,
                                 const gchar        *title,
                                 const gchar        *subtitle)
{
  DzlShortcutNodeData *data;
  GNode *parent;

  g_return_if_fail (!self || DZL_IS_SHORTCUT_MANAGER (self));
  g_return_if_fail (detailed_action_name != NULL);
  g_return_if_fail (title != NULL);

  if (self == NULL)
    self = dzl_shortcut_manager_get_default ();

  section = g_intern_string (section);
  group = g_intern_string (group);
  title = g_intern_string (title);
  subtitle = g_intern_string (subtitle);

  parent = dzl_shortcut_manager_get_group (self, section, group);

  g_assert (parent != NULL);

  data = g_slice_new0 (DzlShortcutNodeData);
  data->type = DZL_SHORTCUT_NODE_ACTION;
  data->name = g_intern_string (detailed_action_name);
  data->title = title;
  data->subtitle = subtitle;

  g_node_append_data (parent, data);

  g_signal_emit (self, signals [CHANGED], 0);
}

void
dzl_shortcut_manager_add_command (DzlShortcutManager *self,
                                  const gchar        *command,
                                  const gchar        *section,
                                  const gchar        *group,
                                  const gchar        *title,
                                  const gchar        *subtitle)
{
  DzlShortcutNodeData *data;
  GNode *parent;

  g_return_if_fail (!self || DZL_IS_SHORTCUT_MANAGER (self));
  g_return_if_fail (command != NULL);
  g_return_if_fail (title != NULL);

  if (self == NULL)
    self = dzl_shortcut_manager_get_default ();

  section = g_intern_string (section);
  group = g_intern_string (group);
  title = g_intern_string (title);
  subtitle = g_intern_string (subtitle);

  parent = dzl_shortcut_manager_get_group (self, section, group);

  g_assert (parent != NULL);

  data = g_slice_new0 (DzlShortcutNodeData);
  data->type = DZL_SHORTCUT_NODE_COMMAND;
  data->name = g_intern_string (command);
  data->title = title;
  data->subtitle = subtitle;

  g_node_append_data (parent, data);

  g_signal_emit (self, signals [CHANGED], 0);
}

static DzlShortcutsShortcut *
create_shortcut (const DzlShortcutChord *chord,
                 const gchar            *title,
                 const gchar            *subtitle)
{
  g_autofree gchar *accel = dzl_shortcut_chord_to_string (chord);

  return g_object_new (DZL_TYPE_SHORTCUTS_SHORTCUT,
                       "accelerator", accel,
                       "subtitle", subtitle,
                       "title", title,
                       "visible", TRUE,
                       NULL);
}

/**
 * dzl_shortcut_manager_add_shortcuts_to_window:
 * @self: A #DzlShortcutManager
 * @window: A #DzlShortcutsWindow
 *
 * Adds shortcuts registered with the #DzlShortcutManager to the
 * #DzlShortcutsWindow.
 */
void
dzl_shortcut_manager_add_shortcuts_to_window (DzlShortcutManager *self,
                                              DzlShortcutsWindow *window)
{
  DzlShortcutManagerPrivate *priv;
  GNode *parent;

  g_return_if_fail (!self || DZL_IS_SHORTCUT_MANAGER (self));
  g_return_if_fail (DZL_IS_SHORTCUTS_WINDOW (window));

  if (self == NULL)
    self = dzl_shortcut_manager_get_default ();

  priv = dzl_shortcut_manager_get_instance_private (self);

  /*
   * The GNode tree is in four levels. priv->root is the root of the tree and
   * contains no data items itself. It is just our stable root. The children
   * of priv->root are our section nodes. Each section node has group nodes
   * as children. Finally, the shortcut nodes are the leaves.
   */

  parent = priv->root;

  for (const GNode *sections = parent->children; sections != NULL; sections = sections->next)
    {
      DzlShortcutNodeData *section_data = sections->data;
      DzlShortcutsSection *section;

      section = g_object_new (DZL_TYPE_SHORTCUTS_SECTION,
                              "title", section_data->title,
                              "section-name", section_data->title,
                              "visible", TRUE,
                              NULL);

      for (const GNode *groups = sections->children; groups != NULL; groups = groups->next)
        {
          DzlShortcutNodeData *group_data = groups->data;
          DzlShortcutsGroup *group;

          group = g_object_new (DZL_TYPE_SHORTCUTS_GROUP,
                                "title", group_data->title,
                                "visible", TRUE,
                                NULL);

          for (const GNode *iter = groups->children; iter != NULL; iter = iter->next)
            {
              DzlShortcutNodeData *data = iter->data;
              const DzlShortcutChord *chord = NULL;
              g_autofree gchar *accel = NULL;
              DzlShortcutsShortcut *shortcut;

              if (data->type == DZL_SHORTCUT_NODE_ACTION)
                chord = dzl_shortcut_theme_get_chord_for_action (priv->theme, data->name);
              else if (data->type == DZL_SHORTCUT_NODE_COMMAND)
                chord = dzl_shortcut_theme_get_chord_for_command (priv->theme, data->name);

              accel = dzl_shortcut_chord_to_string (chord);

              shortcut = create_shortcut (chord, data->title, data->subtitle);
              gtk_container_add (GTK_CONTAINER (group), GTK_WIDGET (shortcut));
            }

          gtk_container_add (GTK_CONTAINER (section), GTK_WIDGET (group));
        }

      gtk_container_add (GTK_CONTAINER (window), GTK_WIDGET (section));
    }
}

GNode *
_dzl_shortcut_manager_get_root (DzlShortcutManager *self)
{
  DzlShortcutManagerPrivate *priv = dzl_shortcut_manager_get_instance_private (self);

  g_return_val_if_fail (DZL_IS_SHORTCUT_MANAGER (self), NULL);

  return priv->root;
}

/**
 * dzl_shortcut_manager_add_shortcut_entries:
 * @self: (nullable): a #DzlShortcutManager or %NULL for the default
 * @shortcuts: (array length=n_shortcuts): shortcuts to add
 * @n_shortcuts: the number of entries in @shortcuts
 * @translation_domain: (nullable): the gettext domain to use for translations
 *
 * This method will add @shortcuts to the #DzlShortcutManager.
 *
 * This provides a simple way for widgets to add their shortcuts to the manager
 * so that they may be overriden by themes or the end user.
 */
void
dzl_shortcut_manager_add_shortcut_entries (DzlShortcutManager     *self,
                                           const DzlShortcutEntry *shortcuts,
                                           guint                   n_shortcuts,
                                           const gchar            *translation_domain)
{
  DzlShortcutTheme *theme;

  g_return_if_fail (!self || DZL_IS_SHORTCUT_MANAGER (self));
  g_return_if_fail (shortcuts != NULL || n_shortcuts == 0);

  if (self == NULL)
    self = dzl_shortcut_manager_get_default ();

  theme = dzl_shortcut_manager_get_theme_by_name (self, "default");

  for (guint i = 0; i < n_shortcuts; i++)
    {
      const DzlShortcutEntry *entry = &shortcuts[i];

      if (entry->command == NULL)
        {
          g_warning ("Shortcut entry missing command id");
          continue;
        }

      if (entry->default_accel != NULL)
        dzl_shortcut_theme_set_accel_for_command (theme, entry->command, entry->default_accel);

      dzl_shortcut_manager_add_command (self,
                                        entry->command,
                                        g_dgettext (translation_domain, entry->section),
                                        g_dgettext (translation_domain, entry->group),
                                        g_dgettext (translation_domain, entry->title),
                                        g_dgettext (translation_domain, entry->subtitle));
    }
}

/**
 * dzl_shortcut_manager_get_theme_by_name:
 * @self: a #DzlShortcutManager
 *
 * Locates a theme by the name of the theme.
 *
 * Returns: (transfer none) (nullable): A #DzlShortcutTheme or %NULL.
 */
DzlShortcutTheme *
dzl_shortcut_manager_get_theme_by_name (DzlShortcutManager *self,
                                        const gchar        *theme_name)
{
  DzlShortcutManagerPrivate *priv = dzl_shortcut_manager_get_instance_private (self);

  g_return_val_if_fail (DZL_IS_SHORTCUT_MANAGER (self), NULL);
  g_return_val_if_fail (theme_name != NULL, NULL);

  for (guint i = 0; i < priv->themes->len; i++)
    {
      DzlShortcutTheme *theme = g_ptr_array_index (priv->themes, i);

      g_assert (DZL_IS_SHORTCUT_THEME (theme));

      if (g_strcmp0 (theme_name, dzl_shortcut_theme_get_name (theme)) == 0)
        return theme;
    }

  return NULL;
}

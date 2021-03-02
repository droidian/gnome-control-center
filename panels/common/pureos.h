#include <gtk/gtk.h>

static inline gboolean
pureos_get_is_phone (void)
{
  GSettingsSchema *schema;
  GSettings *gsettings;
  gboolean is_phone;

  schema = g_settings_schema_source_lookup (g_settings_schema_source_get_default (),
                                            "org.gtk.Settings.Purism", TRUE);

  if (!schema)
    return FALSE;

  gsettings = g_settings_new_full (schema, NULL, NULL);
  is_phone = g_settings_get_boolean (gsettings, "is-phone");
  g_object_unref (gsettings);
  g_settings_schema_unref (schema);

  return is_phone;
}

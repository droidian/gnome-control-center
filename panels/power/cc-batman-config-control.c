// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2023 Bardia Moshiri <fakeshell@bardia.tech>
// Copyright (C) 2023 Erik Inkinen <erik.inkinen@erikinkinen.fi>

#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include "cc-batman-config-control.h"

BatmanConfig batman_config;

void 
read_batman_config() 
{
    GKeyFile *keyfile = g_key_file_new();
    GError *error = NULL;

    if (!g_key_file_load_from_file(keyfile, BATMAN_CONFIG_FILE, G_KEY_FILE_NONE, &error)) {
        g_error("Error loading config file: %s\n", error->message);
    } else {
        batman_config.offline = g_key_file_get_boolean(keyfile, "Settings", "OFFLINE", NULL);
        batman_config.powersave = g_key_file_get_boolean(keyfile, "Settings", "POWERSAVE", NULL);
        batman_config.max_cpu_usage = g_key_file_get_integer(keyfile, "Settings", "MAX_CPU_USAGE", NULL);
        batman_config.chargesave = g_key_file_get_boolean(keyfile, "Settings", "CHARGESAVE", NULL);
        batman_config.bussave = g_key_file_get_boolean(keyfile, "Settings", "BUSSAVE", NULL);
        batman_config.gpusave = g_key_file_get_boolean(keyfile, "Settings", "GPUSAVE", NULL);
        batman_config.btsave = g_key_file_get_boolean(keyfile, "Settings", "BTSAVE", NULL);
    }

    g_key_file_free(keyfile);
}

void 
update_config_value(const char* config_key, const char* config_value) 
{
    FILE *src, *dst;
    char *line = NULL;
    size_t len = 0;
    ssize_t read;
    int found = 0; // To check if key has been found

    src = fopen(BATMAN_CONFIG_FILE, "r");
    if (src == NULL) {
        perror("Failed to open file");
        return;
    }

    dst = fopen(BATMAN_TEMP_FILE, "w");
    if (dst == NULL) {
        perror("Failed to open temp file");
        fclose(src);
        return;
    }

    while ((read = getline(&line, &len, src)) != -1) {
        if (strstr(line, config_key) == line) {
            // This is the line to replace
            fprintf(dst, "%s=%s\n", config_key, config_value);
            found = 1; // Set flag to indicate key has been found
        } else {
            // This line remains unchanged
            fprintf(dst, "%s", line);
        }
    }

    // If key not found, add it
    if (!found) {
        fprintf(dst, "%s=%s\n", config_key, config_value);
    }

    free(line);
    fclose(src);
    fclose(dst);

    // Replace the original file with the modified one
    rename(BATMAN_TEMP_FILE, BATMAN_CONFIG_FILE);
}

gboolean 
powersave_switch_state_set(GtkSwitch*, gboolean state, gpointer) 
{
    update_config_value("POWERSAVE", state ? "true" : "false");
}

gboolean 
offline_switch_state_set(GtkSwitch*, gboolean state, gpointer) 
{
    update_config_value("OFFLINE", state ? "true" : "false");
}

gboolean 
gpusave_switch_state_set(GtkSwitch*, gboolean state, gpointer) 
{
    update_config_value("GPUSAVE", state ? "true" : "false");
}

gboolean 
chargesave_switch_state_set(GtkSwitch*, gboolean state, gpointer) 
{
    update_config_value("CHARGESAVE", state ? "true" : "false");
}

gboolean 
bussave_switch_state_set(GtkSwitch*, gboolean state, gpointer) 
{
    update_config_value("BUSSAVE", state ? "true" : "false");
}

gboolean 
btsave_switch_state_set(GtkSwitch*, gboolean state, gpointer) 
{
    update_config_value("BTSAVE", state ? "true" : "false");
}

void 
max_cpu_entry_apply(AdwEntryRow* sender, gpointer) 
{
    int max_cpu_usage = g_ascii_strtoull(
        gtk_editable_get_text(GTK_EDITABLE(sender)),
        NULL, 10);

    if (max_cpu_usage < 0 || max_cpu_usage > 100) {
        fprintf(stderr, "CPU usage must be between 0 and 100\n");
        max_cpu_usage = 0;  // Set default value
        gtk_editable_set_text(GTK_EDITABLE(sender), "0");
    }

    FILE *file = fopen(BATMAN_CONFIG_FILE, "r");
    if (file == NULL) {
        perror("Unable to open config file");
        exit(1);
    }

    char line[256];
    char config_data[1024] = "";  // Assuming config file is less than 1024 characters
    bool found = false;

    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "MAX_CPU_USAGE=", 14) == 0) {
            sprintf(line, "MAX_CPU_USAGE=%d\n", max_cpu_usage);
            found = true;
        }
        strcat(config_data, line);
    }

    // If MAX_CPU_USAGE is not found in the file, add it.
    if (!found) {
        sprintf(line, "MAX_CPU_USAGE=%d\n", max_cpu_usage);
        strcat(config_data, line);
    }

    fclose(file);

    // Now write the modified config data back to the file
    file = fopen(BATMAN_CONFIG_FILE, "w");
    if (file == NULL) {
        perror("Unable to open config file");
        exit(1);
    }

    fprintf(file, "%s", config_data);

    fclose(file);
}
/* GXPSCoreProperties
 *
 * Copyright (C) 2013  Carlos Garcia Campos <carlosgc@gnome.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <config.h>

#include "gxps-core-properties.h"
#include "gxps-private.h"
#include "gxps-error.h"
#include <string.h>
#include <errno.h>

/**
 * SECTION:gxps-core-properties
 * @Short_description: XPS Core Properties
 * @Title: GXPSCoreProperties
 * @See_also: #GXPSFile
 *
 * #GXPSCoreProperties represents the metadata of a #GXPSFile.
 * #GXPSCoreProperties objects can not be created directly, they
 * are retrieved from a #GXPSFile with gxps_file_get_core_properties().
 *
 * Since: 0.2.3
 */

enum {
        PROP_0,
        PROP_ARCHIVE,
        PROP_SOURCE
};

struct _GXPSCorePropertiesPrivate {
        GXPSArchive *zip;
        gchar       *source;

        gboolean     initialized;
        GError      *init_error;

        gchar       *category;
        gchar       *content_status;
        gchar       *content_type;
        time_t       created;
        gchar       *creator;
        gchar       *description;
        gchar       *identifier;
        gchar       *keywords;
        gchar       *language;
        gchar       *last_modified_by;
        time_t       last_printed;
        time_t       modified;
        gchar       *revision;
        gchar       *subject;
        gchar       *title;
        gchar       *version;
};

static void initable_iface_init (GInitableIface *initable_iface);

G_DEFINE_TYPE_WITH_CODE (GXPSCoreProperties, gxps_core_properties, G_TYPE_OBJECT,
                         G_ADD_PRIVATE (GXPSCoreProperties)
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, initable_iface_init))

/* CoreProperties parser */
typedef enum {
        CP_UNKNOWN,
        CP_CATEGORY,
        CP_CONTENT_STATUS,
        CP_CONTENT_TYPE,
        CP_CREATED,
        CP_CREATOR,
        CP_DESCRIPTION,
        CP_IDENTIFIER,
        CP_KEYWORDS,
        CP_LANGUAGE,
        CP_LAST_MODIFIED_BY,
        CP_LAST_PRINTED,
        CP_MODIFIED,
        CP_REVISION,
        CP_SUBJECT,
        CP_TITLE,
        CP_VERSION
} CoreProperty;

typedef struct _CorePropsParserData {
        GXPSCoreProperties *core_props;
        CoreProperty        property;
        GString            *buffer;
} CorePropsParserData;

static gchar *
parse_int (const gchar *value,
           gint        *int_value)
{
        gint64 result;
        gchar *endptr = NULL;

        *int_value = -1;

        if (!value)
                return NULL;

        errno = 0;
        result = g_ascii_strtoll (value, &endptr, 10);
        if (errno || endptr == value || result > G_MAXINT || result < G_MININT)
                return NULL;

        *int_value = result;

        return endptr;
}

static gboolean
parse_date (const gchar *date,
            gint        *year,
            gint        *mon,
            gint        *day,
            gint        *hour,
            gint        *min,
            gint        *sec,
            gint        *tz)
{
        gint         value;
        const gchar *str = date;

        /* Year:
         *     YYYY (eg 1997)
         * Year and month:
         *     YYYY-MM (eg 1997-07)
         * Complete date:
         *     YYYY-MM-DD (eg 1997-07-16)
         * Complete date plus hours and minutes:
         *     YYYY-MM-DDThh:mmTZD (eg 1997-07-16T19:20+01:00)
         * Complete date plus hours, minutes and seconds:
         *     YYYY-MM-DDThh:mm:ssTZD (eg 1997-07-16T19:20:30+01:00)
         * Complete date plus hours, minutes, seconds and a decimal fraction of a second
         *     YYYY-MM-DDThh:mm:ss.sTZD (eg 1997-07-16T19:20:30.45+01:00)
         * where:
         *
         * YYYY = four-digit year
         * MM   = two-digit month (01=January, etc.)
         * DD   = two-digit day of month (01 through 31)
         * hh   = two digits of hour (00 through 23) (am/pm NOT allowed)
         * mm   = two digits of minute (00 through 59)
         * ss   = two digits of second (00 through 59)
         * s    = one or more digits representing a decimal fraction of a second
         * TZD  = time zone designator (Z or +hh:mm or -hh:mm)
         */

        /* Year */
        str = parse_int (str, &value);
        *year = value;
        if (!str)
                return value != -1;

        if (*str != '-')
                return FALSE;
        str++;

        /* Month */
        str = parse_int (str, &value);
        *mon = value;
        if (!str)
                return value != -1;

        if (*str != '-')
                return FALSE;
        str++;

        /* Day */
        str = parse_int (str, &value);
        *day = value;
        if (!str)
                return value != -1;

        if (*str != 'T')
                return FALSE;
        str++;

        /* Hour */
        str = parse_int (str, &value);
        *hour = value;
        if (!str)
                return value != -1;

        if (*str != ':')
                return FALSE;
        str++;

        /* Minute */
        str = parse_int (str, &value);
        *min = value;
        if (!str)
                return value != -1;

        if (*str != ':')
                return FALSE;
        str++;

        /* Second */
        str = parse_int (str, &value);
        *sec = value;
        if (!str)
                return value != -1;

        /* Fraction of a second */
        if (*str == '.') {
                str = parse_int (++str, &value);
                if (!str)
                        return TRUE;
        }

        /* Time Zone */
        if (*str == '+' || *str == '-') {
                gint tz_hour = -1, tz_min = -1;
                gint sign = *str == '+' ? 1 : -1;

                str++;

                str = parse_int (str, &value);
                if (!str)
                        return value != -1;
                tz_hour = value;

                if (*str != ':')
                        return FALSE;
                str++;

                str = parse_int (str, &value);
                if (!str)
                        return value != -1;
                tz_min = value;

                *tz = (tz_hour * 3600 + tz_min * 60) * sign;
        } else if (*str == 'Z') {
                *tz = 0;
        }

        return TRUE;
}

static time_t
w3cdtf_to_time_t (const gchar *date)
{
        struct tm stm;
        gint      year = -1, mon = -1, day = -1;
        gint      hour = -1, min = -1, sec = -1;
        gint      tz = 0;
        time_t    retval;

        if (!parse_date (date, &year, &mon, &day, &hour, &min, &sec, &tz))
                return (time_t)-1;

        stm.tm_year = year - 1900;
        stm.tm_mon = mon - 1;
        stm.tm_mday = day;
        stm.tm_hour = hour;
        stm.tm_min = min;
        stm.tm_sec = sec;
        stm.tm_isdst = -1;

        retval = mktime (&stm);
        if (retval == (time_t)-1)
                return retval;

        return retval + tz;
}

static void
core_props_start_element (GMarkupParseContext  *context,
                          const gchar          *element_name,
                          const gchar         **names,
                          const gchar         **values,
                          gpointer              user_data,
                          GError              **error)
{
        CorePropsParserData *data = (CorePropsParserData *)user_data;

        data->buffer = g_string_new (NULL);

        if (strcmp (element_name, "category") == 0)
                data->property = CP_CATEGORY;
        else if (strcmp (element_name, "contentStatus") == 0)
                data->property = CP_CONTENT_STATUS;
        else if (strcmp (element_name, "contentType") == 0)
                data->property = CP_CONTENT_TYPE;
        else if (strcmp (element_name, "dcterms:created") == 0)
                data->property = CP_CREATED;
        else if (strcmp (element_name, "dc:creator") == 0)
                data->property = CP_CREATOR;
        else if (strcmp (element_name, "dc:description") == 0)
                data->property = CP_DESCRIPTION;
        else if (strcmp (element_name, "dc:identifier") == 0)
                data->property = CP_IDENTIFIER;
        else if (strcmp (element_name, "keywords") == 0)
                data->property = CP_KEYWORDS;
        else if (strcmp (element_name, "dc:language") == 0)
                data->property = CP_LANGUAGE;
        else if (strcmp (element_name, "lastModifiedBy") == 0)
                data->property = CP_LAST_MODIFIED_BY;
        else if (strcmp (element_name, "lastPrinted") == 0)
                data->property = CP_LAST_PRINTED;
        else if (strcmp (element_name, "dcterms:modified") == 0)
                data->property = CP_MODIFIED;
        else if (strcmp (element_name, "revision") == 0)
                data->property = CP_REVISION;
        else if (strcmp (element_name, "dc:subject") == 0)
                data->property = CP_SUBJECT;
        else if (strcmp (element_name, "dc:title") == 0)
                data->property = CP_TITLE;
        else if (strcmp (element_name, "version") == 0)
                data->property = CP_VERSION;
        else if ((strcmp (element_name, "coreProperties") == 0) ||
                 (strcmp (element_name, "cp:coreProperties") == 0)) {
                /* Do nothing */
        } else {
                gxps_parse_error (context,
                                  data->core_props->priv->source,
                                  G_MARKUP_ERROR_UNKNOWN_ELEMENT,
                                  element_name, NULL, NULL, error);
        }
}

static void
core_props_end_element (GMarkupParseContext  *context,
                        const gchar          *element_name,
                        gpointer              user_data,
                        GError              **error)
{
        CorePropsParserData       *data = (CorePropsParserData *)user_data;
        GXPSCorePropertiesPrivate *priv = data->core_props->priv;
        gchar                     *text;
        gsize                      text_len;

        if (!data->buffer)
                return;

        text_len = data->buffer->len;
        text = g_string_free (data->buffer, FALSE);
        data->buffer = NULL;

        switch (data->property) {
        case CP_CATEGORY:
                priv->category = g_strndup (text, text_len);
                break;
        case CP_CONTENT_STATUS:
                priv->content_status = g_strndup (text, text_len);
                break;
        case CP_CONTENT_TYPE:
                priv->content_type = g_strndup (text, text_len);
                break;
        case CP_CREATED:
                priv->created = w3cdtf_to_time_t (text);
                break;
        case CP_CREATOR:
                priv->creator = g_strndup (text, text_len);
                break;
        case CP_DESCRIPTION:
                priv->description = g_strndup (text, text_len);
                break;
        case CP_IDENTIFIER:
                priv->identifier = g_strndup (text, text_len);
                break;
        case CP_KEYWORDS:
                priv->keywords = g_strndup (text, text_len);
                break;
        case CP_LANGUAGE:
                priv->language = g_strndup (text, text_len);
                break;
        case CP_LAST_MODIFIED_BY:
                priv->last_modified_by = g_strndup (text, text_len);
                break;
        case CP_LAST_PRINTED:
                priv->last_printed = w3cdtf_to_time_t (text);
                break;
        case CP_MODIFIED:
                priv->modified = w3cdtf_to_time_t (text);
                break;
        case CP_REVISION:
                priv->revision = g_strndup (text, text_len);
                break;
        case CP_SUBJECT:
                priv->subject = g_strndup (text, text_len);
                break;
        case CP_TITLE:
                priv->title = g_strndup (text, text_len);
                break;
        case CP_VERSION:
                priv->version = g_strndup (text, text_len);
                break;
        case CP_UNKNOWN:
                break;
        }

        data->property = CP_UNKNOWN;
        g_free (text);
}

static void
core_props_text (GMarkupParseContext *context,
                 const gchar         *text,
                 gsize                text_len,
                 gpointer             user_data,
                 GError             **error)
{
        CorePropsParserData *data = (CorePropsParserData *)user_data;

        if (!data->buffer)
                return;

        g_string_append_len (data->buffer, text, text_len);
}

static const GMarkupParser core_props_parser = {
        core_props_start_element,
        core_props_end_element,
        core_props_text,
        NULL,
        NULL
};

static gboolean
gxps_core_properties_parse (GXPSCoreProperties *core_props,
                            GError            **error)
{
        GInputStream         *stream;
        GMarkupParseContext  *ctx;
        CorePropsParserData   parser_data;

        stream = gxps_archive_open (core_props->priv->zip,
                                    core_props->priv->source);
        if (!stream) {
                g_set_error (error,
                             GXPS_ERROR,
                             GXPS_ERROR_SOURCE_NOT_FOUND,
                             "CoreProperties source %s not found in archive",
                             core_props->priv->source);
                return FALSE;
        }

        parser_data.core_props = core_props;
        parser_data.property = 0;
        parser_data.buffer = NULL;

        ctx = g_markup_parse_context_new (&core_props_parser, 0, &parser_data, NULL);
        gxps_parse_stream (ctx, stream, error);
        g_object_unref (stream);

        g_markup_parse_context_free (ctx);

        return (*error != NULL) ? FALSE : TRUE;
}

static void
gxps_core_properties_finalize (GObject *object)
{
        GXPSCoreProperties *core_props = GXPS_CORE_PROPERTIES (object);

        g_clear_object (&core_props->priv->zip);
        g_clear_pointer (&core_props->priv->source, g_free);
        g_clear_error (&core_props->priv->init_error);

        G_OBJECT_CLASS (gxps_core_properties_parent_class)->finalize (object);
}

static void
gxps_core_properties_init (GXPSCoreProperties *core_props)
{
        core_props->priv = gxps_core_properties_get_instance_private (core_props);
}

static void
gxps_core_properties_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
        GXPSCoreProperties *core_props = GXPS_CORE_PROPERTIES (object);

        switch (prop_id) {
        case PROP_ARCHIVE:
                core_props->priv->zip = g_value_dup_object (value);
                break;
        case PROP_SOURCE:
                core_props->priv->source = g_value_dup_string (value);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gxps_core_properties_class_init (GXPSCorePropertiesClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->set_property = gxps_core_properties_set_property;
        object_class->finalize = gxps_core_properties_finalize;

        g_object_class_install_property (object_class,
                                         PROP_ARCHIVE,
                                         g_param_spec_object ("archive",
                                                              "Archive",
                                                              "The archive",
                                                              GXPS_TYPE_ARCHIVE,
                                                              G_PARAM_WRITABLE |
                                                              G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (object_class,
                                         PROP_SOURCE,
                                         g_param_spec_string ("source",
                                                              "Source",
                                                              "The Core Properties Source File",
                                                              NULL,
                                                              G_PARAM_WRITABLE |
                                                              G_PARAM_CONSTRUCT_ONLY));
}

static gboolean
gxps_core_properties_initable_init (GInitable     *initable,
                                    GCancellable  *cancellable,
                                    GError       **error)
{
        GXPSCoreProperties *core_props = GXPS_CORE_PROPERTIES (initable);

        if (core_props->priv->initialized) {
                if (core_props->priv->init_error) {
                        g_propagate_error (error, g_error_copy (core_props->priv->init_error));

                        return FALSE;
                }

                return TRUE;
        }

        core_props->priv->initialized = TRUE;

        if (!gxps_core_properties_parse (core_props, &core_props->priv->init_error)) {
                g_propagate_error (error, g_error_copy (core_props->priv->init_error));
                return FALSE;
        }

        return TRUE;
}

static void
initable_iface_init (GInitableIface *initable_iface)
{
        initable_iface->init = gxps_core_properties_initable_init;
}

GXPSCoreProperties *
_gxps_core_properties_new (GXPSArchive *zip,
                           const gchar *source,
                           GError     **error)
{
        return g_initable_new (GXPS_TYPE_CORE_PROPERTIES,
                               NULL, error,
                               "archive", zip,
                               "source", source,
                               NULL);
}

/**
 * gxps_core_properties_get_title:
 * @core_props: a #GXPSCoreProperties
 *
 * Get the title.
 *
 * Returns: a string containing the title or %NULL
 *
 * Since: 0.2.3
 */
const gchar *
gxps_core_properties_get_title (GXPSCoreProperties *core_props)
{
        g_return_val_if_fail (GXPS_IS_CORE_PROPERTIES (core_props), NULL);

        return core_props->priv->title;
}

/**
 * gxps_core_properties_get_creator:
 * @core_props: a #GXPSCoreProperties
 *
 * Get the creator.
 *
 * Returns: a string containing the creator or %NULL
 *
 * Since: 0.2.3
 */
const gchar *
gxps_core_properties_get_creator (GXPSCoreProperties *core_props)
{
        g_return_val_if_fail (GXPS_IS_CORE_PROPERTIES (core_props), NULL);

        return core_props->priv->creator;
}

/**
 * gxps_core_properties_get_description:
 * @core_props: a #GXPSCoreProperties
 *
 * Get the description.
 *
 * Returns: a string containing the description or %NULL
 *
 * Since: 0.2.3
 */
const gchar *
gxps_core_properties_get_description (GXPSCoreProperties *core_props)
{
        g_return_val_if_fail (GXPS_IS_CORE_PROPERTIES (core_props), NULL);

        return core_props->priv->description;
}

/**
 * gxps_core_properties_get_subject:
 * @core_props: a #GXPSCoreProperties
 *
 * Get the subject.
 *
 * Returns: a string containing the subject or %NULL
 *
 * Since: 0.2.3
 */
const gchar *
gxps_core_properties_get_subject (GXPSCoreProperties *core_props)
{
        g_return_val_if_fail (GXPS_IS_CORE_PROPERTIES (core_props), NULL);

        return core_props->priv->subject;
}

/**
 * gxps_core_properties_get_keywords:
 * @core_props: a #GXPSCoreProperties
 *
 * Get the keywords.
 *
 * Returns: a string containing the keywords or %NULL
 *
 * Since: 0.2.3
 */
const gchar *
gxps_core_properties_get_keywords (GXPSCoreProperties *core_props)
{
        g_return_val_if_fail (GXPS_IS_CORE_PROPERTIES (core_props), NULL);

        return core_props->priv->keywords;
}

/**
 * gxps_core_properties_get_version:
 * @core_props: a #GXPSCoreProperties
 *
 * Get the version number.
 *
 * Returns: a string containing the version number or %NULL
 *
 * Since: 0.2.3
 */
const gchar *
gxps_core_properties_get_version (GXPSCoreProperties *core_props)
{
        g_return_val_if_fail (GXPS_IS_CORE_PROPERTIES (core_props), NULL);

        return core_props->priv->version;
}

/**
 * gxps_core_properties_get_revision:
 * @core_props: a #GXPSCoreProperties
 *
 * Get the revision number.
 *
 * Returns: a string containing the revision number or %NULL
 *
 * Since: 0.2.3
 */
const gchar *
gxps_core_properties_get_revision (GXPSCoreProperties *core_props)
{
        g_return_val_if_fail (GXPS_IS_CORE_PROPERTIES (core_props), NULL);

        return core_props->priv->revision;
}

/**
 * gxps_core_properties_get_identifier:
 * @core_props: a #GXPSCoreProperties
 *
 * Get the unique identifier.
 *
 * Returns: a string containing the identifier or %NULL
 *
 * Since: 0.2.3
 */
const gchar *
gxps_core_properties_get_identifier (GXPSCoreProperties *core_props)
{
        g_return_val_if_fail (GXPS_IS_CORE_PROPERTIES (core_props), NULL);

        return core_props->priv->identifier;
}

/**
 * gxps_core_properties_get_language:
 * @core_props: a #GXPSCoreProperties
 *
 * Get the language.
 *
 * Returns: a string containing the language or %NULL
 *
 * Since: 0.2.3
 */
const gchar *
gxps_core_properties_get_language (GXPSCoreProperties *core_props)
{
        g_return_val_if_fail (GXPS_IS_CORE_PROPERTIES (core_props), NULL);

        return core_props->priv->language;
}

/**
 * gxps_core_properties_get_category:
 * @core_props: a #GXPSCoreProperties
 *
 * Get the category.
 *
 * Returns: a string containing the category or %NULL
 *
 * Since: 0.2.3
 */
const gchar *
gxps_core_properties_get_category (GXPSCoreProperties *core_props)
{
        g_return_val_if_fail (GXPS_IS_CORE_PROPERTIES (core_props), NULL);

        return core_props->priv->category;
}

/**
 * gxps_core_properties_get_content_status:
 * @core_props: a #GXPSCoreProperties
 *
 * Get the status of the content (e.g. Draft, Reviewed, Final)
 *
 * Returns: a string containing the status of the content or %NULL
 *
 * Since: 0.2.3
 */
const gchar *
gxps_core_properties_get_content_status (GXPSCoreProperties *core_props)
{
        g_return_val_if_fail (GXPS_IS_CORE_PROPERTIES (core_props), NULL);

        return core_props->priv->content_status;
}

/**
 * gxps_core_properties_get_content_type:
 * @core_props: a #GXPSCoreProperties
 *
 * Get the type of content represented, generally defined by a
 * specific use and intended audience. This is not the MIME-Type.
 *
 * Returns: a string containing the type of content or %NULL
 *
 * Since: 0.2.3
 */
const gchar *
gxps_core_properties_get_content_type (GXPSCoreProperties *core_props)
{
        g_return_val_if_fail (GXPS_IS_CORE_PROPERTIES (core_props), NULL);

        return core_props->priv->content_type;
}

/**
 * gxps_core_properties_get_created:
 * @core_props: a #GXPSCoreProperties
 *
 * Get the creating date.
 *
 * Returns: the creating date as a <type>time_t</type> or -1.
 *
 * Since: 0.2.3
 */
time_t
gxps_core_properties_get_created (GXPSCoreProperties *core_props)
{
        g_return_val_if_fail (GXPS_IS_CORE_PROPERTIES (core_props), -1);

        return core_props->priv->created;
}

/**
 * gxps_core_properties_get_last_modified_by:
 * @core_props: a #GXPSCoreProperties
 *
 * Get the user who performed the last modification.
 *
 * Returns: a string containing the user who performed the
 *    last modification or %NULL
 *
 * Since: 0.2.3
 */
const gchar *
gxps_core_properties_get_last_modified_by (GXPSCoreProperties *core_props)
{
        g_return_val_if_fail (GXPS_IS_CORE_PROPERTIES (core_props), NULL);

        return core_props->priv->last_modified_by;
}

/**
 * gxps_core_properties_get_modified:
 * @core_props: a #GXPSCoreProperties
 *
 * Get the last modification date.
 *
 * Returns: the modification date as a <type>time_t</type> or -1.
 *
 * Since: 0.2.3
 */
time_t
gxps_core_properties_get_modified (GXPSCoreProperties *core_props)
{
        g_return_val_if_fail (GXPS_IS_CORE_PROPERTIES (core_props), -1);

        return core_props->priv->modified;
}

/**
 * gxps_core_properties_get_last_printed:
 * @core_props: a #GXPSCoreProperties
 *
 * Get the date of the last printing.
 *
 * Returns: the date of the last printing as a <type>time_t</type> or -1.
 *
 * Since: 0.2.3
 */
time_t
gxps_core_properties_get_last_printed (GXPSCoreProperties *core_props)
{
        g_return_val_if_fail (GXPS_IS_CORE_PROPERTIES (core_props), -1);

        return core_props->priv->last_printed;
}

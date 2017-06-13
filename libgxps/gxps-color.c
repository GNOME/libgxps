/* GXPSColor
 *
 * Copyright (C) 2011  Carlos Garcia Campos <carlosgc@gnome.org>
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

#include <glib.h>
#ifdef HAVE_LIBLCMS2
#include <lcms2.h>
#endif
#include "gxps-color.h"
#include "gxps-error.h"
#include "gxps-debug.h"

#define ICC_PROFILE_CACHE_KEY "gxps-icc-profile-cache"

#ifdef HAVE_LIBLCMS2
#ifdef GXPS_ENABLE_DEBUG
static const gchar *
get_color_space_string (cmsColorSpaceSignature color_space)
{
        switch (color_space) {
        case cmsSigXYZData:
                return "XYZ";
        case cmsSigLabData:
                return "Lab";
        case cmsSigLuvData:
                return "Luv";
        case cmsSigYCbCrData:
                return "YCbr";
        case cmsSigYxyData:
                return "Yxy";
        case cmsSigRgbData:
                return "RGB";
        case cmsSigGrayData:
                return "GRAY";
        case cmsSigHsvData:
                return "HSV";
        case cmsSigHlsData:
                return "HLS";
        case cmsSigCmykData:
                return "CMYK";
        case cmsSigCmyData:
                return "CMY";
        case cmsSigMCH1Data:
                return "MCH1";
        case cmsSigMCH2Data:
                return "MCH2";
        case cmsSigMCH3Data:
                return "MCH3";
        case cmsSigMCH4Data:
                return "MCH4";
        case cmsSigMCH5Data:
                return "MCH5";
        case cmsSigMCH6Data:
                return "MCH6";
        case cmsSigMCH7Data:
                return "MCH7";
        case cmsSigMCH8Data:
                return "MCH8";
        case cmsSigMCH9Data:
                return "MCH9";
        case cmsSigMCHAData:
                return "MCHA";
        case cmsSigMCHBData:
                return "MCHB";
        case cmsSigMCHCData:
                return "MCHC";
        case cmsSigMCHDData:
                return "MCHD";
        case cmsSigMCHEData:
                return "MCHE";
        case cmsSigMCHFData:
                return "MCHF";
        case cmsSigNamedData:
                return "nmcl";
        case cmsSig1colorData:
                return "1CLR";
        case cmsSig2colorData:
                return "2CLR";
        case cmsSig3colorData:
                return "3CLR";
        case cmsSig4colorData:
                return "4CLR";
        case cmsSig5colorData:
                return "5CLR";
        case cmsSig6colorData:
                return "6CLR";
        case cmsSig7colorData:
                return "7CLR";
        case cmsSig8colorData:
                return "8CLR";
        case cmsSig9colorData:
                return "9CLR";
        case cmsSig10colorData:
                return "ACLR";
        case cmsSig11colorData:
                return "BCLR";
        case cmsSig12colorData:
                return "CCLR";
        case cmsSig13colorData:
                return "DCLR";
        case cmsSig14colorData:
                return "ECLR";
        case cmsSig15colorData:
                return "FCLR";
        case cmsSigLuvKData:
                return "LuvK";
        default:
                g_assert_not_reached ();
        }

        return NULL;
}
#endif /* GXPS_ENABLE_DEBUG */

static cmsHPROFILE
create_rgb_profile (gpointer args)
{
        return cmsCreate_sRGBProfile ();
}

static cmsHPROFILE
get_s_rgb_profile (void)
{
        static GOnce once_init = G_ONCE_INIT;
        return g_once (&once_init, create_rgb_profile, NULL);
}

static gboolean
gxps_color_new_for_icc_profile (cmsHPROFILE profile,
                                gdouble    *values,
                                guint       n_values,
                                GXPSColor  *color)
{
        cmsHTRANSFORM transform;
        gdouble       cmyk[4];
        gdouble       rgb[3];

        if (cmsChannelsOf (cmsGetColorSpace (profile)) != n_values)
                return FALSE;

        if (cmsGetColorSpace (profile) != cmsSigCmykData) {
                GXPS_DEBUG (g_debug ("Unsupported color space %s", get_color_space_string (cmsGetColorSpace (profile))));

                return FALSE;
        }

        cmyk[0] = CLAMP (values[0], 0., 1.) * 100.;
        cmyk[1] = CLAMP (values[1], 0., 1.) * 100.;
        cmyk[2] = CLAMP (values[2], 0., 1.) * 100.;
        cmyk[3] = CLAMP (values[3], 0., 1.) * 100.;

        transform = cmsCreateTransform (profile,
                                        TYPE_CMYK_DBL,
                                        get_s_rgb_profile (),
                                        TYPE_RGB_DBL,
                                        INTENT_PERCEPTUAL, 0);
        cmsDoTransform (transform, cmyk, rgb, 1);
        cmsDeleteTransform (transform);

        color->red = rgb[0];
        color->green = rgb[1];
        color->blue = rgb[2];

        return TRUE;
}

static cmsHPROFILE
gxps_color_create_icc_profile (GXPSArchive *zip,
                               const gchar *icc_profile_uri)
{
        cmsHPROFILE profile;
        guchar     *profile_data;
        gsize       profile_data_len;
        gboolean    res;

        res = gxps_archive_read_entry (zip, icc_profile_uri,
                                       &profile_data, &profile_data_len,
                                       NULL);
        if (!res) {
                GXPS_DEBUG (g_debug ("ICC profile source %s not found in archive", icc_profile_uri));
                return NULL;
        }

        profile = cmsOpenProfileFromMem (profile_data, profile_data_len);
        g_free (profile_data);

        if (!profile) {
                GXPS_DEBUG (g_debug ("Failed to load ICC profile %s", icc_profile_uri));

                return NULL;
        }

        return profile;
}
#endif /* HAVE_LIBLCMS2 */

gboolean
gxps_color_new_for_icc (GXPSArchive *zip,
                        const gchar *icc_profile_uri,
                        gdouble     *values,
                        guint        n_values,
                        GXPSColor   *color)
{
#ifdef HAVE_LIBLCMS2
        GHashTable *icc_cache;
        cmsHPROFILE profile;

        icc_cache = g_object_get_data (G_OBJECT (zip), ICC_PROFILE_CACHE_KEY);
        if (icc_cache) {
                profile = g_hash_table_lookup (icc_cache, icc_profile_uri);
                if (profile)
                        return gxps_color_new_for_icc_profile (profile, values, n_values, color);
        }

        profile = gxps_color_create_icc_profile (zip, icc_profile_uri);
        if (!profile)
                return FALSE;

        if (!icc_cache) {
                icc_cache = g_hash_table_new_full (g_str_hash,
                                                   g_str_equal,
                                                   (GDestroyNotify)g_free,
                                                   (GDestroyNotify)cmsCloseProfile);
                g_object_set_data_full (G_OBJECT (zip), ICC_PROFILE_CACHE_KEY,
                                        icc_cache,
                                        (GDestroyNotify)g_hash_table_destroy);
        }

        g_hash_table_insert (icc_cache, g_strdup (icc_profile_uri), profile);

        return gxps_color_new_for_icc_profile (profile, values, n_values, color);
#else
        return FALSE;
#endif
}

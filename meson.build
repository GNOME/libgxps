project('libgxps', 'c',
        version: '0.3.2',
        default_options: [
          'buildtype=debugoptimized'
        ],
        license: 'LGPL2+',
        meson_version: '>= 0.50.0')

gxps_version = meson.project_version()
version_array = gxps_version.split('.')
gxps_major_version = version_array[0].to_int()
gxps_minor_version = version_array[1].to_int()
gxps_micro_version = version_array[2].to_int()

apiversion = '0.1'

# Libtool versioning.
# Before making a release, the libtool version should be modified.
# The string is of the form C:R:A.
# - If interfaces have been changed or added, but binary compatibility has
#   been preserved, change to C+1:0:A+1
# - If binary compatibility has been broken (eg removed or changed interfaces)
#   change to C+1:0:0
# - If the interface is the same as the previous version, change to C:R+1:A
current = 4
revision = 4
age = 2
soversion = '@0@'.format(current - age)
libversion = '@0@.@1@.@2@'.format(current - age, age, revision)

gxps_prefix = get_option('prefix')
gxps_libdir = join_paths(gxps_prefix, get_option('libdir'))
gxps_includedir = join_paths(gxps_prefix, get_option('includedir'))
gxps_headers_installdir = join_paths(gxps_includedir, 'libgxps')
gxps_datadir = join_paths(gxps_prefix, get_option('datadir'))
gxps_mandir = join_paths(get_option('prefix'), get_option('mandir'))

cc = meson.get_compiler('c')
host_system = host_machine.system()

# Compiler flags
if cc.get_id() == 'msvc'
  # Compiler options taken from msvc_recommended_pragmas.h
  # in GLib, based on _Win32_Programming_ by Rector and Newcomer
  common_flags = ['-FImsvc_recommended_pragmas.h']
else
  test_cflags = [
    '-Wpointer-arith',
    '-Wmissing-declarations',
    '-Wformat=2',
    '-Wstrict-prototypes',
    '-Wmissing-prototypes',
    '-Wnested-externs',
    '-Wold-style-definition',
    '-Wdeclaration-after-statement',
    '-Wunused',
    '-Wno-uninitialized',
    '-Wshadow',
    '-Wcast-align',
    '-Wmissing-noreturn',
    '-Wmissing-format-attribute',
    '-Wlogical-op',
    '-Wno-discarded-qualifiers',
    '-Werror=implicit',
    '-Werror=nonnull',
    '-Werror=init-self',
    '-Werror=main',
    '-Werror=missing-braces',
    '-Werror=sequence-point',
    '-Werror=return-type',
    '-Werror=trigraphs',
    '-Werror=array-bounds',
    '-Werror=write-strings',
    '-Werror=address',
    '-Werror=int-to-pointer-cast',
    '-Werror=pointer-to-int-cast',
    '-fno-strict-aliasing',
    '-Wno-int-conversion',
  ]

  common_flags = cc.get_supported_arguments(test_cflags)
endif

extra_args= []

cdata = configuration_data()

if host_system == 'windows'
  if cc.get_id() == 'msvc'
    cdata.set('_GXPS_EXTERN', '__declspec(dllexport) extern')
  else
    cdata.set('_GXPS_EXTERN', '__attribute__((visibility("default"))) __declspec(dllexport) extern')
    extra_args += ['-fvisibility=hidden']
  endif
else
  cdata.set('_GXPS_EXTERN', '__attribute__((visibility("default"))) extern')
  extra_args += ['-fvisibility=hidden']
endif

core_inc = include_directories('.')

# Required dependencies
glib_req = '2.36.0'
cairo_req = '1.10.0'
archive_req = '2.8.0'

glib_dep = dependency('glib-2.0', version: '>=' + glib_req)
gobject_dep = dependency('gobject-2.0', version: '>=' + glib_req)
gio_dep = dependency('gio-2.0', version: '>=' + glib_req)
cairo_dep = dependency('cairo', version: '>=' + cairo_req, required: cc.get_id() != 'msvc')
cairo_pdf_dep = dependency('cairo-pdf', version: '>=' + cairo_req, required: false)
cairo_ps_dep = dependency('cairo-ps', version: '>=' + cairo_req, required: false)
cairo_svg_dep = dependency('cairo-svg', version: '>=' + cairo_req, required: false)

cairo_pc_found = cairo_dep.found()
cairo_pdf_found = cairo_pdf_dep.found()
cairo_ps_found = cairo_ps_dep.found()
cairo_svg_found = cairo_svg_dep.found()

if cc.get_id() == 'msvc' and not cairo_dep.found()
  if cc.has_header('cairo.h')
    cairo_dep = cc.find_library('cairo')
    if not cairo_pdf_dep.found() and cc.has_function('cairo_pdf_surface_create',
                                                     prefix: '#include <cairo-pdf.h>',
                                                     dependencies: cairo_dep)
      cairo_pdf_dep = cairo_dep
      cairo_pdf_found = true
    endif
    if not cairo_ps_dep.found() and cc.has_function('cairo_ps_surface_create',
                                                     prefix: '#include <cairo-ps.h>',
                                                     dependencies: cairo_dep)
      cairo_ps_dep = cairo_dep
      cairo_ps_found = true
    endif
    if not cairo_svg_dep.found() and cc.has_function('cairo_svg_surface_create',
                                                     prefix: '#include <cairo-svg.h>',
                                                     dependencies: cairo_dep)
      cairo_svg_dep = cairo_dep
      cairo_svg_found = true
    endif
  endif

  assert (cairo_dep.found(), 'Cairo is required')
endif
archive_dep = dependency('libarchive', version: '>=' + archive_req)
freetype_dep = dependency('freetype2', required: cc.get_id() != 'msvc')

if cc.get_id() == 'msvc' and not freetype_dep.found()
  if cc.has_header ('ft2build.h')
    freetype_dep = cc.find_library('freetype')
  endif

  assert(freetype_dep.found(), 'FreeType is required')
endif

png_dep = dependency('libpng', required: false)

png_found = png_dep.found()

if cc.get_id() == 'msvc' and not png_dep.found() and cc.has_header('png.h')
  # MSVC: First look for the DLL + import .lib build of libpng,
  # which is normally libpngxx.lib, when libpng's pkg-config can't
  # be found, which is quite normal on MSVC.
  foreach png: [ 'libpng16', 'libpng15', 'libpng14', 'libpng12', 'libpng13', 'libpng10' ]
    if not png_found
      png_lib = cc.find_library(png, required: false)
      if png_lib.found()
        zlib_lib = cc.find_library('zlib1')
        png_found = true
      endif
    endif
  endforeach
  if png_found
    png_dep = [png_lib, zlib_lib]
  endif
endif

lcms2_dep = dependency('lcms2', required: get_option('with-liblcms2') and cc.get_id() != 'msvc')

if cc.get_id() == 'msvc' and not lcms2_dep.found()
  if cc.has_header('lcms2.h')
    lcms2_dep = cc.find_library('lcms2', required: get_option('with-liblcms2'))
    if lcms2_dep.found()
      # For MSVC builds, we need to check whether the lcms2 we have is a DLL build
      # and we must apply -DCMS_DLL to the cflags if it is so, otherwise the code
      # won't link
      message('MSVC builds: The following line checks whether lcms2 is built as a DLL...')
      if cc.has_function('cmsGetEncodedCMMversion',
                         prefix: '''#define CMS_DLL
                                    #include <lcms2.h>''',
                         dependencies: lcms2_dep)
        common_flags += '-DCMS_DLL'
      endif
    endif
  endif
  if get_option('with-liblcms2')
    assert (lcms2_dep.found(), 'LCMS2 requested but LCMS2 cannot be found')
  endif
endif

jpeg_dep = dependency('libjpeg', required: get_option('with-libjpeg') and cc.get_id() != 'msvc')

if cc.get_id() == 'msvc' and not jpeg_dep.found()
  if cc.has_header('jpeglib.h')
    jpeg_dep = cc.find_library('jpeg', required: get_option('with-libjpeg'))
  endif
  if get_option('with-libjpeg')
    assert (jpeg_dep.found(), 'JPEG support requested but libjpeg/libjpeg-turbo cannot be found')
  endif
endif

tiff_dep = dependency('libtiff-4', required: get_option('with-libtiff') and cc.get_id() != 'msvc')

if cc.get_id() == 'msvc' and not tiff_dep.found()
  if cc.has_header('tiff.h')
    tiff_dep = cc.find_library('libtiff_i', required: get_option('with-libtiff'))
  endif
  if get_option('with-libtiff')
    assert (tiff_dep.found(), 'TIFF support requested but libtiff cannot be found')
  endif
endif
cdata.set('HAVE_CAIRO_PDF', cairo_pdf_found)
cdata.set('HAVE_CAIRO_PS', cairo_ps_found)
cdata.set('HAVE_CAIRO_SVG', cairo_svg_found)
cdata.set('HAVE_LIBPNG', png_found)
cdata.set('HAVE_LIBLCMS2', lcms2_dep.found())
cdata.set('HAVE_LIBJPEG', jpeg_dep.found())
cdata.set('HAVE_LIBTIFF', tiff_dep.found())

# Maths functions might be implemented in libm
libm_dep = cc.find_library('m', required: false)

gnome = import('gnome')
gir = find_program('g-ir-scanner', required: false)
build_gir = gir.found() and not get_option('disable-introspection')

configure_file(output: 'config.h', configuration: cdata)

subdir('libgxps')
subdir('tools')
subdir('docs')

if get_option('enable-test')
  gtk3_dep = dependency('gtk+-3.0')
  subdir('test')
endif

run_target('release',
           command: [join_paths('mesonscripts', 'release.sh'),
                     meson.project_name(),
                     meson.project_version()])

project('libgxps', 'c',
        version: '0.2.5',
        default_options: [
          'buildtype=debugoptimized'
        ],
        license: 'LGPL2+',
        meson_version: '>= 0.36.0')

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
revision = 1
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
  # Make MSVC more pedantic, this is a recommended pragma list
  # from _Win32_Programming_ by Rector and Newcomer.  Taken from
  # glib's msvc_recommended_pragmas.h--please see that file for
  # the meaning of the warning codes used here
  test_cflags = [
    '-we4002', # too many actual parameters for macro
    '-we4003', # not enough actual parameters for macro
    '-w14010', # single-line comment contains line-continuation character
    '-we4013', # 'function' undefined; assuming extern returning int
    '-w14016', # no function return type; using int as default
    '-we4020', # too many actual parameters
    '-we4021', # too few actual parameters
    '-we4027', # function declared without formal parameter list
    '-we4029', # declared formal parameter list different from definition
    '-we4033', # 'function' must return a value
    '-we4035', # 'function' : no return value
    '-we4045', # array bounds overflow
    '-we4047', # different levels of indirection
    '-we4049', # terminating line number emission
    '-we4053', # an expression of type void was used as an operand
    '-we4071', # no function prototype given
    '-we4819', # the file contains a character that cannot be represented in the current code page
  ]
elif cc.get_id() == 'gcc' or cc.get_id() == 'clang'
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
else
  test_cflags = []
endif
common_flags = []
foreach cflag: test_cflags
  if cc.has_argument(cflag)
    common_flags += [ cflag ]
  endif
endforeach

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
cairo_dep = dependency('cairo', version: '>=' + cairo_req)
cairo_pdf_dep = dependency('cairo-pdf', version: '>=' + cairo_req, required: false)
cairo_ps_dep = dependency('cairo-ps', version: '>=' + cairo_req, required: false)
cairo_svg_dep = dependency('cairo-svg', version: '>=' + cairo_req, required: false)
archive_dep = dependency('libarchive', version: '>=' + archive_req)
freetype_dep = dependency('freetype2')
png_dep = dependency('libpng', required: false)
lcms2_dep = dependency('lcms2', required: get_option('with-liblcms2'))
jpeg_dep = dependency('libjpeg', required: get_option('with-libjpeg'))
tiff_dep = dependency('libtiff-4', required: get_option('with-libtiff'))

cdata.set('HAVE_CAIRO_PDF', cairo_pdf_dep.found())
cdata.set('HAVE_CAIRO_PS', cairo_ps_dep.found())
cdata.set('HAVE_CAIRO_SVG', cairo_svg_dep.found())
cdata.set('HAVE_LIBPNG', png_dep.found())
cdata.set('HAVE_LIBLCMS2', lcms2_dep.found())
cdata.set('HAVE_LIBJPEG', jpeg_dep.found())
cdata.set('HAVE_LIBTIFF', tiff_dep.found())

# Maths functions might be implemented in libm
libm_dep = cc.find_library('m')

gnome = import('gnome')
gir = find_program('g-ir-scanner', required: false)
build_gir = gir.found() and not meson.is_cross_build() and not get_option('disable-introspection')

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

headers = [
  'gxps.h',
  'gxps-core-properties.h',
  'gxps-document.h',
  'gxps-document-structure.h',
  'gxps-error.h',
  'gxps-file.h',
  'gxps-links.h',
  'gxps-page.h',
]

private_headers = [
  'gxps-archive.h',
  'gxps-brush.h',
  'gxps-color.h',
  'gxps-debug.h',
  'gxps-fonts.h',
  'gxps-glyphs.h',
  'gxps-images.h',
  'gxps-matrix.h',
  'gxps-page-private.h',
  'gxps-parse-utils.h',
  'gxps-path.h',
  'gxps-private.h',
  'gxps-resources.h',
]

introspection_sources = [
  'gxps-brush.c',
  'gxps-color.c',
  'gxps-core-properties.c',
  'gxps-debug.c',
  'gxps-document.c',
  'gxps-document-structure.c',
  'gxps-error.c',
  'gxps-file.c',
  'gxps-glyphs.c',
  'gxps-links.c',
  'gxps-matrix.c',
  'gxps-page.c',
  'gxps-path.c',
]

sources = [
  'gxps-archive.c',
  'gxps-fonts.c',
  'gxps-images.c',
  'gxps-parse-utils.c',
  'gxps-resources.c',
]

sources += introspection_sources

version_data = configuration_data()
version_data.set('GXPS_MAJOR_VERSION', gxps_major_version)
version_data.set('GXPS_MINOR_VERSION', gxps_minor_version)
version_data.set('GXPS_MICRO_VERSION', gxps_micro_version)
version_data.set('PACKAGE_VERSION', meson.project_version())

gxps_version_h = configure_file(input: 'gxps-version.h.in',
                                output: 'gxps-version.h',
                                install_dir: gxps_headers_installdir,
                                install: true,
                                configuration: version_data)

install_headers(headers, subdir: 'libgxps')

gxps_deps = [ glib_dep, gobject_dep, gio_dep, cairo_dep, cairo_pdf_dep, cairo_ps_dep, cairo_svg_dep, archive_dep, freetype_dep, png_dep, lcms2_dep, jpeg_dep, tiff_dep, libm_dep ]

common_ldflags = []

if host_system == 'linux'
  common_ldflags = [ '-Wl,-Bsymbolic' ]
endif

gxps = shared_library('gxps',
                      include_directories: core_inc,
                      sources: sources + headers + private_headers + [ gxps_version_h ],
                      soversion: soversion,
                      version: libversion,
                      install: true,
                      dependencies: gxps_deps,
                      c_args: extra_args + common_flags + [
                        '-DG_LOG_DOMAIN="GXPS"',
                        '-DGXPS_COMPILATION',
                      ],
                      link_args: common_ldflags)

# Internal dependency, for tests
gxps_inc = include_directories([ '.' ])
gxps_dep = declare_dependency(link_with: gxps,
                              include_directories: [ gxps_inc, core_inc ],
                              dependencies: gxps_deps)

# Generate the pkg-config file
pkgg = import('pkgconfig')

cairo_dep_str = cairo_pc_found ? ', cairo >= ' + cairo_req : ''
cairo_dep_libs = cairo_pc_found ? [] : cairo_dep

pkgg.generate(libraries: [gxps, cairo_dep_libs],
              version: gxps_version,
              name: 'libgxps',
              description: 'XPS Documents library',
              requires: 'gobject-2.0 >= ' + glib_req + ', gio-2.0 >= ' + glib_req + ', libarchive >= ' + archive_req + cairo_dep_str)

if build_gir
  gir_extra_args = [
    '--identifier-prefix=GXPS',
    '--c-include=gxps.h'
  ]
  gnome.generate_gir(gxps,
                     sources: introspection_sources + headers + [ gxps_version_h ],
                     namespace: 'GXPS',
                     nsversion: apiversion,
                     identifier_prefix: 'Gxps',
                     symbol_prefix: 'gxps',
                     includes: [ 'GObject-2.0', 'GLib-2.0', 'Gio-2.0', 'cairo-1.0' ],
                     install: true,
                     extra_args: gir_extra_args + [
                       '-DGXPS_COMPILATION',
                     ])
endif

version_conf = configuration_data()
version_conf.set('PACKAGE_VERSION', meson.project_version())
configure_file(input: 'version.xml.in',
               output: 'version.xml',
               configuration: version_conf)

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

glib_prefix = dependency('glib-2.0').get_pkgconfig_variable('prefix')
glib_docpath = join_paths(glib_prefix, 'share', 'gtk-doc', 'html')
docpath = join_paths(gxps_datadir, 'gtk-doc', 'html')

gnome.gtkdoc('libgxps',
             main_xml: 'libgxps-docs.sgml',
             src_dir: [ join_paths(meson.source_root(), 'libgxps'), join_paths(meson.build_root(), 'libgxps'), ],
             dependencies: gxps_dep,
             gobject_typesfile: 'libgxps.types',
             scan_args: [
               '--rebuild-types',
               '--ignore-decorators=_GXPS_EXTERN',
               '--ignore-headers=' + ' '.join(private_headers),
             ],
             fixxref_args: [
               '--html-dir=@0@'.format(docpath),
               '--extra-dir=@0@'.format(join_paths(glib_docpath, 'glib')),
               '--extra-dir=@0@'.format(join_paths(glib_docpath, 'gobject')),
               '--extra-dir=@0@'.format(join_paths(glib_docpath, 'gio')),
             ],
             install: true)

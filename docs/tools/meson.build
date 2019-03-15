xsltproc = find_program('xsltproc', required: false)
if xsltproc.found()
  xlstproc_flags = [
    '--nonet',
  ]

  man_files = []

  if jpeg_dep.found()
    man_files += [ 'xpstojpeg' ]
  endif

  if cairo_pdf_dep.found()
    man_files += [ 'xpstopdf' ]
  endif

  if png_found
    man_files += [ 'xpstopng' ]
  endif

  if cairo_ps_dep.found()
    man_files += [ 'xpstops' ]
  endif

  if cairo_svg_dep.found()
    man_files += [ 'xpstosvg' ]
  endif

  foreach m: man_files
    custom_target(m + ' man page',
                  input: '@0@.xml'.format(m),
                  output: '@0@.1'.format(m),
                  command: [
                    xsltproc,
                    xlstproc_flags,
                    '-o', '@OUTPUT@',
                    'http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl',
                    '@INPUT@',
                  ],
                  install: true,
                  install_dir: join_paths(gxps_mandir, 'man1'))
  endforeach
endif

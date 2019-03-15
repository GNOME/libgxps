tools_sources = [
  'gxps-converter.c',
  'gxps-converter.h',
  'gxps-image-converter.c',
  'gxps-image-converter.h',
  'gxps-image-writer.c',
  'gxps-image-writer.h',
  'gxps-print-converter.c',
  'gxps-print-converter.h',
]

gxps_tools_deps = [ glib_dep, gobject_dep, gio_dep, cairo_dep, cairo_pdf_dep, cairo_ps_dep, cairo_svg_dep, archive_dep, freetype_dep, png_dep, lcms2_dep, jpeg_dep, tiff_dep, libm_dep, gxps_dep ]

gxps_tools = static_library('gxpstools',
                            include_directories: core_inc,
                            sources: tools_sources,
                            install: false,
                            dependencies: gxps_tools_deps)

gxps_tools_dep = declare_dependency(link_with: gxps_tools,
                                    dependencies: gxps_tools_deps)

if png_found
  xpstopng_sources = [
    'gxps-converter-main.c',
    'gxps-png-converter.c',
    'gxps-png-converter.h',
    'gxps-png-writer.c',
    'gxps-png-writer.h',
  ]

  executable('xpstopng', xpstopng_sources,
             dependencies: gxps_tools_dep,
             install: true,
             c_args: [
               '-DCONVERTER_TYPE=GXPS_TYPE_PNG_CONVERTER',
               '-DCONVERTER_HEADER=gxps-png-converter.h',
             ])
endif

if jpeg_dep.found()
  xpstojpeg_sources = [
    'gxps-converter-main.c',
    'gxps-jpeg-converter.c',
    'gxps-jpeg-converter.h',
    'gxps-jpeg-writer.c',
    'gxps-jpeg-writer.h',
  ]

  executable('xpstojpeg', xpstojpeg_sources,
             dependencies: gxps_tools_dep,
             install: true,
             c_args: [
               '-DCONVERTER_TYPE=GXPS_TYPE_JPEG_CONVERTER',
               '-DCONVERTER_HEADER=gxps-jpeg-converter.h',
             ])
endif

if cairo_pdf_dep.found()
  xpstopdf_sources = [
    'gxps-converter-main.c',
    'gxps-pdf-converter.c',
    'gxps-pdf-converter.h',
  ]

  executable('xpstopdf', xpstopdf_sources,
             dependencies: gxps_tools_dep,
             install: true,
             c_args: [
               '-DCONVERTER_TYPE=GXPS_TYPE_PDF_CONVERTER',
               '-DCONVERTER_HEADER=gxps-pdf-converter.h',
             ])
endif

if cairo_ps_dep.found()
  xpstops_sources = [
    'gxps-converter-main.c',
    'gxps-ps-converter.c',
    'gxps-ps-converter.h',
  ]

  executable('xpstops', xpstops_sources,
             dependencies: gxps_tools_dep,
             install: true,
             c_args: [
               '-DCONVERTER_TYPE=GXPS_TYPE_PS_CONVERTER',
               '-DCONVERTER_HEADER=gxps-ps-converter.h',
             ])
endif

if cairo_svg_dep.found()
  xpstosvg_sources = [
    'gxps-converter-main.c',
    'gxps-svg-converter.c',
    'gxps-svg-converter.h',
  ]

  executable('xpstosvg', xpstosvg_sources,
             dependencies: gxps_tools_dep,
             install: true,
             c_args: [
               '-DCONVERTER_TYPE=GXPS_TYPE_SVG_CONVERTER',
               '-DCONVERTER_HEADER=gxps-svg-converter.h',
             ])
endif

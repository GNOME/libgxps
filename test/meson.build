test_programs = [
  'test-gxps',
]

foreach test_program: test_programs
  executable(test_program, test_program + '.c',
             dependencies: [ gxps_dep, gtk3_dep ],
             include_directories: gxps_inc)
endforeach



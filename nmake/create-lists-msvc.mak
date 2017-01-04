# Convert the source listing to object (.obj) listing in
# another NMake Makefile module, include it, and clean it up.
# This is a "fact-of-life" regarding NMake Makefiles...
# This file does not need to be changed unless one is maintaining the NMake Makefiles

# For those wanting to add things here:
# To add a list, do the following:
# # $(description_of_list)
# if [call create-lists.bat header $(makefile_snippet_file) $(variable_name)]
# endif
#
# if [call create-lists.bat file $(makefile_snippet_file) $(file_name)]
# endif
#
# if [call create-lists.bat footer $(makefile_snippet_file)]
# endif
# ... (repeat the if [call ...] lines in the above order if needed)
# !include $(makefile_snippet_file)
#
# (add the following after checking the entries in $(makefile_snippet_file) is correct)
# (the batch script appends to $(makefile_snippet_file), you will need to clear the file unless the following line is added)
#!if [del /f /q $(makefile_snippet_file)]
#!endif

# In order to obtain the .obj filename that is needed for NMake Makefiles to build DLLs/static LIBs or EXEs, do the following
# instead when doing 'if [call create-lists.bat file $(makefile_snippet_file) $(file_name)]'
# (repeat if there are multiple $(srcext)'s in $(source_list), ignore any headers):
# !if [for %c in ($(source_list)) do @if "%~xc" == ".$(srcext)" @call create-lists.bat file $(makefile_snippet_file) $(intdir)\%~nc.obj]
#
# $(intdir)\%~nc.obj needs to correspond to the rules added in build-rules-msvc.mak
# %~xc gives the file extension of a given file, %c in this case, so if %c is a.cc, %~xc means .cc
# %~nc gives the file name of a given file without extension, %c in this case, so if %c is a.cc, %~nc means a

NULL=

# For GXPS
!if [call create-lists.bat header gxps_objs.mak gxps_dll_OBJS]
!endif

!if [for %c in ($(GXPS_SOURCES)) do @if "%~xc" == ".c" @call create-lists.bat file gxps_objs.mak ^$(CFG)\^$(PLAT)\gxps\%~nc.obj]
!endif

!if [call create-lists.bat footer gxps_objs.mak]
!endif

!include gxps_objs.mak

!if [del /f /q gxps_objs.mak]
!endif

# For gxpstools
!if [call create-lists.bat header gxps_objs.mak gxps_tools_OBJS]
!endif

!if [for %c in ($(LIBGXPS_TOOLS_SOURCES)) do @if "%~xc" == ".c" @call create-lists.bat file gxps_objs.mak ^$(CFG)\^$(PLAT)\tools\%~nc.obj]
!endif

!if [call create-lists.bat footer gxps_objs.mak]
!endif

# For xpstopdf
!if "$(CAIRO_PDF)" == "1"

!if [call create-lists.bat header gxps_objs.mak xpstopdf_OBJS]
!endif

!if [for %c in ($(XPS_TO_PDF_SOURCES)) do @if "%~xc" == ".c" @call create-lists.bat file gxps_objs.mak ^$(CFG)\^$(PLAT)\xpstopdf\%~nc.obj]
!endif

!if [call create-lists.bat footer gxps_objs.mak]
!endif
!endif

# Gather the list of headers
!if [call create-lists.bat header gxps_srcs.mak GXPS_ACTUAL_HEADERS]
!endif

!if [for %h in ($(GXPS_HEADERS)) do @call create-lists.bat file gxps_srcs.mak ..\libgxps\%h]
!endif

!if [call create-lists.bat footer gxps_srcs.mak]
!endif

!include gxps_srcs.mak

!if [del /f /q gxps_srcs.mak]
!endif

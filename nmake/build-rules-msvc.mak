# NMake Makefile portion for compilation rules
# Items in here should not need to be edited unless
# one is maintaining the NMake build files.  The format
# of NMake Makefiles here are different from the GNU
# Makefiles.  Please see the comments about these formats.

# Inference rules for compiling the .obj files.
# Used for libs and programs with more than a single source file.
# Format is as follows
# (all dirs must have a trailing '\'):
#
# {$(srcdir)}.$(srcext){$(destdir)}.obj::
# 	$(CC)|$(CXX) $(cflags) /Fo$(destdir) /c @<<
# $<
# <<
{..\libgxps\}.c{$(CFG)\$(PLAT)\gxps\}.obj::
	$(CXX) $(CFLAGS) $(GXPS_DEFINES) $(GXPS_LIB_CFLAGS) /Fo$(CFG)\$(PLAT)\gxps\ /c @<<
$<
<<

{..\tools\}.c{$(CFG)\$(PLAT)\tools\}.obj::
	$(CXX) $(CFLAGS) $(GXPS_DEFINES) $(GXPS_LIB_CFLAGS) /Fo$(CFG)\$(PLAT)\tools\ /c @<<
$<
<<

{..\tools\}.c{$(CFG)\$(PLAT)\xpstopdf\}.obj::
	$(CXX) $(CFLAGS) $(GXPS_DEFINES) /DCONVERTER_TYPE=GXPS_TYPE_PDF_CONVERTER /DCONVERTER_HEADER=gxps-pdf-converter.h $(GXPS_LIB_CFLAGS) /Fo$(CFG)\$(PLAT)\xpstopdf\ /c @<<
$<
<<

# Inference rules for building the test programs
# Used for programs with a single source file.
# Format is as follows
# (all dirs must have a trailing '\'):
#
# {$(srcdir)}.$(srcext){$(destdir)}.exe::
# 	$(CC)|$(CXX) $(cflags) $< /Fo$*.obj  /Fe$@ [/link $(linker_flags) $(dep_libs)]
{..\tests\}.c{$(CFG)\$(PLAT)\}.exe:
	$(CXX) $(CFLAGS) $(GXPS_DEFINES) $(GXPS_CFLAGS) /DSRCDIR="\"../../../tests\"" $< /Fo$*.obj /Fe$@ /link $(LDFLAGS) $(CFG)\$(PLAT)\gxps.lib $(GXPS_TESTS_DEP_LIBS)

# Rules for building .lib files
$(CFG)\$(PLAT)\gxps.lib: $(GXPS_DLL_FILENAME).dll

# Rules for linking DLLs
# Format is as follows (the mt command is needed for MSVC 2005/2008 builds):
# $(dll_name_with_path): $(dependent_libs_files_objects_and_items)
#	link /DLL [$(linker_flags)] [$(dependent_libs)] [/def:$(def_file_if_used)] [/implib:$(lib_name_if_needed)] -out:$@ @<<
# $(dependent_objects)
# <<
# 	@-if exist $@.manifest mt /manifest $@.manifest /outputresource:$@;2
$(GXPS_DLL_FILENAME).dll: $(gxps_dll_OBJS) $(CFG)\$(PLAT)\gxps
	link /DLL $(LDFLAGS) $(GXPS_DEP_LIBS) /implib:$(CFG)\$(PLAT)\gxps.lib -out:$@ @<<
$(gxps_dll_OBJS)
<<
	@-if exist $@.manifest mt /manifest $@.manifest /outputresource:$@;2

$(CFG)\$(PLAT)\gxpstools.lib: $(CFG)\$(PLAT)\gxps.lib $(CFG)\$(PLAT)\tools $(gxps_tools_OBJS)
	lib $(LDFLAGS_BASE) $(CFG)\$(PLAT)\gxps.lib -out:$@ @<<
$(gxps_tools_OBJS)
<<

# Rules for linking Executables
# Format is as follows (the mt command is needed for MSVC 2005/2008 builds):
# $(dll_name_with_path): $(dependent_libs_files_objects_and_items)
#	link [$(linker_flags)] [$(dependent_libs)] -out:$@ @<<
# $(dependent_objects)
# <<
# 	@-if exist $@.manifest mt /manifest $@.manifest /outputresource:$@;1
$(CFG)\$(PLAT)\xpstopdf.exe: $(CFG)\$(PLAT)\gxps.lib $(CFG)\$(PLAT)\gxpstools.lib $(CFG)\$(PLAT)\xpstopdf $(xpstopdf_OBJS)
	link $(LDFLAGS) $(CFG)\$(PLAT)\gxps.lib $(CFG)\$(PLAT)\gxpstools.lib $(GXPS_DEP_LIBS) -out:$@ @<<
$(xpstopdf_OBJS)
<<
	@-if exist $@.manifest mt /manifest $@.manifest /outputresource:$@;1

# Other .obj files requiring individual attention, that could not be covered by the inference rules.
# Format is as follows (all dirs must have a trailing '\'):
#
# $(obj_file):
# 	$(CC)|$(CXX) $(cflags) /Fo$(obj_destdir) /c @<<
# $(srcfile)
# <<

clean:
	@-del /f /q $(CFG)\$(PLAT)\*.pdb
	@-if exist $(CFG)\$(PLAT)\.exe.manifest del /f /q $(CFG)\$(PLAT)\*.exe.manifest
	@-if exist $(CFG)\$(PLAT)\.exe del /f /q $(CFG)\$(PLAT)\*.exe
	@-del /f /q $(CFG)\$(PLAT)\*.dll.manifest
	@-del /f /q $(CFG)\$(PLAT)\*.dll
	@-del /f /q $(CFG)\$(PLAT)\*.ilk
	@-del /f /q $(CFG)\$(PLAT)\*.obj
	@-del /f /q $(CFG)\$(PLAT)\gxps\*.obj
	@-if exist $(CFG)\$(PLAT)\tools del /f /q $(CFG)\$(PLAT)\tools\*.obj
	@-rmdir /s /q $(CFG)\$(PLAT)
	@-del vc$(VSVER)0.pdb

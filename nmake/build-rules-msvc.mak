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

# Rules for linking Executables
# Format is as follows (the mt command is needed for MSVC 2005/2008 builds):
# $(dll_name_with_path): $(dependent_libs_files_objects_and_items)
#	link [$(linker_flags)] [$(dependent_libs)] -out:$@ @<<
# $(dependent_objects)
# <<
# 	@-if exist $@.manifest mt /manifest $@.manifest /outputresource:$@;1


# Other .obj files requiring individual attention, that could not be covered by the inference rules.
# Format is as follows (all dirs must have a trailing '\'):
#
# $(obj_file):
# 	$(CC)|$(CXX) $(cflags) /Fo$(obj_destdir) /c @<<
# $(srcfile)
# <<

clean:
	@-del /f /q $(CFG)\$(PLAT)\*.pdb
	@-del /f /q $(CFG)\$(PLAT)\*.dll.manifest
	@-del /f /q $(CFG)\$(PLAT)\*.dll
	@-del /f /q $(CFG)\$(PLAT)\*.ilk
	@-del /f /q $(CFG)\$(PLAT)\*.obj
	@-del /f /q $(CFG)\$(PLAT)\gxps\*.obj
	@-rmdir /s /q $(CFG)\$(PLAT)
	@-del vc$(VSVER)0.pdb

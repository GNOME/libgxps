# NMake Makefile snippet for copying the built libraries, utilities and headers to
# a path under $(PREFIX).

install: all
	@if not exist $(PREFIX)\bin\ mkdir $(PREFIX)\bin
	@if not exist $(PREFIX)\lib\ mkdir $(PREFIX)\lib
	@if not exist $(PREFIX)\include\libgxps\ mkdir $(PREFIX)\include\libgxps
	@copy /b $(GXPS_DLL_FILENAME).dll $(PREFIX)\bin
	@copy /b $(GXPS_DLL_FILENAME).pdb $(PREFIX)\bin
	@copy /b $(CFG)\$(PLAT)\gxps.lib $(PREFIX)\lib
	@if exist $(CFG)\$(PLAT)\xpstopdf.exe copy /b $(CFG)\$(PLAT)\xpstopdf.exe $(PREFIX)\bin
	@if exist $(CFG)\$(PLAT)\xpstopdf.exe copy /b $(CFG)\$(PLAT)\xpstopdf.pdb $(PREFIX)\bin
	@for %h in ($(GXPS_ACTUAL_HEADERS)) do @copy %h $(PREFIX)\include\libgxps

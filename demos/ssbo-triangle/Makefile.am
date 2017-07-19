bin_PROGRAMS = ssbo-triangle

AM_CPPFLAGS = @DEMO_DEPS_CFLAGS@

# ------------------------------
# SSBO-Triangle
# ------------------------------

BUILT_SOURCES = \
	shader.vert.spv \
	shader_16bit.frag \
	shader_32bit.frag \
	shader_16bit.frag.spv \
	shader_32bit.frag.spv

CLEANFILES = \
	$(BUILT_SOURCES)

$(BUILT_SOURCES): shader.vert shader.frag.mako
	$(top_srcdir)/$(GLSLANG) -V shader.vert -o shader.vert.spv
	python gen_fragment_shader.py

ssbo_triangle_SOURCES = \
	main.cpp

ssbo_triangle_CXXFLAGS = \
	-DPREFIX=$(prefix) \
	-D_GNU_SOURCE \
	@VKDF_DEFINES@

ssbo_triangle_LDADD = \
	$(abs_top_builddir)/framework/.libs/libvkdf.so \
	@DEMO_DEPS_LIBS@ \
	-lm

# -----------------------------

MAINTAINERCLEANFILES = \
	Makefile.in \
	*~

DISTCLEANFILES = $(MAINTAINERCLEANFILES)

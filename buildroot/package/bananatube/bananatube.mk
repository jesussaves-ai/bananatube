################################################################################
#
# bananatube -- a native dual-screen YouTube client for the RG DS and RG DS Plus.
#
# Nothing here spawns a Python interpreter on the search path: the search and the metadata are one libcurl
# request parsed with cJSON, and a stream URL is had from an Invidious instance or YouTube's own InnerTube
# API (see docs/ARCHITECTURE.md). On a 1 GB handheld that difference is the whole project: a yt-dlp per
# keystroke would cost ~70 MB and 3-5 seconds.
#
# The binary takes the DRM master from bananad exactly as BananaDS does. Its launcher is
# /usr/libexec/banana/run-bananatube.
#
################################################################################

BANANATUBE_VERSION = main
BANANATUBE_SITE = $(call github,jesussaves-ai,bananatube,$(BANANATUBE_VERSION))
BANANATUBE_SITE_METHOD = git
BANANATUBE_LICENSE = GPL-3.0-or-later
BANANATUBE_LICENSE_FILES = LICENSE
# libdrm (the panels), Mesa/Panfrost (GBM + EGL on the top screen), libcurl + cJSON (the backends),
# libjpeg (thumbnails), FreeType (text), zlib
BANANATUBE_DEPENDENCIES = libdrm mesa3d-panfrost libcurl cjson jpeg freetype zlib mpv host-pkgconf

BANANATUBE_CONF_OPTS = \
	-DCMAKE_BUILD_TYPE=Release \
	-DBANANATUBE_KMS=ON \
	-DBANANATUBE_TESTS=OFF \
	-DBANANATUBE_MPV=ON

define BANANATUBE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(BANANATUBE_BUILDDIR)/bananatube $(TARGET_DIR)/usr/bin/bananatube
	$(INSTALL) -D -m 0755 $(@D)/system/run-bananatube $(TARGET_DIR)/usr/libexec/banana/run-bananatube
endef

$(eval $(cmake-package))

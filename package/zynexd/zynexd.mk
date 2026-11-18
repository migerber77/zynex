ZYNEXD_VERSION = 0.3
ZYNEXD_SITE = $(BR2_EXTERNAL_ZYNEX_PATH)/src/zynexd
ZYNEXD_SITE_METHOD = local
ZYNEXD_DEPENDENCIES = zynex-llama

define ZYNEXD_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" \
		CFLAGS="$(TARGET_CFLAGS)" LDFLAGS="$(TARGET_LDFLAGS)" \
		LLAMA=1 LLAMA_CFLAGS="-I$(STAGING_DIR)/usr/include" \
		LLAMA_LIBS="-L$(STAGING_DIR)/usr/lib -lllama -lggml -lggml-cpu -lggml-base" \
		zynexd
endef

define ZYNEXD_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/zynexd $(TARGET_DIR)/usr/bin/zynexd
endef

$(eval $(generic-package))

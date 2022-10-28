# Copyright (c) SUSE LLC
# SPDX-License-Identifier: GPL-2.0-or-later

$(DEVLIB): $(LIBS)
	$(Q)$(LN) $(LIBS) $@

$(LIBS): $(OBJS) $(VERSION_SCRIPT)
	$(Q)$(CC) $(LDFLAGS) $(SHARED_FLAGS) -Wl,-soname=$@ \
		-Wl,--version-script=$(VERSION_SCRIPT) -o $@ $(OBJS) $(LIBDEPS)

$(LIBS:%.so.$(SONAME)=%-nv.so):	$(OBJS) $(NV_VERSION_SCRIPT)
	$(Q)$(CC) $(LDFLAGS) $(SHARED_FLAGS) -Wl,-soname=$@ \
		-Wl,--version-script=$(NV_VERSION_SCRIPT) -o $@ $(OBJS) $(LIBDEPS)

abi:    $(LIBS:%.so.$(SONAME)=%-nv.abi)

$(TOPDIR)/config.mk $(multipathdir)/autoconfig.h:
	$(Q)$(MAKE) -C $(TOPDIR) -f create-config.mk

# Copyright (c) SUSE LLC
# SPDX-License-Identifier: GPL-2.0-or-later

$(DEVLIB): $(LIBS)
	$(LN) $(LIBS) $@

$(LIBS): $(OBJS) $(VERSION_SCRIPT)
	$(CC) $(LDFLAGS) $(SHARED_FLAGS) -Wl,-soname=$@ \
		-Wl,--version-script=$(VERSION_SCRIPT) -o $@ $(OBJS) $(LIBDEPS)

$(LIBS:%.so.$(SONAME)=%-nv.so):	$(OBJS) $(NV_VERSION_SCRIPT)
	$(CC) $(LDFLAGS) $(SHARED_FLAGS) -Wl,-soname=$@ \
		-Wl,--version-script=$(NV_VERSION_SCRIPT) -o $@ $(OBJS) $(LIBDEPS)

abi:    $(LIBS:%.so.$(SONAME)=%-nv.abi)

$(TOPDIR)/config.mk $(multipathdir)/autoconfig.h:
	$(MAKE) -C $(TOPDIR) -f create-config.mk

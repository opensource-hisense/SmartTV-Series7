#-----------------------------------------------------------------------------
# Copyright (c) 2011, MediaTek Inc.
# All rights reserved.
#
# Unauthorized use, practice, perform, copy, distribution, reproduction,
# or disclosure of this information in whole or in part is prohibited.
#-----------------------------------------------------------------------------
# $RCSfile:  $
# $Revision:
# $Date:
# $Author:  $
# $CCRevision:  $
# $SWAuthor:  $
# $MD5HEX:  $
#
# Description:
#        A set of files describing the framebuffer interface and core types.
#---------------------------------------------------------------------------*/
ifndef VM_LINUX_ROOT
VM_LINUX_ROOT := $(word 1, $(subst /vm_linux/,/vm_linux /, $(shell pwd)))
endif
include $(VM_LINUX_ROOT)/oss/source/mak/target.mak

JOBS ?= 20

export RALINK_BINDIR=./bin

.PHONY: all ralink install clean wifi_target clean

all: wifi_target 

#ra_nl:
#	cd ralink/src/libnl-1.1/; \
#	sh ./build_ce.sh

get_wpa_supplicant_source_tree = \
$(shell \
if [ "$(1)" == 'RT557X' ]; then \
	echo "wpa_supplicant_wext/src/wpa_supplicant"; \
elif [ "$(1)" == 'MT7601U' -o "$(1)" == 'MT7662U' -o "$(1)" == 'MT7603U' ]; then \
	echo "wpa_supplicant_cfg80211/wpa_supplicant";\
else \
	echo "wpa_supplicant_cfg80211/wpa_supplicant";\
fi \
)

ralink:	
	#make -j $(JOBS) -C ralink/src/wpa_supplicant -f Makefile
	make -C ./$(call get_wpa_supplicant_source_tree,$(WIFI_DONGLE)) -f Makefile install BINDIR=$(RALINK_BINDIR)
	$(STRIP) --strip-unneeded ./$(call get_wpa_supplicant_source_tree,$(WIFI_DONGLE))/$(RALINK_BINDIR)/*
	
wifi_target: ralink
	mkdir -p $(OSS_LIB_ROOT)/wpa_supplicant/common/
	cp -a ./$(call get_wpa_supplicant_source_tree,$(WIFI_DONGLE))/bin/wpa_supplicant  $(OSS_LIB_ROOT)/wpa_supplicant/common/
	cp -a ./$(call get_wpa_supplicant_source_tree,$(WIFI_DONGLE))/bin/wpa_cli         $(OSS_LIB_ROOT)/wpa_supplicant/common/


install_ralink:
	make -C ./$(call get_wpa_supplicant_source_tree,$(WIFI_DONGLE)) -f Makefile install BINDIR=$(RALINK_BINDIR)
	$(STRIP) --strip-unneeded ./$(call get_wpa_supplicant_source_tree,$(WIFI_DONGLE))/$(RALINK_BINDIR)/*
	
clean_ralink clean:
	make -C ./$(call get_wpa_supplicant_source_tree,$(WIFI_DONGLE)) -f Makefile BINDIR=$(RALINK_BINDIR) clean -s --no-print-directory
	
	
notice:
	cp -rf NOTICE.txt $(OSS_NOTICE_OUTPUT)/wpa_supplicant_NOTICE.txt
	
info:
	cp -rf OSS_INFO.txt $(OSS_INFO_OUTPUT)/wpa_supplicant_INFO.txt

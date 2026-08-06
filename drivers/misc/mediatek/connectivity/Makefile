#
# Copyright (C) 2015 MediaTek Inc.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#

# Connectivity combo driver
# If KERNELRELEASE is defined, we've been invoked from the
# kernel build system and can use its language.
ifneq ($(KERNELRELEASE),)
    subdir-ccflags-y += -I$(srctree)/
    subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/base/power/include
    subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/base/power/include/clkbuf_v1
    subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/base/power/include/clkbuf_v1/$(MTK_PLATFORM)
    subdir-ccflags-y += -Werror -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include
    subdir-ccflags-y += -Werror -I$(srctree)/drivers/misc/mediatek/include/mt-plat
ifeq ($(CONFIG_MTK_PMIC_CHIP_MT6359),y)
    subdir-ccflags-y += -Werror -I$(srctree)/drivers/misc/mediatek/pmic/include/mt6359
endif
ifeq ($(CONFIG_MTK_PMIC_NEW_ARCH),y)
    subdir-ccflags-y += -Werror -I$(srctree)/drivers/misc/mediatek/pmic/include
endif
    subdir-ccflags-y += -I$(srctree)/drivers/mmc/core
    subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/eccci/$(MTK_PLATFORM)
    subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/eccci/
    subdir-ccflags-y += -I$(srctree)/drivers/clk/mediatek/
    subdir-ccflags-y += -I$(srctree)/drivers/pinctrl/mediatek/
    subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/power_throttling/

    # Do Nothing, move to standalone repo
    MODULE_NAME := connadp
    obj-y += $(MODULE_NAME).o
    $(MODULE_NAME)-objs += common/connectivity_build_in_adapter.o
    $(MODULE_NAME)-objs += common/wmt_build_in_adapter.o
    $(MODULE_NAME)-objs += power_throttling/adapter.o
    $(MODULE_NAME)-objs += power_throttling/core.o
    $(MODULE_NAME)-objs += power_throttling/test.o


    ifeq ($(CONFIG_WLAN_DRV_BUILD_IN),y)
        $(info $$CONFIG_MTK_COMBO_CHIP is [${CONFIG_MTK_COMBO_CHIP}])
        MTK_PLATFORM_ID := $(patsubst CONSYS_%,%,$(subst ",,$(CONFIG_MTK_COMBO_CHIP)))
        $(info MTK_PLATFORM_ID is [${MTK_PLATFORM_ID}])

        ifneq (,$(filter $(CONFIG_MTK_COMBO_CHIP), "CONSYS_6877"))
            export MTK_COMBO_CHIP=CONNAC2X2_SOC5_0
            PATH_TO_WMT_DRV      = vendor/mediatek/kernel_modules/connectivity/conninfra
            export CONNAC_VER=2_0
        else ifneq (,$(filter $(CONFIG_MTK_COMBO_CHIP), "CONSYS_6885" "CONSYS_6893"))
            export MTK_COMBO_CHIP=CONNAC2X2_SOC3_0
            PATH_TO_WMT_DRV      = vendor/mediatek/kernel_modules/connectivity/conninfra
            export CONNAC_VER=2_0
        else ifneq (,$(filter $(CONFIG_MTK_COMBO_CHIP), "CONSYS_6833"))
            export MTK_COMBO_CHIP=SOC2_1X1
            PATH_TO_WMT_DRV      = vendor/mediatek/kernel_modules/connectivity/common
            export CONNAC_VER=1_0
        else ifneq (,$(filter $(CONFIG_MTK_COMBO_CHIP), "CONSYS_6779" "CONSYS_6873" "CONSYS_6853"))
            export MTK_COMBO_CHIP=SOC2_2X2
            PATH_TO_WMT_DRV      = vendor/mediatek/kernel_modules/connectivity/common
            export CONNAC_VER=1_0
        else
            export MTK_COMBO_CHIP=CONNAC
            PATH_TO_WMT_DRV      = vendor/mediatek/kernel_modules/connectivity/common
            export CONNAC_VER=1_0
        endif

        export MTK_PLATFORM_WMT=$(MTK_PLATFORM)
        export TARGET_BOARD_PLATFORM_WMT=$(patsubst CONSYS_%,mt%,$(subst ",,$(CONFIG_MTK_COMBO_CHIP)))

        PATH_TO_WLAN_CHR_DRV = vendor/mediatek/kernel_modules/connectivity/wlan/adaptor
        PATH_TO_WLAN_DRV     = vendor/mediatek/kernel_modules/connectivity/wlan/core/gen4m
        PATH_TO_CONNFEM_DRV  = vendor/mediatek/kernel_modules/connectivity/connfem
        PATH_TO_BT_DRV       = vendor/mediatek/kernel_modules/connectivity/bt/mt66xx/btif
        PATH_TO_FMRADIO_DRV  = vendor/mediatek/kernel_modules/connectivity/fmradio
        PATH_TO_GPS_DRV      = vendor/mediatek/kernel_modules/connectivity/gps

        # Prefer the versioned in-tree subtrees, while retaining support for
        # MediaTek's original Android layout with vendor/ beside the kernel.
        ifneq (,$(wildcard $(srctree)/$(PATH_TO_WMT_DRV)))
            CONNECTIVITY_ROOT := $(srctree)
        else
            CONNECTIVITY_ROOT := $(srctree)/..
        endif
        export TOP := $(CONNECTIVITY_ROOT)

	ABS_PATH_TO_WMT_DRV      := $(abspath $(CONNECTIVITY_ROOT)/$(PATH_TO_WMT_DRV))
	ABS_PATH_TO_WLAN_CHR_DRV := $(abspath $(CONNECTIVITY_ROOT)/$(PATH_TO_WLAN_CHR_DRV))
	ABS_PATH_TO_WLAN_DRV     := $(abspath $(CONNECTIVITY_ROOT)/$(PATH_TO_WLAN_DRV))
	ABS_PATH_TO_CONNFEM_DRV  := $(abspath $(CONNECTIVITY_ROOT)/$(PATH_TO_CONNFEM_DRV))
	ABS_PATH_TO_BT_DRV       := $(abspath $(CONNECTIVITY_ROOT)/$(PATH_TO_BT_DRV))
	ABS_PATH_TO_FMRADIO_DRV  := $(abspath $(CONNECTIVITY_ROOT)/$(PATH_TO_FMRADIO_DRV))
	ABS_PATH_TO_GPS_DRV      := $(abspath $(CONNECTIVITY_ROOT)/$(PATH_TO_GPS_DRV))

        # Check every source tree required by the built-in stack.
        ifeq (,$(wildcard $(ABS_PATH_TO_WMT_DRV)))
            $(error $(ABS_PATH_TO_WMT_DRV) is not existed)
        endif
        ifeq (,$(wildcard $(ABS_PATH_TO_WLAN_CHR_DRV)))
            $(error $(ABS_PATH_TO_WLAN_CHR_DRV) is not existed)
        endif
        ifeq (,$(wildcard $(ABS_PATH_TO_WLAN_DRV)))
            $(error $(ABS_PATH_TO_WLAN_DRV) is not existed)
        endif
        ifeq (,$(wildcard $(ABS_PATH_TO_CONNFEM_DRV)))
            $(error $(ABS_PATH_TO_CONNFEM_DRV) is not existed)
        endif
        ifeq (,$(wildcard $(ABS_PATH_TO_BT_DRV)))
            $(error $(ABS_PATH_TO_BT_DRV) is not existed)
        endif
        ifeq (,$(wildcard $(ABS_PATH_TO_FMRADIO_DRV)))
            $(error $(ABS_PATH_TO_FMRADIO_DRV) is not existed)
        endif
        ifeq (,$(wildcard $(ABS_PATH_TO_GPS_DRV)))
            $(error $(ABS_PATH_TO_GPS_DRV) is not existed)
        endif

        # Kbuild cannot descend into a source directory outside this one, so
        # expose each versioned subtree through a local symlink.  Keep an
        # already-correct link intact: recursive/parallel make invocations
        # otherwise race while repeatedly unlinking and recreating it.
        define ensure-connectivity-link
        $(if $(shell \
            if [ ! -e "$(1)" ] && [ ! -L "$(1)" ]; then \
                ln -s "$(2)" "$(1)" 2>/dev/null || true; \
            fi; \
            [ "$$(readlink -f "$(1)")" = "$$(readlink -f "$(2)")" ] || echo error), \
            $(error $(1) does not resolve to $(2)))
        endef

        $(call ensure-connectivity-link,$(srctree)/$(src)/wmt_drv,$(ABS_PATH_TO_WMT_DRV))
        $(call ensure-connectivity-link,$(srctree)/$(src)/wmt_chrdev_wifi,$(ABS_PATH_TO_WLAN_CHR_DRV))
        $(call ensure-connectivity-link,$(srctree)/$(src)/wlan_drv_gen4m,$(ABS_PATH_TO_WLAN_DRV))
        $(call ensure-connectivity-link,$(srctree)/$(src)/connfem,$(ABS_PATH_TO_CONNFEM_DRV))
        $(call ensure-connectivity-link,$(srctree)/$(src)/bt,$(ABS_PATH_TO_BT_DRV))
        $(call ensure-connectivity-link,$(srctree)/$(src)/fmradio,$(ABS_PATH_TO_FMRADIO_DRV))
        $(call ensure-connectivity-link,$(srctree)/$(src)/gps_drv,$(ABS_PATH_TO_GPS_DRV))

        # for gen4m options
        export CONFIG_MTK_COMBO_WIFI_HIF=axi
        export WLAN_CHIP_ID=$(MTK_PLATFORM_ID)
        export MTK_ANDROID_WMT=y
        export MTK_ANDROID_EMI=y
        export MTK_WLAN_SERVICE=yes
        export _MTK_BT_CHIP=MTK_CONSYS_MT6893
        export CFG_BUILD_CONNAC2=true
        export CFG_FM_CHIP_ID=$(MTK_PLATFORM_ID)
        export CFG_FM_CHIP=mt6635

        WLAN_IP_SET_1_SERIES := 6765 6761 6885 6893
        WLAN_IP_SET_2_SERIES := 3967 6785
        WLAN_IP_SET_3_SERIES := 6779 6873 6853
        ifneq ($(filter $(WLAN_IP_SET_3_SERIES), $(WLAN_CHIP_ID)),)
            $(info WIFI_IP_SET is 3)
            export WIFI_IP_SET=3
        else ifneq ($(filter $(WLAN_IP_SET_2_SERIES), $(WLAN_CHIP_ID)),)
            $(info WIFI_IP_SET is 2)
            export WIFI_IP_SET=2
        else
            $(info WIFI_IP_SET is 1)
            export WIFI_IP_SET=1
        endif

        # Keep providers ahead of their consumers in vmlinux initcall order.
        subdir-ccflags-y += -D MTK_WCN_REMOVE_KERNEL_MODULE
        subdir-ccflags-y += -D MTK_WCN_BUILT_IN_DRIVER
        # Modern Clang diagnoses legacy vendor bookkeeping variables throughout
        # these imported drivers.  Preserve the warning but do not let the
        # vendor-wide -Werror turn it into a build failure.
        subdir-ccflags-y += $(call cc-option,-Wno-error=unused-but-set-variable)
        obj-y += connfem/
        obj-y += wmt_drv/
        obj-y += wmt_chrdev_wifi/
        obj-y += wlan_drv_gen4m/
        obj-y += bt/
        obj-y += fmradio/
        obj-y += gps_drv/

    endif

# Otherwise we were called directly from the command line;
# invoke the kernel build system.
else
    KERNELDIR ?= /lib/modules/$(shell uname -r)/build
    PWD  := $(shell pwd)
default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules
endif

# SPDX-License-Identifier: GPL-2.0

dtb-$(CONFIG_ARCH_MEDIATEK) += mt2712-evb.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt6755-evb.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt6795-evb.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt6797-evb.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt7622-rfb1.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8173-evb.dtb

ifeq ($(strip $(CONFIG_BUILD_ARM64_APPENDED_DTB_IMAGE)), y)
dtb-y += $(addsuffix .dtb, $(notdir $(subst $\",,$(CONFIG_BUILD_ARM64_APPENDED_DTB_IMAGE_NAMES))))
endif

ifeq ($(strip $(subst $\",,$(CONFIG_ARCH_MTK_PROJECT))),p11_pro_2gen)
dtbo-y += p11_pro_2gen.dtbo
endif

always		:= $(dtb-y) $(dtbo-y)
subdir-y	:= $(dts-dirs)
clean-files	:= *.dtb *.dtbo

$(obj)/%.dtbo: $(src)/%.dts FORCE
	$(call if_changed_dep,dtc)

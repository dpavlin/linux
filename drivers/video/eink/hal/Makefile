obj-$(CONFIG_FB_EINK_HAL)            += eink_fb_hal.o
eink_fb_hal-objs                     := einkfb_hal_main.o                   \
                                        einkfb_hal_events.o                 \
                                        einkfb_hal_util.o                   \
                                        einkfb_hal_proc.o                   \
                                        einkfb_hal_mem.o                    \
                                        einkfb_hal_io.o                     \
                                        einkfb_hal_pm.o

obj-$(CONFIG_FB_EINK_HAL_EMULATOR)   += eink_fb_hal_emu.o 
eink_fb_hal_emu-objs                 := ../emulator/emulator_hal.o

obj-$(CONFIG_FB_EINK_HAL_BROADSHEET) += eink_fb_hal_broads.o 
eink_fb_hal_broads-objs              := ../broadsheet/broadsheet_hal.o      \
                                        ../broadsheet/broadsheet_mxc.o      \
                                        ../broadsheet/broadsheet_papyrus.o  \
                                        ../broadsheet/broadsheet_pmic.o     \
                                        ../broadsheet/broadsheet.o          

obj-$(CONFIG_FB_EINK_HAL_AUO)        += eink_fb_hal_auo.o
eink_fb_hal_auo-objs                 := ../auo/auo.o              \
                                        ../auo/auo_hal.o          \
                                        ../auo/auo_mxc.o

# only for testing!

obj-$(CONFIG_FB_EINK_HAL_BUILTIN_EM) += eink_fb_hal_builtin_em.o
eink_fb_hal_builtin_em-objs          := einkfb_hal_builtin_base.o           \
                                        ../emulator/builtin_em.o

obj-$(CONFIG_FB_EINK_HAL_BUILTIN_BS) += eink_fb_hal_builtin_bs.o
eink_fb_hal_builtin_bs-objs          := einkfb_hal_builtin_base.o           \
                                        ../broadsheet/builtin_bs.o
                                        

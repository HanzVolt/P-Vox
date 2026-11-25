# Host checks for the scratch Polivoks tree. Not the target link.
CC ?= gcc
ROOT := ..
FW := $(ROOT)/firmware/src
HOSTCFLAGS := -std=c11 -Wall -Wextra -Werror -O2 -I$(FW) -I$(ROOT)/firmware/board_fm1 -I$(ROOT)/firmware/board_smk37 -I$(ROOT)/firmware/hal -I.
SANCFLAGS := -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=undefined -fsanitize-trap=undefined -I$(FW) -I$(ROOT)/firmware/board_fm1 -I$(ROOT)/firmware/board_smk37 -I$(ROOT)/firmware/hal -I.
OUT ?= ../build/host-tests
SMKHDR := $(ROOT)/firmware/board_smk37

.PHONY: test test-san clean

test: $(OUT)/test_fcore $(OUT)/test_voice $(OUT)/test_nav $(OUT)/test_safety $(OUT)/test_env $(OUT)/test_tune $(OUT)/test_preset $(OUT)/test_fader_cut $(OUT)/test_fader_resp $(OUT)/test_pad_spectrum $(OUT)/test_fader_steady $(OUT)/test_legato $(OUT)/test_amp_env $(OUT)/test_adsr_plot $(OUT)/test_border $(OUT)/test_spectrum $(OUT)/test_bend $(OUT)/test_arp $(OUT)/test_merge $(OUT)/test_padrec $(OUT)/test_drumstep $(OUT)/test_seq64 $(OUT)/test_playgen
	mkdir -p $(OUT)/wav $(OUT)/logs
	$(OUT)/test_fcore $(OUT)/wav | tee $(OUT)/logs/test_fcore.log
	$(OUT)/test_voice $(OUT)/wav | tee $(OUT)/logs/test_voice.log
	$(OUT)/test_nav | tee $(OUT)/logs/test_nav.log
	$(OUT)/test_safety | tee $(OUT)/logs/test_safety.log
	$(OUT)/test_env | tee $(OUT)/logs/test_env.log
	$(OUT)/test_tune | tee $(OUT)/logs/test_tune.log
	$(OUT)/test_preset | tee $(OUT)/logs/test_preset.log
	$(OUT)/test_fader_cut | tee $(OUT)/logs/test_fader_cut.log
	$(OUT)/test_fader_resp | tee $(OUT)/logs/test_fader_resp.log
	$(OUT)/test_pad_spectrum | tee $(OUT)/logs/test_pad_spectrum.log
	$(OUT)/test_fader_steady | tee $(OUT)/logs/test_fader_steady.log
	$(OUT)/test_legato | tee $(OUT)/logs/test_legato.log
	$(OUT)/test_amp_env | tee $(OUT)/logs/test_amp_env.log
	$(OUT)/test_adsr_plot | tee $(OUT)/logs/test_adsr_plot.log
	$(OUT)/test_border | tee $(OUT)/logs/test_border.log
	$(OUT)/test_spectrum | tee $(OUT)/logs/test_spectrum.log
	$(OUT)/test_bend | tee $(OUT)/logs/test_bend.log
	$(OUT)/test_arp | tee $(OUT)/logs/test_arp.log
	$(OUT)/test_merge $(OUT) | tee $(OUT)/logs/test_merge.log
	$(OUT)/test_padrec | tee $(OUT)/logs/test_padrec.log
	$(OUT)/test_drumstep | tee $(OUT)/logs/test_drumstep.log
	$(OUT)/test_seq64 | tee $(OUT)/logs/test_seq64.log
	$(OUT)/test_playgen | tee $(OUT)/logs/test_playgen.log

test-san: $(OUT)/test_fcore_san $(OUT)/test_voice_san $(OUT)/test_nav_san $(OUT)/test_safety_san $(OUT)/test_env_san $(OUT)/test_tune_san $(OUT)/test_preset_san $(OUT)/test_fader_cut_san $(OUT)/test_fader_resp_san $(OUT)/test_pad_spectrum_san $(OUT)/test_fader_steady_san $(OUT)/test_legato_san $(OUT)/test_amp_env_san $(OUT)/test_adsr_plot_san $(OUT)/test_border_san $(OUT)/test_spectrum_san $(OUT)/test_bend_san $(OUT)/test_arp_san $(OUT)/test_merge_san $(OUT)/test_padrec_san $(OUT)/test_drumstep_san $(OUT)/test_seq64_san $(OUT)/test_playgen_san $(OUT)/test_playgen_asan
	mkdir -p $(OUT)/wav $(OUT)/logs
	$(OUT)/test_fcore_san $(OUT)/wav | tee $(OUT)/logs/test_fcore_san.log
	$(OUT)/test_voice_san $(OUT)/wav | tee $(OUT)/logs/test_voice_san.log
	$(OUT)/test_nav_san | tee $(OUT)/logs/test_nav_san.log
	$(OUT)/test_safety_san | tee $(OUT)/logs/test_safety_san.log
	$(OUT)/test_env_san | tee $(OUT)/logs/test_env_san.log
	$(OUT)/test_tune_san | tee $(OUT)/logs/test_tune_san.log
	$(OUT)/test_preset_san | tee $(OUT)/logs/test_preset_san.log
	$(OUT)/test_fader_cut_san | tee $(OUT)/logs/test_fader_cut_san.log
	$(OUT)/test_fader_resp_san | tee $(OUT)/logs/test_fader_resp_san.log
	$(OUT)/test_pad_spectrum_san | tee $(OUT)/logs/test_pad_spectrum_san.log
	$(OUT)/test_fader_steady_san | tee $(OUT)/logs/test_fader_steady_san.log
	$(OUT)/test_legato_san | tee $(OUT)/logs/test_legato_san.log
	$(OUT)/test_amp_env_san | tee $(OUT)/logs/test_amp_env_san.log
	$(OUT)/test_adsr_plot_san | tee $(OUT)/logs/test_adsr_plot_san.log
	$(OUT)/test_border_san | tee $(OUT)/logs/test_border_san.log
	$(OUT)/test_spectrum_san | tee $(OUT)/logs/test_spectrum_san.log
	$(OUT)/test_bend_san | tee $(OUT)/logs/test_bend_san.log
	$(OUT)/test_arp_san | tee $(OUT)/logs/test_arp_san.log
	$(OUT)/test_merge_san $(OUT) | tee $(OUT)/logs/test_merge_san.log
	$(OUT)/test_padrec_san | tee $(OUT)/logs/test_padrec_san.log
	$(OUT)/test_drumstep_san | tee $(OUT)/logs/test_drumstep_san.log
	$(OUT)/test_seq64_san | tee $(OUT)/logs/test_seq64_san.log
	$(OUT)/test_playgen_san | tee $(OUT)/logs/test_playgen_san.log
	$(OUT)/test_playgen_asan | tee $(OUT)/logs/test_playgen_asan.log

$(OUT)/test_fcore: test_fcore.c $(FW)/poli_fcore.c $(FW)/poli_fcore.h wav_write.h
	mkdir -p $(OUT)
	$(CC) $(HOSTCFLAGS) test_fcore.c -lm -o $@

$(OUT)/test_voice: test_voice.c $(FW)/polivoks_limited.c $(FW)/polivoks.h $(FW)/poli_fcore.c wav_write.h
	mkdir -p $(OUT)
	$(CC) $(HOSTCFLAGS) test_voice.c -lm -o $@

$(OUT)/test_nav: test_nav.c $(FW)/poli_nav.c $(FW)/poli_nav.h $(FW)/polivoks_limited.c
	mkdir -p $(OUT)
	$(CC) $(HOSTCFLAGS) test_nav.c -lm -o $@

$(OUT)/test_safety: test_safety.c $(FW)/polivoks_limited.c
	mkdir -p $(OUT)
	$(CC) $(HOSTCFLAGS) test_safety.c -lm -o $@

$(OUT)/test_fcore_san: test_fcore.c $(FW)/poli_fcore.c wav_write.h
	mkdir -p $(OUT)
	$(CC) $(SANCFLAGS) test_fcore.c -lm -o $@

$(OUT)/test_voice_san: test_voice.c $(FW)/polivoks_limited.c $(FW)/poli_fcore.c
	mkdir -p $(OUT)
	$(CC) $(SANCFLAGS) test_voice.c -lm -o $@

$(OUT)/test_nav_san: test_nav.c $(FW)/poli_nav.c $(FW)/polivoks_limited.c
	mkdir -p $(OUT)
	$(CC) $(SANCFLAGS) test_nav.c -lm -o $@

$(OUT)/test_safety_san: test_safety.c $(FW)/polivoks_limited.c
	mkdir -p $(OUT)
	$(CC) $(SANCFLAGS) test_safety.c -lm -o $@

$(OUT)/test_env: test_env.c $(FW)/poli_nav.c $(FW)/poli_nav.h $(FW)/polivoks_limited.c
	mkdir -p $(OUT)
	$(CC) $(HOSTCFLAGS) test_env.c -lm -o $@

$(OUT)/test_env_san: test_env.c $(FW)/poli_nav.c $(FW)/polivoks_limited.c
	mkdir -p $(OUT)
	$(CC) $(SANCFLAGS) test_env.c -lm -o $@

$(OUT)/test_tune: test_tune.c $(FW)/poli_nav.c $(FW)/polivoks_limited.c
	mkdir -p $(OUT)
	$(CC) $(HOSTCFLAGS) test_tune.c -lm -o $@

$(OUT)/test_tune_san: test_tune.c $(FW)/poli_nav.c $(FW)/polivoks_limited.c
	mkdir -p $(OUT)
	$(CC) $(SANCFLAGS) test_tune.c -lm -o $@

PRESET_DEPS := test_preset.c $(FW)/poli_preset.c $(FW)/poli_preset.h $(FW)/poli_presets_factory.inc $(FW)/poli_name_font.inc $(FW)/poli_nav.c $(FW)/polivoks_limited.c

$(OUT)/test_preset: $(PRESET_DEPS)
	mkdir -p $(OUT)
	$(CC) $(HOSTCFLAGS) test_preset.c -lm -o $@

$(OUT)/test_preset_san: $(PRESET_DEPS)
	mkdir -p $(OUT)
	$(CC) $(SANCFLAGS) test_preset.c -lm -o $@


$(OUT)/test_fader_cut: test_fader_cut.c $(FW)/poli_nav.c $(FW)/poli_nav.h $(SMKHDR)/smk_cut_smooth.h $(FW)/polivoks_limited.c
	mkdir -p $(OUT)
	$(CC) $(HOSTCFLAGS) test_fader_cut.c -lm -o $@

$(OUT)/test_fader_cut_san: test_fader_cut.c $(FW)/poli_nav.c $(SMKHDR)/smk_cut_smooth.h $(FW)/polivoks_limited.c
	mkdir -p $(OUT)
	$(CC) $(SANCFLAGS) test_fader_cut.c -lm -o $@


$(OUT)/test_fader_resp: test_fader_resp.c $(FW)/poli_nav.c $(FW)/poli_nav.h $(SMKHDR)/smk_cut_smooth.h $(FW)/polivoks_limited.c
	mkdir -p $(OUT)
	$(CC) $(HOSTCFLAGS) test_fader_resp.c -lm -o $@

$(OUT)/test_fader_resp_san: test_fader_resp.c $(FW)/poli_nav.c $(SMKHDR)/smk_cut_smooth.h $(FW)/polivoks_limited.c
	mkdir -p $(OUT)
	$(CC) $(SANCFLAGS) test_fader_resp.c -lm -o $@


$(OUT)/test_pad_spectrum: test_pad_spectrum.c $(FW)/poli_nav.c $(SMKHDR)/smk_pad_spectrum.h $(FW)/poli_preset.c $(FW)/polivoks_limited.c
	mkdir -p $(OUT)
	$(CC) $(HOSTCFLAGS) test_pad_spectrum.c -lm -o $@

$(OUT)/test_pad_spectrum_san: test_pad_spectrum.c $(FW)/poli_nav.c $(SMKHDR)/smk_pad_spectrum.h $(FW)/poli_preset.c
	mkdir -p $(OUT)
	$(CC) $(SANCFLAGS) test_pad_spectrum.c -lm -o $@


$(OUT)/test_fader_steady: test_fader_steady.c $(SMKHDR)/smk_fader_steady.h
	mkdir -p $(OUT)
	$(CC) $(HOSTCFLAGS) test_fader_steady.c -o $@

$(OUT)/test_fader_steady_san: test_fader_steady.c $(SMKHDR)/smk_fader_steady.h
	mkdir -p $(OUT)
	$(CC) $(SANCFLAGS) test_fader_steady.c -o $@


$(OUT)/test_legato: test_legato.c $(FW)/polivoks_limited.c $(FW)/polivoks.h
	mkdir -p $(OUT)
	$(CC) $(HOSTCFLAGS) test_legato.c -lm -o $@

$(OUT)/test_legato_san: test_legato.c $(FW)/polivoks_limited.c
	mkdir -p $(OUT)
	$(CC) $(SANCFLAGS) test_legato.c -lm -o $@


$(OUT)/test_amp_env: test_amp_env.c $(SMKHDR)/smk_amp_env.h $(FW)/polivoks_limited.c $(FW)/polivoks.h
	mkdir -p $(OUT)
	$(CC) $(HOSTCFLAGS) test_amp_env.c -lm -o $@

$(OUT)/test_amp_env_san: test_amp_env.c $(SMKHDR)/smk_amp_env.h $(FW)/polivoks_limited.c
	mkdir -p $(OUT)
	$(CC) $(SANCFLAGS) test_amp_env.c -lm -o $@

$(OUT)/test_adsr_plot: test_adsr_plot.c $(SMKHDR)/smk_amp_env.h $(FW)/poli_nav.c $(FW)/polivoks_limited.c
	mkdir -p $(OUT)
	$(CC) $(HOSTCFLAGS) test_adsr_plot.c -lm -o $@

$(OUT)/test_adsr_plot_san: test_adsr_plot.c $(SMKHDR)/smk_amp_env.h $(FW)/poli_nav.c $(FW)/polivoks_limited.c
	mkdir -p $(OUT)
	$(CC) $(SANCFLAGS) test_adsr_plot.c -lm -o $@

# Frame-pixel check: needs the SMK draw path and a host LCD framebuffer.
BORDERFLAGS := -DBOARD_SMK37=1 -DSMK_SPEC_HOST_FEED -Wno-unused-function -I$(ROOT)/firmware/gen -I$(ROOT)/firmware/hal -I$(ROOT)/firmware/board_smk37
$(OUT)/test_border: test_border.c $(FW)/poli_ui.c $(FW)/poli_nav.c $(FW)/polivoks_limited.c
	mkdir -p $(OUT)
	$(CC) $(HOSTCFLAGS) $(BORDERFLAGS) test_border.c -lm -o $@

$(OUT)/test_border_san: test_border.c $(FW)/poli_ui.c $(FW)/poli_nav.c $(FW)/polivoks_limited.c
	mkdir -p $(OUT)
	$(CC) $(SANCFLAGS) $(BORDERFLAGS) test_border.c -lm -o $@


$(OUT)/test_spectrum: test_spectrum.c $(SMKHDR)/smk_spectrum.h
	mkdir -p $(OUT)
	$(CC) $(HOSTCFLAGS) test_spectrum.c -lm -o $@

$(OUT)/test_spectrum_san: test_spectrum.c $(SMKHDR)/smk_spectrum.h
	mkdir -p $(OUT)
	$(CC) $(SANCFLAGS) test_spectrum.c -lm -o $@


$(OUT)/test_bend: test_bend.c $(FW)/polivoks_limited.c $(FW)/polivoks.h $(FW)/smk_wheel.h $(FW)/poli_nav.c
	mkdir -p $(OUT)
	$(CC) $(HOSTCFLAGS) test_bend.c -lm -o $@

$(OUT)/test_bend_san: test_bend.c $(FW)/polivoks_limited.c $(FW)/smk_wheel.h $(FW)/poli_nav.c
	mkdir -p $(OUT)
	$(CC) $(SANCFLAGS) test_bend.c -lm -o $@

BOARD := $(ROOT)/firmware/board_smk37
ARPFLAGS := -DBOARD_SMK37=1 -DPOLI_ARP=1 -I$(BOARD)
$(OUT)/test_arp: test_arp.c $(BOARD)/poli_arp.c $(BOARD)/poli_arp.h $(FW)/poli_nav.c $(FW)/poli_preset.c $(FW)/polivoks_limited.c
	mkdir -p $(OUT)
	$(CC) $(HOSTCFLAGS) $(ARPFLAGS) test_arp.c -lm -o $@

$(OUT)/test_arp_san: test_arp.c $(BOARD)/poli_arp.c $(BOARD)/poli_arp.h $(FW)/poli_nav.c $(FW)/poli_preset.c $(FW)/polivoks_limited.c
	mkdir -p $(OUT)
	$(CC) $(SANCFLAGS) $(ARPFLAGS) test_arp.c -lm -o $@


clean:
	rm -rf $(OUT)

# PVox merge (17 Nov 2026): note 127 clip, velocity loudness after Bass Support, bend reset (poli_arp.c and
# test_arp.c were removed with the classic Elite arp; the EXTRAS checks are in ../../smk-tests)
$(OUT)/test_merge: test_merge.c $(FW)/polivoks_limited.c $(FW)/poli_nav.c
	mkdir -p $(OUT)
	$(CC) $(HOSTCFLAGS) test_merge.c -lm -o $@

$(OUT)/test_merge_san: test_merge.c $(FW)/polivoks_limited.c $(FW)/poli_nav.c
	mkdir -p $(OUT)
	$(CC) $(SANCFLAGS) test_merge.c -lm -o $@


PADRECFLAGS := -DBOARD_SMK37=1 -I$(BOARD)
$(OUT)/test_padrec: test_padrec.c $(FW)/poli_nav.c $(FW)/poli_xtra.c $(FW)/poli_padrec.inc $(FW)/polivoks_limited.c
	mkdir -p $(OUT)
	$(CC) $(HOSTCFLAGS) $(PADRECFLAGS) test_padrec.c -lm -o $@

$(OUT)/test_padrec_san: test_padrec.c $(FW)/poli_nav.c $(FW)/poli_xtra.c $(FW)/poli_padrec.inc $(FW)/polivoks_limited.c
	mkdir -p $(OUT)
	$(CC) $(SANCFLAGS) $(PADRECFLAGS) test_padrec.c -lm -o $@

# Drum-Step (22 Nov 2026): the DRUM STEP page on the Elite (white keys, chance, PAD REC sharing USER)
DRUMSTEP_DEPS := test_drumstep.c $(FW)/poli_nav.c $(FW)/poli_xtra.c $(FW)/poli_padrec.inc $(FW)/polivoks_limited.c
$(OUT)/test_drumstep: $(DRUMSTEP_DEPS)
	mkdir -p $(OUT)
	$(CC) $(HOSTCFLAGS) $(PADRECFLAGS) test_drumstep.c -lm -o $@

$(OUT)/test_drumstep_san: $(DRUMSTEP_DEPS)
	mkdir -p $(OUT)
	$(CC) $(SANCFLAGS) $(PADRECFLAGS) test_drumstep.c -lm -o $@

# Seq64 (22 Nov 2026): SEQ STEPS 1..64 (the FM-1 test plus Elite keybed REC and the PAD REC blob)
SEQ64_DEPS := test_seq64.c $(FW)/poli_nav.c $(FW)/poli_xtra.c $(FW)/poli_padrec.inc $(FW)/polivoks_limited.c
$(OUT)/test_seq64: $(SEQ64_DEPS)
	mkdir -p $(OUT)
	$(CC) $(HOSTCFLAGS) $(PADRECFLAGS) test_seq64.c -lm -o $@
$(OUT)/test_seq64_san: $(SEQ64_DEPS)
	mkdir -p $(OUT)
	$(CC) $(SANCFLAGS) $(PADRECFLAGS) test_seq64.c -lm -o $@

# PLAY GEN (24 Nov 2026): DRUM STEP K7, a fresh random loop on every PLAY, USER untouched
PLAYGEN_DEPS := test_playgen.c $(FW)/poli_nav.c $(FW)/poli_xtra.c $(FW)/poli_padrec.inc $(FW)/polivoks_limited.c
ASANCFLAGS := -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=undefined -fsanitize=address -I$(FW) -I$(ROOT)/firmware/board_fm1 -I$(ROOT)/firmware/board_smk37 -I$(ROOT)/firmware/hal -I.
$(OUT)/test_playgen: $(PLAYGEN_DEPS)
	mkdir -p $(OUT)
	$(CC) $(HOSTCFLAGS) $(PADRECFLAGS) -I$(ROOT)/firmware/gen test_playgen.c -lm -o $@
$(OUT)/test_playgen_san: $(PLAYGEN_DEPS)
	mkdir -p $(OUT)
	$(CC) $(SANCFLAGS) $(PADRECFLAGS) -I$(ROOT)/firmware/gen test_playgen.c -lm -o $@
$(OUT)/test_playgen_asan: $(PLAYGEN_DEPS)
	mkdir -p $(OUT)
	$(CC) $(ASANCFLAGS) $(PADRECFLAGS) -I$(ROOT)/firmware/gen test_playgen.c -lm -o $@

# Host checks for the scratch Polivoks tree. Not the target link.
CC ?= gcc
ROOT := ..
FW := $(ROOT)/firmware/src
HOSTCFLAGS := -std=c11 -Wall -Wextra -Werror -O2 -I$(FW) -I.
SANCFLAGS := -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=undefined -fsanitize-trap=undefined -I$(FW) -I.
OUT ?= ../build/host-tests

.PHONY: test test-san clean

test: $(OUT)/test_fcore $(OUT)/test_voice $(OUT)/test_nav $(OUT)/test_safety $(OUT)/test_env $(OUT)/test_tune $(OUT)/test_preset $(OUT)/test_drumstep $(OUT)/test_seq64 $(OUT)/test_padrec $(OUT)/test_amp_env $(OUT)/test_adsr_plot $(OUT)/test_spectrum $(OUT)/test_parity $(OUT)/test_playgen
	mkdir -p $(OUT)/wav $(OUT)/logs
	$(OUT)/test_fcore $(OUT)/wav | tee $(OUT)/logs/test_fcore.log
	$(OUT)/test_voice $(OUT)/wav | tee $(OUT)/logs/test_voice.log
	$(OUT)/test_nav | tee $(OUT)/logs/test_nav.log
	$(OUT)/test_safety | tee $(OUT)/logs/test_safety.log
	$(OUT)/test_env | tee $(OUT)/logs/test_env.log
	$(OUT)/test_tune | tee $(OUT)/logs/test_tune.log
	$(OUT)/test_preset | tee $(OUT)/logs/test_preset.log
	$(OUT)/test_drumstep | tee $(OUT)/logs/test_drumstep.log
	$(OUT)/test_seq64 | tee $(OUT)/logs/test_seq64.log
	$(OUT)/test_padrec | tee $(OUT)/logs/test_padrec.log
	$(OUT)/test_amp_env | tee $(OUT)/logs/test_amp_env.log
	$(OUT)/test_adsr_plot | tee $(OUT)/logs/test_adsr_plot.log
	$(OUT)/test_spectrum | tee $(OUT)/logs/test_spectrum.log
	$(OUT)/test_parity | tee $(OUT)/logs/test_parity.log
	$(OUT)/test_playgen | tee $(OUT)/logs/test_playgen.log

test-san: $(OUT)/test_fcore_san $(OUT)/test_voice_san $(OUT)/test_nav_san $(OUT)/test_safety_san $(OUT)/test_env_san $(OUT)/test_tune_san $(OUT)/test_preset_san $(OUT)/test_drumstep_san $(OUT)/test_seq64_san $(OUT)/test_padrec_san $(OUT)/test_amp_env_san $(OUT)/test_adsr_plot_san $(OUT)/test_spectrum_san $(OUT)/test_parity_san $(OUT)/test_playgen_san $(OUT)/test_playgen_asan
	mkdir -p $(OUT)/wav $(OUT)/logs
	$(OUT)/test_fcore_san $(OUT)/wav | tee $(OUT)/logs/test_fcore_san.log
	$(OUT)/test_voice_san $(OUT)/wav | tee $(OUT)/logs/test_voice_san.log
	$(OUT)/test_nav_san | tee $(OUT)/logs/test_nav_san.log
	$(OUT)/test_safety_san | tee $(OUT)/logs/test_safety_san.log
	$(OUT)/test_env_san | tee $(OUT)/logs/test_env_san.log
	$(OUT)/test_tune_san | tee $(OUT)/logs/test_tune_san.log
	$(OUT)/test_preset_san | tee $(OUT)/logs/test_preset_san.log
	$(OUT)/test_drumstep_san | tee $(OUT)/logs/test_drumstep_san.log
	$(OUT)/test_seq64_san | tee $(OUT)/logs/test_seq64_san.log
	$(OUT)/test_padrec_san | tee $(OUT)/logs/test_padrec_san.log
	$(OUT)/test_amp_env_san | tee $(OUT)/logs/test_amp_env_san.log
	$(OUT)/test_adsr_plot_san | tee $(OUT)/logs/test_adsr_plot_san.log
	$(OUT)/test_spectrum_san | tee $(OUT)/logs/test_spectrum_san.log
	$(OUT)/test_parity_san | tee $(OUT)/logs/test_parity_san.log
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

# Drum-Step (22 Nov 2026): the DRUM STEP page
DRUMSTEP_DEPS := test_drumstep.c $(FW)/poli_nav.c $(FW)/poli_nav.h $(FW)/poli_xtra.c $(FW)/poli_xtra.h $(FW)/polivoks_limited.c

$(OUT)/test_drumstep: $(DRUMSTEP_DEPS)
	mkdir -p $(OUT)
	$(CC) $(HOSTCFLAGS) test_drumstep.c -lm -o $@

$(OUT)/test_drumstep_san: $(DRUMSTEP_DEPS)
	mkdir -p $(OUT)
	$(CC) $(SANCFLAGS) test_drumstep.c -lm -o $@

# Seq64 (22 Nov 2026): SEQ STEPS 1..64
SEQ64_DEPS := test_seq64.c $(FW)/poli_nav.c $(FW)/poli_nav.h $(FW)/poli_xtra.c $(FW)/poli_xtra.h $(FW)/polivoks_limited.c
$(OUT)/test_seq64: $(SEQ64_DEPS)
	mkdir -p $(OUT)
	$(CC) $(HOSTCFLAGS) test_seq64.c -lm -o $@
$(OUT)/test_seq64_san: $(SEQ64_DEPS)
	mkdir -p $(OUT)
	$(CC) $(SANCFLAGS) test_seq64.c -lm -o $@
# Parity (23 Nov 2026): PAD REC, the classic arp, the EG plots, the analyser, the value-only repaint on the FM-1.
# test_padrec / test_amp_env / test_adsr_plot / test_spectrum are the Elite's suites built for the FM-1; test_parity is
# the FM-1 side (keybed pads, K4 cursor, the arp through polivoks_midi, the screen). It includes poli_ui.c and gfx.c,
# whose unused helpers need -Wno-unused-function (as the firmware build has).
PARITY_DEPS := $(wildcard $(FW)/*.c $(FW)/*.h $(FW)/*.inc)
PARITY_X := -Wno-unused-function -DSMK_SPEC_HOST_FEED -I$(ROOT)/firmware/gen -I$(ROOT)/firmware/hal
$(OUT)/test_padrec: test_padrec.c $(PARITY_DEPS)
	mkdir -p $(OUT)
	$(CC) $(HOSTCFLAGS) test_padrec.c -lm -o $@
$(OUT)/test_padrec_san: test_padrec.c $(PARITY_DEPS)
	mkdir -p $(OUT)
	$(CC) $(SANCFLAGS) test_padrec.c -lm -o $@
$(OUT)/test_amp_env: test_amp_env.c $(PARITY_DEPS)
	mkdir -p $(OUT)
	$(CC) $(HOSTCFLAGS) test_amp_env.c -lm -o $@
$(OUT)/test_amp_env_san: test_amp_env.c $(PARITY_DEPS)
	mkdir -p $(OUT)
	$(CC) $(SANCFLAGS) test_amp_env.c -lm -o $@
$(OUT)/test_adsr_plot: test_adsr_plot.c $(PARITY_DEPS)
	mkdir -p $(OUT)
	$(CC) $(HOSTCFLAGS) test_adsr_plot.c -lm -o $@
$(OUT)/test_adsr_plot_san: test_adsr_plot.c $(PARITY_DEPS)
	mkdir -p $(OUT)
	$(CC) $(SANCFLAGS) test_adsr_plot.c -lm -o $@
$(OUT)/test_spectrum: test_spectrum.c $(PARITY_DEPS)
	mkdir -p $(OUT)
	$(CC) $(HOSTCFLAGS) test_spectrum.c -lm -o $@
$(OUT)/test_spectrum_san: test_spectrum.c $(PARITY_DEPS)
	mkdir -p $(OUT)
	$(CC) $(SANCFLAGS) test_spectrum.c -lm -o $@
$(OUT)/test_parity: test_parity.c $(PARITY_DEPS)
	mkdir -p $(OUT)
	$(CC) $(HOSTCFLAGS) $(PARITY_X) test_parity.c -lm -o $@
$(OUT)/test_parity_san: test_parity.c $(PARITY_DEPS)
	mkdir -p $(OUT)
	$(CC) $(SANCFLAGS) $(PARITY_X) test_parity.c -lm -o $@
# PLAY GEN (25 Nov 2026): DRUM STEP i=2, a fresh random loop on every PLAY, USER untouched
PLAYGEN_DEPS := test_playgen.c $(FW)/poli_nav.c $(FW)/poli_xtra.c $(FW)/poli_padrec.inc $(FW)/polivoks_limited.c
PLAYGEN_X := -I$(ROOT)/firmware/gen
ASANCFLAGS := -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=undefined -fsanitize=address -I$(FW) -I.
$(OUT)/test_playgen: $(PLAYGEN_DEPS)
	mkdir -p $(OUT)
	$(CC) $(HOSTCFLAGS) $(PLAYGEN_X) test_playgen.c -lm -o $@
$(OUT)/test_playgen_san: $(PLAYGEN_DEPS)
	mkdir -p $(OUT)
	$(CC) $(SANCFLAGS) $(PLAYGEN_X) test_playgen.c -lm -o $@
$(OUT)/test_playgen_asan: $(PLAYGEN_DEPS)
	mkdir -p $(OUT)
	$(CC) $(ASANCFLAGS) $(PLAYGEN_X) test_playgen.c -lm -o $@

# FM-1 DRUM STEP PLAY GEN frames (EN/RU, OFF/ON)
SHOT_PLAYGEN_DEPS := shot_playgen.c $(FW)/poli_nav.c $(FW)/poli_ui.c $(FW)/gfx.c $(FW)/poli_preset.c
$(OUT)/shot_playgen: $(SHOT_PLAYGEN_DEPS)
	mkdir -p $(OUT)
	$(CC) $(HOSTCFLAGS) -Wno-unused-function -DSMK_SPEC_HOST_FEED -I$(ROOT)/firmware/gen -I$(ROOT)/firmware/hal shot_playgen.c -lm -o $@

clean:
	rm -rf $(OUT)

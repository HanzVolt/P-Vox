# Host checks for the scratch Polivoks tree. Not the target link.
CC ?= gcc
ROOT := ..
FW := $(ROOT)/firmware/src
HOSTCFLAGS := -std=c11 -Wall -Wextra -Werror -O2 -I$(FW) -I.
SANCFLAGS := -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=undefined -fsanitize-trap=undefined -I$(FW) -I.
OUT ?= ../build/host-tests

.PHONY: test test-san clean

test: $(OUT)/test_fcore $(OUT)/test_voice $(OUT)/test_nav $(OUT)/test_safety $(OUT)/test_env $(OUT)/test_tune $(OUT)/test_preset
	mkdir -p $(OUT)/wav $(OUT)/logs
	$(OUT)/test_fcore $(OUT)/wav | tee $(OUT)/logs/test_fcore.log
	$(OUT)/test_voice $(OUT)/wav | tee $(OUT)/logs/test_voice.log
	$(OUT)/test_nav | tee $(OUT)/logs/test_nav.log
	$(OUT)/test_safety | tee $(OUT)/logs/test_safety.log
	$(OUT)/test_env | tee $(OUT)/logs/test_env.log
	$(OUT)/test_tune | tee $(OUT)/logs/test_tune.log
	$(OUT)/test_preset | tee $(OUT)/logs/test_preset.log

test-san: $(OUT)/test_fcore_san $(OUT)/test_voice_san $(OUT)/test_nav_san $(OUT)/test_safety_san $(OUT)/test_env_san $(OUT)/test_tune_san $(OUT)/test_preset_san
	mkdir -p $(OUT)/wav $(OUT)/logs
	$(OUT)/test_fcore_san $(OUT)/wav | tee $(OUT)/logs/test_fcore_san.log
	$(OUT)/test_voice_san $(OUT)/wav | tee $(OUT)/logs/test_voice_san.log
	$(OUT)/test_nav_san | tee $(OUT)/logs/test_nav_san.log
	$(OUT)/test_safety_san | tee $(OUT)/logs/test_safety_san.log
	$(OUT)/test_env_san | tee $(OUT)/logs/test_env_san.log
	$(OUT)/test_tune_san | tee $(OUT)/logs/test_tune_san.log
	$(OUT)/test_preset_san | tee $(OUT)/logs/test_preset_san.log

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

clean:
	rm -rf $(OUT)

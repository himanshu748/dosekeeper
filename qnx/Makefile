# Build with the QNX SDP 8.0 toolchain:  source ~/qnx800/qnxsdp-env.sh && make
ifeq ($(origin CC),default)
CC      := qcc
endif
VARIANT ?= gcc_ntox86_64
CFLAGS  += -V$(VARIANT) -std=gnu11 -O2 -Wall -Wextra -Werror
OUT     := build
BINS    := $(addprefix $(OUT)/,motor_sim pumpd guardian loadgen gateway)

all: $(BINS)

$(OUT)/%: %.c common.c dose.c common.h dose.h | $(OUT)
	$(CC) $(CFLAGS) -o $@ $< common.c dose.c $(LDLIBS_$*)

LDLIBS_gateway := -lsocket

$(OUT):
	mkdir -p $@

test: | $(OUT)
	gcc -std=gnu11 -Wall -Wextra -Werror -O2 dose.c test/test_dose.c -o $(OUT)/test_dose && $(OUT)/test_dose

clean:
	rm -rf $(OUT)

.PHONY: all test clean

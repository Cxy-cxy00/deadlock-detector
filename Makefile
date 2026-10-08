# Makefile — build libdetect.so และโปรแกรมทดสอบ
# ใช้บน Linux เท่านั้น (LD_PRELOAD เป็นกลไกของ glibc dynamic linker)

CC       := gcc
CFLAGS   := -std=c11 -Wall -Wextra -O2 -g -fPIC -pthread -D_GNU_SOURCE
LDFLAGS  := -shared
LDLIBS   := -ldl -pthread

BUILD    := build
SRC      := $(wildcard src/*.c)
OBJ      := $(patsubst src/%.c,$(BUILD)/%.o,$(SRC))
LIB      := $(BUILD)/libdetect.so
TESTS    := $(BUILD)/bank $(BUILD)/no_deadlock

.PHONY: all tests clean demo playground
all: $(LIB) tests

$(LIB): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

tests: $(TESTS)

# -g จำเป็น: ไม่มี debug info แล้ว detector ชี้บรรทัดไม่ได้
$(BUILD)/%: tests/%.c | $(BUILD)
	$(CC) -std=c11 -Wall -Wextra -g -O0 -pthread -o $@ $<

$(BUILD):
	mkdir -p $(BUILD)

demo: all
	./tests/run_demo.sh

# หน้าเว็บที่กดรันโค้ดได้จริง — เปิด http://127.0.0.1:8000
# เซิร์ฟเวอร์นี้ compile และรันโค้ดที่ส่งมาจากหน้าเว็บ ใช้เฉพาะบนเครื่องตัวเอง
playground: all
	python3 tools/playground.py

clean:
	rm -rf $(BUILD) *.dot *.png

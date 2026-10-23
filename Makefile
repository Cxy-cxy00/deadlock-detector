# Makefile — build libdetect.so และโปรแกรมทดสอบ
# ใช้บน Linux เท่านั้น (LD_PRELOAD เป็นกลไกของ glibc dynamic linker)

CC       := gcc
# -MMD -MP: ให้ gcc เขียนไฟล์ .d บอกว่า .o แต่ละตัว include header อะไรบ้าง
# แก้ header แล้ว make จะ build .o ที่เกี่ยวข้องใหม่เอง (ไม่ต้อง make clean)
CFLAGS   := -std=c11 -Wall -Wextra -O2 -g -fPIC -pthread -D_GNU_SOURCE -MMD -MP
LDFLAGS  := -shared
LDLIBS   := -ldl -pthread

BUILD    := build
SRC      := $(wildcard src/*.c)
OBJ      := $(patsubst src/%.c,$(BUILD)/%.o,$(SRC))
DEPS     := $(OBJ:.o=.d)
LIB      := $(BUILD)/libdetect.so
TESTS    := $(BUILD)/bank $(BUILD)/no_deadlock $(BUILD)/sample_webapp $(BUILD)/two_pairs \
            $(BUILD)/cycle3 $(BUILD)/condvar
BENCH    := $(BUILD)/bench

.PHONY: all tests clean demo playground sample_webapp bench
all: $(LIB) tests

$(LIB): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

tests: $(TESTS)

sample_webapp: $(BUILD)/sample_webapp

# -g จำเป็น: ไม่มี debug info แล้ว detector ชี้บรรทัดไม่ได้
$(BUILD)/%: tests/%.c | $(BUILD)
	$(CC) -std=c11 -Wall -Wextra -g -O0 -pthread -o $@ $<

# bench วัดเวลา จึงใช้ -O2 แทน -O0 ของโปรแกรมทดสอบ (rule เฉพาะชนะ pattern rule ข้างบน)
$(BENCH): tests/bench.c | $(BUILD)
	$(CC) -std=c11 -Wall -Wextra -O2 -pthread -o $@ $<

# วัด overhead มี/ไม่มี LD_PRELOAD แล้วเขียนผลลง tests/logs/bench.txt
bench: $(LIB) $(BENCH)
	bash tests/run_bench.sh

$(BUILD):
	mkdir -p $(BUILD)

demo: all
	bash tests/run_demo.sh

# หน้าเว็บที่กดรันโค้ดได้จริง — เปิด http://127.0.0.1:8000
# เซิร์ฟเวอร์นี้ compile และรันโค้ดที่ส่งมาจากหน้าเว็บ ใช้เฉพาะบนเครื่องตัวเอง
playground: all
	python3 tools/playground.py

clean:
	rm -rf $(BUILD) *.dot *.png

-include $(DEPS)

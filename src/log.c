/* log.c — พิมพ์ข้อความของเราออก stderr โดยไม่ไปยุ่งกับ stdio ของโปรแกรมเป้าหมาย */
#include <stdarg.h>
#include "common.h"

void dd_logf(const char *fmt, ...) {
    (void)fmt;  /* TODO: vsnprintf ลง buffer บน stack แล้ว write(2, ...) */
}

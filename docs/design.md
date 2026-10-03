# บันทึกการออกแบบ

สถานะ: โครงไฟล์ ยังไม่ implement — ไฟล์นี้ไว้จดการตัดสินใจระหว่างทำ

## คำถามที่ยังไม่ตัดสินใจ

- **ตารางสถานะ**: array ขนาดคงที่ (`DD_MAX_MUTEXES`) หรือ hash table ที่ขยายได้
  array ง่ายกว่าและไม่ต้อง malloc ใน lock path แต่จำกัดจำนวน mutex
- **กัน recursion**: ธง `__thread` หรือใช้ `real_mutex_*` ล้วนภายในโค้ดเรา
  ถ้าโค้ดของเราเผลอเรียก `pthread_mutex_lock` ตัวที่เราทับไว้ จะวนกลับมาที่ตัวเองไม่สิ้นสุด
- **หา file:line**: `backtrace()` + เรียก `addr2line` ตอนรายงาน หรือฝัง libbacktrace
  วิธีแรกง่ายกว่าแต่ต้องมี binutils ตอนรัน
- **ตรวจตอนไหน**: detector thread ตามคาบ (`DD_CHECK_INTERVAL_MS`) หรือตรวจทันทีทุกครั้งที่มี wait edge ใหม่
  ตรวจทันทีไวกว่า แต่ทำให้ lock path ช้าลงและเสี่ยง deadlock ในโค้ดของเราเอง
- **รายงานซ้ำ**: cycle เดิมควรพิมพ์ครั้งเดียวหรือพิมพ์ทุกคาบ

## ข้อควรระวัง

- โค้ดใน lock path ทำงานใน thread ของโปรแกรมเป้าหมาย — หลีกเลี่ยง `malloc`, `printf`, และการล็อกใด ๆ
- `dd_logf` ต้องใช้ `write(2, ...)` ไม่ใช่ `fprintf` เพราะ stdio ของเป้าหมายอาจถูกล็อกค้างอยู่
- ลำดับ `wait_begin` → `real_lock` → `wait_end` ต้องถูกต้องเสมอ ถ้า `real_lock` คืน error ก็ยังต้องลบ wait edge

## อ้างอิง

- `ld.so(8)`, `dlsym(3)` — กลไก `LD_PRELOAD` และ `RTLD_NEXT`
- `pthread_mutex_lock(3)`
- `docs/overview.html` — หน้าสรุปโปรเจ็คที่ใช้นำเสนอ

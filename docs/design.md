# บันทึกการออกแบบ

ไฟล์นี้จดว่าแต่ละทางเลือกตัดสินใจไปทางไหนและเพราะอะไร

## Scope ที่ทำจริง

- interpose `pthread_mutex_lock` / `pthread_mutex_trylock` / `pthread_mutex_unlock` เท่านั้น
- ตารางสถานะ (array ขนาดคงที่) · wait-for graph + DFS · detector thread ตรวจทุก 500 ms
- รายงานทีละ 1 cycle พร้อม `file:line` (PIE + `-g` + `addr2line`) · export Graphviz
- cycle ยาวเกิน `DD_MAX_CYCLE_LEN` รายงานเป็น partial path
- robust mutex เฉพาะ 2 กรณี: `EOWNERDEAD` นับว่าได้ lock · unlock โดยคนที่ไม่ใช่เจ้าของไม่ลบเจ้าของเดิม
- **ยังไม่ทำ:** เตือน lock-order inversion ล่วงหน้า
- ข้อจำกัดทั้งหมดอยู่ที่หัวข้อ Limitations ใน README

## การตัดสินใจที่ลงตัวแล้ว

**โครงสร้างตารางสถานะ: array ขนาดคงที่ ไม่ใช่ hash table**
เหตุผลหลักไม่ใช่ "ง่ายกว่า" แต่เป็น **ห้าม `malloc` ใน lock path**
(`malloc` มี lock ข้างใน จะวนกลับมาที่ `pthread_mutex_lock` ที่เรา interpose ไว้)
static array จึงเป็นทางเลือกเดียวที่ปลอดภัยจริง
ขนาดตารางมาจากธรรมชาติของปัญหา: 1 thread รอได้ทีละ 1 mutex, 1 mutex มีเจ้าของได้ทีละ 1 คน

**กัน recursion: ใช้ทั้งสองวิธีพร้อมกัน**
ธง `__thread int in_dd` เป็นเกราะชั้นแรก และวินัย "ในโค้ดเราเรียกแต่ `real_*`" เป็นชั้นที่สอง
อย่างใดอย่างหนึ่งเดียว ๆ ไม่พอ เพราะ `dlsym` กับ `malloc` เรียก lock ข้างในโดยที่เราคุมไม่ได้

**หา file:line: `dladdr()` + `addr2line` ผ่าน `popen`**
ไม่ฝัง libbacktrace เพราะไม่อยากเพิ่ม dependency ตอน build
ทำแค่ตอนรายงาน (ครั้งเดียวต่อหนึ่ง cycle) ซึ่งอยู่ใน detector thread ไม่ใช่ lock path
glibc รุ่นใหม่ทำ `popen` ด้วย `posix_spawn` (child exec ทันที) ไม่ใช่ fork แล้วรันโค้ดต่อ
ความเสี่ยงเรื่อง fork ใน process หลาย thread จึงต่ำ แต่ `popen` ยังเรียก `malloc` และ lock ภายในของ stdio
แลกกับการต้องมี binutils ตอนรัน ถ้าไม่มีก็ถอยไปแสดง pc ดิบ

**ตรวจตอนไหน: detector thread ตามคาบ ไม่ใช่ตรวจทันทีทุก wait edge**
ตรวจทันทีจะไวกว่าก็จริง แต่ทำให้ lock path ช้าลงทุกครั้งที่ล็อก และเสี่ยงสร้าง deadlock
ในโค้ดของเราเอง  คาบ 500ms ไวพอสำหรับ deadlock ซึ่งค้างถาวรอยู่แล้ว ไม่ได้หายไปไหน

**รายงานซ้ำ: พิมพ์ครั้งเดียวต่อหนึ่ง cycle**
เทียบว่าเป็นวงเดิมหรือไม่แบบ "เซต" ไม่ใช่เทียบทีละตำแหน่ง เพราะ DFS อาจเริ่มไล่จากคนละ node
แล้วได้ลำดับเริ่มต้นต่างกันทั้งที่เป็นวงเดียวกัน

## เรื่องที่รู้ตอนลงมือทำจริง

- **กราฟนี้มี out-degree ไม่เกิน 1** เพราะ thread ค้างใน `pthread_mutex_lock` ได้ทีละ mutex เดียว
  DFS จึงไม่ต้องใช้ recursion หรือ adjacency list — เดินตามโซ่เป็น loop ธรรมดาได้
  ถ้าวันหนึ่งไปดัก rwlock หรือ semaphore ข้อนี้จะไม่จริงอีกต่อไป ต้องเปลี่ยนเป็น DFS เต็มรูป
- **`__builtin_return_address(0)` คืนที่อยู่ของคำสั่ง "ถัดจาก" `call`** ต้องลบ 1 ก่อนส่งให้ `addr2line`
  ไม่งั้นจะชี้บรรทัดถัดไป ซึ่งผิดแบบที่ดูน่าเชื่อถือมาก
- **pc ที่เก็บได้เป็นที่อยู่ตอนรัน ไม่ใช่ offset ในไฟล์** เพราะ PIE + ASLR
  ต้องลบ `dli_fbase` จาก `dladdr()` ออกก่อน — สูตรนี้ถูกเฉพาะ PIE / shared object
  binary แบบ non-PIE ได้ `dli_fbase` เป็นที่อยู่โหลดจริง (เช่น 0x400000) ไม่ใช่ 0
  offset ที่ได้จึงผิด addr2line หาไม่เจอ แล้วรายงานถอยไปแสดง `at pc 0x...` (ทดสอบกับ `-no-pie` แล้ว)
- **มาโคร ไม่ใช่ฟังก์ชัน** สำหรับเก็บ return address — `DD_CALLER_SITE()` ต้อง expand ในตัว wrapper
  ถ้าทำเป็นฟังก์ชันจะได้ที่อยู่ของ wrapper เราเองแทนที่จะเป็นของโปรแกรมเป้าหมาย
- **`LD_PRELOAD` ตกทอดไปยัง process ลูก** เช่น `LD_PRELOAD=... timeout 5 ./bank`
  ตัว `timeout` ก็โดน preload ไปด้วย เป็นข้อดีถ้าอยากตามทั้ง process tree แต่เป็นกับดักถ้าไม่รู้
  (รวมถึง `sh`/`addr2line` ที่ report.c เรียกผ่าน `popen` — เห็นได้จาก `DD_VERBOSE=1`)
- **ld.so แยก `LD_PRELOAD` ด้วยช่องว่างและ `:`** path เต็มที่มีช่องว่าง (เช่น `.../Bachelor degree/...`)
  จะโหลดไม่ขึ้นแบบเงียบ ๆ  เจอตอนทำ baseline (`tests/logs/baseline_pathspace/`)
  `run_demo.sh` จึงใช้ path แบบ relative และ `playground.py` แจ้ง error
- **ใช้ `pthread_barrier` บังคับจังหวะใน test ไม่ใช่ `usleep`** sleep แค่ "น่าจะ" นานพอ
  barrier ของ glibc ใช้ futex ของมันเอง ไม่ผ่าน `pthread_mutex_lock` จึงไม่ปนในตาราง (ตรวจด้วย `DD_VERBOSE=1`)
- **stdio ของ glibc ไม่ได้ใช้ `pthread_mutex_lock`** แต่ใช้ lock ภายในของตัวเอง
  เราจึงไม่เห็น lock ของ `printf` เลย ซึ่งเป็นผลดี — เห็นแต่ mutex ของโปรแกรมเป้าหมายจริง ๆ
- **detector ไม่ต้องกรองตัวเองออกจากกราฟ** เพราะล็อกตารางผ่าน `real_mutex_lock` ซึ่งไม่ผ่าน wrapper
  จึงไม่เคยถูกบันทึกตั้งแต่แรก (ยังตั้งธง `in_dd` ค้างไว้เป็นเกราะชั้นสองอยู่ดี)

## ข้อควรระวังสำหรับคนที่มาแก้ต่อ

- โค้ดใน lock path ทำงานใน thread ของโปรแกรมเป้าหมาย — หลีกเลี่ยง `malloc`, `printf`, และการล็อกใด ๆ
- `dd_logf` ต้องใช้ `write(2, ...)` ไม่ใช่ `fprintf` เพราะ stdio ของเป้าหมายอาจถูกล็อกค้างอยู่
  และ buffer ของ stdio จะไม่ถูก flush ถ้าโปรแกรมไม่เคยจบ
- ลำดับ `wait_begin` -> `real_lock` -> `wait_end` ต้องถูกต้องเสมอ ถ้า `real_lock` คืน error
  ก็ยังต้องลบ wait edge ไม่งั้นจะมี edge ผีค้างแล้วรายงาน deadlock ที่ไม่มีจริง
- `dd_state_released` ต้องจดก่อนปล่อยจริง ไม่งั้น thread อื่นอาจคว้า mutex ไปแล้วจดตัวเองเป็นเจ้าของ
  พอเรามาจดทีหลังจะไปลบข้อมูลของเขาทิ้ง

## อ้างอิง

- `ld.so(8)`, `dlsym(3)`, `dladdr(3)` — กลไก `LD_PRELOAD`, `RTLD_NEXT` และการหา base address
- `pthread_mutex_lock(3)`, `gettid(2)`
- `docs/overview.html` — หน้าสรุปโปรเจ็คที่ใช้นำเสนอ

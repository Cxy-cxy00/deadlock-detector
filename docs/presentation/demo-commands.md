# Demo Commands

## เข้าโฟลเดอร์และ build

```bash
cd ~/dd
```

```bash
make
```

## Step 1

```bash
timeout 3 ./build/bank; echo "exit=$?"
```

## Step 2

```bash
timeout 3 env LD_PRELOAD=./build/libdetect.so ./build/bank; echo "exit=$?"
```

## Step 3

```bash
DD_DOT_OUT=cycle.dot timeout 3 env LD_PRELOAD=./build/libdetect.so ./build/bank 2>/dev/null; dot -Tpng cycle.dot -o cycle.png; explorer.exe cycle.png
```

## Step 4

```bash
timeout 15 env LD_PRELOAD=./build/libdetect.so ./build/no_deadlock; echo "exit=$?"
```

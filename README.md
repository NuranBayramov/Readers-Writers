# Readers-Writers (C)
Readers Writers problem with Load Balancing

## Build

```bash
gcc -o readers_writers readers_writers.c -lpthread
```

## Run

```bash
./readers_writers
```

Output goes to the console and `system.log`.

## What it does

- Spawns 20 reader threads at random intervals
- Runs 1 writer thread that does 5 write cycles
- Readers are spread across 3 replica files (least-loaded wins)
- Writer blocks new readers as soon as it announces intent (`writers_waiting++`)
- All three replicas are updated before the writer lets readers back in

## Tweaking

Change these defines at the top of `readers_writers.c`:

```c
#define NUM_READERS  20   // total reader threads
#define NUM_WRITES   5    // how many times the writer runs
```

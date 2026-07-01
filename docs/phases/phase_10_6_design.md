# Phase 10.6 — Multi-Threading Foundation: Job System (MT1 + MT2 Design Doc)

**Status:** **Signed off (2026-07-01)** — cold-eyes converged over 4 cold loops
(CRITICAL/HIGH/MEDIUM all 0 by Loop 4; see §11), delegated sign-off per
`[[feedback_spec_signoff_delegated]]` (gate is `/cold-eyes` convergence, not a
blocking user review). Implementation may begin at MT2-S1.

**Scope of this doc:** MT1 (pick the job-system library) + MT2 (the
`engine/core/job_system.{h,cpp}` wrapper). This is the foundation the rest of the
Phase 10.6 threading bullets (MT3–MT15) and the first real consumer —
**AX1 geometric audio occlusion** — build on. MT3 (THREADING.md policy), MT5+
(render-thread split, parallel culling, ECS scheduler) are **out of scope here**
and remain their own roadmap bullets; this doc seeds the safety rules they will
formalise (§4) but does not implement them.

**Driving decision (user, 2026-07-01):** AX1's rays were specified to run on
"MT2 (Phase 10.6)", which was unbuilt. Given the choice *build MT2 first* vs
*ship AX1 synchronously now*, the user chose **build MT2 first** — so this
foundation lands before AX1.

---

## 0. What already exists (reality check, verified 2026-07-01)

Verified against current source (Explore agent map + direct reads):

- **No engine-level job system exists.** No `engine/threading/`, no
  `engine/jobs/`, no `class JobSystem` / `ThreadPool` / `TaskScheduler` /
  `parallelFor`. Grep across the whole tree is empty. `ROADMAP.md:654` (MT2)
  describes it as a spec, not code.
- **The only threads today** are: `Logger` (mutex-guarded), `AsyncTextureLoader`
  (one worker), autosave (`std::async`), tile streaming (Phase 11A), OpenAL's
  internal mixer thread, and Jolt's **internal** `JPH::JobSystemThreadPool`
  (owned privately by `PhysicsWorld`, `engine/physics/physics_world.h:294`, `m_jobSystem`),
  which is **already constructed multi-threaded** with
  `max(1, hardware_concurrency()-1)` workers (`engine/physics/physics_world.cpp:104-111`) — so
  the roadmap MT8 premise ("today runs a single-threaded `JobSystemSingleThreaded`")
  is stale; MT8's live remaining scope is *sharing* Jolt's pool with MT2, a
  *separate* bullet, out of scope here.
- **The engine tick is serial.** `Engine`'s main loop runs `updateAll(dt)`
  (`engine/core/engine.cpp:1355`) after physics is stepped (`engine/core/engine.cpp:1320-1350`). There
  is **no dedicated engine audio thread** — `AudioSystem::update` runs on the
  main thread in the `PostCamera` phase (`engine/systems/audio_system.h:43`).
  (Throughout this doc "audio thread" means OpenAL's *internal mixer* thread —
  a distinct thread this doc never runs jobs on but §6 reserves a core for.)
- **FetchContent is the dependency mechanism.** `external/CMakeLists.txt` pins
  every dep by tag (`GIT_TAG` + `GIT_SHALLOW TRUE`): GLFW `3.4`, GLM `1.0.1`,
  Jolt `v5.3.0`, OpenAL-Soft `1.25.1`, libebur128 `v1.2.6`, GoogleTest
  `v1.15.2`, etc. New deps follow this exact pattern.
- **The project is C++17** (`CMakeLists.txt:9`, `CMAKE_CXX_STANDARD 17`,
  `CMAKE_CXX_EXTENSIONS OFF`). Any library we pull must build under C++17.

**Implication:** MT2 is greenfield infrastructure. Because there is no audio
worker thread today, MT2's *first* value to AX1 is not "get work off a busy
thread" (there isn't one) but "spread the per-frame raycast batch across cores
so the main-thread tick doesn't pay the whole cost serially", plus giving the
project the substrate the roadmap's whole Phase 10.6 program needs.

---

## 1. Goals & non-goals

### Goals
1. A minimal, well-tested `JobSystem` wrapping a proven library, exposing the
   `ROADMAP.md:654` surface: `submit(fn)`, `wait(handle)`, `runOnMainThread(fn)`,
   plus the ergonomic primitive AX1 actually needs — `parallelFor(count, fn)`.
   The roadmap also names `submitGraph(deps)`; it is **deferred to MT2.1** (D2 /
   §3.1) — no v1 consumer, and building an untested DAG wrapper is the scaffolding
   Rule 2 forbids.
2. **Deterministic test seam** — worker count `0` selects a **wrapper-owned
   synchronous mode**: the wrapper creates *no* enkiTS scheduler and runs each
   submitted job inline on the caller, returning an already-complete handle
   (`wait` is a no-op). This does **not** depend on any enkiTS 0-thread
   behaviour — auto mode resolves to `max(1, hardware_concurrency()-1)`, so
   enkiTS never receives 0. Unit tests and determinism-sensitive replay paths
   (Phase 11A) get reproducible behaviour and the dependency-injection seam MT1
   asks for.
3. Single global instance **owned by `Engine`**, reachable by subsystems via the
   existing `SystemRegistry`/`Engine&` seam (footstep-system pattern).
4. Protect the **60 FPS floor**: submission is cheap; the design forbids the
   main-thread-blocking-wait anti-pattern (§6).
5. Ship with a **race-detector safety net** — a `-DENGINE_TSAN=ON` build option +
   a job-system stress test (a minimal down-payment on MT4, because shipping a
   new threading subsystem with no TSAN coverage is precisely the gap MT4 names).

### Non-goals (explicit — deferred to their own bullets)
- **Render-thread split / parallel culling / ECS scheduler** (MT5–MT11). Not here.
- **Swapping Jolt onto the shared pool** (MT8). Jolt keeps its own internal pool
  for now; MT2 does not touch physics threading.
- **Fibers / coroutines** (marl/concurrencpp style). OS-thread tasks only.
- **`VESTIGE_ASSERT_MAIN_THREAD()` everywhere** (MT14) — we add the *macro* and
  use it inside JobSystem, but do not sweep it across all GL/ImGui call sites.
- **The full THREADING.md policy doc** (MT3) — §4 seeds it; the standalone doc is
  its own bullet.

---

## 2. MT1 — Library evaluation & decision

MT1 requires an honest evaluation before MT2 wraps anything. Candidates from
`ROADMAP.md:650` (license-safe only): **enkiTS**, **Taskflow**, **marl**,
**concurrencpp**, **in-house**.

| Candidate | License | C++ std | Model | Dep-graph | Pinned/main-thread | Size | FetchContent fit | Verdict |
|-----------|---------|---------|-------|-----------|--------------------|------|------------------|---------|
| **enkiTS v1.11** | zlib | **C++11** | OS-thread task-sets | Yes (`SetDependency`, `enki::Dependency`) | Yes (`IPinnedTask`, `threadNum`) | ~3 KLOC | `add_subdirectory`/FetchContent, CMake native | **CHOSEN** |
| Taskflow 3.10 | MIT | C++17 (4.0 needs C++20) | Header-only task-graph | Excellent (its whole point) | Via executor, less direct | Large headers, heavier compile | Header-only | Runner-up |
| marl | Apache-2.0 | C++11 | **Fibers** | Via `Event`/scheduler | Yes | Medium | add_subdirectory | Rejected — fibers = complexity/debuggability cost we don't need yet |
| concurrencpp | MIT | **C++20** | Coroutines | Via `result`/`when_all` | Executors | Medium | add_subdirectory | Rejected — C++20 (project is C++17) |
| in-house | — | C++17 | `std::thread` pool | hand-rolled | hand-rolled | small | n/a | Rejected — reinvents a solved, race-prone wheel (global Rule 3) |

### Decision: **enkiTS v1.11** (matches the roadmap's default presumption)

Reasons, weighed against *our* load profile (a per-frame batch of independent
raycasts now; parallel culling/animation/ECS later):

1. **C++11 → builds cleanly under our C++17** with zero standard-version
   friction. Taskflow 4.0 wants C++20; concurrencpp *requires* C++20. (Global
   Rule 5 — latest idiom, but not at the cost of a standard bump the whole
   engine isn't ready for.)
2. **zlib license** — same permissive class as GLFW/GLM, already accepted in
   `THIRD_PARTY_NOTICES.md`; no copyleft, no attribution-in-binary burden.
3. **Every MT2 verb has a native enkiTS primitive** (see §3 mapping): task-sets →
   `parallelFor`/`submit`, pinned tasks → main-thread option, `SetDependency` →
   `submitGraph`, `numThreadsToCreate` → worker count, `0 threads` →
   deterministic synchronous mode. Almost nothing to invent; the wrapper is thin.
4. **~3 KLOC, one static lib** — negligible binary-size and compile-time cost vs
   Taskflow's large template headers. It is Doom Eternal's scheduler (idTech 7);
   its design point ("a few thousand medium-grained tasks per frame") is exactly
   ours.
5. **Proven, stable, maintained** — v1.0 shipped 2019; v1.11 (2024) is the
   current tag; API is settled.

**Taskflow** would win *only* if dependency-graph ergonomics were the dominant
need. They are not yet: AX1 needs a parallel-for, not a DAG. If a future bullet
(MT9 ECS scheduler) finds enkiTS's dependency API too bare, revisiting Taskflow
is cheap because MT2's wrapper hides the library behind our own `JobSystem` type
(that indirection is a primary reason MT2 exists — Rule 3(b)).

### Integration (FetchContent)
`external/CMakeLists.txt`, mirroring the existing tagged-pin pattern:
```cmake
# enkiTS — CPU task scheduler (MT2). zlib license.
FetchContent_Declare(
    enkiTS
    GIT_REPOSITORY https://github.com/dougbinks/enkiTS.git
    GIT_TAG        v1.11
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(enkiTS)   # provides the `enkiTS` static target
```
Add `enkiTS` to the engine target's link libraries; record it in
`THIRD_PARTY_NOTICES.md` (zlib, v1.11, "CPU job scheduler") per project Rule 8.
Per global Rule 5c we take the current stable tag (`v1.11`); no older pin, so no
written-reason exemption needed.

---

## 3. MT2 — `JobSystem` API design

`engine/core/job_system.{h,cpp}`. Header-visible surface (Allman braces, project
naming). The wrapper owns an `enki::TaskScheduler` and hides it entirely — no
enkiTS type appears in the public header, so the library can be swapped without
touching call sites.

### 3.1 Types

```cpp
/// Opaque completion token returned by every submit. Copyable, cheap
/// (shared-ownership of the in-flight task). Safe to discard for
/// fire-and-forget; the JobSystem keeps the task alive until it completes.
class JobHandle
{
public:
    JobHandle() = default;                 ///< Empty handle: isValid()==false.
    bool isValid() const;
    bool isComplete() const;               ///< Non-blocking poll.
private:
    friend class JobSystem;
    std::shared_ptr<detail::JobTask> m_task;   ///< nullptr for empty/sync jobs.
};

struct JobSystemConfig
{
    /// Worker threads to spawn.
    ///  -1 (default) → max(1, hardware_concurrency()-1) — enkiTS never gets 0.
    ///   0 → wrapper-owned SYNCHRONOUS mode: NO enkiTS scheduler is created;
    ///       submit()/parallelFor() run inline on the caller and return an
    ///       already-complete handle; wait() is a no-op. Deterministic; used by
    ///       unit tests and the determinism-sensitive replay set. Does not rely
    ///       on any enkiTS 0-thread semantics.
    ///  >=1 → that many enkiTS worker threads.
    int numWorkers = -1;
};

```

> **`submitGraph` / `JobGraph` are DEFERRED out of MT2 v1** (see D2). AX1 — the
> only near-term consumer — needs `parallelFor`, not a dependency graph. Building
> a correct enkiTS-backed DAG (non-copyable `Dependency` objects that must
> outlive their tasks, acyclic validation, graph-state lifetime) with **no
> consumer to exercise it** is exactly the speculative scaffolding global Rule 2
> forbids. It lands as a follow-up (MT2.1) when a real consumer arrives (MT9 ECS
> scheduler), at which point enkiTS `SetDependency`/`enki::Dependency` is ready.
> The roadmap's MT2 surface names it; this doc records the deliberate deferral
> rather than ship untested graph code.

### 3.2 Methods

```cpp
class JobSystem
{
public:
    explicit JobSystem(const JobSystemConfig& config = {});
    ~JobSystem();                          ///< Waits for all in-flight work, then shuts down.
    JobSystem(const JobSystem&) = delete;  ///< Single owner (Engine).
    JobSystem& operator=(const JobSystem&) = delete;

    /// One unit of work. Returns immediately (async) unless numWorkers==0.
    JobHandle submit(std::function<void()> work);

    /// Data-parallel loop. `work(begin, end)` is called on worker threads over
    /// disjoint sub-ranges covering [0, count). This is AX1's primitive.
    JobHandle parallelFor(uint32_t count, std::function<void(uint32_t begin, uint32_t end)> work);

    // NOTE: submitGraph(JobGraph) is DEFERRED to MT2.1 (see §3.1 note + D2) —
    // no v1 consumer; AX1 needs only parallelFor.

    /// Block the CALLING thread until `handle` completes. While waiting, the
    /// calling thread helps run other queued work (enkiTS WaitforTask
    /// semantics) so there is no deadlock if a worker is busy. No-op on an
    /// empty/complete handle.
    void wait(const JobHandle& handle);

    /// Enqueue `work` to run on the MAIN thread. Thread-safe to call from any
    /// worker. Runs when drainMainThreadQueue() is called at frame top.
    void runOnMainThread(std::function<void()> work);

    /// Drain the runOnMainThread queue. MUST be called only from the main
    /// thread (asserts VESTIGE_ASSERT_MAIN_THREAD). Called once per frame by
    /// Engine at the top of the tick.
    void drainMainThreadQueue();

    int workerCount() const;               ///< Actual worker threads (0 in sync mode).
};
```

### 3.3 enkiTS mapping (implementation sketch — `.cpp` only)

| `JobSystem` verb | enkiTS mechanism |
|------------------|------------------|
| ctor(`numWorkers`) | resolve: `-1`→`max(1, hardware_concurrency()-1)`; `0`→**no scheduler** (wrapper-owned sync mode); `>=1`→that many. When `>=1`: `enki::TaskSchedulerConfig cfg; cfg.numTaskThreadsToCreate = static_cast<uint32_t>(resolved); m_scheduler.Initialize(cfg);` (resolved is clamped `>=1` before the cast, so the `int`→`uint32_t` narrowing is safe) |
| `submit(fn)` | heap `detail::JobTask : enki::ITaskSet` with `m_SetSize=1`, holds `std::function`; `AddTaskSetToPipe(&task)`; `shared_ptr` held by the returned `JobHandle` **and** pushed to `m_inFlight` (main-thread-only, see below) so a discarded handle still lives to completion |
| `parallelFor(n, fn)` | `detail::JobTask` with `m_SetSize=n`; `ExecuteRange(range, threadnum)` calls `fn(range.start, range.end)` — `range.end` **exclusive** (enkiTS partitions are half-open `[start,end)`) |
| `wait(h)` | `m_scheduler.WaitforTask(h.m_task->enkiTaskPtr())` (no-op on empty/complete handle; no-op in sync mode) |
| `runOnMainThread(fn)` | push `fn` to a `std::mutex`-guarded `std::vector` (the ONLY worker-callable verb) |
| `drainMainThreadQueue()` | assert main thread; swap the queue vector under its lock, run each fn outside the lock; **then sweep `m_inFlight`**, dropping tasks whose `enki::ICompletable::GetIsComplete()` is true |
| dtor | `m_scheduler.WaitforAllAndShutdown()` (sync mode: nothing to wait) |

**Fire-and-forget lifetime — resolved by main-thread-only ownership (no
completion callback).** enkiTS has no "completion action" API; its
completion hook (`ICompletable::OnDependenciesComplete`) fires *on a worker
thread*, and freeing a task from inside its own completion path is a
use-after-free footgun. So MT2 does **not** self-free from a worker. Instead:

- `submit`/`parallelFor` are **main-thread-only** (v1 constraint — see §4).
  `runOnMainThread` is the sole worker-callable verb. Therefore `m_inFlight`
  (the `shared_ptr<JobTask>` registry) is **only ever mutated on the main
  thread** — inserted at submit, reclaimed by the `drainMainThreadQueue` sweep.
  Workers never touch it. This removes the data-race / UAF class entirely; its
  `std::mutex` guards only against a future relaxation of the main-thread rule.
- A completed fire-and-forget task's `shared_ptr` lives in `m_inFlight` until the
  next frame's drain observes `GetIsComplete()` and drops it — a bounded
  one-frame reclamation delay, a handful of retired task objects held briefly.
  No leak (dtor's `WaitforAllAndShutdown` + a final sweep clears any stragglers),
  no UAF (the task outlives its own execution; enkiTS is fully done with the
  `ICompletable` by the time `GetIsComplete()` returns true).
- **Synchronous mode (`numWorkers==0`)** never puts anything in flight: `submit`/
  `parallelFor` run the fn inline and return an already-complete handle;
  `m_inFlight` stays empty; `wait` is a no-op.

### 3.4 Ownership & access
- `JobSystem m_jobSystem;` as an early **value member of `Engine`** (constructed
  before systems, destroyed after — RAII join on shutdown). Mirrors how
  `m_physicsWorld` is an Engine value member (`engine/core/engine.h:150`).
- New accessor `JobSystem& Engine::getJobSystem();` (mirrors
  `getPhysicsWorld()`, `engine/core/engine.h:283`).
- Subsystems reach it like `FootstepSystem` caches physics via the direct
  `Engine&` seam (`engine/systems/footstep_system.cpp:85`,
  `m_world = &engine.getPhysicsWorld();`): cache `m_jobs = &engine.getJobSystem();`
  in `initialize(Engine&)`. No new `SystemRegistry` plumbing needed — the
  `Engine&` seam already threads through. (The sibling caches in that same
  `initialize()` use `reg.getSystem<T>()`; the JobSystem-on-Engine case is the
  `getPhysicsWorld()`-style direct one.)
- `Engine` calls `m_jobSystem.drainMainThreadQueue()` **once at the top of the
  frame**, before systems update (so a job that posted main-thread work last
  frame is serviced deterministically this frame).

---

## 4. Threading model & safety (seeds MT3)

This doc does not write `docs/standards/THREADING.md` (that is MT3), but it
fixes the rules MT2 itself must obey and that AX1 will follow:

- **`submit`/`parallelFor`/`wait`/`drainMainThreadQueue` are MAIN-THREAD-ONLY in
  v1** (asserted with `VESTIGE_ASSERT_MAIN_THREAD`). `runOnMainThread` is the
  **sole worker-callable verb** — that is its purpose. This constraint is what
  makes `m_inFlight` race-free (§3.3): the registry is only mutated on the main
  thread, so a worker can never free a task or collide with an insert. AX1
  submits from `AudioSystem::update`, which runs on the main thread, so the
  constraint costs it nothing. (A future consumer that must submit from a worker
  would relax this deliberately, adding real locking — out of MT2 v1 scope.)
- **Worker-run `work` callbacks must not:** call GL, call ImGui, poll GLFW input,
  touch `Logger` in a per-frame hot path without its mutex, or take a Jolt body
  lock via `BodyInterface` (main-thread-only per `engine/physics/physics_world.h:114-116`).
  Jolt data needed on a worker must be read lock-free (the
  `engine/physics/contact_event.h:91-106` `GetUserData()` pattern) or captured before submit.
- **Locking / reclamation:** `JobSystem` holds at most one internal mutex at a
  time (the main-thread queue mutex; the `m_inFlight` mutex is a belt-and-braces
  guard that is, by the rule above, only taken on the main thread), and never
  calls a user callback while holding either. No nested locks → **no deadlock
  class**; main-thread-only `m_inFlight` mutation → **no data-race / UAF class**
  (the two failure modes a job system must rule out — not just deadlock).
- **`VESTIGE_ASSERT_MAIN_THREAD()`** — add the macro (`engine/core/`). The
  reference main-thread id is captured **once, in the `JobSystem` constructor**,
  which §3.4 pins to the `Engine` constructor running on the true main thread
  (capturing lazily on first drain could bind the wrong thread). The assert
  compares that captured id against `std::this_thread::get_id()`; `#ifndef
  NDEBUG` active, no-op in release. Used inside the main-thread-only verbs now;
  the engine-wide GL/ImGui sweep is MT14.

**AX1's obligation (documented here, enforced in the AX1 doc):** the raycast
`work` reads only data snapshotted on the main thread before submit (source
positions, listener position) and Jolt's `NarrowPhaseQuery::CastRay` (which is
const and lock-internal-safe for concurrent read queries — to be re-verified in
the AX1 design against Jolt 5.3.0 docs, not assumed here). Body→material recovery
that needs a lock (`getSurfaceMaterial`) happens on the main thread, not the
worker.

---

## 5. Slice plan & order (each = one commit + its own tests)

Dependency-respecting, simplest-first:

1. **MT2-S1 — Library + skeleton + `submit`/`wait` + synchronous mode.**
   FetchContent enkiTS; `THIRD_PARTY_NOTICES.md` row; `job_system.{h,cpp}` with
   ctor/dtor/config, `submit`, `wait`, `workerCount`, and `numWorkers==0`
   synchronous path. `VESTIGE_ASSERT_MAIN_THREAD` macro.
   *Verify:* `submit`+`wait` runs work once; a fire-and-forget `submit` (dropped
   handle) still runs (poll a counter); sync mode runs inline **at submit**
   (`wait` is a no-op — a never-waited sync submit has already run);
   `workerCount()` honours config.
2. **MT2-S2 — `parallelFor` + fire-and-forget lifetime hardening.**
   `ExecuteRange` mapping; `m_inFlight` registry populated at submit, reclaimed
   by the main-thread `drainMainThreadQueue` sweep (no worker-thread free — §3.3).
   *Verify:* a `parallelFor` summing `[0,N)` into per-range partials totals
   `N(N-1)/2` (all elements visited exactly once, no overlap); 10k dropped
   fire-and-forget jobs complete with no leak (ASan) and no crash after drains.
3. **MT2-S3 — `runOnMainThread` + `drainMainThreadQueue` (+ `m_inFlight` sweep).**
   *Verify:* work posted from a worker runs on the main thread only after drain,
   in FIFO order; drain from a non-main thread trips the assert (debug); the
   sweep reclaims completed fire-and-forget tasks.
4. **MT2-S4 — Engine ownership + frame-top drain + first integration.**
   `Engine::m_jobSystem` + `getJobSystem()` + drain call at tick top.
   *Verify:* full engine boots, `updateAll` still green, headless
   smoke (a system submits a trivial `parallelFor` and reads the result next
   frame) works; full ctest suite green.
   *(`submitGraph` is deferred to MT2.1 — see §3.1 note + D2 — so there is no S5.)*
5. **TSAN safety net (folded into S1/S2 verification, not a separate ship):**
   `-DENGINE_TSAN=ON` CMake option (adds `-fsanitize=thread` to a dedicated
   target) + a `job_system_stress` test (many concurrent `submit`/`parallelFor`/
   `runOnMainThread` with shared-counter reductions) that must be TSAN-clean.
   This is the MT4 down-payment; full CI TSAN gate stays MT4.

AX1 begins only after MT2-S4 (Engine integration) is green and pushed.

---

## 6. Performance (60 FPS hard floor)

- **Submission is cheap.** enkiTS enqueue is a lock-light push; a per-frame batch
  of tens–hundreds of tasks is sub-frame-budget. The scheduler is created once at
  boot, not per frame.
- **Worker count `hardware_concurrency()-1`** leaves a core for the OS + the
  OpenAL mixer thread (roadmap rationale), so audio glitching is not introduced.
- **The one anti-pattern to forbid (and we do, in §4 + the AX1 doc): a
  main-thread `wait()` immediately after `submit()`.** That serialises the work
  *plus* adds scheduling overhead — strictly worse than not threading. Consumers
  submit, do other main-thread work, and either (a) `wait()` late in the frame,
  or (b) consume results **next** frame (AX1's model — occlusion updates at
  ~10–15 Hz, one frame of latency is inaudible). MT2 provides `isComplete()` for
  the poll-not-block path.
- **No per-task heap churn in the hot path beyond one `std::function` +
  task object per submit.** `parallelFor` amortises N elements into one task
  object, so AX1's "N rays over M sources" is M task objects, not N×M.
- **Synchronous mode is not a perf path** — it exists for determinism/tests and
  1-core machines; on `numWorkers==0` the work runs inline (same cost as not
  threading, zero scheduling overhead).
- **Benchmark gate:** a Release-only micro-benchmark asserting `parallelFor` over
  a trivial kernel across all workers completes within a small fixed budget, and
  that scheduler construction is a one-time cost (guards against accidental
  per-frame re-init). Mirrors `test_fog_benchmark.cpp`'s NDEBUG-gated shape.

**CPU / GPU placement (project Rule 7):** **CPU, by nature.** A task scheduler is
branch-heavy, OS-thread-bound, I/O-and-decision work — the exact profile Rule 7
assigns to the CPU. There is no GPU analogue for job scheduling; GPU parallelism
(compute shaders) is an orthogonal axis already served by the renderer. No dual
CPU/GPU implementation applies; no parity test needed.

---

## 7. Accessibility

A job system has no direct user-facing surface, but two indirect obligations:

1. **Determinism preservation.** Parallel execution must never make a
   determinism-sensitive system (Phase 11A replay set) non-reproducible. MT2's
   contribution: the **`numWorkers==0` synchronous mode** gives a deterministic
   execution path for tests and for any consumer that must stay ordered, and
   `parallelFor`'s range split is deterministic given the same count. The general
   rule (determinism-set systems run single-ordered) is enforced by consumers /
   MT9, not MT2.
2. **No audio disruption.** Reserving a core for the audio mixer thread (worker =
   cores−1) protects against audio dropouts, which is itself an accessibility
   concern (audio cues for low-vision users). AX1 must additionally keep its
   occlusion smoothing reduce-motion-agnostic (occlusion is not motion; no
   flashing) — detailed in the AX1 doc, not here.

---

## 8. Testing strategy

- **Unit (GL-free, deterministic):** all of §5's verify bullets. Sync-mode tests
  run the exact same assertions with `numWorkers==0` to pin determinism.
- **Lifetime (ASan):** fire-and-forget flood, no leak / no UAF.
- **Race (TSAN):** the `job_system_stress` target, clean under `-fsanitize=thread`.
- **Integration:** headless engine boot + a trivial cross-frame `parallelFor`
  consumer; the full ctest suite stays green.
- **Perf (Release-only, NDEBUG-gated):** the §6 benchmark.

---

## 9. References

- `ROADMAP.md:648-676` — Phase 10.6 program; MT1 (`:653`) + MT2 (`:654`) surface
  spec; license-safe candidate list (`:650`).
- enkiTS — Doug Binks, zlib, v1.11 (2024, current tag). C++ API:
  `TaskScheduler`, `TaskSchedulerConfig::numTaskThreadsToCreate`,
  `ITaskSet::ExecuteRange` + `m_SetSize`, `IPinnedTask`, `AddTaskSetToPipe`,
  `WaitforTask`, `WaitforAllAndShutdown`, `ICompletable::GetIsComplete`,
  `SetDependency`/`enki::Dependency`. (Note: "completion actions" is a v1.11
  **C-interface** feature only — the C++ path has no such class; MT2 does not use
  it.) <https://github.com/dougbinks/enkiTS>
- Taskflow (runner-up) — MIT, 3.10 (C++17) / 4.0 (C++20).
  <https://github.com/taskflow/taskflow>
- Christian Gyrling, "Parallelizing the Naughty Dog Engine Using Fibers", GDC
  2015 (why fibers — and why we defer them).
- "Multithreading the Entire Destiny Engine", Bungie, GDC 2015 (job-graph model).
- Jolt Physics multi-threading docs (v5.3.0) — for the eventual MT8 pool share;
  and `NarrowPhaseQuery` concurrency semantics AX1 will re-verify.

---

## 10. Resolved decisions

- **D1 — Library = enkiTS v1.11.** C++17-safe (C++11), zlib, native mapping for
  every verb, ~3 KLOC, Doom Eternal-proven. Taskflow revisitable behind the
  wrapper if MT9 needs richer graphs. (§2)
- **D2 — `submitGraph` is DEFERRED to MT2.1 (reversed after cold-eyes).** The
  roadmap names it in MT2's surface and enkiTS's `SetDependency`/`enki::Dependency`
  is the mechanism — but AX1, the only near-term consumer, needs `parallelFor`,
  not a DAG. A correct graph wrapper (non-copyable `Dependency` objects that must
  outlive their tasks, lifetime of the graph-state relative to the returned
  handle, acyclic validation) is real complexity with **no test-driving consumer**
  — the speculative scaffolding Rule 2 forbids. It lands when MT9 (ECS scheduler)
  needs it. This doc records the deliberate deferral rather than ship untested
  graph code. (§3.1 note, §3.2, §5.)
- **D3 — `parallelFor` added beyond the roadmap surface.** It is the primitive
  AX1 needs and enkiTS's core strength. `submit(void())` is *conceptually*
  `parallelFor` over a size-1 range, but §3.3 gives each its own `ITaskSet` row
  (`submit` holds a `std::function<void()>`; `parallelFor` a
  `void(begin,end)`) — implementing `submit` by forwarding to `parallelFor` is
  **optional, not required**; the slices (S1 `submit`, S2 `parallelFor`) build
  them independently. Cheap, obviously useful, not speculative.
- **D4 — Synchronous mode (`numWorkers==0`) is WRAPPER-OWNED**, not delegated to
  enkiTS. The wrapper creates no scheduler and runs jobs inline, so it does not
  depend on any (undocumented) enkiTS 0-thread behaviour; auto mode clamps to
  `max(1, cores-1)` so enkiTS never receives 0. Buys deterministic tests + the
  MT1 DI seam + a 1-core fallback. High value, low cost.
- **D5 — `runOnMainThread` uses a plain mutex+vector**, not enkiTS pinned tasks.
  Simpler, obviously correct, and the queue must be drained at a *specific* frame
  point anyway (pinned tasks would run at `RunPinnedTasks` time, which is the same
  thing with more machinery). Pinned tasks considered, rejected for simplicity.
- **D6 — TSAN down-payment ships with MT2**, not deferred wholesale to MT4.
  Shipping a threading subsystem with zero race coverage is the exact failure MT4
  guards against; the stress test is cheap insurance. Full CI TSAN gate stays MT4.
- **D7 — Jolt pool NOT shared (MT8 untouched).** Out of scope; MT2 is CPU-generic
  and does not reach into physics threading.

---

## 11. Cold-eyes loop log

Cold each loop, no prior-findings briefing per global Rule 14.

- **Loop 1 (2026-07-01) — 1 CRITICAL, 3 HIGH, 4 MEDIUM, 3 LOW; all actioned.**
  - C1: "completion action" cited as an enkiTS API — it is not; enkiTS uses
    `OnDependenciesComplete`/`Dependency`. → §3.3 rewritten; fabricated term
    removed.
  - H1: worker-thread `m_inFlight` self-free = UAF/race. → redesigned to
    main-thread-only `m_inFlight` reclaimed by the `drainMainThreadQueue` sweep;
    `submit`/`parallelFor` made main-thread-only (§3.3, §4).
  - H2: `numWorkers==0` relied on unverified enkiTS 0-thread behaviour. → sync
    mode made wrapper-owned (no scheduler); auto clamps to `max(1,cores-1)`
    (Goal 2, §3.1/§3.3, D4).
  - H3: `submitGraph` speculative (no consumer). → deferred to MT2.1 (§3.1/§3.2,
    §5, D2), leaving `submit`/`parallelFor`/`wait`/`runOnMainThread` as v1.
  - M1 `:658`→`:650`; M2 `engine/systems/` path prefixes; M3 §3.4 seam wording;
    M4 dropped brittle test count; L1 "audio thread" = OpenAL mixer clarified;
    L2 assert-id capture pinned to `JobSystem` ctor; L3 `submit`/`parallelFor`
    adapter noted (D3). Loop-1 INFO confirmed the remaining citations correct.
- **Loop 2 (2026-07-01) — 0 CRITICAL, 0 HIGH, 3 MEDIUM, 2 LOW; all actioned.**
  Reviewer independently confirmed the lifetime model race-free, sync mode
  consistent, and that enkiTS *cannot* init with 0 threads (so wrapper-owned sync
  is required, not just clean). Fixes: M-1 enkiTS tag year 2022→2024 (×2); M-2
  `engine/core/` prefix on the `engine.cpp`/`engine.h` citations Loop-1 missed;
  M-3 "completion actions" is a v1.11 **C-interface** feature — dropped from the
  §9 C++ API list; L-1 (upgraded on verify) — `engine/physics/physics_world.cpp:104-111` already
  builds `JobSystemThreadPool` multi-threaded, so §0's "single-threaded" echo of
  the stale MT8 premise was corrected; L-2 D3 clarified as conceptual (not
  required call-forwarding). No design/logic defect found.
- **Loop 3 (2026-07-01) — 0 CRITICAL, 0 HIGH, 1 MEDIUM, 2 LOW; all actioned.**
  Reviewer re-verified the full enkiTS API, lifetime model, sync mode, and every
  citation as correct. M1: the S1 verify bullet said sync mode "runs inline at
  `wait`", contradicting the canonical run-at-submit model (and the
  fire-and-forget guarantee) — corrected to "at submit, `wait` is a no-op". L1:
  noted the clamp-then-`static_cast<uint32_t>` narrowing in the ctor row. L2:
  noted `range.end` is exclusive (half-open). No design/logic defect found.
- **Loop 4 (2026-07-01) — 0 CRITICAL, 0 HIGH, 0 MEDIUM, 1 LOW; converged.**
  Reviewer re-verified the full enkiTS v1.11 C++ API, the 0-thread illegality,
  the C-only completion-actions, the run-at-submit sync model (cross-checked all
  six mentions agree), the race/leak/UAF-free lifetime model, the `submitGraph`
  deferral, slice numbering, and every citation — all accurate. Sole finding L1:
  the `physics_world`/`contact_event` citations lacked the `engine/physics/`
  prefix (Loop-2's M-2 fixed only the `engine/core/` ones) — now prefixed.
  Verdict: implementation-ready.
- **Sign-off (2026-07-01, delegated per `[[feedback_spec_signoff_delegated]]`):**
  4 loops converged to zero design/logic findings (CRITICAL/HIGH/MEDIUM all 0
  by Loop 4; only cosmetic citation-prefix polish remained, now applied).
  Signed off — implementation may begin (MT2-S1).

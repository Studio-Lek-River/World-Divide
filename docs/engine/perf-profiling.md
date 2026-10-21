# Profiling and measurement

## What it is

Unreal has three layers of measurement. Stat commands give a quick on-screen look. Unreal Insights records a full timeline of CPU, GPU, frame, memory and custom events. The CSV profiler writes long per-frame captures that compare well between runs.
`ProfileGPU` breaks one frame down by GPU pass. Code adds its own named scopes with trace and stat macros, so they show up in all of these.
Profile before you optimise. Every claimed gain needs a before and an after capture of the same scene, build and machine.

## Key concepts

- **Frame threads.**
  - A frame runs on the game thread (gameplay, ticks, AI, UI), the render thread (scene traversal, visibility, draw submission) and the RHI thread (talking to the driver). Then the GPU executes it.
  - The slowest of these sets the frame time.
- **Reading `stat unit`.**
  - `Frame` is the total. `Game`, `Draw` (render thread), `RHIT` and `GPU` are the per-thread times.
  - The bound is whichever of `Game`, `Draw` or `GPU` sits closest to `Frame`.
  - Game bound (`Game` ≈ `Frame`): look at ticks, AI, movement, Blueprint and UI in Insights.
  - Render-thread bound (`Draw` ≈ `Frame`): too many primitives, draw calls or dynamic relevance. See `stat scenerendering` and `stat initviews`.
  - GPU bound (`GPU` ≈ `Frame`): use `ProfileGPU` or the GPU track in Insights. Lower `r.ScreenPercentage`; if the time falls, it is pixel bound.
  - A capped frame (`t.MaxFPS`, `r.VSync`) hides the bound. Uncap first.
- **Unreal Insights.**
  - The runtime streams trace events over channels into a `.utrace` file or to a live trace server.
  - `UnrealInsights.exe` (in `Engine/Binaries/Win64`) opens them: Timing view (threads, frames, regions, bookmarks), Memory Insights, Networking Insights and more.
- **Channels.**
  - Common ones: `cpu`, `gpu`, `frame`, `log`, `bookmark`, `region`, `screenshot`, `memtag`, `memalloc`, `callstack`, `module`, `loadtime`, `task`.
  - Preset `default` = `cpu,gpu,frame,log,bookmark,screenshot,region`.
  - Preset `memory` = `memtag,memalloc,callstack,module`.
  - More presets live under `[Trace.ChannelPresets]` in `BaseEngine.ini`: `Rendering`, `Loading`, `TaskGraph`, `ContextSwitching`.
- **Named events.**
  - By default Insights shows only scopes declared with trace macros.
  - `-statnamedevents` also emits every stat scope (`SCOPE_CYCLE_COUNTER` and engine stats) as a CPU event. More detail, more overhead.
- **Bookmarks and regions.**
  - A bookmark is a labelled point on the timeline; a region is a labelled span.
  - Mark scenario boundaries with them, so captures can be cut and compared.
- **Stat system.**
  - `stat <group>` shows a group's cycle counters on screen.
  - `stat startfile` / `stat stopfile` record them to a `.uestats` file, which Insights can also open.
  - Stats cost more than trace scopes. Prefer Insights for anything beyond a glance.
- **CSV profiler.**
  - One row per frame, one column per stat, written to `Saved/Profiling/CSV`.
  - Low overhead; meant for long runs, soak tests and automated comparison.
  - Compiled in every configuration except Shipping (`CSV_PROFILER`, unless `CSV_PROFILER_ENABLE_IN_SHIPPING`).
- **`ProfileGPU`.**
  - Captures one frame of GPU passes and prints the timing tree to the log (with a window in the editor).
  - Good for "which pass costs what". Use the Insights GPU track for trends over time.

## Rules and pitfalls

- **Measure standalone, not in PIE.**
  - PIE shares the process with the editor: editor ticks, Slate, the details panel and the editor viewport all cost time.
  - At minimum run `UnrealEditor.exe <project> <map> -game`. Best is a packaged Test build.
- **Know the build configuration.**
  - *Development*: all stats, trace, CSV and console commands. Has checks, so it runs slower than shipping.
  - *Test*: shipping-like optimisation, trace and CSV still available. The closest to the player's build that you can still measure.
  - *Shipping*: no stats, no CSV, most console commands stripped. Measure only frame times, externally.
  - The editor binary with `-game` is a Development build with editor modules loaded. Use it for relative comparisons, not absolute targets.
- **Compare like with like.**
  - Same map, camera, unit count, resolution, scalability level, build and machine.
  - Uncap the frame (`t.MaxFPS 0`, `r.VSync 0`) and turn dynamic resolution off.
- **Warm up before sampling.** Shader compilation, PSO creation, streaming and first spawns spike the first seconds. Skip them.
- **Read percentiles, not one number.** Use the average and the 95th percentile of frame or game-thread time over a fixed window. `stat fps` alone proves nothing.
- **Hitches hide in averages.** A 50 ms spike every few seconds needs the frame graph in Insights, `stat unitgraph`, or the max and percentile columns of a CSV.
- **Trace overhead is real.**
  - `cpu` with `-statnamedevents`, and `memalloc` with `callstack`, slow the game noticeably.
  - Never report timings from a run that also traced memory.
- **Keep scopes in hot code cheap.**
  - `TRACE_CPUPROFILER_EVENT_SCOPE` costs almost nothing while the channel is off.
  - Do not build a scope name string per call; use the static-name form.
- **Turn stat overlays off during a timing run.** Drawing them costs render and game time.
- **GPU time in `stat unit` arrives late** (read back frames later). Attribute GPU cost with Insights or `ProfileGPU`.
- **Close extra realtime editor viewports.** A second realtime viewport doubles render work.

## APIs, cvars and commands

| Name | Kind | Use |
|---|---|---|
| `-trace=<channels or presets>` | command line | Trace from launch, e.g. `-trace=cpu,gpu,frame,bookmark,region` or `-trace=default,memory`. |
| `-tracefile=<path>` | command line | Write to a file instead of the trace server. |
| `-tracehost=<ip>` | command line | Stream to a trace server on another machine. |
| `-statnamedevents` | command line | Emit stat scopes as CPU trace events too. |
| `Trace.File [Path] [Channels]` | console | Start tracing to a file at runtime. `Trace.Start` still exists but is deprecated in favour of `Trace.File`. |
| `Trace.Stop`, `Trace.Pause`, `Trace.Resume`, `Trace.Status` | console | Control a running trace. |
| `Trace.Enable` / `Trace.Disable <channels>` | console | Toggle channels on a running trace. |
| `Trace.Bookmark <name>` | console | Add a bookmark. |
| `Trace.RegionBegin` / `Trace.RegionEnd <name>` | console | Open and close a named region. |
| `Trace.SnapshotFile [Path]` | console | Write the in-memory trace buffer to a file. |
| `TRACE_CPUPROFILER_EVENT_SCOPE(Name)` | macro | Named CPU scope for Insights; `_STR` and `_TEXT` variants take a string literal or dynamic text. |
| `TRACE_BOOKMARK(Format, ...)` | macro | Bookmark from code. |
| `TRACE_BEGIN_REGION` / `TRACE_END_REGION` | macro | Region from code. |
| `DECLARE_STATS_GROUP`, `DECLARE_CYCLE_STAT` | macro | Declare a stat group and a cycle counter. |
| `SCOPE_CYCLE_COUNTER(STAT_X)` | macro | Time a scope into a declared stat; shows under `stat <group>`. |
| `QUICK_SCOPE_CYCLE_COUNTER(STAT_X)` | macro | Same without a separate declaration; lands in the Quick group. |
| `CSV_DEFINE_CATEGORY`, `CSV_SCOPED_TIMING_STAT`, `CSV_CUSTOM_STAT` | macro | Add columns to CSV captures. |
| `stat unit`, `stat unitgraph` | console | Per-thread frame times; graph over time. |
| `stat fps` | console | Frame rate and frame time. |
| `stat game` | console | Game-thread group (ticks, spawn, physics time). |
| `stat gpu` | console | GPU pass timings from the stat system. |
| `stat scenerendering` | console | Render-thread totals, draw calls, primitives. |
| `stat initviews` | console | Visibility and culling cost, primitives culled and drawn. |
| `stat memory` | console | Memory stat group. |
| `stat startfile` / `stat stopfile` | console | Record stats to a `.uestats` file. |
| `MemReport` | console | Dump a memory report under `Saved/Profiling`. |
| `ProfileGPU` | console | One-frame GPU breakdown; `r.ProfileGPU.Root` filters, `r.ProfileGPU.ShowUI` toggles the window. |
| `CsvProfile Start` / `Stop` / `Frames=N` | console | Start, stop, or capture N frames of CSV. |
| `-csvCaptureFrames=N` | command line | Capture N frames of CSV from startup. |
| `-csvGpuStats`, `r.GPUCsvStatsEnabled` | command line / cvar | Add GPU columns to CSV (off by default; has a cost). |
| `-csvExecCmds=` | command line | Run console commands at given CSV frames. |
| `t.MaxFPS`, `r.VSync` | cvar | Uncap the frame for measurement. |
| `-ExecCmds="..."` | command line | Run console commands at startup (stat views, cvars). |

## In World Divide

- Use the project harness, not ad-hoc PIE numbers. `Tools/Scripts/ue.ps1 -Task bench` plays L_Bench at 1/30/120/240 units, fixed camera, uncapped, dynamic resolution off, and writes a table, a CSV and `bench.json`.
  - `-Long` is the performance gate; `-Quick` is for iterating only.
  - `-Trace` records an Insights capture (`cpu,frame,region,gpu`) with one region per scenario; `-Task unitshots -Stress 240 -Trace` traces the stat views.
- The bench runs the Development editor binary with `-game`. Compare against stored baselines (`-SaveBaseline`, `-Compare`), never against absolute shipping targets. A run that was tracing is flagged in `bench.json`.
- Hot paths carry `TRACE_CPUPROFILER_EVENT_SCOPE` (units, movement, avoidance, weapons, running gear, dust, exhaust, lights). Give every new per-frame system one.
- Rules: [`../tech/PERFORMANCE_GUIDELINES.md`](../tech/PERFORMANCE_GUIDELINES.md); results: [`../tech/scale/PERFORMANCE_RESULTS.md`](../tech/scale/PERFORMANCE_RESULTS.md); method: [`../tech/scale/RTS_MOVEMENT_PERFORMANCE.md`](../tech/scale/RTS_MOVEMENT_PERFORMANCE.md). Project docs win on conflict.

## Sources

- Unreal Insights: <https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-insights-in-unreal-engine?application_version=5.8>
- Stat commands: <https://dev.epicgames.com/documentation/en-us/unreal-engine/stat-commands-in-unreal-engine?application_version=5.8>
- CSV profiler (only a 4.27 page was found; behaviour checked against the 5.8 source): <https://dev.epicgames.com/documentation/en-us/unreal-engine/csv-profiler?application_version=4.27>
- UE 5.8 engine source:
  - `Runtime/Core/Private/ProfilingDebugging/TraceAuxiliary.cpp`: trace commands, channel presets, `-tracehost`, `-tracefile`.
  - `Runtime/Core/Private/ProfilingDebugging/CsvProfiler.cpp`, `Public/ProfilingDebugging/CsvProfilerConfig.h`: CSV commands and build gating.
  - `Runtime/Core/Private/Stats/StatsCommand.cpp`, `StatsData.cpp`: `stat startfile`, `.uestats`.
  - `Runtime/Engine/Private/UnrealClient.cpp`: `stat unit` / `unitgraph`.
  - `Runtime/RHI/Private/GPUProfiler.cpp`: `r.GPUCsvStatsEnabled`.
  - `Config/BaseEngine.ini`: `[Trace.ChannelPresets]`.
- Project: `Tools/Scripts/ue.ps1` (bench, unitshots, trace arguments).

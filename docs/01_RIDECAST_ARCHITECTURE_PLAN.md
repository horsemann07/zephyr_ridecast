# 01 — RideCast Architecture & Portability Plan

> **Document type:** Product intent, architectural agreement, and future-development handoff  
> **Version:** 0.1 (initial architecture baseline)  
> **Date:** 2026-11-05  
> **Status:** Agreed direction / planning baseline — **not** a claim that any proposed hardware combination is working  
> **Audience:** Project owner, embedded/firmware developers, host-application developers, and future AI assistants

---

## 0. Read this first — the decision we reached

**RideCast is a reusable wireless communication service/adapter for a vehicle instrument cluster. The instrument-cluster host and the RideCast service are always logically separate, but they may run on either the same processor or on two different processors.**

The design must support all of these deployments **without rewriting the RideCast protocol or its portable business logic**:

1. **Combined STM32 prototype:** One **STM32H7B3** runs both the RideCast service and the host/display application; an external **TI CC335x** wireless companion handles Wi-Fi (and BLE if the selected variant/driver supports it). RideCast ↔ Host communication uses an **in-process transport** rather than an unnecessary physical UART loop.
2. **Combined TI platform:** A suitable future TI MCU/MPU runs both logical roles; the same product contract is used, with board/OS-specific drivers and internal transport.
3. **Separate RideCast adapter:** A dedicated MCU + TI CC335x make up a small RideCast PCB. The adapter connects over **UART initially** (potentially USB later) to an independent **STM32 cluster, TI MPU cluster, or laptop host**.

**Changing the display host should not force changes to the separate RideCast adapter firmware**, as long as the new host supports the established wire protocol and physical/electrical link. **Changing the MCU that runs RideCast itself will require a new platform/driver port and validation**, but should preserve its portable services and protocol.

This distinction — **logical roles versus physical chips** — is the central design rule.

## 1. What RideCast is, and what it is not

RideCast is a **phone-to-host communications gateway** that manages wireless connectivity, authorized sessions, incoming data, protocol validation, and reliable delivery of structured events to a host application.

**RideCast responsibilities:**
- Control Wi-Fi/BLE connectivity through supported radio drivers and a connection manager.
- Respond to host START, STOP, RETRY, RESTART, STATUS and related control operations.
- Handle pairing/session authorization and report connection state.
- Receive permitted phone-side data from a companion application; validate, bound, decode and route it.
- Forward structured navigation/location, notification/chat-alert, and media-status events to the host.
- Report failures, disconnections, reconnect attempts, protocol incompatibility and capabilities.
- Remain independent of the host's display library and of the particular MCU family where feasible.

**RideCast is not:**
- The instrument-cluster display engine, OpenStreetMap renderer, or GUI.
- A GPS receiver. **CC335x supplies wireless connectivity, not GNSS positioning.** Coordinates normally originate from the phone's location service or another explicit GNSS source.
- A means to automatically read all phone notifications or music data merely by establishing Wi-Fi/BLE. A companion mobile application and OS-granted access are required for supported features.
- An audio decoder, media player, or speaker output system in the current scope. **Music metadata** (song title, artist, playing/paused state) is not audio streaming.

**Host responsibilities:**
- Own system-level user interface, map tiles/map rendering, GPS marker and navigation visuals, notification popups, chat/message presentation, and music widgets.
- Decide when RideCast services are permitted to start, stop or retry; show pairing and status information.
- Consume the validated RideCast messages and implement the same Host API regardless of MCU, OS or display toolkit.
- Own audio playback if later required, with its own audio source/codec/output pipeline.

## 2. Hardware terminology and roles

| Term | Meaning | Examples |
|---|---|---|
| **TI CC335x** | Wi-Fi companion radio (selected variant may provide BLE). **Not a standalone RideCast application MCU.** | CC3351 as the candidate when BLE + Wi-Fi is required |
| **RideCast processor** | MCU/processor running the RideCast connection manager, networking host driver, protocol and routing | STM32H7B3 initially; a suitable smaller STM32 or TI processor later |
| **Host processor** | Cluster/controller that consumes events and renders information | STM32H7B3, TI MCU/MPU, laptop |
| **RideCast adapter PCB** | In split mode, the **RideCast processor + CC335x + required support circuits** | Future small board |
| **Combined mode** | Both logical roles share one MCU/processor | STM32H7B3 + CC335x |
| **Split mode** | RideCast and Host run on different processors, linked by a physical transport | MCU+CC335x adapter ↔ UART ↔ STM32/TI/laptop |

**Do not confuse:** a future **TI host MPU** is not the same thing as the **TI CC335x wireless chip**.

The **STM32G0 family is only an example candidate for a future low-cost RideCast MCU**. Whether a specific G0 model has enough RAM, flash, processing headroom, bus performance and driver support to host CC335x is **not yet established**. It must pass a feasibility review before selection.

## 3. Deployment configurations

### A. Combined STM32H7B3 + CC335x — first hardware prototype

```text
Phone / Phone Companion App
       |  Wi-Fi (BLE later if validated)
       v
   TI CC335x Radio
       |  Supported Wi-Fi host bus / BLE HCI interface
       v
+------------------------------------------------------------------+
| STM32H7B3 — initially Zephyr RTOS                                 |
|                                                                  |
|  [RideCast Service] -- [In-process link] -- [Host/Cluster Service] |
|   Wi-Fi, pairing,       same shared protocol      UI state, map,  |
|   message routing      and event semantics       alerts, media  |
|                                                    |             |
|                                               LVGL/display       |
+----------------------------------------------------|-------------+
                                                     v
                                                    LCD
```

- **One MCU, two independent software roles.**
- Host-to-RideCast requests must still use the common Host API and preserve the host-controlled lifecycle.
- Prefer an internal queue/callback/IPC adapter for message transport; **no physical UART is necessary between two modules on the same chip**.
- Keep protocol encoding/decoding usable and testable even if an optimized in-process implementation transfers typed messages directly.
- STM32H7B3's onboard Wi-Fi (where the specific development kit includes it) is **not the targeted radio for this experiment**: the test intends to integrate the **TI CC335x**.

### B. Combined future TI host

```text
Phone <--Wi-Fi/BLE--> CC335x <--supported bus--> TI MCU/MPU
                                               |
                                  +------------+-------------+
                                  | RideCast service          |
                                  | In-process/IPC link       |
                                  | Host application + UI     |
                                  +--------------------------+
```

- Reuse the **same logical API, protocol version, state machine, payload semantics and test cases**.
- A TI **Linux** MPU may require Linux Wi-Fi/BLE host drivers, POSIX threads and Unix-domain IPC; Zephyr-specific device/queue calls will **not** be copied unchanged.
- If the chosen TI platform uses Zephyr, implement a Zephyr-specific board/driver port instead.

### C. Separate adapter + external host — eventual hardware product

```text
Phone / Companion App
        |  Wi-Fi/BLE
        v
+------------------------------+
| RideCast ADAPTER PCB         |
|  TI CC335x                   |
|  +                           |
|  Dedicated RideCast MCU      |
|  Connection + protocol + I/O |
+---------------+--------------+
                |
         UART initially
       (USB considered later)
                |
+---------------+--------------+
| HOST / CLUSTER                |
|  STM32H7B3  or TI MCU/MPU    |
|  or Windows/Linux laptop     |
|  Host API + map/GUI + media   |
+------------------------------+
```

- The physical RideCast adapter should **not know which host brand it is attached to**.
- Host-specific UART/USB implementation lives on the **host side**. A normal laptop may use a USB-to-UART converter; native USB CDC ACM would require explicit adapter MCU/firmware support.
- **Application-level “master”/“slave” means the host controls RideCast services; it does not necessarily describe MCU peripheral modes or an electrical master/slave bus.**

## 4. Architectural invariants — rules we should not break

1. **Strict logical separation:** `RideCast Service` and `Host Application` are separate modules with a documented interface, even in a combined binary.
2. **Host-controlled lifecycle:** the host decides START / STOP / RETRY / RESTART. RideCast reports actual readiness, faults and state; it must not silently override a terminal host decision.
3. **One protocol contract:** stable message names, IDs, field encodings, bounds, and behavior across combined/split modes.
4. **Transport substitution:** internal queue / IPC / UART / future USB should be interchangeable **below** the common Host API. Neither app logic nor UI should directly use UART/SPI functions.
5. **Portable core, isolated ports:** radio host drivers, RTOS synchronization, GPIO/SPI/UART, timing, randomness and logging reside behind explicit platform boundaries.
6. **No display dependencies in RideCast:** no LVGL, map tile decoder or display resources within the gateway/service.
7. **No CC335x dependency in the Host UI:** the host consumes semantic events, not raw wireless driver callbacks.
8. **Asynchronous operation:** neither UI rendering nor networking blocks the other. Use queues/event dispatch and defined overflow/backpressure policies.
9. **One owner per state machine:** serialize RideCast manager mutations; avoid unsynchronized modifications from multiple threads.
10. **Capability/version negotiation:** future optional BLE, audio, USB, live location streaming, or protocol extensions must not break older hosts.
11. **Clear security/trust boundaries:** validate all inbound data; pairing codes, transport security and access control must be specified separately.
12. **Hardware changes require validation, not application redesign:** porting is not guaranteed to be zero-effort, but the portable logic and protocol should survive.

## 5. Common software structure (proposed)

```text
ridecast/
├── docs/
│   └── 01_RIDECAST_ARCHITECTURE_PLAN.md    # This document
├── common/
│   ├── protocol/          # Framing, version, types, CRC, validation
│   ├── contracts/         # Host API; event and command definitions
│   └── utilities/         # Portable helpers, bounded buffers
├── service/
│   ├── connection/        # Lifecycle, pairing, auth, retry policies
│   ├── phone/             # Phone data ingestion, validation
 
├── host/ 
│   ├── client/            # API/client-side command/event handling
│   ├── navigation/        # Host-side map / position data model
│   ├── notifications/     # Host-side popups
│   └── media/             # Host-side media information
├── transports/
│   ├── inprocess/         # For combined deployment
│   ├── uart/              # For split deployment
│   └── usb/               # Optional future
├── ports/
│   ├── stm32h7_zephyr/    # Board, OS and radio host driver adaptation
│   ├── smaller_mcu/       # Candidate; TBD after feasibility review
│   ├── ti_zephyr/         # If relevant
│   ├── ti_linux/          # If relevant
│   └── pc_host/           # PC host simulator
├── targets/
│   ├── stm32h7_combined/
│   ├── ti_combined/
│   ├── ridecast_adapter/
│   └── laptop_host/
└── tests/
    ├── protocol/
    ├── contract/
    ├── transport/
    └── integration/
```

This tree defines **ownership and dependency direction**, not an instruction to immediately rename/rewrite the existing repository. Assess current files before refactoring.

### Example of the stable boundary

```text
Host Application
   |
   |  host_start(), host_stop(), host_get_status()
   |  on_navigation(), on_notification(), on_media()
   v
Host API / Contract
   |
   +--> In-process Queue / Callback --+   combined build
   |
   +--> Framed UART / USB -----------+   separate build
                                        |
                                        v
                                  RideCast Service
                                        |
                                  CC335x Host Driver
```

The exact C API and callback semantics remain to be designed. **Do not introduce two different incompatible application protocols just because there are two transport types.**

### Existing-code preservation

The preexisting RideCast project already describes a **portable C application/BSP**, an **ESP32 + ESP-IDF 5.5 Phase 1** build, and portable protocol/manager components. It also describes a host-controlled framed-protocol prototype. Those are **prior work to audit and reuse**, not proof that the new Zephyr/CC335x target is implemented. Do not delete or rewrite the original implementation merely to adopt this plan.

Earlier source discussions mentioned C11, a reusable BSP and CMSIS-RTOS2 preferences. For a new Zephyr target, decide whether to use a small OS abstraction or native Zephyr threads/queues inside the platform layer. **Keep the business logic independent of both ESP-IDF and Zephyr-specific APIs** where feasible.

## 6. Protocol direction and existing baseline

### Existing documented prototype (not final production standard)

A previous RideCast protocol design specifies framed messages with:

- Magic `0x52 0x43` (`RC`), protocol version, message type, payload length, sequence, CRC-16/CCITT-FALSE.
- A **384-byte maximum payload** in that prototype.
- Host service commands such as `START`, `STOP`, `RETRY`, `RESTART`, `STATUS`.
- Device events such as `READY`, `PAIR_CODE`, `CONNECTED`, `DISCONNECTED`, `RETRYING`, `FAILED`, `STOPPED`, `STATE`.
- Authorized phone-content events: **notification `0x40`**, **music metadata `0x41`**, **navigation guidance `0x42`**.
- A six-digit *application pairing* code in the prototype, separate from a Wi-Fi passphrase.

**Important:** This is a **documented prototype contract**. Compare it with the actual checked-in source and tests before implementation; earlier Phase 1 notes also describe an initial **raw Wi-Fi TCP → UART bridge** without framing. These are **different development stages**, not evidence that every feature is currently built. Preserve backward compatibility or provide an explicit migration/version plan.

### Additional contract work needed for our new flexible architecture

- Explicit `HELLO` / protocol-version negotiation and device capabilities (message IDs TBD; do not overwrite old IDs casually).
- Distinguish **radio initialized**, **Wi-Fi network ready**, **phone transport connected**, and **RideCast session authenticated**.
- Commands with bounded completion and actual results, plus status queries.
- Error and recovery semantics, including what happens if UART link or internal queue is unavailable.
- Max sizes, flow control, message ordering, timeouts, duplicate/replay handling, fragmentation if ever required.
- Portable serialization with no reliance on compiler struct packing, endianness, or MCU pointer widths.
- Unit tests that replay exactly the same protocol cases against both in-process and physical-UART transports.
- **A separate continuous-location message/stream may be necessary.** Existing `0x42` navigation guidance includes lat/lon in its payload, but that alone is not a complete live GPS tracking protocol. The new location/update cadence, accuracy, timestamps, route state and loss-of-signal behavior need a versioned design.
- Define permissions, redaction and length limits for chat/notification content.

## 7. End-to-end feature ownership
### 7.1 RideCast Dual-Wireless Data Routing Policy

RideCast uses Wi-Fi and Bluetooth Low Energy (BLE) as
two logical wireless communication channels.

Preferred channel allocation:

1. Wi-Fi:
   - Live GPS location updates
   - Navigation guidance
   - Route geometry
   - Optional map data / map tile transfer
   - Future bandwidth-intensive features

2. BLE:
   - Phone notifications
   - Messaging application notifications
   - Incoming call information
   - Music metadata and playback status
   - Initial phone discovery and secure enrollment

The phone companion application provides authorized
phone data through these channels.

RideCast receives, authenticates, validates and routes
data to the Host Application.

RideCast does not render maps or user-interface content.

The Host Application renders the map, navigation guidance,
notifications and media information.

Wi-Fi and BLE shall maintain independently observable
connection states.

Both channels must be bound to the same authenticated,
authorized active phone session before personal data
can be forwarded.

The product initially prefers Wi-Fi for navigation and
BLE for notification/media metadata.

Automatic fallback between Wi-Fi and BLE is not yet
approved and must be defined separately.

Actual simultaneous Wi-Fi/BLE operation, performance,
power consumption and driver support must be validated
on the selected CC3351 hardware and host platform.

### 7.2 
| User-visible feature | Phone or data source | RideCast service | Host application |
|---|---|---|---|
| Live location | Phone GPS or separate GNSS source | Securely receives and forwards position updates | Updates location marker and map viewport |
| Turn-by-turn guidance | Companion app / authorized routing source | Validates/forwards guidance | Renders maneuver, distance, ETA, street |
| OpenStreetMap display | Tile source / licensed offline data, on host side | **No map rendering** | Stores/loads tiles and draws map with its graphics engine |
| Notifications | Phone app with granted permissions | Routes permitted and bounded message fields | Shows popup with privacy controls |
| Chat alert | Phone notification integration where allowed | Routes permitted metadata/content | Displays message notification |
| Music metadata | Phone media session / companion app | Routes title, artist, play state | Displays media widget |
| Music **audio** playback | Separate audio source/protocol (future) | **Not included in current gateway scope** | Audio decode and output if later specified |
| Wireless pairing | Phone companion app | Radio control, session authorization, state | Displays code and status; controls lifecycle |

**Map clarification:** In combined STM32 mode, the **same physical STM32** renders the map because it is running the **Host Application** as well as RideCast. This does **not** mean the RideCast service gains a mapping responsibility.

**Mobile integration clarification:** CC335x alone cannot obtain another app's map, notifications, chats or media status. Android requires explicit permissions and a companion app for supported data. iOS restrictions may differ substantially and require separate feasibility investigation.

**Map-data clarification:** OpenStreetMap data can support a custom map viewer, but public tile servers are not an unrestricted bulk/offline-download service. Select an appropriate data/tile license and hosting or offline-package strategy.

## 8. Connectivity, OS and hardware assumptions

**Initial intended hardware trial:** `STM32H7B3I-DK or confirmed STM32H7B3 board` + `TI CC3351/CC335x evaluation hardware`; run Zephyr on STM32 and use LVGL to demonstrate host UI.

**Items NOT yet verified:**
- Exact STM32 board revision and target part; actual TFT/pin allocation, flash, RAM and external SDRAM setup.
- Exact CC335x variant (CC3350 versus CC3351), module/evaluation-board revision, and required host interface for Wi-Fi/BLE.
- CC335x **Zephyr driver availability and compatibility with the selected STM32 board/Zephyr version**; an experimental/different-board example is not a validated STM32 port.
- Correct SPI/SDIO, UART/HCI, IRQ, RESET, enable, clocks, power sequencing, IO voltages and level shifting. **Never assume direct 3.3 V GPIO wiring is safe from the chip name.**
- Zephyr Wi-Fi/BLE coexistence and the availability of phone-app features on the selected radio/host combination.
- The performance and memory budget for network traffic plus LVGL/map drawing on a single MCU.
- Whether a selected STM32G0 model can practically host CC335x at all.
- Future TI MPU OS, networking driver and graphics/runtime stack.

**RTOS choice:** Zephyr is the **current learning and proposed prototype platform**, not a requirement that every future MCU/MPU use Zephyr. Cross-platform contracts must allow Zephyr, ESP-IDF/FreeRTOS, other RTOSes and Linux implementations.

## 9. Proposed development path — incremental and testable

### Phase 0 — Contract and hardware feasibility (next action)

**Deliverables:**
- Inventory the existing RideCast repository, its working branches, existing frame parser/manager, UART/BSP, tests and Phase 1 scope.
- Write down **Host API v1** and use one set of command/event definitions in all builds.
- Select and verify exact STM32H7B3 and CC335x evaluation hardware; map required electrical connections and prove driver feasibility or document a fallback.
- Make a **platform-independent simulator** that sends fake phone events to a fake host via the shared API.

**Gate:** One protocol conformance test suite passes with synthetic messages, without an attached radio or display.

### Phase 1 — Combined STM32H7B3, no wireless dependency yet

**Deliverables:**
- Bring up Zephyr display/LVGL and a minimal host dashboard.
- Run `RideCast Service` and `Host App` as two logical modules on one STM32H7B3.
- Inject simulated location, notification, chat-alert and music-metadata events through the in-process transport.
- Verify host control commands and RideCast state changes using the same API.

**Gate:** Map marker and popups update from RideCast events; simulated START/STOP/FAILED states behave as documented.

### Phase 2 — Physical link and laptop simulator

**Deliverables:**
- Add the UART-framed transport adapter and a PC serial host simulator.
- Test the *same* RideCast event contract over UART, including partial frames, CRC failures, malformed lengths, disconnects and queue overflow.
- Show fake coordinates/notification/media updates on a laptop UI.

**Gate:** The host UI works both with in-process and serial transports without changes to its business/event handling.

### Phase 3 — CC335x wireless bring-up

**Deliverables:**
- Validate CC335x driver and wiring on STM32H7B3; establish Wi-Fi connectivity and robust connection handling.
- Connect a phone/PC test client and send actual protocol frames.
- Add BLE only after the chosen variant and Zephyr host integration are validated.

**Gate:** Wireless-to-RideCast-to-Host messages are reliable and observable; service lifecycle survives reconnect and radio errors.

### Phase 4 — Phone feature integrations

**Deliverables:**
- Android-first companion app sending allowed GPS, navigation instructions, notification content and media status.
- Host map/tile display, live marker, notification popups and music widgets.
- Extend protocol in a versioned, backward-compatible way if continuous GPS or other payloads need new message types.

**Gate:** A real permissioned phone sends data that appears correctly on STM32 and on the laptop test host.

### Phase 5 — Portability demonstration and dedicated adapter

**Deliverables:**
- Select an MCU that **actually meets CC335x host-driver and resource requirements** (STM32G0 only if proven suitable).
- Build an adapter-only RideCast firmware: service + wireless + UART/USB, no LVGL/map.
- Connect it unchanged to an STM32, a laptop and (later) a TI host.
- For a combined TI host, replace board/OS-specific ports while preserving common logic and Host API.

**Gate:** At least two distinct hosts consume the same physical RideCast adapter with no adapter application/protocol changes; target-specific bugs may require validated fixes.

### Future / productization gates

Electrical design, EMC, automotive safety/rider-distraction considerations, authenticated encryption, secure update, credential provisioning, privacy, recovery, watchdog, power-loss robustness, long-duration soak tests, and target product requirements are **not declared complete by a demo**.

## 10. Key acceptance tests for portability

1. Same `START`, `STOP`, `RETRY`, `STATUS` behavior in combined and split deployments.
2. Same serialized events parsed by STM32, TI (when available) and laptop host implementations.
3. Protocol conformance tests run on host PC, independent of CC335x or LVGL.
4. Same synthetic navigation/location/notification/media fixture renders on different host UIs.
5. In-process and UART transports preserve ordering, bounds and error semantics according to contract.
6. A deliberately malformed frame is rejected without deadlock, memory corruption or an invalid UI update.
7. Wi-Fi association is not mislabeled as an authenticated RideCast session.
8. Radio-service failures and exhausted reconnection policies return control to the host.
9. RideCast core builds without display or graphics dependencies; Host App builds without CC335x driver headers.
10. Board changes isolate edits to **platform ports, configuration, wiring, and host UI**, except for explicitly versioned capability changes.

## 11. Decisions made vs. items still open

### Architecture decisions (baseline)

- RideCast is a **wireless communications adapter/service**, not the map/UI renderer.
- Logical RideCast and logical Host remain separate in all deployments.
- **Combined single-processor** and **physically separate adapter** are both supported modes.
- Host controls connection lifecycle; RideCast reports actual state and routes authorized content.
- One common, versioned Host API and framing/semantics serve all deployments.
- Start with STM32H7B3 + CC335x as a combined proof of architecture; later port to suitable TI and smaller-MCU targets.
- UART first for split mode; laptop host is a first-class test target.
- Zephyr is a strong initial platform option, while common logic stays RTOS-neutral.
- Existing RideCast source and documentation should be **audited and reused**, not casually replaced.

### Open engineering decisions — do not assume solved

- The exact CC335x part and evaluation kit; proof that Wi-Fi and BLE host drivers work on the selected platform.
- Whether STM32G0 is viable or a more capable adapter MCU is required.
- Whether combined mode uses exactly the serialized wire-format bytes internally or typed messages with shared schema/semantic tests; both options must preserve conformance.
- Final Host API signatures, protocol version negotiation, transport callback contracts and location-stream extension.
- How the phone gets/map-provides navigation/location; update rate, accuracy and offline/online map policy.
- Whether chat content is supported beyond permissioned notifications; privacy behavior.
- Whether actual music audio is a future feature or only music metadata.
- Chosen TI host model, OS, graphics stack and physical transport.
- Production cybersecurity, vehicle integration and nonfunctional targets.

## 12. Handoff instructions for the next conversation or developer

**When this file is shared again:**

1. **Treat Section 0 and Section 4 as the architectural baseline.** Do not propose permanently tying RideCast to STM32 graphics or requiring two MCUs in combined mode.
2. Identify the requested **deployment**: STM32 combined, TI combined, separate adapter + STM32/TI, or adapter + laptop.
3. Before editing code, inspect the **actual repository**, exact board revisions, source files, Zephyr/ESP-IDF versions and any current protocol tests. This document is **a plan, not a record of compiled firmware**.
4. Preserve host-controlled START/STOP/RETRY semantics and separation of RideCast service from map/UI.
5. Reuse the existing portable protocol/BSP where possible. If old raw bridge and framed-message phases disagree, record the migration and protect backward compatibility deliberately.
6. Prefer **small, compilable milestones with test evidence** over one large, unverified port.
7. Flag unresolved hardware/driver questions openly rather than claiming Wi-Fi/BLE support on an untested STM32+CC335x combination.
8. When asked to produce code, clearly state which **phase**, which **target**, which APIs exist and what is actually validated.

### One-sentence project summary

> **RideCast is a portable, host-controlled Wi-Fi/BLE gateway that forwards authorized phone data to a cluster; its service and host UI are logically distinct but may run on the same MCU or separate devices, using one shared protocol so STM32, TI and PC deployments can evolve without rewriting the product logic.**

---

### Provenance and status of this document

This document consolidates the **current architectural conversation** and the previously authored RideCast `README` / `RIDECAST_CLUSTER_MASTER_FLOW` design notes. It deliberately distinguishes **agreed product intent**, **proposed engineering organization**, and **unverified hardware/software feasibility**. No CC335x-on-STM32 integration, BLE link, live map, phone app or production security claim is certified by this document.

**Suggested next work item:** Phase 0 — audit the current RideCast repository and create the **Host API + protocol compatibility matrix** for combined/in-process and split/UART deployments.

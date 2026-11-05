from copy import deepcopy
from pathlib import Path

from docx import Document
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Inches, Pt

SOURCE = Path(r"C:\Users\Admin\Downloads\Tatkal_Core_Project_Synopsis (3) (1).docx")
OUTPUT = Path(r"C:\Users\Admin\Downloads\Tatkal_Core_Project_Synopsis_Final.docx")


def remove_numbering(paragraph):
    ppr = paragraph._p.get_or_add_pPr()
    numpr = ppr.find(qn("w:numPr"))
    if numpr is not None:
        ppr.remove(numpr)


def replace_paragraph(paragraph, text, numbered=False):
    for run in paragraph.runs:
        run._element.getparent().remove(run._element)
    run = paragraph.add_run(text)
    run.font.name = "Times New Roman"
    run._element.rPr.rFonts.set(qn("w:ascii"), "Times New Roman")
    run._element.rPr.rFonts.set(qn("w:hAnsi"), "Times New Roman")
    if numbered:
        remove_numbering(paragraph)
        paragraph.paragraph_format.left_indent = Inches(0.25)
        paragraph.paragraph_format.first_line_indent = Inches(-0.2)


def replace_cell(cell, text):
    paragraph = cell.paragraphs[0]
    replace_paragraph(paragraph, text)
    for extra in cell.paragraphs[1:]:
        extra._element.getparent().remove(extra._element)


doc = Document(SOURCE)

# Correct character corruption and make the proposal's implementation boundary explicit.
updates = {
    19: "The implementation will use Linux and POSIX interfaces where appropriate, including processes, POSIX threads, shared memory, mutexes, semaphores, and non-blocking I/O. The baseline ingress will support a local POSIX shared-memory client/server path. Linux-specific TCP and epoll ingress are optional extensions isolated behind the ingress module.",
    20: "Some subsystems - especially virtual memory and disk-arm scheduling - will be implemented as explicit simulations. This distinction is important: the project will demonstrate operating-system algorithms in user space, not replace or modify the Linux kernel. The goal is to produce a reliable, observable, and testable laboratory system rather than claim production-scale railway infrastructure.",
    30: "An ingress component accepts and validates requests from the local client. An optional networked version may use non-blocking sockets and epoll without allocating one thread per connection.",
    43: "The primary implementation language is C. The target environment is Linux with a POSIX-compatible C compiler and development tools. A typical build uses GCC or Clang, pthread support, shared-memory and semaphore APIs, standard debugging utilities, and a Makefile. Linux-specific epoll support is optional and remains isolated behind the ingress module.",
    52: "Implement a user-space three-queue scheduler: high, medium, and general. FCFS service is used within each queue; a configurable aging threshold promotes an overdue task immediately so sustained high-priority arrivals cannot starve lower-priority work.",
    54: "Implement Banker's algorithm as a separate simulation with a documented fixed resource model, maximum-demand matrix, allocation matrix, need matrix, and available-resource vector.",
    57: "Implement C-SCAN over the virtual cylinder range 0-199, moving initially in the increasing direction. Report service order, total head movement including the wrap from 199 to 0, and a documented simulated waiting-time estimate.",
    58: "Persist confirmed reservation events through a WAL with record length, integrity information, and a commit rule: allocation is confirmed only after append, flush, and fsync succeed. Test recovery from complete, corrupt, and interrupted records.",
    65: "A demonstrable Readers-Writers solution for concurrent availability-chart queries and exclusive booking updates.",
    72: "A 10,000-request workload is a stress-test target, not a guaranteed result. The final report will state the hardware, worker count, arrival pattern, run duration, completion ratio, and measurements.",
    74: "The Banker's algorithm module will use a documented fixed resource model, such as inventory-lock tokens, transaction slots, and WAL-buffer slots, so its safety checks are mathematically meaningful and distinct from berth allocation.",
    86: "A supervisor process will initialise IPC resources, launch and reap a limited number of child processes where process separation is demonstrated, monitor their status, and perform orderly shutdown. The design will avoid creating one process for every booking request. Failures and cleanup outcomes will be logged.",
    87: "POSIX shared memory, created with shm_open() and mapped with mmap(), will hold a bounded request ring buffer shared by the local client and server. Queue operations will use process-shared semaphores for mutex, free-slot, and item-count coordination. Ownership, cleanup, and abnormal-termination recovery will be documented.",
    90: "CPU Scheduling Policy",
    91: "Tatkal-Core will implement a user-space scheduler rather than attempt to replace Linux's kernel scheduler. It will use high, medium, and general queues. Requests are FCFS within a queue; priority decides the normal queue order; and a configurable aging threshold promotes any overdue head request for the next dispatch opportunity.",
    92: "The policy will use higher numeric values for higher priority. The default aging threshold is 200 ms and will be reported with every benchmark. Promotion is immediate at dispatch time, which guarantees a service opportunity for aged work even while high-priority work continues to arrive. Booking tasks are non-preemptive once a worker begins allocation.",
    94: "Each booking request will contain a unique request ID, train identifier, requested berth count, priority, optional berth preference, arrival timestamp, and processing status. Berths will have stable identifiers and states such as AVAILABLE, RESERVED, and temporarily HELD. Request IDs and berth state will be checked under the relevant synchronization mechanism to prevent duplicate confirmations.",
    98: "Multiple requests can be processed by different workers, but shared inventory updates must remain atomic. Read-only availability queries may run concurrently, while berth allocation requires exclusive access to the relevant inventory state. The implementation will use POSIX Readers-Writers locks for availability-chart queries and booking updates. Multi-berth allocation uses increasing berth-ID lock order; an allocation that cannot acquire all required berths is rolled back.",
    100: "The Banker's algorithm component will evaluate a candidate simulated resource request by temporarily applying it and running a safety check. The test model will document resource types, total units, maximum demand, current allocation, remaining need, and available units. An unsafe request will remain pending or be rejected according to the selected demonstration scenario.",
    101: "A separate Wait-For Graph component will represent waiting relationships between simulated transactions. Depth-first search will detect cycles. For the controlled recovery demonstration, the victim-selection rule will choose the highest-numbered transaction in the detected cycle, release its simulated locks, roll back tentative changes, and mark it for retry. Avoidance checks allocations before granting them; detection identifies cycles that already exist.",
    103: "The memory simulator will model a configurable set of 4 KB frames. A workload item may reference one or more simulated pages. When a referenced page is not resident, the simulator records a page fault. If no free frame exists, Clock (Second-Chance) examines reference bits in circular order, clears bits for recently used pages, and replaces the first frame whose bit is zero. The trace will report hits, faults, replacements, and final frame contents. LRU is an optional comparison algorithm.",
    105: "Storage Scheduling and WAL",
    106: "The storage module will model requests to cylinders 0-199. C-SCAN services requests in increasing-cylinder order, moves to cylinder 199, wraps to cylinder 0 without servicing requests on the return journey, and continues. The trace will report service order, total virtual head movement including the wrap, and a documented simulated waiting-time estimate. FCFS may be used as a comparison baseline.",
    107: "Separately, a WAL file will store confirmed booking events in a structured record format. Records will include a length or version field, request ID, berth identifier, outcome, and integrity information. A confirmation is returned only after WAL append, flush, and fsync succeed. Recovery will reject malformed or incomplete records, handle duplicate request IDs consistently, and rebuild confirmed booking state from valid records.",
    109: "The command-line interface will expose commands such as queue-stats, show-bookings, memory-stats, deadlock-test, arm-trace, workload-run, WAL recovery, and shutdown. Each command will report measured state or a clearly labelled deterministic simulation trace rather than hard-coded operational values.",
    153: "Priority aging: higher numeric values indicate higher priority. A waiting task is promoted when it has waited at least the configurable aging threshold; the default is 200 ms. Promotion is evaluated at dispatch time and grants the aged task the next service opportunity.",
    154: "Banker's algorithm: for each candidate request, the simulator temporarily allocates the requested resources and runs a safety check using documented maximum, allocation, need, and available vectors. The request is granted only if a safe sequence exists; otherwise it remains pending or is rejected according to the selected policy.",
    155: "Clock replacement: a circular pointer examines frame reference bits. A referenced frame receives a second chance by having its bit cleared; the first eligible unreferenced frame is selected for replacement. The trace reports hits, faults, replacements, and final frame contents.",
    156: "C-SCAN: requests are sorted by cylinder position in the increasing sweep direction. The virtual head services requests to cylinder 199, wraps to cylinder 0 without servicing the return journey, and continues. Metrics include the complete head path, including the 199-to-0 wrap movement.",
    162: "The project is expected to produce a working C-based reservation prototype and independently testable operating-system demonstrations. Outcomes will be reported only after execution; the synopsis does not assume that a specific throughput or latency has already been achieved.",
    176: "The workload will be increased in stages: a small correctness workload, then 100 and 1,000 requests, followed by a 10,000-request stress test if implementation and available machine resources permit it. The generator will distinguish submitted requests from simultaneously active requests. The final report will state hardware, worker count, arrival pattern, run duration, completion ratio, and whether results were repeated.",
    193: "The proposal distinguishes Linux/POSIX APIs from ISO C, separates real operating-system behaviour from user-space simulations, and defines concrete rules for priority aging, resource allocation, page replacement, disk scheduling, WAL commit, and recovery. A 10,000-request run is a benchmark target rather than a claim of production readiness.",
}
for index, text in updates.items():
    replace_paragraph(doc.paragraphs[index], text)

# The original cover split the final two faculty names onto a second page.
# Compact only that title-block area, preserving the original cover design.
for index in (9, 10, 11, 12):
    paragraph = doc.paragraphs[index]
    paragraph.paragraph_format.space_before = Pt(2)
    paragraph.paragraph_format.space_after = Pt(2)
    for run in paragraph.runs:
        run.font.size = Pt(13 if index == 9 else 10)

# Two source section breaks created mostly blank continuation pages. Convert
# those exact breaks to continuous flow so the next planned section fills space.
for index in (75, 151):
    ppr = doc.paragraphs[index]._p.get_or_add_pPr()
    sect = ppr.find(qn("w:sectPr"))
    if sect is not None:
        section_type = sect.find(qn("w:type"))
        if section_type is None:
            section_type = OxmlElement("w:type")
            sect.insert(0, section_type)
        section_type.set(qn("w:val"), "continuous")

def plain_heading(index, text, size):
    paragraph = doc.paragraphs[index]
    paragraph.style = doc.styles["Normal"]
    remove_numbering(paragraph)
    replace_paragraph(paragraph, text)
    paragraph.paragraph_format.space_before = Pt(10)
    paragraph.paragraph_format.space_after = Pt(5)
    paragraph.paragraph_format.keep_with_next = True
    run = paragraph.runs[0]
    run.bold = True
    run.font.size = Pt(size)
    color = run._element.get_or_add_rPr().find(qn("w:color"))
    if color is None:
        color = OxmlElement("w:color")
        run._element.get_or_add_rPr().append(color)
    color.set(qn("w:val"), "1F3656")

def remove_page_break_before(paragraph):
    ppr = paragraph._p.get_or_add_pPr()
    page_break = ppr.find(qn("w:pageBreakBefore"))
    if page_break is not None:
        ppr.remove(page_break)

# These headings inherit page-before behaviour from the template. Make their
# formatting explicit so they follow preceding content instead of creating
# sparse continuation pages.
plain_heading(76, "6. DESIGN OF THE PROJECT", 16)
plain_heading(84, "6. DESIGN OF THE PROJECT (CONTINUED)", 15)
plain_heading(152, "8.1 SCHEDULING AND ALGORITHM DEFINITIONS", 13)
plain_heading(42, "3.3 Target Platform and Tools", 12)

# The template's multilevel list lost its parents when the two design headings
# were made explicit. Render the affected headings as stable, literal labels.
for index, text, size in [
    (77, "6.1 System Architecture", 12),
    (81, "6.2 Request Flow", 12),
    (85, "6.3 Process Management and Inter-Process Communication", 12),
    (88, "6.4 Multithreading and Worker Pool", 12),
    (90, "6.5 CPU Scheduling Policy", 12),
    (93, "6.6 Data Model", 12),
    (96, "7. FEATURES OF TATKAL-CORE", 16),
    (97, "7.1 Concurrent Booking and Inventory Protection", 12),
    (99, "7.2 Deadlock Avoidance, Detection, and Recovery", 12),
    (102, "7.3 Simulated Virtual Memory", 12),
    (105, "7.4 Storage Scheduling and WAL", 12),
    (108, "7.5 Monitoring CLI", 12),
    (111, "8. METHODOLOGY", 16),
    (157, "9. IMPLEMENTATION PLAN AND TESTING STRATEGY", 15),
    (158, "9.1 Suggested Module Structure", 10),
    (159, "9.2 Test Cases", 10),
    (161, "10. EXPECTED OUTCOMES", 16),
    (163, "10.1 Functional Outcomes", 12),
    (174, "10.2 Performance and Correctness Metrics", 12),
    (175, "10.3 Load-Test Plan", 12),
    (179, "11. FUTURE SCOPE", 16),
    (191, "12. CONCLUSION", 16),
    (196, "13. REFERENCES", 16),
]:
    plain_heading(index, text, size)

# Let the algorithm definitions follow phase 7 so neither page is a sparse
# continuation page.
remove_page_break_before(doc.paragraphs[152])
remove_page_break_before(doc.paragraphs[84])

# Correct list numbering that incorrectly starts at 8 and 20 in the source.
objectives = [
    "Design a modular reservation engine for trains, berths, requests, confirmations, and rejections.",
    "Demonstrate process creation, supervision, and correct fork lifecycle handling.",
    "Demonstrate POSIX shared-memory IPC and synchronization around bounded queues.",
    "Use a fixed-size POSIX thread pool without unbounded thread creation.",
    "Implement the documented three-queue FCFS scheduler with priority aging.",
    "Use mutexes, semaphores, and Readers-Writers protection for shared booking data.",
    "Implement Banker's algorithm as a fixed resource-allocation simulation.",
    "Construct a Wait-For Graph cycle and demonstrate victim rollback and retry.",
    "Simulate 4 KB frames with Clock replacement and optional LRU comparison.",
    "Implement C-SCAN over cylinders 0-199 and report movement and waiting time.",
    "Persist confirmed events through a WAL and test interrupted-write recovery.",
    "Measure throughput, response time, queue depth, failures, page faults, and algorithm metrics.",
]
for number, text in enumerate(objectives, start=1):
    replace_paragraph(doc.paragraphs[47 + number], f"{number}. {text}", numbered=True)

future_scope = [
    "Performance comparison dashboard: add a lightweight visual dashboard or CSV export for throughput, latency percentiles, queue depth, page faults, and disk-scheduling metrics.",
    "Alternative scheduling policies: compare FCFS, Round Robin, static priority, and aging-based priority under identical workload traces.",
    "Additional memory algorithms: compare Clock with LRU and FIFO using the same page-reference strings.",
    "Alternative disk schedulers: compare C-SCAN with FCFS, SCAN, and LOOK using identical cylinder requests.",
    "Fault injection: simulate worker crashes, queue saturation, partial WAL records, and delayed I/O to study recovery behaviour.",
    "More robust transaction handling: introduce explicit transaction states, idempotency keys, WAL replay, and carefully tested rollback rules.",
    "Networked clients: add a framed TCP client protocol with timeouts and input validation while retaining the local shared-memory mode.",
    "Improved observability: add structured logs, per-module counters, timestamps, and repeatable benchmark scripts.",
    "Portability: isolate Linux-specific facilities such as epoll behind an interface and provide a poll-based or local-input alternative where appropriate.",
    "Security and reliability review: validate input lengths, check allocation and system-call return values, use safe file permissions, and avoid storing real passenger information.",
]
for number, text in enumerate(future_scope, start=1):
    replace_paragraph(doc.paragraphs[180 + number], f"{number}. {text}", numbered=True)

# Align the proposed module map with the project architecture.
module_rows = [
    ("main.c", "Program entry point, configuration, CLI routing, server startup, and signal handling."),
    ("supervisor.c", "Process lifecycle demonstration, child reaping, and shutdown support."),
    ("ipc.c / ipc.h", "POSIX shared-memory request ring and process-shared semaphore operations."),
    ("ingress.c", "Local request parsing and optional network-ingress interface."),
    ("queue.c", "Bounded in-process priority task queue, FCFS dispatch, and aging."),
    ("worker_pool.c", "POSIX thread creation, task dispatch, booking execution, and worker shutdown."),
    ("booking.c", "Berth data, validation, allocation, readers-writers queries, and rollback."),
    ("algorithms.c", "Banker, Wait-For Graph, Clock, and C-SCAN simulations and traces."),
    ("wal.c", "Append-only reservation log, integrity validation, WAL replay, and recovery."),
    ("metrics.c", "Counters, latency summaries, queue statistics, and benchmark reporting."),
    ("client.c", "Interactive local customer client that submits reservation requests."),
    ("tests.c", "Deterministic correctness, queue, concurrency, WAL, and algorithm tests."),
    ("Makefile", "Linux/POSIX build targets for server, client, tests, and smoke checks."),
    ("README.md", "Build, two-terminal reservation workflow, commands, and limitations."),
]
for row, (module, purpose) in zip(doc.tables[4].rows[1:], module_rows):
    replace_cell(row.cells[0], module)
    replace_cell(row.cells[1], purpose)

# Update visible operational test coverage in the existing test-case table.
tests = [
    ("Booking correctness", "Concurrent requests for one berth; at most one confirmation; rollback releases tentative berths."),
    ("Queue boundaries", "Full and empty queue conditions; no overwrite, lost task, or starvation of aged work."),
    ("Client and IPC", "Local client submits validated requests through the shared-memory ring; server consumes them safely."),
    ("Banker's safety", "Known safe and unsafe allocation examples with expected grant or defer outcomes."),
    ("Deadlock detection", "Construct a Wait-For Graph cycle and verify documented victim rollback and retry."),
    ("Page replacement", "Known reference strings; verify faults, replacement trace, and final frame contents."),
    ("Disk scheduling", "Fixed cylinder sequence; verify C-SCAN order, wrap movement, and total movement."),
    ("WAL recovery", "Restart after complete, corrupt, and incomplete records; reject invalid or duplicate events and rebuild valid state."),
]
for row, (case, validation) in zip(doc.tables[5].rows[1:], tests):
    replace_cell(row.cells[0], case)
    replace_cell(row.cells[1], validation)

# This is a static contents table in the supplied template, so keep its page
# references aligned with the verified final pagination.
toc_pages = [2, 2, 3, 4, 4, 5, 7, 7, 9, 10, 11, 11, 12]
for row, page in zip(doc.tables[1].rows, toc_pages):
    replace_cell(row.cells[2], str(page))

# Correct stray corrupted quotes in reference entries without changing citation meaning.
for paragraph in doc.paragraphs:
    if "�" in paragraph.text:
        cleaned = paragraph.text.replace("�", "'")
        replace_paragraph(paragraph, cleaned)

# Keep document-wide typography and headings stable and professional.
for paragraph in doc.paragraphs:
    if paragraph.style.name in {"Body Text", "Normal", "List Paragraph"}:
        paragraph.paragraph_format.space_after = Pt(6)
        paragraph.paragraph_format.line_spacing = 1.15
    if paragraph.style.name.startswith("Heading"):
        paragraph.paragraph_format.keep_with_next = True
        paragraph.paragraph_format.space_before = Pt(10)
        paragraph.paragraph_format.space_after = Pt(5)

for section in doc.sections:
    section.top_margin = Inches(0.75)
    section.bottom_margin = Inches(0.75)
    section.left_margin = Inches(0.8)
    section.right_margin = Inches(0.8)

doc.core_properties.title = "Tatkal-Core Synopsis"
doc.core_properties.subject = "Operating Systems Project Synopsis"
doc.save(OUTPUT)
print(OUTPUT)

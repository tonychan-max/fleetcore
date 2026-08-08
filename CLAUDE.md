# fleetcore

Contract-driven command & telemetry backbone for vehicle terminals.
Clean-room reimplementation; no code or message numbers from any employer.

## Context for assistants

Read these before answering design questions:

- `PLAN.md` — roadmap, Phase 1-6, Step 1-25
- `docs/ARCHITECTURE.md` — current structure. Section 3 lists the design
  principles that override ad-hoc judgement
- `docs/decisions/` — ADRs. Every decision has a cost and a way to falsify it

## Ground rules

- The author is learning C++, Linux and Qt6. Explain why, not just what.
  Do not hand over large blocks of finished code without explanation.
- Explanations in Chinese. Code comments, commit messages and identifiers
  in English. ADRs and PLAN.md in Japanese.
- State facts you verified separately from things you inferred.
  Mark anything unverified as such.
- Flag GPL/AGPL/SSPL dependencies explicitly.
- `common/proto.h` is the single source of truth for the wire format.
  Changing it means updating ARCHITECTURE section 4 as well.
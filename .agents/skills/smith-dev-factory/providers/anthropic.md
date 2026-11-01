---
name: anthropic
description: The Anthropic provider of the smith factory: which model each ticket difficulty runs on, how runners are launched, and Anthropic's model-choice guidance.
---

# Anthropic provider

The only file of the factory that names models (#1409). Tickets name difficulties; this file maps them to models.

## Mapping
| Difficulty | Model | Effort |
|---|---|---|
| `difficulty:easy` | Haiku 5.5, `claude-haiku-5-5` | medium |
| `difficulty:medium` | Sonnet 5.5, `claude-sonnet-5-5` | medium |
| `difficulty:hard` | Sonnet 5.5, `claude-sonnet-5-5` | high |

Opus is never in the mapping. Only the lead assigns it, per ticket, and the lead's daily handover on #73 reports every use.

## Launching a runner
- **Cloud:** a session tagged `smith-runner`, permission mode `auto`, with the prompt `You are a smith runner. Load the skill smith-dev-runner and follow it.` (add `Friction mode.` for the friction runner). Its model is the mapping's model for the ticket's difficulty.
- **Mac:** a bash loop of `claude -p` processes on the owner's API credits, with the key read from 1Password by `apiKeyHelper` passed on the command line. See [smith-dev-mac-runner](../../smith-dev-mac-runner/SKILL.md). Same prompt, same mapping.

## Model choice guidance
From the `claude-api` skill (model table cached 2026-10-06; check the Models API for changes).

| Model | Id | Context | Input / output per 1M tokens | Cache reads per 1M |
|---|---|---|---|---|
| Haiku 5.5 | `claude-haiku-5-5` | 1M | $0.10 / $0.50 (up to 100K-token prompts) | about $0.01 |
| Sonnet 5.5 | `claude-sonnet-5-5` | 1M | $2 / $10 | $0.20 |
| Opus 5.5 | `claude-opus-5-5` | 1M | $4 / $20 | $0.20 |

- **Relative cost:** Sonnet 5.5 costs about 20 times Haiku 5.5 on input and output. Opus 5.5 costs twice Sonnet 5.5. Use ids exactly as written; never add a date suffix.
- **Choose the cheapest model that does the ticket.** Haiku for docs, a README, or a change the ticket spells out line by line. Sonnet for code. Opus only where a ticket needs it, set by the lead.
- **Effort is the second lever.** Haiku 5.5 and Opus 5.5 default to `medium`, Sonnet 5.5 to `high`. Set it explicitly: `medium` for easy and medium tickets, `high` for hard ones. Raise it only when a ticket fails at the lower level. Thinking cannot be switched off on Opus 5.5.
- **Prompt caching:** the default cache lives 5 minutes, and each read refreshes it; the 1-hour cache costs twice as much to write. Cache reads cost about a tenth of base input (a twentieth on Opus 5.5). A runner or lead that keeps working re-reads its whole context from cache on every turn.
- **Cost is dominated by cache reads of large contexts.** A 250k-token context read on every turn costs more than the output. So keep sessions small, end a runner after one ticket, and do not wake a large session just to poll ([factory-experiments](../../../../knowledge/factory-experiments.md)).
